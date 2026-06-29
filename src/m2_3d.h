/*
 * m2_3d.h — Sega Model 2B coprocessor (3D) bring-up, layered on m2.h.
 *
 * Boots STF's two SHARC coprocessors from their firmware blobs:
 *   cpres1 = COP / math      -> CTL 0x980000, IOP 0x8C0000, FIFO 0x884000
 *   cpres2 = GEO / geometry  -> CTL 0x980008, IOP 0x840000, FIFO 0x804000
 * (host-boot: set CTL bit31 = halt + reset DMA counter, program the ADSP IOP
 *  boot regs, stream firmware halfwords to the FIFO, clear bit31 = run.)
 *
 * In MAME model2b only the COP (cpres1) is emulated — the GEO (cpres2) chip is
 * commented out and HLE'd by geo_parse; on real hardware both run. Booting the
 * GEO here is harmless (its targets are unmapped in MAME) and faithful on hw.
 *
 * Include from the SAME .c as m2.h; link cpres1.S + cpres2.S.
 */
#ifndef M2_3D_H
#define M2_3D_H

#include "cpres1.h"    /* const unsigned short cpres_data[]  — bin2c'd cpres1 (COP) firmware */
#include "cpres2.h"    /* const unsigned short cpres_data2[] — bin2c'd cpres2 (GEO) firmware */

/* Host-boot an ADSP-2106x copro: ctl reg, IOP base, FIFO, firmware + count, and
 * the two SYSCON values + DMA params (per copro_down / copro_down2). */
static void m2__copro_upload(u32 ctl_addr, u32 iop_base, u32 fifo_addr,
                             const u16 *fw, int nwords,
                             u32 syscon0, u32 syscon1, u32 dmacfg, int dmacnt) {
    volatile u32 *ctl  = (volatile u32 *)ctl_addr;
    volatile u32 *fifo = (volatile u32 *)fifo_addr;
    u32 save = *ctl;
    int i;
    *ctl = save | 0x80000000u;                              /* halt + count=0   */
    /* Release the coprocessors from reset (clear bits 0,1 of 0x980020). The real
     * firmware's b_crx_copro_down / copro_down2 do this; 0x980020 is NOT mapped in
     * MAME (so the HLE never needed it), but on real silicon the SHARC stays held
     * in reset without it -> streaming firmware + triggering the GEO then faults. */
    *(volatile u32 *)0x00980020u = *(volatile u32 *)0x00980020u & 0xFFFFFFFCu;
    *(volatile u32 *)(iop_base + 0x000) = syscon0;          /* SYSCON           */
    *(volatile u32 *)(iop_base + 0x000) = 0;
    *(volatile u32 *)(iop_base + 0x008) = dmacfg;
    *(volatile u32 *)(iop_base + 0x070) = 0;
    *(volatile u32 *)(iop_base + 0x100) = 0x20000;          /* boot dest addr   */
    *(volatile u32 *)(iop_base + 0x104) = 1;
    *(volatile u32 *)(iop_base + 0x108) = (u32)dmacnt;
    *(volatile u32 *)(iop_base + 0x000) = syscon1;
    *(volatile u32 *)(iop_base + 0x070) = 0xA1;
    *(volatile u32 *)(iop_base + 0x070) = 0;
    for (i = 0; i < nwords; i++) *fifo = fw[i];             /* stream firmware  */
    *ctl = save;                                            /* run              */
}

/* cpres1 = COP / math (the SHARC MAME emulates; verified PC reaches 0x2013F). */
static void m2_cop_boot(void) {
    m2__copro_upload(0x00980000u, 0x008C0000u, 0x00884000u,
                     cpres_data, 14862, 0xA100, 0xA110, 0xC9400, 4954);
}

/* GEO firmware upload — STF copro_down2 @0x1588, decoded from the i960 disasm. Same shape as
 * m2__copro_upload EXCEPT one word: STF writes iop+0x104 from the firmware header word [2] (= 0
 * for the GEO: `st r6,0x104(r3)` with r6 = cpres_start2[2]), whereas m2__copro_upload HARDCODES
 * iop+0x104 = 1. That word is a boot-DMA descriptor (between dest@0x100 and count@0x108); =1 is
 * wrong for the GEO and may corrupt its firmware DMA (the COP tolerates =1, the GEO may not).
 * Everything else (dmacnt@0x108, syscon1@0x000, the 0x070 zero, 9351-short stream) matches the
 * shared upload. (An earlier wp-TRACE reading misread these offsets as 0x040/0x070/0x00C and
 * bricked the board — the DISASM above is authoritative.) hdr104 lets the caller pass header[2]. */
static void m2__geo_upload(u32 ctl_addr, u32 iop_base, u32 fifo_addr,
                           const u16 *fw, int nwords,
                           u32 syscon0, u32 syscon1, u32 dmacfg, int dmacnt, u32 hdr104) {
    volatile u32 *ctl  = (volatile u32 *)ctl_addr;
    volatile u32 *fifo = (volatile u32 *)fifo_addr;
    u32 save = *ctl;
    int i;
    *ctl = save | 0x80000000u;                                   /* halt (setbit 0x1F)      */
    *(volatile u32 *)0x00980020u = *(volatile u32 *)0x00980020u & 0xFFFFFFFCu;  /* release reset */
    *(volatile u32 *)(iop_base + 0x000) = syscon0;
    *(volatile u32 *)(iop_base + 0x000) = 0;
    *(volatile u32 *)(iop_base + 0x008) = dmacfg;
    *(volatile u32 *)(iop_base + 0x070) = 0;
    *(volatile u32 *)(iop_base + 0x100) = 0x20000;               /* boot dest (= header[1])  */
    *(volatile u32 *)(iop_base + 0x104) = hdr104;                /* = header[2] (GEO: 0)     */
    *(volatile u32 *)(iop_base + 0x108) = (u32)dmacnt;
    /* cpres2 GEO MSGR7 = the validation-bypass magic. The GEO microcode's command loop (_L2010C @PM0x10C)
     * compares MSGR7 (dm(0xf)) against 0x7378842E; matching SKIPS the per-command opcode-shape validation
     * (PM0x110-0x118). Without it the first non-conforming word jumps to _L20149 = an infinite error spin
     * -> the GEO never consumes the display list (the "snow"/no-render on silicon; a confirmed cpres2
     * stall in MAME). STF sets this; we must too. Written here while syscon0=0x3100 keeps host-packing OFF
     * (bit4 clear) so it lands as one 32-bit word. MSGR7 = IOP reg 0x0F = iop+0x3C. */
    *(volatile u32 *)(iop_base + 0x03C) = 0x7378842Eu;
    *(volatile u32 *)(iop_base + 0x000) = syscon1;
    *(volatile u32 *)(iop_base + 0x070) = 0xA1;
    *(volatile u32 *)(iop_base + 0x070) = 0;
    for (i = 0; i < nwords; i++) *fifo = fw[i];                  /* stream firmware          */
    *ctl = save;                                                 /* run                      */
}

static void m2_geo_boot(void) {
    /* hdr104 = STF cpres_start2 header[2] = the GEO boot-DMA INTERNAL MODIFY (stride) @0x840104.
     * VERIFIED via IDA: the 3-word header preceding _cpres_data2 (@0xbd748) is {0xA1,0x20000,0x1},
     * so header[2] = 0x1, NOT 0. copro_down2 writes 0x840104 = 0x1. A previous misread set this to
     * 0 -> DMA stride 0 -> the GEO can't walk BUFF_RAM (GEO_RADDR pinned at 0, stripe field, no
     * render); peek/MAME(HLE) can't see it. The COP boot hardcodes 0x104=1 and works -> match it. */
    m2__geo_upload(0x00980008u, 0x00840000u, 0x00804000u,
                   cpres_data2, 9351, 0x3100, 0x3110, 0xC400, 3117, /*hdr104=*/1u);
}

/* Boot both coprocessors (COP then GEO, per STF start-up order). */
static void m2_3d_boot(void) {
    m2_cop_boot();
    m2_geo_boot();
}

/* STF cop_initialize: ARM the COP (cpres1) for rendering — wait until it reports ready (CTL 0x980004
 * bit0), draining any pending words from its FIFO, then write 0 to the FIFO. STF's per-object render
 * setup order is:  geo_func -> cop_initialize -> geo_initialize -> geometry_stuff -> texture upload.
 * A project that draws via the object_data path (cop_submit_object / geo_obj_*) MUST call this after the
 * COP boot + geo_func but BEFORE geo_initialize; without the COP-side arm the COP never commits its
 * transformed geometry (the GEO side can be fully set up yet nothing renders). The geoserial kernel does
 * this as cop_arm(); it was missing from the SDK, so standalone object_data builds skipped it. */
static void m2_cop_initialize(void) {
    volatile u32 *cop_ctl  = (volatile u32 *)0x00980004u;
    volatile u32 *cop_fifo = (volatile u32 *)0x00884000u;
    u32 g = 0;
    while (!(*cop_ctl & 1u) && ++g < 200000u) (void)*cop_fifo;
    *cop_fifo = 0u;
}

/* 3D solid-fill colour table. The solid scanline computes the pixel as
 *   gamma[ colorxlat[ palram[colorbase+0x1000] component ][ luma>>2 ] ]
 * so colorxlat must be filled across the LUMA axis (the tile path only fills the
 * fixed luma=0x40 slice, leaving 3D polygons black). Fill comp*256+luma with a
 * luma-scaled brightness (comp8 * luma / 64); luma 0..64 keeps the tile slice
 * (64) valid. Layout: R @0x1810000, G @0x1814000, B @0x1818000 (u16, stride 2).*/
static void m2_3d_colorxlat(void) {
    int comp, l;
    for (comp = 0; comp < 32; comp++) {
        int c8 = (comp << 3) | (comp >> 2);          /* pal5bit(comp) */
        for (l = 0; l <= 64; l++) {
            u16 v = (u16)((c8 * l) / 64);
            *(volatile u16 *)(0x01810000u + (comp * 256 + l) * 2) = v;  /* R */
            *(volatile u16 *)(0x01814000u + (comp * 256 + l) * 2) = v;  /* G */
            *(volatile u16 *)(0x01818000u + (comp * 256 + l) * 2) = v;  /* B */
        }
    }
}

/* Set the 3D palette entry for a flat solid material. The renderer reads the
 * pixel as palram[colorbase + 0x1000]. The matching texheader (renderer 0 +
 * colorbase) must be streamed into the rasterizer's internal texture_ram via a
 * geo 0x04 (texture-data) command IN THE DISPLAY LIST, then referenced with
 * tha = 0x00800000 (bit23 = texture_ram). See cube3d.c for the list layout. */
static void m2_3d_solid(int colorbase, u16 bgr555) {
    *(volatile u16 *)(0x01800000u + (colorbase + 0x1000) * 2) = bgr555;
}

/* ---- COP runtime command interface (use after m2_cop_boot) ----------------
 * Drive the booted cpres1 SHARC for matrix math. Write a command word then its
 * args to the FIFO (0x884000); read outputs back — the FIFO read stalls the
 * i960 until the SHARC returns the result (MAME) / the HLE answers (m2-hle2).
 * Command word = (N<<23)|(N<<8)|N (the firmware checks all three byte fields).
 * Angles are 16-bit fixed point (0x10000 = 360deg). */
#define M2_COPFIFO (*(volatile u32 *)0x00884000u)

static void  m2_cop(u32 cmd)    { M2_COPFIFO = cmd; }              /* send command  */
static void  m2_cop_pi(u32 v)   { M2_COPFIFO = v; }               /* push int arg  */
static void  m2_cop_pf(float f) { union { float f; u32 u; } x; x.f = f; M2_COPFIFO = x.u; }
static float m2_cop_gf(void)    { union { float f; u32 u; } x; x.u = M2_COPFIFO; return x.f; }

/* Command word from the dispatch-table slot n: the SHARC firmware checks all
 * three byte fields (bits[28:23] | [15:8] | [7:0]), so the word is n in each. */
#define COP_CMD(n)    (((u32)(n) << 23) | ((u32)(n) << 8) | (u32)(n))

#define COP_PUSH      COP_CMD(0x01)  /* push bone frame                         */
#define COP_POP       COP_CMD(0x02)  /* pop bone frame                          */
#define COP_IDENTITY  COP_CMD(0x03)  /* rot = I, T = 0                          */
#define COP_ANG_X     COP_CMD(0x08)  /* in: 1 i16 angle; post-mul rot by Rx     */
#define COP_ANG_Y     COP_CMD(0x09)  /* in: 1 i16 angle; post-mul rot by Ry     */
#define COP_ANG_Z     COP_CMD(0x0A)  /* in: 1 i16 angle; post-mul rot by Rz     */
#define COP_FADD      COP_CMD(0x13)  /* in: a,b float; out: a+b                 */
#define COP_FSUB      COP_CMD(0x14)  /* in: a,b float; out: a-b                 */
#define COP_FMUL      COP_CMD(0x15)  /* in: a,b float; out: a*b                 */
#define COP_FDIV      COP_CMD(0x16)  /* in: a,b float; out: a/b                 */
#define COP_INT2F     COP_CMD(0x17)  /* in: int;   out: (float)int              */
#define COP_F2INT     COP_CMD(0x18)  /* in: float; out: (int)float (truncate)   */
#define COP_SQRT      COP_CMD(0x1A)  /* in: float; out: float sqrt              */
#define COP_SIN       COP_CMD(0x21)  /* in: i16 angle; out: float sin           */
#define COP_COS       COP_CMD(0x22)  /* in: i16 angle; out: float cos           */
#define COP_M2W       COP_CMD(0x29)  /* in: x,y,z float; out: rot*(x,y,z)+T     */
#define COP_ATAN2     COP_CMD(0x2F)  /* in: x1,z1,x2,z2; out: i16 atan2(z2-z1,x2-x1) */
#define COP_WMATRIX   COP_CMD(0x04)  /* in: 12 floats (row-major 3x4) -> bone slot   */
#define COP_RMATRIX   COP_CMD(0x05)  /* out: 12 floats (row-major 3x4) bone -> FIFO  */
#define COP_SET_POS   COP_CMD(0x06)  /* in: x,y,z; T += rot*(x,y,z)                  */
#define COP_SCALE     COP_CMD(0x07)  /* in: sx,sy,sz; rot[col][row] *= s per col     */

#define M2_ANG_SCALE  10430.378f    /* radians -> i16 angle (65536 / 2pi)      */
static u32 m2_ang(float radians) { return (u32)((int)(radians * M2_ANG_SCALE)) & 0xffffu; }

/* Square root on the DSP/SHARC (COP_SQRT). */
static float m2_cop_sqrt(float x) { m2_cop(COP_SQRT); m2_cop_pf(x); return m2_cop_gf(); }

/* Scalar float math on the DSP/SHARC. Each is a FIFO round-trip (the read stalls
 * the i960 until the SHARC answers), so prefer them for real work, not tight loops. */
static float m2_cop_fadd(float a, float b) { m2_cop(COP_FADD); m2_cop_pf(a); m2_cop_pf(b); return m2_cop_gf(); }
static float m2_cop_fsub(float a, float b) { m2_cop(COP_FSUB); m2_cop_pf(a); m2_cop_pf(b); return m2_cop_gf(); }
static float m2_cop_fmul(float a, float b) { m2_cop(COP_FMUL); m2_cop_pf(a); m2_cop_pf(b); return m2_cop_gf(); }
static float m2_cop_fdiv(float a, float b) { m2_cop(COP_FDIV); m2_cop_pf(a); m2_cop_pf(b); return m2_cop_gf(); }
static float m2_cop_int2f(int   i)         { m2_cop(COP_INT2F); m2_cop_pi((u32)i); return m2_cop_gf(); }
static int   m2_cop_f2int(float f)         { m2_cop(COP_F2INT); m2_cop_pf(f); return (int)M2_COPFIFO; }

/* Sin & cos of a 16-bit angle (0x10000 = 360deg) on the DSP, in one call. */
static void cop_sincos(u32 ang, float *s, float *c) {
    m2_cop(COP_COS); m2_cop_pi(ang); *c = m2_cop_gf();
    m2_cop(COP_SIN); m2_cop_pi(ang); *s = m2_cop_gf();
}

/* atan2 on the DSP: angle (i16, 0x10000=360deg) of the vector (x2-x1, z2-z1). */
static u32 m2_cop_atan2(float x1, float z1, float x2, float z2) {
    m2_cop(COP_ATAN2); m2_cop_pf(x1); m2_cop_pf(z1); m2_cop_pf(x2); m2_cop_pf(z2);
    return M2_COPFIFO & 0xffffu;
}

/* Load/read a 3x4 matrix (row-major: 3 rows of [r0 r1 r2 t]) to/from a COP bone slot. */
static void m2_cop_wmatrix(const float m[12]) {
    int i; m2_cop(COP_WMATRIX); for (i = 0; i < 12; i++) m2_cop_pf(m[i]);
}
static void m2_cop_rmatrix(float m[12]) {
    int i; m2_cop(COP_RMATRIX); for (i = 0; i < 12; i++) m[i] = m2_cop_gf();
}

/* ---- COP object submit (polygon_submit op 0x78) — the proven-on-silicon object_data path ---------
 * Instance a model-table object through the COP: CMD13x3 fence + SUBMIT(0x78) + the RAW 4-word model
 * header {tpa,tha,oba,obc}; the COP writes MATRIX+OBJECT to BUFF_RAM at COP_WPOS (= the commit) and
 * returns the running polygon counts. Push the transform (OBJECT/IDENTITY/SET_POS/ANG/SCALE) BEFORE
 * calling this. g_cop_p/g_cop_p2 = running P2_POLYGON/POLYGON; reset to 0 at each frame's first submit.
 * cf = the COP FIFO (0x00884000). (Ported out of geoserial.c so any project can submit objects.)
 *
 * MULTI-OBJECT (critical): to draw MORE THAN ONE object in a frame you MUST pass the RUNNING g_cop_p2/
 * g_cop_p to the submit and READ BACK + SAVE the updated counts (this function does). Resetting the
 * counts to 0,0 per object makes every object write from polygon slot 0 and OVERWRITE the previous one's
 * polygons in BUFF_RAM -> the objects turn to chaos (e.g. "3 cubes -> mess"). Reset the counters ONCE per
 * frame (m2_frame_begin does it), not per object. This is the poly-test threading; STF's main render uses
 * a different per-object path (a separate, deeper COP<->GEO coherence concern). */
static u32 g_cop_p2, g_cop_p;
static void cop_drain(volatile u32 *cf, u32 n) { volatile u32 d = 0u; while (n-- > 0u) d = *cf; (void)d; }
static void cop_submit_object(volatile u32 *cf, const volatile u32 *hdr) {
    u32 wr; volatile u32 s, p2, p;
    *cf = 0x09801313u; *cf = 0x09801313u; *cf = 0x09801313u;  /* COP_CMD(0x13) x3 fence */
    s = *cf;                                                  /* 1 sync read */
    *cf = 0x3C007878u;                                        /* SUBMIT op 0x78 */
    wr = *(volatile u32 *)0x00802008u;                        /* COP_WPOS */
    *(volatile u32 *)0x00801008u = wr + 0x48u;                /* GEO_WRITE = COP_WPOS + 0x48 (reserve) */
    *cf = wr; *cf = 0u;
    *cf = hdr[0]; *cf = hdr[1]; *cf = hdr[2]; *cf = hdr[3];   /* raw model header (obc too big HANGS the COP) */
    *cf = g_cop_p2; *cf = g_cop_p;                            /* P2_POLYGON, POLYGON (running) */
    p2 = *cf; p = *cf;                                        /* read back updated counts */
    g_cop_p2 = p2; g_cop_p = p; (void)s;
}

/* ===========================================================================
 * Fixed camera — ported from STF camera_init @0x1F110 (stfdecomp rom_code1.s:30486).
 *
 * STF's camera_init runs a FIGHTER-TRACKING camera: cam_mode_9 (@0x6B1D8) reads the
 * two fa_rob fighter structs, computes their midpoint + inter-fighter distance (COP
 * dist3D), and dollies so both stay framed. None of that applies to a standalone /
 * X11-server render, so we keep camera_init's STATIC pieces and drop the fighter logic:
 *
 *   - focus_dist_x = focus_dist_y = 280.0f (camera_init :30497, 0x438C0000) — the focal
 *     length, fed to the GEO as the FOCAL register (slot 0x90 = 0x909 + focal pair,
 *     exactly as camera_init :30836).
 *   - scalar FOCAL/ZSORT/LIGHT operands init 0 (camera_init :30507-:30512 -> 0x24/26/28
 *     of the camera struct) — confirms the COP submit may send 0 for these.
 *   - the camera is applied by transforming each object's WORLD position into VIEW space
 *     (world - eye, rotated by -yaw/-pitch) on the i960 and feeding the result to the
 *     per-object COP_SET_POS. This is how STF works too: every object (incl. cam_mode_9's)
 *     does push->set_identity->transform->submit, so the per-object bone always starts at
 *     identity; the camera lives in the view-relative positions camera_work pre-computes,
 *     NOT in a persistent COP view matrix.
 *
 * A fixed FRONT camera = eye at the origin, yaw=pitch=0 -> world_to_view is the identity,
 * so an object placed at world (0,0,Z) submits with COP_SET_POS(0,0,Z) — identical to the
 * proven render path. Move the eye / aim it and objects track correctly.
 *
 * NOT ported (camera_init :30848): GEO slot 0x160 = 0x1616 + a z-projection scale word
 * ([0x5010C4]*[0x5010C8]); both operands are computed at runtime from the COP OP0x5E
 * projection, so the exact value isn't statically known. The render works without it on
 * MAME (HLE GEO); it's a candidate refinement for the silicon raster path.
 * =========================================================================== */

#define M2_FOCUS_DIST  280.0f       /* 0x438C0000 — camera_init :30497 */

typedef struct {
    float eye[3];               /* camera position in world space (looks down +Z) */
    u32   yaw, pitch;           /* i16 view angles (0x10000 = 360deg); 0 = look +Z */
    float focus;                /* focal length (focus_dist), default 280.0        */
    /* derived by m2_cam_set_angles() from yaw/pitch — default to identity rotation */
    float sy, cy, sx, cx;
} m2_camera_t;

static m2_camera_t m2_cam = { {0.0f, 0.0f, 0.0f}, 0u, 0u, M2_FOCUS_DIST,
                              0.0f, 1.0f, 0.0f, 1.0f };

static void m2_cam_set_eye(float x, float y, float z) {
    m2_cam.eye[0] = x; m2_cam.eye[1] = y; m2_cam.eye[2] = z;
}
static void m2_cam_set_focus(float f) { m2_cam.focus = f; }

/* Recompute the cached sin/cos for yaw & pitch. Uses the COP FIFO (cop_sincos), so call
 * it OUTSIDE display-list building (e.g. once per frame before drawing), never between the
 * COP command writes of an object submit — it would corrupt the in-flight command stream. */
static void m2_cam_set_angles(u32 yaw, u32 pitch) {
    m2_cam.yaw = yaw; m2_cam.pitch = pitch;
    cop_sincos(yaw,   &m2_cam.sy, &m2_cam.cy);
    cop_sincos(pitch, &m2_cam.sx, &m2_cam.cx);
}

/* World -> view (eye-relative): translate by -eye, yaw about Y, then pitch about X.
 * out is the position to feed COP_SET_POS for an object at world position p. */
static void m2_cam_world_to_view(const float p[3], float out[3]) {
    float dx = p[0] - m2_cam.eye[0];
    float dy = p[1] - m2_cam.eye[1];
    float dz = p[2] - m2_cam.eye[2];
    float x1 =  m2_cam.cy * dx - m2_cam.sy * dz;     /* yaw about Y   */
    float z1 =  m2_cam.sy * dx + m2_cam.cy * dz;
    float y2 =  m2_cam.cx * dy - m2_cam.sx * z1;     /* pitch about X */
    float z2 =  m2_cam.sx * dy + m2_cam.cx * z1;
    out[0] = x1; out[1] = y2; out[2] = z2;
}

/* GEO command FIFO + slot-register window (g10 = 0x800000 base, FIFO @ +0x4000). */
#define M2_GEO_SLOT(n)  (*(volatile u32 *)(0x00800000u + (u32)(n)))
#define M2_GEOFIFO      (*(volatile u32 *)0x00804000u)
static void m2_geo_pf(float f) { union { float f; u32 u; } x; x.f = f; M2_GEOFIFO = x.u; }

/* Emit a GEO slot-register command header. The slot's command word is (slot>>4)*0x101
 * (slot 0x80 -> 0x808, 0x30 -> 0x303, 0x100 -> 0x1010, ...); any operands follow on the
 * FIFO. NB this is the command form only — writing a non-encoding value to a slot (e.g.
 * M2_GEO_END = buffer offset) is a data write, not a command, and stays explicit. */
static void m2_geo_cmd(u32 slot) { M2_GEO_SLOT(slot) = ((slot >> 4) & 0xFFu) * 0x101u; }

/* Per-frame GEO projection feed (camera_init :30836): FOCAL setup slot 0x90 = 0x909, then
 * the focal_x / focal_y pair. Call once per frame before submitting objects. */
static void m2_cam_geo_proj(void) {
    m2_geo_cmd(0x90u);
    m2_geo_pf(m2_cam.focus);
    m2_geo_pf(m2_cam.focus);
}

/* Master Z-clip control register (write 0xFF to disable board-level near clip). */
#define ZCLIP_REG  0x0181C000u

/* Route scalar float math through native i960 ops (the COP helpers above stay defined
   for anyone who needs them, but every module compiled after this point -- m2_geo.h and
   the games -- uses the native path). Big perf win for the 3D games on MAME.
   NOTE: native ops emit i960 hardware FP (mulr/addr/cvtir...), which MAME's i960 core
   and the real Model 2 i960KB (it has an FPU) run, but which m2emulator does NOT
   emulate -> INVALID OPCODES on m2emulator. Define M2_NO_FASTMATH (e.g. an m2emulator
   build) to keep the COP-FIFO math path, which uses no i960 FP -- slower, but the COP
   does the float. (Or use -msoft-float / M2_SOFTFLOAT for a fully FP-free binary.) */
#ifndef M2_NO_FASTMATH
#include "m2_fastmath.h"
#endif

#endif /* M2_3D_H */
