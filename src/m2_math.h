/*
 * m2_math.h — math coprocessor (COP / cpres1 SHARC) op emitters.
 *
 * Low-level helpers that push ONE COP math op + its operands to a COP command FIFO `cf`
 * and drain the single result. Opcode names are COP_* (m2_3d.h); the SHARC semantics are
 * verified against the cpres1 disassembly (../m2-hle):
 *   COP_INT2F  (0x17)  integer -> float                   (handler: f0 = float r1)
 *   COP_F2INT  (0x18)  float   -> integer, truncate        (handler: r0 = fix f1)
 *   COP_DIST2D (0x2B)  4 floats (two XZ points) -> sqrt((x2-x1)^2 + (z2-z1)^2)  (length, float)
 *   COP_ATAN2  (0x2F)  4 floats (two XZ points) -> atan2(z2-z1, x2-x1)  (azimuth, i16: 0x10000=360deg)
 * Each op yields ONE result that MUST be drained (read back) — an un-drained result backs up the COP
 * output FIFO and stalls it.
 *
 * These are the explicit-FIFO, drain-and-discard form (used by the STF object preamble,
 * stf_cop_preamble.h). The value-RETURNING COP helpers on the global COP FIFO —
 * m2_cop_fadd/fsub/fmul/sqrt/sincos/atan2/... — live in m2_3d.h.
 */
#ifndef M2_MATH_H
#define M2_MATH_H

#include "m2_3d.h"   /* COP_* opcodes + the COP command FIFO */

/* reinterpret an exactly-representable float literal (+-2.0, 0.0, ...) as its FIFO word.
 * Compile-time foldable, no runtime FP / FPU access, so it is soft-float-safe. */
static u32 cop__fbits(float f) { union { float f; u32 u; } x; x.f = f; return x.u; }
/* drain one COP result word (one FIFO read), same as cop_drain(cf,1). */
static void cop__drain1(volatile u32 *cf) { volatile u32 d = *cf; (void)d; }

/* push one COP math op + operands to FIFO `cf`, then drain its single result. */
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

#endif /* M2_MATH_H */
