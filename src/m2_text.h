/*
 * m2_text.h — 3D TEXT GLYPHS via the COP object_data path (silicon-capable).
 *
 * Renders the built-in 8x8 font (gFont) as textured+translucent quads instanced through the COP
 * (model 456, the flat quad). This is the path proved on Model 2B silicon (object 1 + the ice cube):
 * direct_data text (m2_gfx2d.h g2d_ttext) is MAME-HLE only; this object_data path runs on hardware.
 *
 * Use:
 *     m2_font_atlas();                                  // ONCE: gFont -> texram0 (CPU write)
 *     // inside a GEO frame (after the camera is set up), then commit:
 *     m2_draw_text(wx, wy, wz, scale, colorbase, "HELLO");
 *     m2_draw_glyph(wx, wy, wz, scale, colorbase, 'A'); // one glyph
 *
 * Customise:
 *   - position  : world (wx,wy,wz) of the string origin (left edge)
 *   - scale     : model 456 is 12 world units wide; screen px ~= 2400*scale at z~1.4 (~0.012 = ~29px)
 *   - colorbase : the ink hue (its palram[cb+0x1000] entry); texel 0xf is transparent (shows through)
 *   - m2_text_advance : per-glyph advance in model-widths (default 12.5; lower = tighter)
 *   - the font camera is the current m2_cam (set m2_cam_set_eye/angles before drawing)
 *
 * Depends on: m2_3d.h (m2_cam, cop_submit_object, m2_cop_pf, m2_geo_pf, ZCLIP_REG), m2_geo.h
 * (g_flatquad via geo_flatquad_init(), GEO_TEXRAM_BIT), m2font.h (gFont).
 */
#ifndef M2_TEXT_H
#define M2_TEXT_H

#include "m2font.h"     /* gFont[128*32] 8x8 4bpp */
#include "m2_3d.h"      /* COP/camera helpers + cop_submit_object */
#include "m2_geo.h"     /* g_flatquad (model 456), GEO_TEXRAM_BIT */

#define M2_TEXT_HDR_SLOT 0x100u   /* GEO texture_ram base slot: header (4 words) */
#define M2_TEXT_UV_SLOT  0x108u   /* GEO texture_ram base slot: UVs    (8 words) */

/* tha/tpa are POINTERS into texture_ram; if every poly uses the same slot, they all read the LAST
 * header written at raster time = one colour/shape for the whole frame. Rotate the slot per draw so
 * each poly gets its OWN header+UV (header at 0x100+i*0x10, UV at +8), wrapping every 16 draws. */
static u32 m2__tex_slot = 0u;
static u32 m2__next_slot(void) { u32 s = 0x100u + (m2__tex_slot * 0x10u); m2__tex_slot = (m2__tex_slot + 1u) & 15u; return s; }

/* per-glyph advance, in model-456 widths * scale (tweak for tighter/looser spacing). */
static float m2_text_advance = 12.5f;

/* m2_texram0_texel + m2_font_atlas are shared (m2font.h). */

/* Draw ONE glyph c (ASCII) at world (wx,wy,wz), uniform scale, ink colorbase cb. The glyph is a
 * textured+translucent quad: model 456 instanced via the COP (ang_x=90 flips it from the XZ plane to
 * face the camera) with the font-atlas header + the glyph's UV rect (V & U flipped to read upright).
 * Call inside a GEO frame, with the camera already aimed; commit after. */
static void m2_draw_glyph(float wx, float wy, float wz, float scale, u32 cb, u32 c) {
    volatile u32 *cf   = (volatile u32 *)0x00884000u;   /* COP FIFO */
    volatile u32 *fifo = (volatile u32 *)0x00804000u;   /* GEO command FIFO */
    u32 ax = (c & 15u) * 8u, ay = ((c >> 4) & 7u) * 8u;  /* glyph rect in the 128x64 atlas */
    union { float f; u32 u; } sc; sc.f = scale;
    u32 hdr[4];
    u32 hslot = m2__next_slot(), uslot = hslot + 8u;   /* per-glyph header+UV slot (per-poly colour) */
    m2_font_atlas();
    /* GEO frame slots */
    *(volatile u32 *)ZCLIP_REG = 0xFFu;
    m2_cam_geo_proj();
    m2_geo_cmd(0x0A0u);
    *fifo = 0x00000000u; *fifo = 0x00000000u; *fifo = 0x3F800000u;
    m2_geo_cmd(0x030u);
    *fifo = 0x0000007Fu; *fifo = 0x01F001FFu;
    *fifo = 0x00F8013Fu; *fifo = 0x00F8013Fu; *fifo = 0x00F8013Fu; *fifo = 0x00F8013Fu;
    /* TEXPARAM table (0x20 entries): the per-poly luma model — ambient 0x60 | diffuse 0xFF, 1.0. WITHOUT
     * this the cpres2 computes object.luma=0 and the ink renders BLACK regardless of colorbase/palette
     * (luma = lumaram[...] * object.luma / 256). Ambient 0x60 = a bright floor so the ink takes its colour. */
    { int q; *fifo = 0x03000606u; *fifo = 0u; *fifo = 0x20u;
      for (q = 0; q < 0x20; q++) { *fifo = 0x000060FFu; *fifo = 0x3F800000u; } }
    m2_geo_cmd(0x160u);
    m2_geo_pf(5.33333f * m2_cam.focus);
    /* font-atlas texture header (th0: textured bit14 + translucent bit13; 128x64; sheet0; cb) */
    *fifo = 0x02000404u; *fifo = 0x00800000u | hslot; *fifo = 4u;
    *fifo = 0x0000600Au; *fifo = 0u; *fifo = 0u; *fifo = (cb & 0x3ffu) << 6;
    /* per-glyph UV: 4 verts (V,U) TL,TR,BR,BL, scaled <<3. Both axes flipped vs naive mapping to cancel
     * model 456's vert winding + the ang_x=90 flip -> upright, un-mirrored. */
    *fifo = 0x02000404u; *fifo = 0x00800000u | uslot; *fifo = 8u;
    *fifo = ((ay + 8u) << 3); *fifo = ((ax + 8u) << 3);
    *fifo = ((ay + 8u) << 3); *fifo = (ax << 3);
    *fifo = (ay << 3);        *fifo = (ax << 3);
    *fifo = (ay << 3);        *fifo = ((ax + 8u) << 3);
    /* COP: instance model 456 as the textured glyph quad */
    { float w[3], v[3];
      w[0] = wx; w[1] = wy; w[2] = wz;
      m2_cam_world_to_view(w, v);
      *cf = 0x00800101u;                              /* OBJECT push */
      *cf = 0x01800303u;                              /* set_identity */
      *cf = 0x03000606u;                              /* set_pos */
      m2_cop_pf(v[0]); m2_cop_pf(v[1]); m2_cop_pf(v[2]);
      *cf = 0x04800909u; *cf = 0u;                    /* ang_y = 0 */
      *cf = 0x04000808u; *cf = 0x4000u;               /* ang_x = 90deg: flip XZ-plane quad to face camera */
      *cf = 0x05000A0Au; *cf = 0u;                    /* ang_z = 0 */
      *cf = 0x03800707u;                              /* SCALE */
      *cf = sc.u; *cf = sc.u; *cf = sc.u;
      /* poly counts reset once per frame (m2_frame_begin), NOT per submit, so objects thread */
      hdr[0] = 0x00800000u | uslot;         /* tpa = glyph UV   */
      hdr[1] = 0x00800000u | hslot;        /* tha = atlas hdr  */
      hdr[2] = g_flatquad;                            /* oba = model 456  */
      hdr[3] = 0x200u;                                /* obc bound        */
      cop_submit_object(cf, hdr);
      *cf = 0x01000202u;                              /* DIRECT flush */
      { u32 ep = *(volatile u32 *)0x00802008u;
        *(volatile u32 *)(0x00900000u + (ep & 0x0001FFFCu)) = 0x07800F0Fu; } }
}

/* Draw a NUL-terminated string from world (wx,wy,wz): one glyph per char, advancing world-x by
 * scale*m2_text_advance. Spaces blank but still advance. cb = ink colorbase. */
static void m2_draw_text(float wx, float wy, float wz, float scale, u32 cb, const char *s) {
    float x = wx, adv = scale * m2_text_advance;
    u32 i;
    for (i = 0u; s[i]; i++) {
        u32 c = (u32)(unsigned char)s[i];
        if (c != 32u) m2_draw_glyph(x, wy, wz, scale, cb, c);
        x += adv;
    }
}

/* ---- SCREEN-SPACE text (pixel coords, UI-friendly — the path for the X server) ------------------
 * Map a screen pixel position (px,py from the TOP-LEFT) + a pixel glyph height to the world coords the
 * default text camera (m2_cam_set_eye(0,-0.15,0.65), focal 280) projects, then draw. The map is linear
 * at a fixed depth m2_text_z; the constants below are tunable (calibrate once against a known layout).
 * Active raster is 496x384. */
static float m2_text_z   = 1.40f;     /* world depth of the text plane                          */
static float m2_text_wpp = 0.00140f;  /* world units per screen pixel (calibrated: ~+-0.35 across 496px) */
static float m2_text_cx  = 248.0f;    /* screen-x that maps to world-x 0 (raster centre)        */
static float m2_text_cy  = 192.0f;    /* screen-y that maps to world-y 0                        */
static float m2_text_spp = 0.000117f; /* model-456 scale per pixel of glyph height (= wpp/12)    */

static void m2_text_screen(float px, float py, float px_h, u32 cb, const char *s) {
    float wx = (px - m2_text_cx) * m2_text_wpp;
    float wy = (m2_text_cy - py) * m2_text_wpp;   /* screen-y down -> world-y up */
    float scale = px_h * m2_text_spp;
    m2_draw_text(wx, wy, m2_text_z, scale, cb, s);
}

#endif /* M2_TEXT_H */
