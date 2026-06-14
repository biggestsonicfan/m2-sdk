/*
 * m2_fastmath.h — native i960 scalar float math.
 *
 * Overrides the SCALAR COP helpers (m2_cop_fadd/fsub/fmul/fdiv/int2f/f2int/sqrt) with
 * native i960 float ops. Each of those helpers is a SHARC COP FIFO round-trip that
 * stalls the i960 until the DSP answers; the per-frame 3D geometry (tube/view/contour,
 * the GEO vertex-prep in m2_geo.h, etc.) was issuing thousands of them and that
 * dominated the frame time. Doing the scalar math locally removes the stall.
 *
 * The GEO chip still does the real rendering, and the MATRIX / TRIG helpers
 * (wmatrix/rmatrix, cop_sincos, atan2 — which genuinely need the SHARC) still use the
 * COP: those are NOT overridden here.
 *
 * Included at the END of m2_3d.h, so every module compiled afterwards (m2_geo.h and the
 * game headers) picks up the native path. Requires m2.h (for u32) to be included first.
 */
#ifndef M2_FASTMATH_H
#define M2_FASTMATH_H

/* sqrt via fast inverse-sqrt + 2 Newton steps (no COP round-trip). */
static float m2_fsqrt(float x) {
    union { float f; u32 u; } v;
    float g;
    if (x <= 0.0f) return 0.0f;
    v.f = x; v.u = 0x5f3759dfu - (v.u >> 1); g = v.f;        /* seed ~ 1/sqrt(x) */
    g = g * (1.5f - 0.5f * x * g * g);
    g = g * (1.5f - 0.5f * x * g * g);
    return x * g;                                            /* x * (1/sqrt(x)) */
}

#define m2_cop_fadd(a, b)  ((a) + (b))
#define m2_cop_fsub(a, b)  ((a) - (b))
#define m2_cop_fmul(a, b)  ((a) * (b))
#define m2_cop_fdiv(a, b)  ((a) / (b))
#define m2_cop_int2f(i)    ((float)(i))
#define m2_cop_f2int(f)    ((int)(f))
#define m2_cop_sqrt(x)     m2_fsqrt(x)

#endif /* M2_FASTMATH_H */
