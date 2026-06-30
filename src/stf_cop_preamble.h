#ifndef STF_COP_PREAMBLE_H
#define STF_COP_PREAMBLE_H
/* stf_cop_preamble.h — STF poly_test_camera's per-object COP preamble, IN C.
 * Originally a verbatim 46-word capture (tools/cop_polytest_obj1.txt, poly-test obj1, write-positions
 * 43-88); now expressed as the equivalent FIFO emission. The bytes pushed are IDENTICAL to the capture
 * (verified op-by-op) — a readability rewrite, not a behaviour change.
 *
 * WHAT IT IS
 *   STF replays this fixed math sequence on the COP (cpres1, the math SHARC) before it transforms+submits
 *   an object. EMPIRICALLY it is required: skip it and the COP reports ready (COP_STAT=1) but never commits
 *   a frame (COP_WPOS frozen). NOTE the four ops below are PURE COMPUTE (in -> out) — they do NOT mutate the
 *   COP's bone/matrix/projection state (unlike set_pos / ang_x / ...). So the preamble does not "set render
 *   state" by side effect; each result is computed and immediately drained here. Replaying the exact
 *   captured FIFO sequence is a drain/handshake PREREQUISITE for the commit, not a state write. (The operands
 *   themselves read like a poly_test_camera viewport extent ~4000 + an axis frame from +-2.0 unit vectors.)
 *
 * THE OPS  (verified against the cpres1 disassembly in ../m2-hle; op = command word / 0x800101)
 *   0x17 int2f   integer -> float             (handler: f0 = float r1)
 *   0x18 f2int   float   -> integer, truncate (handler: r0 = fix f1)
 *   0x2B dist2D  4 floats (two XZ points) -> sqrt((x2-x1)^2 + (z2-z1)^2)   (vector length, float)
 *   0x2F azimuth 4 floats (two XZ points) -> atan2(z2-z1, x2-x1)           (vector angle, i16: 0x10000=360deg)
 *   Every op yields ONE result that MUST be drained (read back) — an un-drained result backs up the COP
 *   output FIFO and stalls it before the submit. Each emit helper below drains its op's single result.
 *
 * STRUCTURE — two near-identical passes, each = [viewport pair] + [axis-frame ops]:
 *   pass 1 : f2int(VIEWPORT)x2, int2f(3999)x2, then azimuth + 3x dist2D.
 *   pass 2 : same viewport pair, then azimuth + 1x dist2D.
 *   (Op identities + signatures are from the disassembly; the "viewport/axis-frame" labels are how the
 *    captured operands read and don't affect the emitted bytes.)
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
