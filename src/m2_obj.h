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

#include "m2_boot.h"           /* m2_silicon_boot + the COP/GEO/colour layers it composes (m2_3d/geo/color) */
#include "stf_cop_preamble.h"  /* cop_obj_preamble: poly_test_camera COP basis/viewport preamble      */

#define M2_FLAT_QUAD_MODEL 456u   /* STF model-table flat quad (1 face, 12x12, normal +Y, +-6 in XZ) */
#define M2_MODEL_TABLE     0x020E0004u  /* model n's 4-word header {tpa,tha,oba,obc} at +n*16        */

/* Model header (4 words @ M2_MODEL_TABLE + model*16): [0]=uv/tpa, [1]=material/tha, [2]=mesh/oba,
 * [3]=counts (low 16 bits = emitted face count). Material stream = M2_MAIN_DATA + tha*2, one 8-byte
 * (4x u16) record per face: th0/th1/th2/th3. (Format decoded from m2-hle2 geo3d_decode_model.) */
#define M2_MDL_MAT(hdr4)    ((volatile const u16 *)(M2_MAIN_DATA + (hdr4)[1] * 2u)) /* material stream */
#define M2_MDL_NFACES(hdr4) ((hdr4)[3] & 0xFFFFu)                                   /* emitted faces  */
#define M2_MAT_TEXTURED(th0) ((th0) & 0x4000u)          /* th0 bit14 = textured (else flat)          */
#define M2_MAT_SHEET(th2)    (((th2) >> 12) & 1u)        /* th2 bit12 = texram sheet select           */
#define M2_MAT_COLORBASE(th3)(((th3) >> 6) & 0x3FFu)     /* th3>>6 = colorbase (palette / palram idx) */

static u32 m2_obj_fb(float f){ union{float f;u32 u;}x; x.f=f; return x.u; }  /* float -> IEEE bits */

/* Populate polygon Color RAM with a model's REAL per-face palette — the send_tex_col stand-in.
 * STF character/object models (e.g. Pengo 4311/4312) reference many colorbases (th3 matidx) whose
 * palram entries a bare kernel never fills, so their faces render BLACK. Walk the model's material
 * stream, and for each referenced colorbase copy its BGR555 from the polygon-palette ROM
 * (M2_POLY_PAL + cb*2, SET bit already set) into palram[cb+0x1000]. Call once before drawing the
 * model; cheap (<=~96 faces). Works for any STF model, textured or flat. */
static void m2_load_model_palette(u32 model) {
    volatile const u32 *hdr = (volatile const u32 *)(M2_MODEL_TABLE + model * 16u);
    volatile const u16 *mat = M2_MDL_MAT(hdr);
    volatile const u16 *pal = (volatile const u16 *)M2_POLY_PAL;
    u32 nfaces = M2_MDL_NFACES(hdr), f;
    for (f = 0u; f < nfaces; f++) {
        u32 cb = M2_MAT_COLORBASE(mat[f * 4u + 3u]);
        if (cb == 0u) continue;                          /* colorbase 0 reserved (Fig 4-4) */
        *(volatile u16 *)(M2_PALRAM + (cb + 0x1000u) * 2u) = (u16)(pal[cb] | M2_PAL_SET);
    }
}

/* m2_silicon_boot() — the one-call bring-up — lives in m2_boot.h (included above). */

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
#define M2_OBJ_PATCH_TH2 M2_TH2_TILE(M2_OBJ_PATCH_X, M2_OBJ_PATCH_Y)

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
    volatile u32 *fifo = (volatile u32 *)M2_GEOFIFO_ADDR;
    *fifo = GEO_OP_TEXDATA;
    *fifo = GEO_TEXRAM_BIT | (cb * 4u);         /* dest = texram header slot cb*4 */
    *fifo = 4u;                                 /* 4 header words */
    *fifo = M2_TH0_TEX | M2_TH0_MAPX(2u) | M2_TH0_MAPY(2u); *fifo = 0u; *fifo = (u32)M2_OBJ_PATCH_TH2;  /* th0=textured128, th1=0, th2=patch */
    *fifo = M2_TH3_COLORBASE(cb);               /* th3 = colorbase */
}

/* ---- per-FRAME projection setup (run ONCE per frame after m2_frame_begin) ------------------------
 * GEO render state (FOCAL/LIGHT/WINDOW/TEXPARAM/LOD) + the STF poly_test_camera BASIS+VIEWPORT
 * preamble (drained per op or the COP output FIFO fills and stalls before a submit). */
static void m2_obj_frame_setup(void) {
    volatile u32 *cf   = (volatile u32 *)M2_COPFIFO_ADDR;        /* COP FIFO */
    volatile u32 *fifo = (volatile u32 *)M2_GEOFIFO_ADDR;        /* GEO FIFO */
    m2_cam_geo_proj();                                           /* GEO FOCAL slot 0x90 */
    m2_geo_fifo_light(STF_LIGHT_X_BITS, STF_LIGHT_Y_BITS, STF_LIGHT_Z_BITS);
    m2_geo_fifo_window_full();                                   /* WINDOW + clip   */
    m2_geo_fifo_texparam(0x000060FFu);                           /* TEXPARAM table  */
    *fifo = GEO_OP_LOD; *fifo = geo__f(2560.0f);                 /* base mip 2560.0 */
    cop_emit_obj_preamble(cf);                                   /* STF projection+basis preamble */
}

/* The tail every model-table submit shares, after its transform stream: the fadd x3 fence, then
 * polygon_submit(0x78) with the model header = the COMMIT, the running polygon counts read back, and
 * the GEO END mark. m2_obj_submit and m2_sprite.h's M2_SPR_COP quads both end with it. */
static void m2__obj_commit(volatile u32 *cf, u32 tpa, u32 tha, u32 oba, u32 obc) {
    *cf = COP_FADD; *cf = COP_FADD; *cf = COP_FADD;              /* fadd x3 fence */
    cop_drain(cf, 1u);
    { u32 wr = *(volatile u32 *)COP_WPOS_REG;
      *(volatile u32 *)GEO_WRITE_REG = wr + 0x48u;               /* GEO_WRITE = COP_WPOS + 0x48 */
      *cf = COP_SUBMIT; *cf = wr; *cf = 0u;                      /* polygon_submit (op 0x78) */
      *cf = tpa; *cf = tha; *cf = oba; *cf = obc;                /* model header */
      *cf = g_cop_p2; *cf = g_cop_p;                             /* P2_POLYGON, POLYGON (running) */
      g_cop_p2 = *cf; g_cop_p = *cf;                             /* read back + save */
      { u32 ep = *(volatile u32 *)COP_WPOS_REG;                  /* GEO END mark */
        *(volatile u32 *)(GEO_BUFFERRAM + (ep & 0x0001FFFCu)) = GEO_OP_END; } }
}

/* ---- per-OBJECT submit (after m2_obj_frame_setup; threads multiple objects via g_cop_p) ----------
 * Transform stream (identity/pos/angles/scale) + polygon_submit(0x78) = the COMMIT, then the GEO END
 * mark. tha_override != 0 replaces the model's material header (e.g. a flat colorbase header for model
 * 456); 0 = use the model's own. */
static void m2_obj_submit(u32 model_no, u32 tha_override, u32 px, u32 py, u32 pz,
                          u32 ax, u32 ay, u32 az, u32 sxb, u32 syb, u32 szb) {
    volatile u32 *cf = (volatile u32 *)M2_COPFIFO_ADDR;
    volatile const u32 *hdr = (volatile const u32 *)(M2_MODEL_TABLE + model_no * 16u);
    u32 tha = tha_override ? tha_override : hdr[1];
    *cf = COP_IDENTITY;                                          /* set_identity */
    *cf = COP_SET_POS; *cf = px; *cf = py; *cf = pz;             /* set_pos */
    *cf = COP_ANG_Y; *cf = ay;
    *cf = COP_ANG_X; *cf = ax;
    *cf = COP_ANG_Z; *cf = az;
    *cf = COP_SCALE; *cf = sxb; *cf = syb; *cf = szb;            /* per-axis scale */
    m2__obj_commit(cf, hdr[0], tha, hdr[2], hdr[3]);             /* tha may be the override */
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
