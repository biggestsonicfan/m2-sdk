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

/* cpres2 = GEO / geometry (real hardware; no-op/HLE'd in MAME model2b). */
static void m2_geo_boot(void) {
    m2__copro_upload(0x00980008u, 0x00840000u, 0x00804000u,
                     cpres_data2, 9351, 0x3100, 0x3110, 0xC400, 3117);
}

/* Boot both coprocessors (COP then GEO, per STF start-up order). */
static void m2_3d_boot(void) {
    m2_cop_boot();
    m2_geo_boot();
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
