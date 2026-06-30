/*
 * m2_math.h — math coprocessor (COP / cpres1 SHARC) command interface + ops.
 *
 * The low-level COP layer: the command FIFO (0x884000), the COP_* opcode encodings, and the scalar /
 * vector / matrix math the booted cpres1 SHARC performs (write a command word + args to the FIFO, read
 * the result back — the read stalls the i960 until the SHARC answers). m2_3d.h boots the COP and builds
 * the camera / object-submit path on top of this; the STF object preamble (stf_cop_preamble.h) uses the
 * explicit-FIFO emitters at the bottom.
 *
 * Op semantics verified against the cpres1 disassembly (../m2-hle). Command word = (N<<23)|(N<<8)|N
 * (the firmware checks all three byte fields). Angles are 16-bit fixed point (0x10000 = 360deg).
 *
 * Include from the SAME .c as m2.h (for u32). m2_fastmath.h (included at the bottom unless
 * M2_NO_FASTMATH) then overrides the SCALAR helpers with native i960 ops for modules compiled after.
 */
#ifndef M2_MATH_H
#define M2_MATH_H

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
#define COP_DIST2D    COP_CMD(0x2B)  /* in: x1,z1,x2,z2; out: sqrt((x2-x1)^2+(z2-z1)^2) */
#define COP_ATAN2     COP_CMD(0x2F)  /* in: x1,z1,x2,z2; out: i16 atan2(z2-z1,x2-x1) (azimuth) */
#define COP_WMATRIX   COP_CMD(0x04)  /* in: 12 floats (row-major 3x4) -> bone slot   */
#define COP_RMATRIX   COP_CMD(0x05)  /* out: 12 floats (row-major 3x4) bone -> FIFO  */
#define COP_SET_POS   COP_CMD(0x06)  /* in: x,y,z; T += rot*(x,y,z)                  */
#define COP_SCALE     COP_CMD(0x07)  /* in: sx,sy,sz; rot[col][row] *= s per col     */
#define COP_SUBMIT    COP_CMD(0x78)  /* polygon_submit: commit the transformed object */

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

/* ---- explicit-FIFO op emitters: push ONE COP math op + operands to a COP FIFO `cf`, then drain its
 * single result (drain-and-discard form; the STF object preamble in stf_cop_preamble.h uses these).
 * Semantics from the cpres1 disassembly:
 *   COP_INT2F  integer -> float                   (handler: f0 = float r1)
 *   COP_F2INT  float   -> integer, truncate        (handler: r0 = fix f1)
 *   COP_DIST2D 4 floats (two XZ points) -> sqrt((x2-x1)^2 + (z2-z1)^2)  (length)
 *   COP_ATAN2  4 floats (two XZ points) -> atan2(z2-z1, x2-x1)  (azimuth, i16) */
static u32 cop__fbits(float f) { union { float f; u32 u; } x; x.f = f; return x.u; }
static void cop__drain1(volatile u32 *cf) { volatile u32 d = *cf; (void)d; }

static void cop_f2int_raw(volatile u32 *cf, u32 fbits) { *cf = COP_F2INT; *cf = fbits; cop__drain1(cf); }
static void cop_int2f(volatile u32 *cf, u32 ival)      { *cf = COP_INT2F; *cf = ival;  cop__drain1(cf); }
static void cop_azimuth(volatile u32 *cf, float a, float b, float c, float d) {  /* COP_ATAN2 */
    *cf = COP_ATAN2;
    *cf = cop__fbits(a); *cf = cop__fbits(b); *cf = cop__fbits(c); *cf = cop__fbits(d);
    cop__drain1(cf);
}
static void cop_dist2d(volatile u32 *cf, float a, float b, float c, float d) {
    *cf = COP_DIST2D;
    *cf = cop__fbits(a); *cf = cop__fbits(b); *cf = cop__fbits(c); *cf = cop__fbits(d);
    cop__drain1(cf);
}

/* Route scalar float math through native i960 ops (the COP helpers above stay defined for anyone who
   needs them, but every module compiled after this point — m2_3d.h, m2_geo.h and the games — uses the
   native path). Big perf win for the 3D games on MAME.
   NOTE: native ops emit i960 hardware FP (mulr/addr/cvtir...), which MAME's i960 core and the real
   Model 2 i960KB (it has an FPU) run, but which m2emulator does NOT emulate -> INVALID OPCODES on
   m2emulator. Define M2_NO_FASTMATH (e.g. an m2emulator build) to keep the COP-FIFO math path, which
   uses no i960 FP — slower, but the COP does the float. (Or -msoft-float / M2_SOFTFLOAT for a fully
   FP-free binary.) */
#ifndef M2_NO_FASTMATH
#include "m2_fastmath.h"
#endif

#endif /* M2_MATH_H */
