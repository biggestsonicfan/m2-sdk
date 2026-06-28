#ifndef STF_COP_PREAMBLE_H
#define STF_COP_PREAMBLE_H
/* stf_cop_preamble.h — STF poly_test_camera's per-object COP PROJECTION+BASIS preamble, IN C.
 * Originally a verbatim 46-word capture (tools/cop_polytest_obj1.txt, poly-test obj1, write-positions
 * 43-88); now expressed as the equivalent FIFO emission. The bytes pushed are IDENTICAL to the capture
 * (verified op-by-op) — this is a readability rewrite, not a behaviour change.
 *
 * WHAT IT IS
 *   Before STF transforms+submits ANY object it primes the COP (cpres1, the math SHARC) with two pieces
 *   of render state: the screen PROJECTION extent (~4000) and the object's ORTHONORMAL BASIS (its axis
 *   frame, derived from ±2.0 unit vectors). Skipping it is why our COP would report ready (COP_STAT=1)
 *   but never commit a frame (COP_WPOS frozen): the state it transforms against was never established.
 *
 * THE OPS  (cpres1 dispatch; the command word encodes op = word / 0x800101)
 *   0x17 int2f   integer -> float
 *   0x18 f2int   float   -> integer
 *   0x2B dist2D  2D vector length  (4 operands; normalises the basis axes)
 *   0x2F azimuth 2D vector angle   (4 operands; atan2-style, gives the basis ANGLE)
 *   Every op yields ONE result that MUST be drained (read back) — an un-drained result backs up the COP
 *   output FIFO and stalls it before the submit. Each emit helper below drains its op's single result.
 *
 * STRUCTURE — two near-identical passes, each = [viewport extent] + [orthonormal basis]:
 *   pass 1 : f2int(VIEWPORT)x2, int2f(3999)x2, then azimuth(+2,-2) + 3x dist2D normalise.
 *   pass 2 : same viewport, then azimuth(-2,+2) + 1x dist2D (the other axis).
 *   (The per-axis role of each azimuth/dist2D is inferred from the op types + the ±2.0 / ~4000 constants;
 *    the emitted words are exact, so the inference doesn't affect behaviour.)
 */

/* The viewport extent operand. 0x4579FFFF = 4000.0f MINUS ONE ULP (3999.999756f) — NOT "3999.99".
 * Kept as the exact captured bit pattern: the literal 3999.99f rounds ~40 ULPs away and would no longer
 * match the silicon capture. (Looks like an inclusive upper bound just under 4000 = nextafterf(4000,0).) */
#define COP_VIEWPORT_MAX 0x4579FFFFu

/* reinterpret an exactly-representable float literal (±2.0, 0.0) as its FIFO word. Compile-time foldable,
 * no runtime FP / FPU access, so it is soft-float-safe. */
static u32 cop__fbits(float f) { union { float f; u32 u; } x; x.f = f; return x.u; }
/* drain one COP result word (one FIFO read), same as cop_drain(cf,1). */
static void cop__drain1(volatile u32 *cf) { volatile u32 d = *cf; (void)d; }

/* the four cpres1 ops used by the preamble: push opcode + operands, then drain the single result. */
static void cop_f2int_raw(volatile u32 *cf, u32 fbits) { *cf = 0x0C001818u; *cf = fbits;     cop__drain1(cf); }
static void cop_int2f(volatile u32 *cf, u32 ival)      { *cf = 0x0B801717u; *cf = ival;      cop__drain1(cf); }
static void cop_azimuth(volatile u32 *cf, float a, float b, float c, float d) {
    *cf = 0x17802F2Fu;
    *cf = cop__fbits(a); *cf = cop__fbits(b); *cf = cop__fbits(c); *cf = cop__fbits(d);
    cop__drain1(cf);
}
static void cop_dist2d(volatile u32 *cf, float a, float b, float c, float d) {
    *cf = 0x15802B2Bu;
    *cf = cop__fbits(a); *cf = cop__fbits(b); *cf = cop__fbits(c); *cf = cop__fbits(d);
    cop__drain1(cf);
}

/* Emit STF's per-object PROJECTION+BASIS preamble to the COP command FIFO `cf` (0x884000). */
static void cop_emit_obj_preamble(volatile u32 *cf) {
    /* ---- pass 1 : viewport extent ---- */
    cop_f2int_raw(cf, COP_VIEWPORT_MAX);
    cop_f2int_raw(cf, COP_VIEWPORT_MAX);
    cop_int2f(cf, 3999u);
    cop_int2f(cf, 3999u);
    /* ---- pass 1 : orthonormal basis (±2.0 vectors) ---- */
    cop_azimuth(cf, 0.0f, 0.0f,  2.0f, -2.0f);
    cop_dist2d (cf, -2.0f,  2.0f, 0.0f, 0.0f);
    cop_dist2d (cf,  2.0f, -2.0f, 0.0f, 0.0f);
    cop_dist2d (cf,  2.0f, -2.0f, 0.0f, 0.0f);
    /* ---- pass 2 : viewport extent (re-asserted) ---- */
    cop_f2int_raw(cf, COP_VIEWPORT_MAX);
    cop_f2int_raw(cf, COP_VIEWPORT_MAX);
    cop_int2f(cf, 3999u);
    cop_int2f(cf, 3999u);
    /* ---- pass 2 : orthonormal basis (other axis) ---- */
    cop_azimuth(cf, 0.0f, 0.0f, -2.0f,  2.0f);
    cop_dist2d (cf,  2.0f, -2.0f, 0.0f, 0.0f);
}
#endif
