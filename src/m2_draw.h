/*
 * m2_draw.h — 2D FILLED SHAPES + outlines via the GEO DIRECT-data path.
 *
 * Each primitive is a screen-space quad submitted straight to the GEO DIRECT handler (FIFO cmd
 * 0x01000202): OUR OWN pixel-coord verts + OUR OWN per-quad material (tha) INLINE — no ROM mesh, no
 * per-face material walk. That is what gives this path two things the COP/model-456 object path could
 * not: a DIFFERENT colorbase per quad (multi-colour in one frame) and a clean per-quad z-LAYER (the
 * depth pz is a pure sort key; the emitter pre-multiplies coords so position is fixed). Flat fills go
 * untextured; ellipses/circles sample a small disc texture; text samples the font atlas (m2_text.h).
 * MAME-proven (real cpres2) across multi-colour shapes, text, outlines, xeyes; silicon burn pending.
 *
 * FRAME MODEL — these calls only EMIT geometry; wrap them in a frame (see m2_geo.h m2_frame_*):
 *     m2_frame_begin();
 *     m2_draw_frame_setup();                     // ONCE per frame: the DIRECT render-state prelude
 *     m2_fill_rect(x, y, w, h, cb);              // top-left x,y
 *     m2_fill_ellipse(cx, cy, rx, ry, cb);       // centre + radii
 *     m2_fill_circle(cx, cy, r, cb);
 *     m2_fill_rect_o / _ellipse_o / _circle_o(..., fill_cb, outline_cb, thickness);  // bordered
 *     m2_frame_commit();   // or m2_frame_end() to also wait on the frame-done IRQ
 * Redraw every frame. The host must boot+prime the GEO/COP once first (see m2_frame_* notes).
 *
 * COLOUR: cb = colorbase = HUE (set palram[cb+0x1000] to a BGR555 colour via m2_draw_color()).
 * m2_set_luma(0..255) = brightness/shade (textured fills honour it; the flat-fill path saturates).
 * DEPTH/LAYER: m2_text_z (the screen-text depth global) is the pz for shapes too — smaller = nearer.
 */
#ifndef M2_DRAW_H
#define M2_DRAW_H

#include "m2_text.h"   /* m2_font_atlas/m2_texram0_texel (m2font.h), screen->world globals, cop_submit_object, m2_cam, g_flatquad */

/* shape textures live in texram0 just BELOW the 128x64 font atlas (y >= 64). Sampled with a 128x128
 * texture header (th0 hbits=2) + absolute UV, exactly like the glyphs. */
#define M2_CIRC_X   0u
#define M2_CIRC_Y   64u
#define M2_CIRC_D   64u     /* circle diameter (texels) */
#define M2_SOLID_X  64u
#define M2_SOLID_Y  64u
#define M2_SOLID_D  32u     /* solid block side (32 = min valid tile size for the rect's textured fill) */

static int m2__shapes_loaded = 0;

/* Set a colorbase's ink colour (BGR555). palram[cb+0x1000]'s 5-bit R/G/B pick the colorxlat row/channel. */
static void m2_draw_color(u32 cb, u16 bgr555) {
    *(volatile u16 *)(0x01800000u + (cb + 0x1000u) * 2u) = bgr555;
}

/* Write the SOLID + CIRCLE shape textures into texram0 (once). ink=14, outside=0xf (transparent). */
static void m2_shapes_init(void) {
    u32 x, y; int r = (int)(M2_CIRC_D / 2), r2 = r * r;
    if (m2__shapes_loaded) return;
    m2_font_atlas();   /* ensure the font region is also present (shared texram header) */
    for (y = 0; y < M2_CIRC_D; y++)
        for (x = 0; x < M2_CIRC_D; x++) {
            int dx = (int)x - r, dy = (int)y - r;
            m2_texram0_texel((int)(M2_CIRC_X + x), (int)(M2_CIRC_Y + y),
                           (u8)((dx * dx + dy * dy <= r2) ? 14 : 0x0f));
        }
    for (y = 0; y < M2_SOLID_D; y++)
        for (x = 0; x < M2_SOLID_D; x++)
            m2_texram0_texel((int)(M2_SOLID_X + x), (int)(M2_SOLID_Y + y), 14);
    m2__shapes_loaded = 1;
}

/* ---- DIRECT-path screen-space quads (per-quad colorbase via the wire tha -> MULTI-COLOUR) ----------
 * The GEO DIRECT handler (cmd 0x01000202) takes our own screen verts + our own tha INLINE, with NO ROM
 * mesh + NO per-face material walk (the model-456 limitation that capped us to one colour/frame). Verts
 * are IEEE-754 floats = pixel offset from the window centre (~248,192): screen = 248 + vx/pz, 192 - vy/pz.
 * pz scales the verts so position is fixed while pz acts as the depth/LAYER (z-sort). */
static u32 m2__fb(float f) { union { float f; u32 u; } x; x.f = f; return x.u; }

/* Per-quad LUMA (brightness), 0..255. The colorbase picks the HUE (which colorxlat R/G/B rows); luma
 * scales the intensity within those rows. At luma 255 every non-zero channel saturates (so colours
 * collapse to the 8 on/off combos); LOWER luma gives shades (e.g. a white colorbase at luma 0x60 = grey).
 * Set via m2_set_luma (host may wrap it, e.g. gs_set_luma); the poly luma word is (m2_draw_luma << 23). */
static u32 m2_draw_luma = 0xFFu;
static void m2_set_luma(u32 l) { m2_draw_luma = l & 0xFFu; }

/* DIRECT-path per-FRAME render state (window / projection / light / clip + TEXPARAM). The DIRECT
 * shape & text primitives below only EMIT geometry — call this ONCE per frame, right after
 * m2_frame_begin(), so this ~70-word prelude is NOT re-sent per primitive. (Re-sending it per quad
 * was the old smoothness/perf killer: a dashed line of N quads paid N preludes.) Prelude from
 * app_directtest, proven to reach the rasterizer. */
static void m2_draw_frame_setup(void) {
    volatile u32 *fifo = (volatile u32 *)0x00804000u;
    m2_geo_cmd(0x080u); *fifo = m2__fb(4.0f);                        /* ZSORT granularity (coarse) */
    m2_geo_cmd(0x090u); *fifo = m2__fb(280.0f); *fifo = m2__fb(280.0f); /* FOCAL distance */
    m2_geo_fifo_light(0x3F34CA6Eu, 0xBF3167ABu, 0x3E147F30u);
    m2_geo_fifo_window_full();
    m2_geo_fifo_texparam(0x000010FFu);
}

/* flat-colour DIRECT quad over screen rect (x,y,w,h), colorbase cb, depth pz. */
static void m2_direct_rect(float x, float y, float w, float h, u32 cb, float pz) {
    volatile u32 *fifo = (volatile u32 *)0x00804000u;
    u32 tha = GEO_TEXRAM_BIT | (cb * 4u), zf = m2__fb(pz);
    float l = (x - 248.0f) * pz,  r = ((x + w) - 248.0f) * pz;
    float t = (192.0f - y) * pz,  b = (192.0f - (y + h)) * pz;
    /* render state is emitted ONCE per frame by m2_draw_frame_setup(); here just the header + quad */
    /* flat colorbase header at THIS cb's own tha slot (cb*4) */
    *fifo = GEO_OP_TEXDATA; *fifo = tha; *fifo = 4u;
    *fifo = 0u; *fifo = 0u; *fifo = 0u; *fifo = (cb & 0x3ffu) << 6;
    /* the DIRECT quad: TR, TL, [polyHdr, luma, dist], BR, BL, sentinel */
    *fifo = GEO_OP_DIRECT;
    *fifo = 0u; *fifo = tha;
    *fifo = m2__fb(r); *fifo = m2__fb(t); *fifo = zf;
    *fifo = m2__fb(l); *fifo = m2__fb(t); *fifo = zf;
    *fifo = GEO_POLY_QUAD; *fifo = (m2_draw_luma << 23); *fifo = 0u;
    *fifo = m2__fb(r); *fifo = m2__fb(b); *fifo = zf;
    *fifo = m2__fb(l); *fifo = m2__fb(b); *fifo = zf;
    *fifo = 0u; *fifo = 0u;
}

/* textured+translucent DIRECT quad over screen rect (x,y,w,h): sample the th2 tile (texx,texy) and map
 * the UV sub-rect (u0,v0)+(uw,vh) texels across the quad, colorbase cb, depth pz. th0 carries
 * textured+translucent + the tile dims. UV sub-rect lets glyphs (8x8 at sub-tile offsets) index a tile
 * whose origin is 32px-granular. NOTE: the UV TEXDATA write needs the 0x800000 texture_ram select bit
 * (same as the header) — without it the UVs never land and the renderer reads stale config = arcs. */
static void m2_direct_tquad_uv(float x, float y, float w, float h, u32 cb, float pz, u32 th0,
                               u32 texx, u32 texy, u32 u0, u32 v0, u32 uw, u32 vh) {
    volatile u32 *fifo = (volatile u32 *)0x00804000u;
    u32 tha = GEO_TEXRAM_BIT | (cb * 4u), zf = m2__fb(pz);
    u32 th2 = ((texx / 32u) & 0x3fu) | (((texy / 32u) & 0x1fu) << 6);
    u32 uvoff = 0x40u;
    u32 pu0 = u0 << 3, pv0 = v0 << 3, pu1 = (u0 + uw) << 3, pv1 = (v0 + vh) << 3;
    float l = (x - 248.0f) * pz,  r = ((x + w) - 248.0f) * pz;
    float t = (192.0f - y) * pz,  b = (192.0f - (y + h)) * pz;
    /* render state is emitted ONCE per frame by m2_draw_frame_setup() */
    /* texture header (cb's slot) + per-vert UVs (v0 TL, v1 TR, v2 BR, v3 BL) */
    *fifo = GEO_OP_TEXDATA; *fifo = tha; *fifo = 4u;
    *fifo = th0; *fifo = 0u; *fifo = th2; *fifo = (cb & 0x3ffu) << 6;
    *fifo = GEO_OP_TEXDATA; *fifo = GEO_TEXRAM_BIT | uvoff; *fifo = 8u;   /* texture_ram select bit (as header) */
    *fifo = pv0; *fifo = pu0;   /* v0 TL */
    *fifo = pv0; *fifo = pu1;   /* v1 TR */
    *fifo = pv1; *fifo = pu1;   /* v2 BR */
    *fifo = pv1; *fifo = pu0;   /* v3 BL */
    /* DIRECT quad: tpa=UV slot; verts TR,TL,BR,BL.
     * dist word = texlod (handler decodes texlod = ((w>>8)&0x7f80) - 0x3f80 + log_ram[w&0x7fff], then
     * mml = -texlod + log2(z), level = clamp(mml>>7,0,max)). 0x3F800000 -> texlod 0 -> the NATURAL
     * z-based mip, which for our glyph depths is the level that actually renders the atlas correctly.
     * NOTE: do NOT force level 0 here (0x7F800000 drives mml hugely negative -> level 0): level 0 of this
     * atlas renders as SOLID BLOCKS in MAME (verified) — every glyph collapses to a filled cell. So keep
     * the natural mip. Our pz is a 2D LAYER not a real depth, so the GEO's z-based auto-mip can't pick by
     * glyph SIZE — small-text mip selection is done MANUALLY (see m2_text.h / m2_draw_glyph_px). */
    *fifo = GEO_OP_DIRECT;
    *fifo = GEO_TEXRAM_BIT | uvoff; *fifo = tha;
    *fifo = m2__fb(r); *fifo = m2__fb(t); *fifo = zf;
    *fifo = m2__fb(l); *fifo = m2__fb(t); *fifo = zf;
    *fifo = GEO_POLY_QUAD; *fifo = (m2_draw_luma << 23); *fifo = m2__fb(1.0f);  /* dist = texlod 0 (natural mip) */
    *fifo = m2__fb(r); *fifo = m2__fb(b); *fifo = zf;
    *fifo = m2__fb(l); *fifo = m2__fb(b); *fifo = zf;
    *fifo = 0u; *fifo = 0u;
}

/* full-tile textured quad (UV 0..tw, 0..th). */
static void m2_direct_tquad(float x, float y, float w, float h, u32 cb, float pz,
                            u32 th0, u32 texx, u32 texy, u32 tw, u32 th) {
    m2_direct_tquad_uv(x, y, w, h, cb, pz, th0, texx, texy, 0u, 0u, tw, th);
}

static void m2_fill_ellipse(float cx, float cy, float rx, float ry, u32 cb) {
    m2_shapes_init();
    /* th0 = textured(bit14)+translucent(bit13) + 64x64 (wbits=1,hbits=1) = 0x6009 */
    m2_direct_tquad(cx - rx, cy - ry, rx * 2.0f, ry * 2.0f, cb, m2_text_z,
                    0x6009u, M2_CIRC_X, M2_CIRC_Y, M2_CIRC_D, M2_CIRC_D);
}
static void m2_fill_circle(float cx, float cy, float r, u32 cb) { m2_fill_ellipse(cx, cy, r, r, cb); }

static void m2_fill_rect(float x, float y, float w, float h, u32 cb) {
    m2_direct_rect(x, y, w, h, cb, m2_text_z);   /* flat untextured quad (proven multi-colour + layering) */
}

/* ---- OUTLINES / BORDERS ---------------------------------------------------------------------------
 * A border is the SAME shape grown by `t` pixels, drawn one z-LAYER behind the fill in its own colorbase
 * (the single-colorbase-per-quad model can't put two colours in one quad). The fill draws over the
 * grown shape's centre, leaving a `t`-pixel ring of outline_cb showing around it. m2_outline_dz is the
 * depth the border sits behind the fill — an INTEGER step (1.0) lands in a clearly separate z-sort bucket;
 * space your own layers accordingly (e.g. fill at z=2 puts its border at z=3). */
static float m2_outline_dz = 1.0f;

static void m2_fill_rect_o(float x, float y, float w, float h, u32 fill_cb, u32 outline_cb, float t) {
    float z = m2_text_z;
    if (t > 0.0f) { m2_text_z = z + m2_outline_dz;
                    m2_fill_rect(x - t, y - t, w + 2.0f * t, h + 2.0f * t, outline_cb); }
    m2_text_z = z; m2_fill_rect(x, y, w, h, fill_cb);
}
static void m2_fill_ellipse_o(float cx, float cy, float rx, float ry, u32 fill_cb, u32 outline_cb, float t) {
    float z = m2_text_z;
    if (t > 0.0f) { m2_text_z = z + m2_outline_dz;
                    m2_fill_ellipse(cx, cy, rx + t, ry + t, outline_cb); }
    m2_text_z = z; m2_fill_ellipse(cx, cy, rx, ry, fill_cb);
}
static void m2_fill_circle_o(float cx, float cy, float r, u32 fill_cb, u32 outline_cb, float t) {
    m2_fill_ellipse_o(cx, cy, r, r, fill_cb, outline_cb, t);
}

/* ---- SCREEN-SPACE text via the DIRECT path (per-glyph colorbase -> MULTI-COLOUR text) -------------
 * The font atlas (gFont -> texram0, 128x64) is sampled per glyph by a DIRECT textured+translucent quad
 * with a UV sub-rect = the glyph's 8x8 cell. Unlike the COP/model-456 glyph (one colour/frame), each
 * DIRECT glyph carries its own inline tha, so colour is per-glyph and z (pz=m2_text_z) is a pure layer.
 * Atlas orientation matches the natural DIRECT UV (verified: the full atlas renders upright). */
static float m2_text_px_advance = 1.0f;   /* glyph advance as a fraction of the cell size sz */

/* Screen-space glyph: a DIRECT textured+translucent quad sampling the glyph's 8x8 cell in the 128x64
 * font atlas. (A small-text AA "mipA" was tried but reverted — the LOD0->mipA size selection hit a
 * soft-float miscompile; small text is the plain LOD0 downsample, legible but blocky < ~6px.) */
static void m2_draw_glyph_px(float px, float py, float sz, u32 cb, u32 c) {
    u32 ax = (c & 15u) * 8u, ay = ((c >> 4) & 7u) * 8u;   /* glyph cell in the 128x64 (16x8) atlas */
    m2_font_atlas();
    /* th0=0x600A: textured(bit14)+translucent(bit13) + 128 wide (wbits=2) + 64 tall (hbits=1). */
    m2_direct_tquad_uv(px, py, sz, sz, cb, m2_text_z, 0x600Au, 0u, 0u, ax, ay, 8u, 8u);
}

/* NUL-terminated string from screen pixel (px,py) top-left, sz px/glyph cell, ink colorbase cb. */
static void m2_draw_text_px(float px, float py, float sz, u32 cb, const char *s) {
    float x = px, adv = sz * m2_text_px_advance;
    u32 i;
    for (i = 0u; s[i]; i++) {
        u32 c = (u32)(unsigned char)s[i];
        if (c != 32u) m2_draw_glyph_px(x, py, sz, cb, c);
        x += adv;
    }
}

#endif /* M2_DRAW_H */
