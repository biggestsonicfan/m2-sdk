/*
 * m2_gfx2d.h — hardware-accelerated 2D graphics for the Model 2b SDK.
 *
 * Draws flat-colour 2D primitives through the board's GEO polygon rasterizer
 * instead of CPU pixel loops: you build a display list of screen-space polygons
 * and the hardware fills them (and double-buffers, so no tearing). Distilled from
 * the m2-x11 xeyes work into a reusable, ergonomic API.
 *
 * Quick start (after m2_init()):
 *     g2d_init();                              // colour pipeline + clear tiles
 *     g2d_background(M2_RGB(2,3,7));           // root colour
 *     g2d_color(1, M2_RGB(31,31,31));          // define colour slot 1 = white
 *     g2d_color(2, M2_RGB(31,0,0));            //              slot 2 = red
 *     for (;;) {
 *         g2d_begin();                         // start a frame
 *         g2d_fill_circle(200,192, 40, 1);     //  (slots drawn in call order,
 *         g2d_rect(10,10, 80,20, 2);           //   later = on top)
 *         g2d_end();                           // submit to the GEO
 *         m2_vsync();
 *     }
 *
 * How it works: flat polygons are emitted as GEO direct_data (cmd 0x02), which
 * renders on MAME's GEO HLE (and needs NO coprocessor/firmware). Projection is
 * screen-space (geo_focal 1,1 -> screen = center + x/z); each vertex for screen
 * pixel (px,py) at depth z is ((px-CX)*z, (CY-py)*z, z). z only sets draw order,
 * not position, so g2d auto-assigns a nearer z per primitive (painter's order).
 *
 * NOTE: direct_data is the MAME render path. On real hardware the equivalent is
 * object_data (instanced quads via the SHARC COP — see m2-snake m2_geo.h).
 *
 * Include AFTER m2.h. Pulls in m2_color.h (3D colour pipeline).
 */
#ifndef M2_GFX2D_H
#define M2_GFX2D_H

#include "m2.h"
#include "m2_color.h"

/* ---- low-level GEO display list (direct_data) ---------------------------- */
#define G2D_BUFFERRAM   0x00900000u
#define G2D_READ_REG    0x00803008u
#define G2D_OP_DIRECT   0x01000202u
#define G2D_OP_TEXDATA  0x02000404u
#define G2D_OP_WINDOW   0x01800303u
#define G2D_OP_TEXPARAM 0x03000606u
#define G2D_OP_MODE     0x03800707u
#define G2D_OP_ZSORT    0x04000808u
#define G2D_OP_FOCAL    0x04800909u
#define G2D_OP_LIGHT    0x05000A0Au
#define G2D_OP_END      0x07800F0Fu
#define G2D_TEXRAM_BIT  0x00800000u

/* screen-space projection center (calibrated to the 496x384 visible area) */
#define G2D_CX 248.0f
#define G2D_CY 192.0f

static u32 g2d__buf[0x3000];    /* GEO display list (fits bufferram past 0x10000) */
static u32 g2d__n;
static u32 g2d__slots;          /* active colorbase bitmask (slots 1..31) */
static float g2d__zc;           /* current draw depth (decrements per primitive) */
#define G2D_ZBASE 60.0f
#define G2D_ZSTEP 0.10f

static u32  g2d__f(float f) { union { float f; u32 u; } x; x.f = f; return x.u; }
static void g2d__w(u32 v)   { if (g2d__n < 0x3000) g2d__buf[g2d__n++] = v; }
static float g2d__nextz(void) { float z = g2d__zc; g2d__zc -= G2D_ZSTEP; if (g2d__zc < 4.0f) g2d__zc = 4.0f; return z; }

/* screen pixel (px,py) at depth z -> direct_data world vertex */
static void g2d__v(float out[3], float px, float py, float z) {
    out[0] = (px - G2D_CX) * z; out[1] = (G2D_CY - py) * z; out[2] = z;
}

/* one flat-colour quad (points v1,v0,v2,v3), doubleside (winding-agnostic) */
static void g2d__quad(const float v0[3], const float v1[3],
                      const float v2[3], const float v3[3], u32 cb) {
    if (g2d__n + 17 > 0x3000) return;        /* skip cleanly when the list is full */
    g2d__w(G2D_OP_DIRECT);
    g2d__w(0u);
    g2d__w(G2D_TEXRAM_BIT | (cb * 4u));
    g2d__w(g2d__f(v1[0])); g2d__w(g2d__f(v1[1])); g2d__w(g2d__f(v1[2]));
    g2d__w(g2d__f(v0[0])); g2d__w(g2d__f(v0[1])); g2d__w(g2d__f(v0[2]));
    g2d__w(1u | (1u << 8) | (1u << 17));      /* quad, linktype 1, doubleside */
    g2d__w(0xFFu << 23);                       /* luma full */
    g2d__w(0u);
    g2d__w(g2d__f(v2[0])); g2d__w(g2d__f(v2[1])); g2d__w(g2d__f(v2[2]));
    g2d__w(g2d__f(v3[0])); g2d__w(g2d__f(v3[1])); g2d__w(g2d__f(v3[2]));
    g2d__w(0u); g2d__w(0u);
}
static void g2d__color_header(u32 cb) {
    g2d__w(G2D_OP_TEXDATA);
    g2d__w(G2D_TEXRAM_BIT | (cb * 4u));
    g2d__w(4u);
    g2d__w(0u); g2d__w(0u); g2d__w(0u);
    g2d__w((cb & 0x3ffu) << 6);
}
static float g2d__sqrt(float x) {           /* Newton's method (no libm needed) */
    float g; int i;
    if (x <= 0.0f) return 0.0f;
    g = x; for (i = 0; i < 6; i++) g = 0.5f * (g + x / g);
    return g;
}

/* ---- public API ---------------------------------------------------------- */

/* Set up hardware 2D: 3D colour pipeline + blank the tile layers (so they don't
 * show over/under the polygon plane). Call once after m2_init(). Leaves the board
 * in normal 3D render mode (m2_init's default), which the GEO draws into. */
static void g2d_init(void) {
    int i;
    m2_color_init();
    for (i = 0; i < 32; i++) M2_CHARGFX[i] = 0;          /* blank char 0       */
    for (i = 0; i < 64 * 64; i++) { M2_TILE_FG[i] = 0; M2_TILE_BG[i] = 0; }
    g2d__slots = 0;
}

/* Root/background colour (shown where nothing is drawn). */
static void g2d_background(u16 bgr555) { m2_setpal(0, bgr555); }

/* Define colour slot `cb` (1..31) as a BGR555 hue; referenced by the draw calls.
 * Slots are auto-registered with the GEO each frame by g2d_begin(). */
static void g2d_color(u32 cb, u16 bgr555) {
    m2_setcolor(cb, bgr555);
    if (cb < 32u) g2d__slots |= (1u << cb);
}

/* Begin a frame: reset the list, emit the scene preamble + a colour header for
 * every defined slot, and reset the painter's-order depth. */
static void g2d_begin(void) {
    u32 cb;
    g2d__n = 0;
    g2d__w(G2D_OP_ZSORT); g2d__w(0x40800000u);
    g2d__w(G2D_OP_MODE);  g2d__w(3u);
    g2d__w(G2D_OP_MODE);  g2d__w(1u);
    g2d__w(G2D_OP_TEXPARAM); g2d__w(0u); g2d__w(0x20u);
    for (cb = 0; cb < 0x20u; cb++) { g2d__w((u32)0xFFu | (0x60u << 8)); g2d__w(g2d__f(1.0f)); }
    g2d__w(G2D_OP_FOCAL); g2d__w(g2d__f(1.0f)); g2d__w(g2d__f(1.0f));
    g2d__w(G2D_OP_LIGHT); g2d__w(g2d__f(0.706f)); g2d__w(g2d__f(-0.693f)); g2d__w(g2d__f(0.145f));
    g2d__w(G2D_OP_WINDOW);
    g2d__w(0x0000007Fu); g2d__w(0x01F001FFu);
    g2d__w(0x00F8013Fu); g2d__w(0x00F8013Fu); g2d__w(0x00F8013Fu); g2d__w(0x00F8013Fu);
    for (cb = 1; cb < 32u; cb++) if (g2d__slots & (1u << cb)) g2d__color_header(cb);
    g2d__zc = G2D_ZBASE;
}

/* arbitrary screen-space quad (corners in order), colour slot cb */
static void g2d_quad(float x0, float y0, float x1, float y1,
                     float x2, float y2, float x3, float y3, u32 cb) {
    float a[3], b[3], c[3], d[3], z = g2d__nextz();
    g2d__v(a, x0, y0, z); g2d__v(b, x1, y1, z);
    g2d__v(c, x2, y2, z); g2d__v(d, x3, y3, z);
    g2d__quad(a, b, c, d, cb);
}
/* screen-space triangle (degenerate quad) */
static void g2d_tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 cb) {
    g2d_quad(x0, y0, x1, y1, x2, y2, x2, y2, cb);
}
/* filled axis-aligned rectangle */
static void g2d_rect(float x, float y, float w, float h, u32 cb) {
    g2d_quad(x, y, x + w, y, x + w, y + h, x, y + h, cb);
}
/* filled ellipse (triangle fan) */
static void g2d_fill_ellipse(float cx, float cy, float rx, float ry, u32 cb) {
    const float cd = 0.974928f, sd = 0.222521f;   /* cos/sin of 2*pi/28 */
    float ca = 1.0f, sa = 0.0f, z = g2d__nextz();
    float c0[3], prev[3], cur[3];
    int i;
    g2d__v(c0, cx, cy, z);
    g2d__v(prev, cx + rx, cy, z);
    for (i = 0; i < 28; i++) {
        float nca = ca * cd - sa * sd, nsa = ca * sd + sa * cd;
        ca = nca; sa = nsa;
        g2d__v(cur, cx + rx * ca, cy + ry * sa, z);
        g2d__quad(c0, prev, cur, cur, cb);
        prev[0] = cur[0]; prev[1] = cur[1]; prev[2] = cur[2];
    }
}
static void g2d_fill_circle(float cx, float cy, float r, u32 cb) { g2d_fill_ellipse(cx, cy, r, r, cb); }

/* thick line from (x0,y0) to (x1,y1), full width w */
static void g2d_line(float x0, float y0, float x1, float y1, float w, u32 cb) {
    float dx = x1 - x0, dy = y1 - y0;
    float n = g2d__sqrt(dx * dx + dy * dy), px, py;
    if (n < 0.001f) { px = w * 0.5f; py = 0.0f; }
    else { px = -dy * (w * 0.5f) / n; py = dx * (w * 0.5f) / n; }
    g2d_quad(x0 + px, y0 + py, x1 + px, y1 + py, x1 - px, y1 - py, x0 - px, y0 - py, cb);
}

/* Hardware text: each 8x8 glyph from the built-in font (gFont, pixel 1 = ink) is
 * drawn as flat quads, run-length-merged per row so a glyph is only a few quads.
 * Pixel-positioned and composited on the GEO like everything else. `scale` >= 1
 * enlarges; top-left origin at (x,y). */
static void g2d_text_scaled(float x, float y, const char *s, u32 cb, int scale) {
    int i, py, px;
    if (scale < 1) scale = 1;
    for (i = 0; s[i]; i++) {
        const u8 *g = gFont + (u32)((u8)s[i] & 0x7f) * 32;
        float gx = x + (float)(i * 8 * scale);
        for (py = 0; py < 8; py++) {
            px = 0;
            while (px < 8) {
                u8 v = g[py * 4 + (px >> 1)];
                int ink = (px & 1) ? (v >> 4) : (v & 0x0f);
                if (ink) {
                    int run = 1;
                    while (px + run < 8) {
                        u8 v2 = g[py * 4 + ((px + run) >> 1)];
                        if (!(((px + run) & 1) ? (v2 >> 4) : (v2 & 0x0f))) break;
                        run++;
                    }
                    g2d_rect(gx + (float)(px * scale), y + (float)(py * scale),
                             (float)(run * scale), (float)scale, cb);
                    px += run;
                } else {
                    px++;
                }
            }
        }
    }
}
static void g2d_text(float x, float y, const char *s, u32 cb) { g2d_text_scaled(x, y, s, cb, 1); }

/* ---- textured quads (de-risk spike toward crisp glyph fonts) ------------- *
 * Texels live in texram0 (0x11000000) in a 2x2-swizzled 4bpp layout; texture
 * headers + per-vertex UVs live in the GEO texture_ram, written via cmd 0x04
 * (bit 0x800000). A texel is a luminance mask of the colorbase hue; value 0xf
 * (15) is TRANSPARENT. (See memory/model2-texture-format.) */
#define G2D_TEXRAM0 ((volatile u32 *)0x11000000u)

/* set 4-bit texel (val) at (x,y) in texram0 sheet (inverse of get_texel) */
static void g2d_texel(int x, int y, u8 val) {
    u32 offset = (u32)((y / 2) * 512 + (x / 2));
    u32 widx = offset >> 1;
    int shift = ((x & 1) ? 0 : 4) + ((y & 1) ? 0 : 8) + ((offset & 1) ? 16 : 0);
    u32 w = G2D_TEXRAM0[widx];
    G2D_TEXRAM0[widx] = (w & ~(0xFu << shift)) | ((u32)(val & 0xf) << shift);
}

/* write a 4-word texture header into GEO texture_ram at hdr (cmd 0x04). */
static void g2d_tex_header(u32 hdr, int texx, int texy, int wbits, int hbits,
                           int sheet1, u32 colorbase, u32 lumabase) {
    u32 th0 = 0x4000u | ((u32)wbits & 7) | (((u32)hbits & 7) << 3);   /* bit14=textured */
    u32 th1 = lumabase & 0xffu;
    u32 th2 = ((u32)(texx / 32) & 0x3f) | (((u32)(texy / 32) & 0x1f) << 6) | (sheet1 ? 0x1000u : 0);
    u32 th3 = (colorbase & 0x3ffu) << 6;
    g2d__w(G2D_OP_TEXDATA); g2d__w(G2D_TEXRAM_BIT | hdr); g2d__w(4u);
    g2d__w(th0); g2d__w(th1); g2d__w(th2); g2d__w(th3);
}

/* Write 4 vertex UV pairs into texture_ram, each pair (pv, pu) i.e. V then U.
 * The 4 vertices are in g2d_tquad's order: TL, TR, BR, BL (top-left, top-right,
 * bottom-right, bottom-left) -- so a normal upright mapping of a WxH texture is
 *   { 0,0,  0,W-1,  H-1,W-1,  H-1,0 }.  (g2d_tquad pushes pts b,a,c,d which the
 * GEO reads back as v[0]=TL, v[1]=TR, v[2]=BR, v[3]=BL; getting this order wrong
 * twists the top edge's U opposite the bottom -> a diagonal warp/swirl.)
 * uv[] is in PLAIN TEXEL COORDS (e.g. 0..31 for a 32-wide texture); we scale by
 * 8 here because the GEO recovers the texel as pu_input/8 (it computes pu*(1/z)/8
 * then perspective-divides by 1/z). Without the x8, sampling collapses to texels
 * 0..tex/8 (the "flat" bug). */
static void g2d_tex_uv(u32 uvoff, const int uv[8]) {
    int i;
    g2d__w(G2D_OP_TEXDATA); g2d__w(G2D_TEXRAM_BIT | uvoff); g2d__w(8u);
    for (i = 0; i < 8; i++) g2d__w(((u32)uv[i] << 3) & 0xffffu);
}

/* Direct-data "distance" word -> object.texlod in the GEO renderer. NOTE the
 * GEO's geo_direct_data re-pushes this word with a >>8, so command_buffer[10] =
 * (our word)>>8, and then:
 *     texlod = ((command_buffer[10]>>8)&0x7f80) - 0x3f80
 *            = ((our_word>>16)&0x7f80) - 0x3f80.
 * Mip level = (-texlod + fast_log2(z)) >> 7, clamped to max_level (=4 for 32px),
 * fast_log2(z) = log2(z) in 8.8 (~log2(z)*256). For 2D we want 1:1 texels
 * (level 0). Painter z up to ~60 -> log2(60)*256 ~= 1512, so we need texlod >=
 * 1512-127 ~= 1385. 0x45800000 -> texlod = 0x4580-0x3f80 = 0x600 (1536): level 0
 * across z 4..60 with margin. (Pushing 0 gives texlod = -0x3f80 -> always max
 * mip -> one averaged texel = the "textures render flat/black" bug.) */
#define G2D_TEX_LOD0 0x45800000u

/* textured quad (screen rect x,y,w,h) referencing UV block uvoff + header hdr */
static void g2d_tquad(float x, float y, float w, float h, u32 uvoff, u32 hdr) {
    float a[3], b[3], c[3], d[3], z = g2d__nextz();
    g2d__v(a, x, y, z); g2d__v(b, x + w, y, z); g2d__v(c, x + w, y + h, z); g2d__v(d, x, y + h, z);
    g2d__w(G2D_OP_DIRECT);
    g2d__w(G2D_TEXRAM_BIT | uvoff);          /* tpa = UV points  */
    g2d__w(G2D_TEXRAM_BIT | hdr);            /* tha = texheader  */
    g2d__w(g2d__f(b[0])); g2d__w(g2d__f(b[1])); g2d__w(g2d__f(b[2]));
    g2d__w(g2d__f(a[0])); g2d__w(g2d__f(a[1])); g2d__w(g2d__f(a[2]));
    g2d__w(1u | (1u << 8) | (1u << 17));
    g2d__w(0xFFu << 23);
    g2d__w(G2D_TEX_LOD0);                     /* texlod bias -> mip level 0 */
    g2d__w(g2d__f(c[0])); g2d__w(g2d__f(c[1])); g2d__w(g2d__f(c[2]));
    g2d__w(g2d__f(d[0])); g2d__w(g2d__f(d[1])); g2d__w(g2d__f(d[2]));
    g2d__w(0u); g2d__w(0u);
}

/* ---- textured bitmap font (atlas glyphs via the texture pipeline) -------- *
 * Uploads the built-in 8x8 font (gFont) into texram0 as a 128x64 atlas (a 16x8
 * grid of glyphs), then draws each glyph as ONE textured+transparent quad --
 * one poly per glyph instead of the flat-quad g2d_text's dozens, with free
 * colorbase tint and transparency over any background.
 *
 * QUALITY NOTE: the GEO texture unit is always BILINEAR and the transparent path
 * expands ink by ~half a texel (the 50%-alpha cutoff, model2rd.ipp:189/325), and
 * the GEO image is itself rescaled to the display -- so this SOFTENS a 1-bit 8px
 * font (strokes thicken/merge as you scale up). For crisp small UI text prefer
 * the nearest-style flat-quad g2d_text; reach for g2d_ttext when you want smooth
 * scaled-up text, a tinted/transparent string, or to save display-list space.
 * The atlas occupies texram0 (0,0)..(128,64); keep other textures off that area. */
#define G2D_FONT_HDR  0x100u    /* texture_ram slot: atlas header (4 words)   */
#define G2D_FONT_UV   0x108u    /* texture_ram slot: per-glyph UVs (8 words)  */

/* Upload gFont -> texram0 atlas. Call ONCE after g2d_init (writes texels via the
 * CPU, not the display list). Ink (any nonzero font nibble; the font uses small
 * values 1/2, not 15) -> bright texel 14; background 0 -> texel 0xf (transparent
 * in the translucent renderer). */
static void g2d_font_atlas(void) {
    int c, gy, gx;
    for (c = 0; c < 128; c++) {
        const u8 *g = gFont + (u32)c * 32;
        int ax = (c & 15) * 8, ay = (c >> 4) * 8;
        for (gy = 0; gy < 8; gy++)
            for (gx = 0; gx < 8; gx++) {
                u8 v = g[gy * 4 + (gx >> 1)];
                int ink = (gx & 1) ? (v >> 4) : (v & 0x0f);
                /* gFont is two-layer: nibble 1 = letter strokes, 2 = fill/shadow.
                 * Use only the strokes (value 1) so counters stay open and glyphs
                 * read cleanly; value 2 alone would fill letters into blobs.
                 * Write the column mirrored (7-gx): the GEO samples a textured quad
                 * mirrored in U, so storing each glyph flipped renders it upright. */
                g2d_texel(ax + (7 - gx), ay + gy, (u8)(ink == 1 ? 14 : 0));
            }
    }
}

/* Draw text with the textured font atlas. cb = colorbase hue; each glyph is
 * 8*scale px wide. Call between g2d_begin()/g2d_end(); run g2d_font_atlas() once
 * beforehand. Glyphs reuse one UV slot, rewritten before each quad (the GEO
 * captures the UVs into each polygon as it parses the list, in order). */
static void g2d_ttext_scaled(float x, float y, const char *s, u32 cb, float scale) {
    int i;
    float gw = 8.0f * scale;
    /* atlas header: 128x64 (wbits=2,hbits=1), sheet0, transparent(bit13)+textured(bit14) */
    g2d__w(G2D_OP_TEXDATA); g2d__w(G2D_TEXRAM_BIT | G2D_FONT_HDR); g2d__w(4u);
    g2d__w(0x4000u | 2u | (1u << 3));       /* th0: textured, OPAQUE (AA edges) */
    g2d__w(0u);                             /* th1 lumabase 0 */
    g2d__w(0u);                             /* th2 texx=0 texy=0 sheet0 */
    g2d__w((cb & 0x3ffu) << 6);             /* th3 colorbase */
    for (i = 0; s[i]; i++) {
        int c = (u8)s[i] & 0x7f;
        int u0 = (c & 15) * 8, v0 = (c >> 4) * 8;
        int uv[8];                          /* TL,TR,BR,BL order (pv,pu) */
        uv[0] = v0;     uv[1] = u0;         /* maps screen px i -> texel u0+i at 1:1 */
        uv[2] = v0;     uv[3] = u0 + 8;
        uv[4] = v0 + 8; uv[5] = u0 + 8;
        uv[6] = v0 + 8; uv[7] = u0;
        g2d_tex_uv(G2D_FONT_UV, uv);
        g2d_tquad(x + (float)i * gw, y, gw, gw, G2D_FONT_UV, G2D_FONT_HDR);
    }
}
static void g2d_ttext(float x, float y, const char *s, u32 cb) {
    g2d_ttext_scaled(x, y, s, cb, 1.0f);
}

/* Submit the frame to the GEO (it rasterizes on the next vblank). */
static void g2d_end(void) {
    volatile u32 *buf = (volatile u32 *)(G2D_BUFFERRAM + 0x10000u);
    u32 i;
    g2d__w(G2D_OP_END);
    for (i = 0; i < g2d__n; i++) buf[i] = g2d__buf[i];
    *(volatile u32 *)G2D_READ_REG = 0x10000u;
}

/* Submit an empty frame to wipe the polygon plane (e.g. before handing the screen
 * back to a tile-only menu, so the last drawn frame doesn't linger). */
static void g2d_clear(void) { g2d_begin(); g2d_end(); }

/* ---- 3D vector primitives (real Z, depth-sorted) ------------------------- *
 * The helpers above project screen pixels at an auto-assigned painter's-order Z.
 * These instead take vertices ALREADY in direct_data space — projected as
 * screen = G2D_CX + X/Z — and keep the real Z, so the GEO z-sorts them. That
 * gives correct occlusion for a 3D vector scene (e.g. Tempest's tube). For a
 * camera-space point (x,y,z) at focal f, supply (f*x, f*y, z). Call between
 * g2d_begin()/g2d_end() like the others. */
static void g2d_vquadl(const float v0[3], const float v1[3],
                       const float v2[3], const float v3[3], u32 cb, u32 luma) {
    if (g2d__n + 17 > 0x3000) return;         /* skip cleanly when the list is full */
    g2d__w(G2D_OP_DIRECT);
    g2d__w(0u);
    g2d__w(G2D_TEXRAM_BIT | (cb * 4u));
    g2d__w(g2d__f(v1[0])); g2d__w(g2d__f(v1[1])); g2d__w(g2d__f(v1[2]));
    g2d__w(g2d__f(v0[0])); g2d__w(g2d__f(v0[1])); g2d__w(g2d__f(v0[2]));
    g2d__w(1u | (1u << 8) | (1u << 17));      /* quad, linktype 1, doubleside */
    g2d__w((luma & 0xFFu) << 23);
    g2d__w(0u);
    g2d__w(g2d__f(v2[0])); g2d__w(g2d__f(v2[1])); g2d__w(g2d__f(v2[2]));
    g2d__w(g2d__f(v3[0])); g2d__w(g2d__f(v3[1])); g2d__w(g2d__f(v3[2]));
    g2d__w(0u); g2d__w(0u);
}
static void g2d_vquad(const float v0[3], const float v1[3],
                      const float v2[3], const float v3[3], u32 cb) {
    g2d_vquadl(v0, v1, v2, v3, cb, 0xFFu);
}
/* thin-quad line a->b, full width w, widened perpendicular in the projected XY
   plane; each end keeps its real Z. */
static void g2d_vline(const float a[3], const float b[3], float w, u32 cb) {
    float dx = b[0]-a[0], dy = b[1]-a[1];
    float n = g2d__sqrt(dx*dx + dy*dy), hw = w * 0.5f, px, py;
    float v0[3], v1[3], v2[3], v3[3];
    if (n < 0.0001f) { px = hw; py = 0.0f; } else { px = -dy*hw/n; py = dx*hw/n; }
    v0[0]=a[0]+px; v0[1]=a[1]+py; v0[2]=a[2];
    v1[0]=a[0]-px; v1[1]=a[1]-py; v1[2]=a[2];
    v2[0]=b[0]-px; v2[1]=b[1]-py; v2[2]=b[2];
    v3[0]=b[0]+px; v3[1]=b[1]+py; v3[2]=b[2];
    g2d_vquadl(v0, v1, v2, v3, cb, 0xFFu);
}
/* solid-fill triangle (degenerate quad) in direct_data space. */
static void g2d_vtril(const float a[3], const float b[3], const float c[3], u32 cb, u32 luma) {
    g2d_vquadl(a, b, c, c, cb, luma);
}
static void g2d_vtri(const float a[3], const float b[3], const float c[3], u32 cb) {
    g2d_vquadl(a, b, c, c, cb, 0xFFu);
}

/* Project a CAMERA-space point (x,y,z) into a direct_data vertex for focal length
   `focal`: direct_data projects screen = G2D_CX + X/Z (no focal), so pre-scale x,y
   by focal. Feed `out` to the g2d_v* primitives. (This is the camera->screen bridge
   the Tempest tube uses: t_v(v) == g2d_v3(out, T_FOCAL, v[0],v[1],v[2]).) */
static void g2d_v3(float out[3], float focal, float x, float y, float z) {
    out[0] = x * focal; out[1] = y * focal; out[2] = z;
}

/* Lambert-ish face luma for g2d_vquadl/g2d_vtril: ambient + diffuse*|n.l|/|n|, with
   (nx,ny,nz) the (unnormalized) face normal and (lx,ly,lz) the light direction.
   Lets flat panels read as shaded 3D (e.g. Tempest's tube wall). */
static u32 g2d_luma(float nx, float ny, float nz, float lx, float ly, float lz) {
    float nn = g2d__sqrt(nx*nx + ny*ny + nz*nz);
    float nd = nn > 1e-6f ? (nx*lx + ny*ly + nz*lz) / nn : 0.0f;
    if (nd < 0.0f) nd = -nd;
    if (nd > 1.0f) nd = 1.0f;
    return 0x40u + (u32)(nd * 191.0f);          /* ambient 0x40 + diffuse up to 0xFF */
}

/* ---- z-sort granularity --------------------------------------------------- *
 * The GEO buckets primitives by Z at this granularity before painter-drawing them.
 * COARSE (the g2d_begin default) is fine for 2D painter's order. But in a 3D vector
 * scene where an overlay line lies ON a filled surface (wireframe-on-panel), the two
 * are coplanar: at COARSE granularity they land in the same bucket, TIE, and the
 * panel hides the line. Call g2d_zsort_fine() right after g2d_begin() so a small
 * forward Z bias on the lines reliably wins. (Learned porting Tempest's shaded tube.) */
#define G2D_ZSORT_COARSE 0x40800000u   /* ~4.0  — 2D painter's order            */
#define G2D_ZSORT_FINE   0x3C23D70Au   /* ~0.01 — 3D coplanar overlay separation */
static void g2d_zsort(u32 mode) { g2d__w(G2D_OP_ZSORT); g2d__w(mode); }
static void g2d_zsort_fine(void) { g2d_zsort(G2D_ZSORT_FINE); }

#endif /* M2_GFX2D_H */
