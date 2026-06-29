/*
 * m2_obj.h — silicon (real SHARC/GEO) object & flat-quad rendering for the m2 SDK.
 *
 * The COP-bridge path that renders GEO model-table objects on REAL Model 2B hardware (and MAME with
 * M2_HLE_GEO_OFF): the COP transforms a model's verts into BUFF_RAM and the real cpres2 GEO
 * rasterizes them. A raw GEO-list / BUFF_RAM write does NOT commit on silicon — geometry must be
 * driven through the COP FIFO (the polygon_submit op 0x78). Distilled from m2-snake's ice-cube
 * sandbox + m2-x11's gs_draw_object (STF poly_test_camera @0x2368C).
 *
 * Usage:
 *     m2_init();
 *     m2_silicon_boot();                 // ONCE: COP + real GEO boot + colour pipeline + GEO seed
 *     ... set your backdrop / colorbase palette ...
 *     for (;;) {
 *         m2_frame_begin();              // (m2_geo.h) start the GEO display-list frame
 *         m2_obj_frame_setup();          // ONCE/frame: GEO render state + COP projection preamble
 *         m2_obj_submit(model, 0, px,py,pz, ax,ay,az, sx,sy,sz);  // a model-table object (own material)
 *         m2_solid_quad(cb, px,py,pz, ax,ay,az, sx,sy,sz);        // a flat-colour quad (colorbase cb)
 *         m2_frame_commit(); m2_vsync(); // 60fps  (m2_frame_end() = 2-vblank/30fps STF parity)
 *     }
 *
 * Positions/scales are IEEE-754 bit patterns (use m2_obj_fb); angles are 16-bit (0x10000 = 360deg).
 *
 * HARD CONSTRAINT (MAME model2rd.ipp draw_scanline_tex): a textured polygon's texel only supplies the
 * LUMA (brightness) of its SINGLE colorbase hue — there is NO per-texel palette-colour mode on this
 * GEO. Multi-hue scenes therefore need multiple colorbases, one per quad (e.g. a gradient = N colour
 * bands; a checker = interleaved colorbase cells). See m2_solid_quad.
 *
 * The projection basis (m2_obj_frame_setup) is PER-FRAME, not per-object: run it once, then submit
 * many objects. Putting it per-object (the obvious-but-wrong approach) makes a busy scene unusably
 * slow (N * the 46-op COP preamble).
 */
#ifndef M2_OBJ_H
#define M2_OBJ_H

#include "m2_3d.h"             /* m2_3d_boot, m2_cop_initialize, m2_cam_geo_proj, cop_drain, g_cop_p* */
#include "m2_geo.h"            /* geo_func/geo_initialize/geometry_stuff, m2_frame_*, geo__f          */
#include "m2_color.h"          /* m2_color_init, m2_load_poly_palette                                 */
#include "stf_cop_preamble.h"  /* cop_obj_preamble: poly_test_camera COP basis/viewport preamble      */

#define M2_FLAT_QUAD_MODEL 456u   /* STF model-table flat quad (1 face, 12x12, normal +Y, +-6 in XZ) */
#define M2_MODEL_TABLE     0x020E0004u  /* model n's 4-word header {tpa,tha,oba,obc} at +n*16        */

static u32 m2_obj_fb(float f){ union{float f;u32 u;}x; x.f=f; return x.u; }  /* float -> IEEE bits */

/* One-call silicon bring-up — the PROVEN order (each omission breaks it: missing geometry_stuff =>
 * the real GEO firmware walks into an unimplemented SHARC IOP write; missing m2_cop_initialize =>
 * the COP never commits, GEO stuck on header words). Call ONCE after m2_init(); set backdrop/palette
 * after (m2_color_init has loaded the default 1024-colour polygon palette). */
static void m2_silicon_boot(void) {
    m2_3d_boot();            /* COP (cpres1) + real GEO geometrizer (cpres2) firmware upload */
    m2_color_init();         /* colorxlat + lumaram (else everything renders black)          */
    m2_load_poly_palette();  /* STF 1024-colour default polygon palette                      */
    geo_func();              /* prime the GEO command region (GEO_RELATED)                    */
    m2_cop_initialize();     /* arm the COP (wait-ready + write 0)                            */
    geo_initialize();        /* STF GEO init: 4 rotating display-list buffers                */
    geometry_stuff();        /* seed the 5 GEO regions (clip/matrix/microcode) — REQUIRED    */
}

/* ---- flat-colour quad support: a uniform-luma texram patch the colour headers sample -------------
 * A flat fill on real silicon needs a TEXTURED header (th0=0x4012) sampling a uniform-luma region —
 * the untextured path samples garbage on hardware. Patch defaults to (0,896) in sheet 0; override
 * M2_OBJ_PATCH_X/Y if it collides with your textures. */
#ifndef M2_OBJ_PATCH_X
#define M2_OBJ_PATCH_X 0u
#endif
#ifndef M2_OBJ_PATCH_Y
#define M2_OBJ_PATCH_Y 896u
#endif
#define M2_OBJ_PATCH_TH2 (((M2_OBJ_PATCH_X/32u)&0x3fu) | (((M2_OBJ_PATCH_Y/32u)&0x1fu)<<6))

static int m2__obj_patch_done = 0;
/* texel write is shared: m2_texram0_texel (m2font.h). */
/* Fill the 128x128 uniform-luma patch (opaque texel 0xE). Idempotent; call after your texram is set
 * up (also lazily run by m2_solid_quad). */
static void m2_obj_fill_patch(void) {
    int x, y;
    if (m2__obj_patch_done) return;
    for (y = 0; y < 128; y++)
        for (x = 0; x < 128; x++)
            m2_texram0_texel((int)M2_OBJ_PATCH_X + x, (int)M2_OBJ_PATCH_Y + y, 0xEu);
    m2__obj_patch_done = 1;
}
/* Write a flat-colour texture header for colorbase cb into texture_ram (slot cb*4), sampling the
 * patch. Reference it from a submit via tha = 0x00800000 | (cb*4). Emit before the quad that uses it. */
static void m2_obj_color_header(u32 cb) {
    volatile u32 *fifo = (volatile u32 *)0x00804000u;
    *fifo = GEO_OP_TEXDATA;
    *fifo = GEO_TEXRAM_BIT | (cb * 4u);         /* dest = texram header slot cb*4 */
    *fifo = 4u;                                 /* 4 header words */
    *fifo = 0x4012u; *fifo = 0u; *fifo = (u32)M2_OBJ_PATCH_TH2;  /* th0=textured128, th1=0, th2=patch */
    *fifo = (cb & 0x3ffu) << 6;                 /* th3 = colorbase */
}

/* ---- per-FRAME projection setup (run ONCE per frame after m2_frame_begin) ------------------------
 * GEO render state (FOCAL/LIGHT/WINDOW/TEXPARAM/LOD) + the STF poly_test_camera BASIS+VIEWPORT
 * preamble (drained per op or the COP output FIFO fills and stalls before a submit). */
static void m2_obj_frame_setup(void) {
    volatile u32 *cf   = (volatile u32 *)0x00884000u;            /* COP FIFO */
    volatile u32 *fifo = (volatile u32 *)0x00804000u;            /* GEO FIFO */
    m2_cam_geo_proj();                                           /* GEO FOCAL slot 0x90 */
    m2_geo_fifo_light(0x3F3504EDu, 0xBF2F9844u, 0x3E2FEF42u);    /* slot 0xA0 LIGHT */
    m2_geo_fifo_window_full();                                   /* WINDOW + clip   */
    m2_geo_fifo_texparam(0x000060FFu);                           /* TEXPARAM table  */
    *fifo = GEO_OP_LOD; *fifo = geo__f(2560.0f);                 /* base mip 2560.0 */
    cop_emit_obj_preamble(cf);                                   /* STF projection+basis preamble */
}

/* ---- per-OBJECT submit (after m2_obj_frame_setup; threads multiple objects via g_cop_p) ----------
 * Transform stream (identity/pos/angles/scale) + polygon_submit(0x78) = the COMMIT, then the GEO END
 * mark. tha_override != 0 replaces the model's material header (e.g. a flat colorbase header for model
 * 456); 0 = use the model's own. */
static void m2_obj_submit(u32 model_no, u32 tha_override, u32 px, u32 py, u32 pz,
                          u32 ax, u32 ay, u32 az, u32 sxb, u32 syb, u32 szb) {
    volatile u32 *cf = (volatile u32 *)0x00884000u;
    volatile const u32 *hdr = (volatile const u32 *)(M2_MODEL_TABLE + model_no * 16u);
    u32 tha = tha_override ? tha_override : hdr[1];
    *cf = COP_IDENTITY;                                          /* set_identity */
    *cf = COP_SET_POS; *cf = px; *cf = py; *cf = pz;             /* set_pos */
    *cf = COP_ANG_Y; *cf = ay;
    *cf = COP_ANG_X; *cf = ax;
    *cf = COP_ANG_Z; *cf = az;
    *cf = COP_SCALE; *cf = sxb; *cf = syb; *cf = szb;            /* per-axis scale */
    *cf = COP_FADD; *cf = COP_FADD; *cf = COP_FADD;              /* fadd x3 fence */
    cop_drain(cf, 1u);
    { u32 wr = *(volatile u32 *)0x00802008u;                     /* COP_WPOS */
      *(volatile u32 *)0x00801008u = wr + 0x48u;                 /* GEO_WRITE = COP_WPOS + 0x48 */
      *cf = COP_SUBMIT; *cf = wr; *cf = 0u;                      /* polygon_submit (op 0x78) */
      *cf = hdr[0]; *cf = tha; *cf = hdr[2]; *cf = hdr[3];       /* tpa / tha(maybe override) / oba / obc */
      *cf = g_cop_p2; *cf = g_cop_p;                             /* P2_POLYGON, POLYGON (running) */
      g_cop_p2 = *cf; g_cop_p = *cf;                             /* read back + save */
      { u32 ep = *(volatile u32 *)0x00802008u;                   /* GEO END mark */
        *(volatile u32 *)(0x00900000u + (ep & 0x0001FFFCu)) = GEO_OP_END; } }
}

/* Convenience: one flat-colour quad (model 456) of colorbase cb (colour = palram[cb+0x1000]). Writes
 * the colour header (sampling the uniform patch) then submits. ax=0x4000 (90deg) faces the camera;
 * pass your own angles for tilted/oriented quads. */
static void m2_solid_quad(u32 cb, u32 px, u32 py, u32 pz, u32 ax, u32 ay, u32 az,
                          u32 sxb, u32 syb, u32 szb) {
    m2_obj_fill_patch();
    m2_obj_color_header(cb);
    m2_obj_submit(M2_FLAT_QUAD_MODEL, GEO_TEXRAM_BIT | (cb * 4u), px, py, pz, ax, ay, az, sxb, syb, szb);
}

#endif /* M2_OBJ_H */
