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

/* ---- extended scalar / vector math (cpres1 handler bodies, disasm-verified) ----
 * These all FIFO round-trip like the wrappers below; none are overridden by
 * m2_fastmath.h (they genuinely need the SHARC). The three marked "no STF opcode"
 * are valid cpres1 handlers the shipping game never emits, but which still run. */
#define COP_RSQRT     COP_CMD(0x19)  /* in: float; out: 1/sqrt(x) (0 if x<=0)        */
#define COP_TAN       COP_CMD(0x23)  /* in: i16 angle; out: float tan (sin/cos)      */
#define COP_SINSCALE  COP_CMD(0x24)  /* in: i16 angle, s; out: sin(angle)*s          */
#define COP_COSSCALE  COP_CMD(0x25)  /* in: i16 angle, s; out: cos(angle)*s          */
#define COP_ASIN      COP_CMD(0x26)  /* in: float [-1,1]; out: i16 asin              */
#define COP_ATAN2F    COP_CMD(0x27)  /* in: x,y float; out: i16 atan2(y,x)           */
#define COP_DOT3      COP_CMD(0x2A)  /* in: a0,b0,a1,b1,a2,b2; out: a.b  (no STF op) */
#define COP_DIST3D    COP_CMD(0x2C)  /* in: x1,x2,y1,y2,z1,z2; out: 3D distance      */
#define COP_MAG2D     COP_CMD(0x2D)  /* in: a,b; out: sqrt(a^2+b^2)                  */
#define COP_MAG3D     COP_CMD(0x2E)  /* in: a,b,c; out: sqrt(a^2+b^2+c^2) (no STF op)*/
#define COP_NORM3     COP_CMD(0x30)  /* in: x,y,z; out: unit vector (cpres1; see note)*/
#define COP_DOT2D     COP_CMD(0x59)  /* in: a,b,c,d; out: a*b+c*d                    */
#define COP_NORM2     COP_CMD(0x5A)  /* in: x,y; out: unit vector  (cpres1; see note) */
#define COP_ROT2D     COP_CMD(0x5B)  /* in: i16 angle,x,y; out: rotated x,y (cpres1) */
#define COP_ADD3      COP_CMD(0x5C)  /* in: a0,b0,a1,b1,a2,b2; out: a+b              */
#define COP_SUB3      COP_CMD(0x5D)  /* in: a0,b0,a1,b1,a2,b2; out: a-b  (no STF op) */
#define COP_SCALE3    COP_CMD(0x5E)  /* in: s,x,y,z; out: s*x,s*y,s*z                */
#define COP_W2M       COP_CMD(0x6A)  /* in: x,y,z; out: rot*((x,y,z)-T) world->model */

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

/* atan2 on the DSP: i16 angle (0x10000=360deg) of the vector from (x1,z1) to
 * (x2,z2) — atan2(z2-z1, x2-x1). NOTE the arg order is x1,x2,z1,z2: the COP
 * (handler index 0x2F) subtracts args 0-1 and 2-3, so the two X's and two Z's
 * must be adjacent, not interleaved. */
static u32 m2_cop_atan2(float x1, float x2, float z1, float z2) {
    m2_cop(COP_ATAN2); m2_cop_pf(x1); m2_cop_pf(x2); m2_cop_pf(z1); m2_cop_pf(z2);
    return M2_COPFIFO & 0xffffu;
}

/* tan / scaled sin & cos of a 16-bit angle (0x10000 = 360deg). tan is sin/cos on
 * the SHARC, so it blows up near +-90deg exactly like the hardware does. */
static float m2_cop_tan(u32 ang)                { m2_cop(COP_TAN);      m2_cop_pi(ang); return m2_cop_gf(); }
static float m2_cop_sin_scale(u32 ang, float s) { m2_cop(COP_SINSCALE); m2_cop_pi(ang); m2_cop_pf(s); return m2_cop_gf(); }
static float m2_cop_cos_scale(u32 ang, float s) { m2_cop(COP_COSSCALE); m2_cop_pi(ang); m2_cop_pf(s); return m2_cop_gf(); }

/* asin: arg in [-1,1] -> i16 angle. atan2f: i16 angle of the vector (x,y), i.e.
 * atan2(y,x) — same (x first, y second) convention as m2_cop_atan2's deltas. */
static u32 m2_cop_asin(float a)            { m2_cop(COP_ASIN);   m2_cop_pf(a); return M2_COPFIFO & 0xffffu; }
static u32 m2_cop_atan2f(float x, float y) { m2_cop(COP_ATAN2F); m2_cop_pf(x); m2_cop_pf(y); return M2_COPFIFO & 0xffffu; }

/* 1/sqrt on the SHARC (rsqrt seed + 3 Newton steps); returns 0 for x<=0. */
static float m2_cop_rsqrt(float x) { m2_cop(COP_RSQRT); m2_cop_pf(x); return m2_cop_gf(); }

/* Magnitudes / distance. m2_cop_dist3d pairs its args as (x1,x2),(y1,y2),(z1,z2). */
static float m2_cop_mag2d(float a, float b)          { m2_cop(COP_MAG2D); m2_cop_pf(a); m2_cop_pf(b); return m2_cop_gf(); }
static float m2_cop_mag3d(float a, float b, float c) { m2_cop(COP_MAG3D); m2_cop_pf(a); m2_cop_pf(b); m2_cop_pf(c); return m2_cop_gf(); }
static float m2_cop_dist3d(float x1, float x2, float y1, float y2, float z1, float z2) {
    m2_cop(COP_DIST3D);
    m2_cop_pf(x1); m2_cop_pf(x2); m2_cop_pf(y1); m2_cop_pf(y2); m2_cop_pf(z1); m2_cop_pf(z2);
    return m2_cop_gf();
}

/* Dot products. dot2D = a*b + c*d; dot3 takes the two 3-vectors interleaved. */
static float m2_cop_dot2d(float a, float b, float c, float d) {
    m2_cop(COP_DOT2D); m2_cop_pf(a); m2_cop_pf(b); m2_cop_pf(c); m2_cop_pf(d); return m2_cop_gf();
}
static float m2_cop_dot3(const float a[3], const float b[3]) {
    m2_cop(COP_DOT3);
    m2_cop_pf(a[0]); m2_cop_pf(b[0]); m2_cop_pf(a[1]); m2_cop_pf(b[1]); m2_cop_pf(a[2]); m2_cop_pf(b[2]);
    return m2_cop_gf();
}

/* Vector add / sub / scale, result returned in out[]. */
static void m2_cop_add3(const float a[3], const float b[3], float out[3]) {
    m2_cop(COP_ADD3);
    m2_cop_pf(a[0]); m2_cop_pf(b[0]); m2_cop_pf(a[1]); m2_cop_pf(b[1]); m2_cop_pf(a[2]); m2_cop_pf(b[2]);
    out[0] = m2_cop_gf(); out[1] = m2_cop_gf(); out[2] = m2_cop_gf();
}
static void m2_cop_sub3(const float a[3], const float b[3], float out[3]) {
    m2_cop(COP_SUB3);
    m2_cop_pf(a[0]); m2_cop_pf(b[0]); m2_cop_pf(a[1]); m2_cop_pf(b[1]); m2_cop_pf(a[2]); m2_cop_pf(b[2]);
    out[0] = m2_cop_gf(); out[1] = m2_cop_gf(); out[2] = m2_cop_gf();
}
static void m2_cop_scale3(float s, const float v[3], float out[3]) {
    m2_cop(COP_SCALE3); m2_cop_pf(s); m2_cop_pf(v[0]); m2_cop_pf(v[1]); m2_cop_pf(v[2]);
    out[0] = m2_cop_gf(); out[1] = m2_cop_gf(); out[2] = m2_cop_gf();
}

/* Transform a point by the current bone matrix.
 *   m2_cop_m2w: model -> world,  out = rot*(x,y,z) + T
 *   m2_cop_w2m: world -> model,  out = rot*((x,y,z) - T)   (the inverse) */
static void m2_cop_m2w(float x, float y, float z, float out[3]) {
    m2_cop(COP_M2W); m2_cop_pf(x); m2_cop_pf(y); m2_cop_pf(z);
    out[0] = m2_cop_gf(); out[1] = m2_cop_gf(); out[2] = m2_cop_gf();
}
static void m2_cop_w2m(float x, float y, float z, float out[3]) {
    m2_cop(COP_W2M); m2_cop_pf(x); m2_cop_pf(y); m2_cop_pf(z);
    out[0] = m2_cop_gf(); out[1] = m2_cop_gf(); out[2] = m2_cop_gf();
}

/* Normalize / 2D rotate. NOTE: these three indices DIVERGE between the cpres1
 * silicon firmware (booted by this SDK) and MAME's HLE COP. On cpres1 they are
 * normalize3 / normalize2 / rotate2D as below; MAME-HLE maps the same indices to
 * unrelated ops, so only trust these on real hardware or with M2_HLE_GEO_OFF. */
static void m2_cop_norm3(const float v[3], float out[3]) {
    m2_cop(COP_NORM3); m2_cop_pf(v[0]); m2_cop_pf(v[1]); m2_cop_pf(v[2]);
    out[0] = m2_cop_gf(); out[1] = m2_cop_gf(); out[2] = m2_cop_gf();
}
static void m2_cop_norm2(float x, float y, float out[2]) {
    m2_cop(COP_NORM2); m2_cop_pf(x); m2_cop_pf(y);
    out[0] = m2_cop_gf(); out[1] = m2_cop_gf();
}
static void m2_cop_rot2d(u32 ang, float x, float y, float out[2]) {  /* (x*cos - y*sin, x*sin + y*cos) */
    m2_cop(COP_ROT2D); m2_cop_pi(ang); m2_cop_pf(x); m2_cop_pf(y);
    out[0] = m2_cop_gf(); out[1] = m2_cop_gf();
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
