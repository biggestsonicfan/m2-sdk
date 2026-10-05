/*
 * m2_sprite.h — 2D sprites drawn as textured GEO polygons.
 *
 * The other way to put a sprite on screen is to plot its pixels into the tile plane
 * (m2_tilefb.h): the i960 rewrites every char cell a sprite touches, and puts the cells
 * back the next frame. That costs i960 time per pixel, and an emulator that draws the tile
 * plane on a GPU (m2-hle2's Dreamcast build) sees the char RAM change every frame. Here a
 * sprite is one textured quad per colour instead: the image is uploaded to texture RAM once,
 * and each frame costs ~30 display-list words per quad, whatever the sprite's size. The GEO
 * does the scaling and flipping, the i960 touches no pixels, and a GPU-backed emulator draws
 * the quads as polygons.
 *
 * Colour: a textured polygon's texel only gives the brightness of ONE colorbase (MAME
 * model2rd.ipp draw_scanline_tex; see m2_obj.h), so an image is split by pen: m2_spr_load
 * writes one transparent mask per pen it uses, and m2_spr_draw draws one quad per pen, each in
 * the colorbase the caller gives that pen. m2_spr_flat_colors() makes a colorbase's colour
 * exact (the GEO's luma/lighting no longer changes it), so an indexed sprite looks the same as
 * its tile-plane version. Set colours with m2_spr_color(cb, bgr555). Palette mode (further down)
 * draws a 16-colour image as ONE quad instead, its texels choosing the colours.
 *
 * Path: the GEO DIRECT-data command (screen-space verts + an inline texture header, as
 * m2_draw.h's textured quads). It renders under MAME (built-in GEO and the real cpres2 with
 * M2_HLE_GEO_OFF) and m2-hle2. It does NOT show on m2emulator, which only draws COP-submitted
 * objects (m2_obj.h; memory note m2emulator-render-path): use M2_SPR_COP there.
 *
 * #define M2_SPR_COP before including this file for the COP path instead: each quad is the flat
 * quad model 456 (as m2_obj.h's m2_solid_quad and m2_text.h's glyphs), submitted through the COP
 * with the same texture header and its own UVs, placed so that it covers the same screen pixels.
 * It needs m2_silicon_boot and costs the SHARC a transform per quad, and it is the path
 * m2emulator draws. Same API; m2_spr_frame_setup then also runs m2_obj_frame_setup.
 *
 * Layering: the System 24 tile plane's cells WITHOUT the M2_PRIO bit are drawn below the
 * polygons, cells with it above (MAME model2_v.cpp screen_update). A tile-plane background
 * with sprites on top therefore wants its entries without M2_PRIO (m2_tilefb.h: define
 * TFB_PRIO 0 before including it). Pen 0 of the tile plane is see-through, so the sprites
 * also show through empty cells.
 *
 * Use (after m2_init + m2_silicon_boot, m2_obj.h; prime the GEO with a few empty frames):
 *     m2_spr_flat_colors();                       // optional: exact colorbase colours
 *     m2_spr_color(20, M2_RGB(31,31,0));          // colorbase 20 = yellow
 *     int id = m2_spr_load(pixels, 16, 16, 16);   // 4-bit pens, 0 = clear; returns -1 when full
 *     ...
 *     m2_frame_begin();
 *     m2_spr_frame_setup();                       // ONCE per frame
 *     u16 pens[16] = { 0, 20 };                   // pen 1 -> colorbase 20; 0 = pen not drawn
 *     m2_spr_draw(id, x, y, M2_SPR_FLIPX, pens, 0);
 *     m2_frame_commit();
 *
 * Coordinates are screen pixels from the top-left of the 496x384 raster. Overlap: a higher
 * `layer` (0..M2_SPR_LAYERS-1) is in front; within one layer, later draws are in front.
 *
 * Texture RAM: the masks are packed into one tile of sheet 0, M2_SPR_TEX_X/Y (32-aligned),
 * 32<<M2_SPR_TEX_LOG2W wide and 32<<M2_SPR_TEX_LOG2H tall; default (256,0) 512x512, clear of
 * the font atlas (0,0)-(128,128) and m2_obj's flat patch (0,896). Each mask keeps a 1-texel
 * transparent border, so the GEO's bilinear filter never picks up a neighbour.
 */
#ifndef M2_SPRITE_H
#define M2_SPRITE_H

#include "m2.h"
#include "m2_constants.h"   /* GEO_OP_*, GEO_TEXRAM_BIT, GEO_POLY_QUAD, M2_GEOFIFO_ADDR, M2_COLORXLAT */
#include "m2_polyattr.h"    /* M2_TH0_*, M2_TH2_TILE, M2_TH3_COLORBASE, M2_PAL_SET */
#include "m2_geo.h"         /* m2_geo_fifo_window_full / _texparam, m2_geo_cmd */
#ifdef M2_SPR_COP
#include "m2_obj.h"         /* m2_obj_frame_setup, cop_drain, M2_MODEL_TABLE, M2_FLAT_QUAD_MODEL */
#endif

#ifndef M2_SPR_TEX_X
#define M2_SPR_TEX_X     256u
#endif
#ifndef M2_SPR_TEX_Y
#define M2_SPR_TEX_Y     0u
#endif
#ifndef M2_SPR_TEX_LOG2W
#define M2_SPR_TEX_LOG2W 4u                  /* 32<<4 = 512 texels */
#endif
#ifndef M2_SPR_TEX_LOG2H
#define M2_SPR_TEX_LOG2H 4u
#endif
#define M2_SPR_TEX_W     (32u << M2_SPR_TEX_LOG2W)
#define M2_SPR_TEX_H     (32u << M2_SPR_TEX_LOG2H)

#ifndef M2_SPR_MAX
#define M2_SPR_MAX       256                 /* images */
#endif
#ifndef M2_SPR_MAX_MASKS
#define M2_SPR_MAX_MASKS 1024                /* one per (image, pen) */
#endif
#ifndef M2_SPR_MAX_QUADS
#define M2_SPR_MAX_QUADS 192                 /* per frame: ~30 words each in the 8K-word list */
#endif
#define M2_SPR_LAYERS    8
#ifndef M2_SPR_LUMABASE
#define M2_SPR_LUMABASE  0xFEu               /* palette mode's lumaram block: 0x7F00..0x7F7F, 0x7EFC.. */
#endif
#ifndef M2_SPR_XLAT
#define M2_SPR_XLAT(c5)  (((c5) * 255u + 15u) / 31u)    /* palette mode: 5-bit channel -> colorxlat */
#endif

/* texture_ram slots (16-bit words): per-colorbase headers, then two banks of per-quad UVs
 * (alternate frames, so the GEO never reads a UV the next frame is already rewriting). Clear of
 * the cb*4 headers (< 0x1000) the other paths use and of m2_text.h/m2_draw.h's 0x100..0x300. */
#define M2_SPR_HDR_SLOT  0x8000u
#define M2_SPR_UV_SLOT   0xA000u

#define M2_SPR_FLIPX     1u                  /* mirror left-right */
#define M2_SPR_FLIPY     2u                  /* mirror top-bottom */

/* Polygon attr bits 11:10 pick the z the GEO sorts a polygon by: 0 = the previous polygon's (the
 * plain GEO_POLY_QUAD, so every quad would share one bucket), 1 = its nearest vertex (MAME
 * model2_v.cpp model2_3d_process_triangle). Buckets draw nearest first and the first pixel written
 * wins, so a nearer bucket is in front; within a bucket the newest polygon is drawn first. */
#define M2_SPR_ZMIN      (1u << 10)

typedef struct { u16 u, v; u8 pen; } m2_spr_mask_t;   /* texel origin inside the atlas tile */
typedef struct { u16 first; u8 n, w, h; } m2_spr_img_t;

static m2_spr_mask_t m2__spr_mask[M2_SPR_MAX_MASKS];
static m2_spr_img_t  m2__spr_img[M2_SPR_MAX];
static int m2__spr_nimg, m2__spr_nmask;
static u32 m2__spr_px, m2__spr_py, m2__spr_rowh;        /* shelf packer cursor */
static u32 m2__spr_hdr_done[32];                        /* colorbase header sent this frame */
static u32 m2__spr_palcb[32];                           /* colorbases in palette mode */
static u32 m2__spr_nq, m2__spr_frame;
static int m2_spr_clip_x0 = 0, m2_spr_clip_y0 = 0, m2_spr_clip_x1 = 496, m2_spr_clip_y1 = 384;

/* one texel of the atlas, both texram0 bank windows (as m2_font_atlas does) */
static void m2__spr_texel(u32 x, u32 y, u8 t) {
    m2_texram_texel_at(0x11000000u, (int)(M2_SPR_TEX_X + x), (int)(M2_SPR_TEX_Y + y), t);
    m2_texram_texel_at(0x11200000u, (int)(M2_SPR_TEX_X + x), (int)(M2_SPR_TEX_Y + y), t);
}

/* Load a w x h image of 4-bit pens (pix[y*stride + x], 1..15 drawn, 0 clear) into texture
 * RAM: one mask per pen it uses. Returns its id, or -1 when the atlas or the tables are full.
 * Slow (a read-modify-write per texel): load at start-up, not per frame. */
static int m2_spr_load(const u8 *pix, int w, int h, int stride) {
    u16 used = 0;
    int id = m2__spr_nimg, x, y, p, n = 0;
    if (id >= M2_SPR_MAX || w < 1 || h < 1 || w > 253 || h > 253) return -1;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) used |= (u16)(1u << (pix[y * stride + x] & 15));
    used &= (u16)~1u;
    for (p = 1; p < 16; p++) if (used & (1u << p)) n++;
    if (m2__spr_nmask + n > M2_SPR_MAX_MASKS) return -1;
    m2__spr_img[id].first = (u16)m2__spr_nmask;
    m2__spr_img[id].n = (u8)n;
    m2__spr_img[id].w = (u8)w;
    m2__spr_img[id].h = (u8)h;
    for (p = 1; p < 16; p++) {
        m2_spr_mask_t *m;
        if (!(used & (1u << p))) continue;
        if (m2__spr_px + (u32)w + 2u > M2_SPR_TEX_W) {             /* next shelf */
            m2__spr_px = 0; m2__spr_py += m2__spr_rowh; m2__spr_rowh = 0;
        }
        if (m2__spr_py + (u32)h + 2u > M2_SPR_TEX_H) return -1;
        m = &m2__spr_mask[m2__spr_nmask++];
        m->u = (u16)(m2__spr_px + 1u);
        m->v = (u16)(m2__spr_py + 1u);
        m->pen = (u8)p;
        for (y = -1; y <= h; y++)                                   /* ink 0xE, the rest 0xF */
            for (x = -1; x <= w; x++) {
                int in = x >= 0 && y >= 0 && x < w && y < h && (pix[y * stride + x] & 15) == p;
                m2__spr_texel(m->u + (u32)x, m->v + (u32)y, (u8)(in ? 0xE : 0xF));
            }
        m2__spr_px += (u32)w + 2u;
        if ((u32)h + 2u > m2__spr_rowh) m2__spr_rowh = (u32)h + 2u;
    }
    m2__spr_nimg++;
    return id;
}

/* Forget every loaded image (the texels stay until overwritten). */
static void m2_spr_reset(void) {
    m2__spr_nimg = 0; m2__spr_nmask = 0;
    m2__spr_px = 0; m2__spr_py = 0; m2__spr_rowh = 0;
}

/* Colorbase cb's colour (BGR555). */
static void m2_spr_color(u32 cb, u16 bgr555) {
    *(volatile u16 *)(M2_PALRAM + (cb + 0x1000u) * 2u) = (u16)(bgr555 | M2_PAL_SET);
}

/* Make every polygon colour exact: colorxlat rows 0..31, luma entries 0..63 all hold the row's
 * own level (row*255/31), so a textured pixel is palram[cb+0x1000]'s BGR555 whatever its luma.
 * It replaces m2_color_init's lit polygon ramps (for all polygons, not just sprites); entries
 * 64.. (the tile plane's colour, m2_init) are left alone. Call after m2_silicon_boot. */
static void m2_spr_flat_colors(void) {
    volatile u16 *R = (volatile u16 *)(M2_COLORXLAT + 0x0000u);
    volatile u16 *G = (volatile u16 *)(M2_COLORXLAT + 0x4000u);
    volatile u16 *B = (volatile u16 *)(M2_COLORXLAT + 0x8000u);
    u32 row, e;
    for (row = 0; row < 32u; row++) {
        u16 v = (u16)((row * 255u + 15u) / 31u);
        for (e = 0; e < 64u; e++) { R[row * 256u + e] = v; G[row * 256u + e] = v; B[row * 256u + e] = v; }
    }
}

/* Sprites outside this screen rectangle (x0,y0 inclusive, x1,y1 exclusive) are cut off. */
static void m2_spr_clip(int x0, int y0, int x1, int y1) {
    m2_spr_clip_x0 = x0; m2_spr_clip_y0 = y0; m2_spr_clip_x1 = x1; m2_spr_clip_y1 = y1;
}

/* Per-frame render state (as m2_draw_frame_setup, or m2_obj_frame_setup for M2_SPR_COP) and the
 * per-frame sprite counters. Once per frame, after m2_frame_begin. */
static void m2_spr_frame_setup(void) {
    int i;
#ifdef M2_SPR_COP
    m2_obj_frame_setup();                                           /* GEO state + COP projection */
    *(volatile u32 *)ZCLIP_REG = 0xFFFF00FFu;                       /* no z clip (as hwdemo.c) */
#else
    volatile u32 *fifo = (volatile u32 *)M2_GEOFIFO_ADDR;
    m2_geo_cmd(GEO_SLOT_ZMODE); *fifo = 0x40800000u;                /* z-sort granularity 4.0 */
    m2_geo_cmd(GEO_SLOT_FOCAL); *fifo = 0x438C0000u; *fifo = 0x438C0000u;   /* 280.0 */
    m2_geo_fifo_window_full();
    m2_geo_fifo_texparam(0x000010FFu);
#endif
    for (i = 0; i < 32; i++) m2__spr_hdr_done[i] = 0;
    m2__spr_nq = 0;
    m2__spr_frame++;
}

/* IEEE-754 bits of v * 2^-k, for |v| < 2^24 (exact; keeps soft-float out of the draw path) */
static u32 m2__spr_f(int v, int k) {
    u32 s = 0, a = (u32)v, e = 23;
    if (v == 0) return 0;
    if (v < 0) { s = 0x80000000u; a = (u32)-v; }
    while (!(a & 0x800000u)) { a <<= 1; e--; }
    return s | ((127u + e - (u32)k) << 23) | (a & 0x7FFFFFu);
}

#ifdef M2_SPR_COP
/* IEEE-754 bits of n/3 * 2^-k, for 0 <= n < 512 (to ~2^-22: model 456 is 12 units wide) */
static u32 m2__spr_f3(u32 n, int k) {
    u32 a, e = 0;
    if (n == 0) return 0;
    a = (n << 23) / 3u;                                             /* n/3 = a * 2^-23 */
    while (a >= 0x1000000u) { a >>= 1; e++; }
    while (!(a & 0x800000u)) { a <<= 1; e--; }
    return ((127u + e - (u32)k) << 23) | (a & 0x7FFFFFu);
}

/* The screen rectangle as model 456 (12x12 units in XZ, centred) through the COP: turned to face
 * the camera (ang_x 90 degrees, as m2_solid_quad), scaled, placed at depth z = 280 * 2^-k so one
 * pixel is 2^-k units (focal 280, hwdemo.c's mapping), with this quad's texture header and UVs. */
static void m2__spr_cop_quad(int x0, int y0, int x1, int y1, u32 tpa, u32 tha, int k) {
    volatile u32 *cf = (volatile u32 *)M2_COPFIFO_ADDR;
    volatile const u32 *mdl = (volatile const u32 *)(M2_MODEL_TABLE + M2_FLAT_QUAD_MODEL * 16u);
    *cf = COP_IDENTITY;
    *cf = COP_SET_POS;                                              /* the centre; world y is up */
    *cf = m2__spr_f(x0 + x1 - 496, k + 1); *cf = m2__spr_f(384 - y0 - y1, k + 1); *cf = m2__spr_f(280, k);
    *cf = COP_ANG_Y; *cf = 0u;
    *cf = COP_ANG_X; *cf = 0x4000u;
    *cf = COP_ANG_Z; *cf = 0u;
    *cf = COP_SCALE;                                                /* size * 2^-k / 12 */
    *cf = m2__spr_f3((u32)(x1 - x0), k + 2); *cf = 0x3F800000u; *cf = m2__spr_f3((u32)(y1 - y0), k + 2);
    *cf = COP_FADD; *cf = COP_FADD; *cf = COP_FADD;                 /* fence, then commit (m2_obj_submit) */
    cop_drain(cf, 1u);
    { u32 wr = *(volatile u32 *)COP_WPOS_REG;
      *(volatile u32 *)GEO_WRITE_REG = wr + 0x48u;
      *cf = COP_SUBMIT; *cf = wr; *cf = 0u;
      *cf = tpa; *cf = tha; *cf = mdl[2]; *cf = mdl[3];
      *cf = g_cop_p2; *cf = g_cop_p;
      g_cop_p2 = *cf; g_cop_p = *cf;
      { u32 ep = *(volatile u32 *)COP_WPOS_REG;
        *(volatile u32 *)(GEO_BUFFERRAM + (ep & 0x0001FFFCu)) = GEO_OP_END; } }
}
#endif

/* Depth of a layer: z = 2^-(layer+1), so a higher layer is nearer. Every z stays <= 1.0, which
 * keeps the GEO's distance mip at level 0 (mml = log2(z) - texlod <= 0 in model2rd.ipp): a 1:1
 * sprite samples its texels as they are. The verts are pre-multiplied by z (screen = 248 + x/z,
 * 192 - y/z), which a power of two keeps exact. M2_SPR_COP: z = 280 * 2^-(layer+8), 1.09 for
 * layer 0, so again <= ~1. */
static void m2__spr_quad(int x0, int y0, int x1, int y1, u32 u0, u32 v0, u32 u1, u32 v1,
                         u32 cb, int layer) {
    volatile u32 *fifo = (volatile u32 *)M2_GEOFIFO_ADDR;
    u32 tha = GEO_TEXRAM_BIT | (M2_SPR_HDR_SLOT + cb * 4u);
    u32 uv = GEO_TEXRAM_BIT | (M2_SPR_UV_SLOT + (m2__spr_frame & 1u) * 0x1000u + m2__spr_nq * 8u);
    if (!(m2__spr_hdr_done[(cb >> 5) & 31u] & (1u << (cb & 31u)))) {
        m2__spr_hdr_done[(cb >> 5) & 31u] |= 1u << (cb & 31u);
        *fifo = GEO_OP_TEXDATA; *fifo = tha; *fifo = 4u;
        *fifo = M2_TH0_TEX | M2_TH0_XLUC | M2_TH0_MAPX(M2_SPR_TEX_LOG2W + 0u) | M2_TH0_MAPY(M2_SPR_TEX_LOG2H + 0u);
        *fifo = (m2__spr_palcb[(cb >> 5) & 31u] >> (cb & 31u)) & 1u ? M2_SPR_LUMABASE : 0u;   /* th1: lumabase */
        *fifo = M2_TH2_TILE(M2_SPR_TEX_X, M2_SPR_TEX_Y);
        *fifo = M2_TH3_COLORBASE(cb);
    }
#ifdef M2_SPR_COP
    /* UVs in model 456's vertex order: BR, BL, TL, TR (m2_text.h's glyphs) */
    *fifo = GEO_OP_TEXDATA; *fifo = uv; *fifo = 8u;
    *fifo = v1; *fifo = u1;
    *fifo = v1; *fifo = u0;
    *fifo = v0; *fifo = u0;
    *fifo = v0; *fifo = u1;
    m2__spr_cop_quad(x0, y0, x1, y1, uv, tha, layer + 8);
#else
    {
    int k = layer + 1;
    u32 zf = 0x3F800000u - ((u32)k << 23);
    u32 l = m2__spr_f(x0 - 248, k), r = m2__spr_f(x1 - 248, k);
    u32 t = m2__spr_f(192 - y0, k), b = m2__spr_f(192 - y1, k);
    /* UVs (1/8 texel) per vertex, (v,u) pairs: TL, TR, BR, BL */
    *fifo = GEO_OP_TEXDATA; *fifo = uv; *fifo = 8u;
    *fifo = v0; *fifo = u0;
    *fifo = v0; *fifo = u1;
    *fifo = v1; *fifo = u1;
    *fifo = v1; *fifo = u0;
    /* the quad: TR, TL, [attr, luma, distance], BR, BL, end. Distance 0x45800000 = texlod
     * 0x600 (m2_gfx2d.h G2D_TEX_LOD0), another guard on mip level 0 */
    *fifo = GEO_OP_DIRECT;
    *fifo = uv; *fifo = tha;
    *fifo = r; *fifo = t; *fifo = zf;
    *fifo = l; *fifo = t; *fifo = zf;
    *fifo = GEO_POLY_QUAD | M2_SPR_ZMIN; *fifo = 0xFFu << 23; *fifo = 0x45800000u;
    *fifo = r; *fifo = b; *fifo = zf;
    *fifo = l; *fifo = b; *fifo = zf;
    *fifo = 0u; *fifo = 0u;
    }
#endif
    m2__spr_nq++;
}

/* Draw image id with its top-left at screen (x,y), scaled to dw x dh pixels. pen_cb[p] is the
 * colorbase of pen p (1..15); 0 leaves that pen out. flags: M2_SPR_FLIPX / M2_SPR_FLIPY. */
static void m2_spr_draw_scaled(int id, int x, int y, int dw, int dh, u32 flags,
                               const u16 *pen_cb, int layer) {
    const m2_spr_img_t *im;
    int x0 = x, y0 = y, x1 = x + dw, y1 = y + dh, i;
    u32 cu0, cv0, cu1, cv1;                         /* the visible part, in 1/8 texels of the image */
    if (id < 0 || id >= m2__spr_nimg || dw <= 0 || dh <= 0) return;
    im = &m2__spr_img[id];
    if (x0 < m2_spr_clip_x0) x0 = m2_spr_clip_x0;
    if (y0 < m2_spr_clip_y0) y0 = m2_spr_clip_y0;
    if (x1 > m2_spr_clip_x1) x1 = m2_spr_clip_x1;
    if (y1 > m2_spr_clip_y1) y1 = m2_spr_clip_y1;
    if (x0 >= x1 || y0 >= y1) return;
    if (layer < 0) layer = 0;
    if (layer >= M2_SPR_LAYERS) layer = M2_SPR_LAYERS - 1;
    /* crop -> texture offsets; a flip mirrors which edge of the image is cut */
    cu0 = (u32)((x0 - x) * im->w * 8 / dw);  cu1 = (u32)((x1 - x) * im->w * 8 / dw);
    cv0 = (u32)((y0 - y) * im->h * 8 / dh);  cv1 = (u32)((y1 - y) * im->h * 8 / dh);
    if (flags & M2_SPR_FLIPX) { u32 a = (u32)im->w * 8u - cu1; cu1 = (u32)im->w * 8u - cu0; cu0 = a; }
    if (flags & M2_SPR_FLIPY) { u32 a = (u32)im->h * 8u - cv1; cv1 = (u32)im->h * 8u - cv0; cv0 = a; }
    for (i = 0; i < im->n; i++) {
        const m2_spr_mask_t *m = &m2__spr_mask[im->first + i];
        u32 cb = pen_cb[m->pen], mu = (u32)m->u * 8u, mv = (u32)m->v * 8u;
        u32 u0 = mu + cu0, u1 = mu + cu1, v0 = mv + cv0, v1 = mv + cv1;
        if (!cb || m2__spr_nq >= M2_SPR_MAX_QUADS) continue;
        if (flags & M2_SPR_FLIPX) { u32 a = u0; u0 = u1; u1 = a; }
        if (flags & M2_SPR_FLIPY) { u32 a = v0; v0 = v1; v1 = a; }
        m2__spr_quad(x0, y0, x1, y1, u0, v0, u1, v1, cb, layer);
    }
}

/* Draw image id at its own size, top-left at screen (x,y). */
static void m2_spr_draw(int id, int x, int y, u32 flags, const u16 *pen_cb, int layer) {
    if (id < 0 || id >= m2__spr_nimg) return;
    m2_spr_draw_scaled(id, x, y, m2__spr_img[id].w, m2__spr_img[id].h, flags, pen_cb, layer);
}

/* ---- palette mode: a colour per texel --------------------------------------------------------
 * The masks above cost one quad per pen. In palette mode an image is ONE quad whose texels are its
 * pens: the texel's luma picks the colour. Texel t goes through lumaram (lumabase M2_SPR_LUMABASE,
 * set in the colorbase's texture header) to luma 4 * (15 - t) + 2, and luma L reads colorxlat
 * entry L of the colorbase's row (model2rd.ipp draw_scanline_tex; the colorbase's palram colour
 * is (row,row,row), so R, G and B all read row `row`). Entries 4p..4p+3 of the row hold pen p's
 * colour, so a pen keeps its colour through the GEO's luma scaling and the small sampling error.
 * A texel is the pen XOR 0xF: pen 0 is texel 0xF, transparent.
 *
 * The bilinear filter only reproduces a texel exactly when the quad is drawn 1:1 at whole pixels
 * (m2_spr_draw_tex does that; flips too): scaled, the in-between lumas land on other pens.
 *
 *     m2_spr_palette_init();                        // once: the lumaram table
 *     m2_spr_palette(30, 1);                        // colorbase 30 = palette row 1 (0..31)
 *     m2_spr_palette_pen(1, 5, M2_RGB(31,0,0));     // row 1, pen 5 = red (1..15)
 *     m2_spr_tex_cell(0, 0, rows);                  // an 8x8 cell of pens into the atlas at (0,0)
 *     ...
 *     m2_spr_draw_tex(x, y, 8, 8, 0, 0, 0, 30, 0);  // between m2_spr_frame_setup and commit
 *
 * A row used as a palette here is not a flat colour (m2_spr_flat_colors writes every row). The
 * tile plane's colours use entry 0x40 of each row (m2_init), which stays as it is. */

/* The lumaram table. Once, before drawing in palette mode. */
static void m2_spr_palette_init(void) {
    volatile u16 *L = (volatile u16 *)M2_LUMARAM + (M2_SPR_LUMABASE << 7);
    int i;
    for (i = -4; i < 124; i++) L[i] = (u16)((15 - (i + 4) / 8) * 4 + 2);   /* texel*8 - 4 .. + 3 */
}

/* Colorbase cb draws in palette mode with the colours of row (0..31). */
static void m2_spr_palette(u32 cb, u32 row) {
    *(volatile u16 *)(M2_PALRAM + (cb + 0x1000u) * 2u) = (u16)((row & 31u) * 0x421u | M2_PAL_SET);
    m2__spr_palcb[(cb >> 5) & 31u] |= 1u << (cb & 31u);
}

/* Pen (1..15) of palette row (0..31) = bgr555. */
static void m2_spr_palette_pen(u32 row, u32 pen, u16 bgr555) {
    volatile u16 *R = (volatile u16 *)(M2_COLORXLAT + 0x0000u) + row * 256u + pen * 4u;
    volatile u16 *G = (volatile u16 *)(M2_COLORXLAT + 0x4000u) + row * 256u + pen * 4u;
    volatile u16 *B = (volatile u16 *)(M2_COLORXLAT + 0x8000u) + row * 256u + pen * 4u;
    u16 r = (u16)M2_SPR_XLAT(bgr555 & 31u), g = (u16)M2_SPR_XLAT((bgr555 >> 5) & 31u);
    u16 b = (u16)M2_SPR_XLAT((bgr555 >> 10) & 31u);
    R[0] = r; R[1] = r; R[2] = r; R[3] = r;
    G[0] = g; G[1] = g; G[2] = g; G[3] = g;
    B[0] = b; B[1] = b; B[2] = b; B[3] = b;
}

/* An 8x8 cell of pens into the atlas at (tx,ty) (texels, multiples of 8, inside the atlas),
 * for palette mode. rows[r] is row r, eight 4-bit pens, the leftmost in bits 31..28 (a Mega
 * Drive pattern row). Eight stores: texram0 holds texels in 2x2 blocks, a 32-bit word being
 * columns 0..3 of two rows (model2rd.ipp get_texel). Only texram0: mip level 0 reads nothing
 * else, and m2_spr_draw_tex never leaves level 0. Fast enough to run per frame. */
static void m2_spr_tex_cell(u32 tx, u32 ty, const u32 *rows) {
    volatile u32 *T = (volatile u32 *)0x11000000u + ((M2_SPR_TEX_Y + ty) >> 1) * 256u + ((M2_SPR_TEX_X + tx) >> 2);
    int r;
    for (r = 0; r < 8; r += 2, T += 256) {
        u32 a = ~rows[r], b = ~rows[r + 1];                    /* texel = pen ^ 0xF */
        u32 t = (a & 0xff00ff00u) | ((b >> 8) & 0x00ff00ffu); /* a0a1 b0b1 a4a5 b4b5 */
        u32 u = ((a << 8) & 0xff00ff00u) | (b & 0x00ff00ffu); /* a2a3 b2b3 a6a7 b6b7 */
        T[0] = (u & 0xffff0000u) | (t >> 16);
        T[1] = (u << 16) | (t & 0xffffu);
    }
}

/* Draw the atlas rectangle (tu,tv) w x h 1:1 with its top-left at screen (x,y), in colorbase cb
 * (palette mode, or a mask's colour), cut to the clip rectangle. flags: M2_SPR_FLIPX / FLIPY.
 * One quad; nothing when the frame's M2_SPR_MAX_QUADS are used. */
static void m2_spr_draw_tex(int x, int y, int w, int h, u32 tu, u32 tv, u32 flags, u32 cb, int layer) {
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    u32 cu0, cv0, cu1, cv1, u0, u1, v0, v1;
    if (x0 < m2_spr_clip_x0) x0 = m2_spr_clip_x0;
    if (y0 < m2_spr_clip_y0) y0 = m2_spr_clip_y0;
    if (x1 > m2_spr_clip_x1) x1 = m2_spr_clip_x1;
    if (y1 > m2_spr_clip_y1) y1 = m2_spr_clip_y1;
    if (x0 >= x1 || y0 >= y1 || m2__spr_nq >= M2_SPR_MAX_QUADS) return;
    if (layer < 0) layer = 0;
    if (layer >= M2_SPR_LAYERS) layer = M2_SPR_LAYERS - 1;
    cu0 = (u32)(x0 - x); cu1 = (u32)(x1 - x); cv0 = (u32)(y0 - y); cv1 = (u32)(y1 - y);
    if (flags & M2_SPR_FLIPX) { u32 a = (u32)w - cu1; cu1 = (u32)w - cu0; cu0 = a; }
    if (flags & M2_SPR_FLIPY) { u32 a = (u32)h - cv1; cv1 = (u32)h - cv0; cv0 = a; }
    u0 = (tu + cu0) * 8u; u1 = (tu + cu1) * 8u; v0 = (tv + cv0) * 8u; v1 = (tv + cv1) * 8u;
    if (flags & M2_SPR_FLIPX) { u32 a = u0; u0 = u1; u1 = a; }
    if (flags & M2_SPR_FLIPY) { u32 a = v0; v0 = v1; v1 = a; }
    m2__spr_quad(x0, y0, x1, y1, u0, v0, u1, v1, cb, layer);
}

#endif /* M2_SPRITE_H */
