#ifndef STF_COP_PREAMBLE_H
#define STF_COP_PREAMBLE_H
/* stf_cop_preamble.h — STF poly_test_camera's per-object COP preamble, IN C.
 * Originally a verbatim 46-word capture (tools/cop_polytest_obj1.txt, poly-test obj1, write-positions
 * 43-88); now expressed as the equivalent FIFO emission via the m2_math.h COP op emitters. The bytes
 * pushed are IDENTICAL to the capture (verified op-by-op) — a readability rewrite, not a behaviour change.
 *
 * WHAT IT IS
 *   STF replays this fixed math sequence on the COP (cpres1, the math SHARC) before it transforms+submits
 *   an object. EMPIRICALLY it is required: skip it and the COP reports ready (COP_STAT=1) but never commits
 *   a frame (COP_WPOS frozen). The ops (m2_math.h: int2f / f2int / azimuth / dist2D) are PURE COMPUTE
 *   (in -> out), each result drained here — they do NOT mutate the COP's bone/matrix/projection state.
 *   Replaying the exact captured FIFO sequence is a drain/handshake PREREQUISITE for the commit, not a
 *   state write. (The operands read like a poly_test_camera viewport extent ~4000 + an axis frame from
 *   +-2.0 unit vectors.)
 *
 * STRUCTURE — two near-identical passes, each = [viewport pair] + [axis-frame ops]:
 *   pass 1 : f2int(VIEWPORT)x2, int2f(3999)x2, then azimuth + 3x dist2D.
 *   pass 2 : same viewport pair, then azimuth + 1x dist2D.
 *   (The "viewport/axis-frame" labels are how the captured operands read; the emitted bytes are exact.)
 */

#include "m2_math.h"   /* cop_f2int_raw / cop_int2f / cop_azimuth / cop_dist2d */

/* The viewport extent operand. 0x4579FFFF = 4000.0f MINUS ONE ULP (3999.999756f) — NOT "3999.99".
 * Kept as the exact captured bit pattern: the literal 3999.99f rounds ~40 ULPs away and would no longer
 * match the silicon capture. (Looks like an inclusive upper bound just under 4000 = nextafterf(4000,0).) */
#define COP_VIEWPORT_MAX 0x4579FFFFu

/* Emit STF's per-object preamble to the COP command FIFO `cf` (0x884000). */
static void cop_emit_obj_preamble(volatile u32 *cf) {
    /* ---- pass 1 : viewport extent ---- */
    cop_f2int_raw(cf, COP_VIEWPORT_MAX);
    cop_f2int_raw(cf, COP_VIEWPORT_MAX);
    cop_int2f(cf, 3999u);
    cop_int2f(cf, 3999u);
    /* ---- pass 1 : axis frame (+-2.0 vectors) ---- */
    cop_azimuth(cf, 0.0f, 0.0f,  2.0f, -2.0f);
    cop_dist2d (cf, -2.0f,  2.0f, 0.0f, 0.0f);
    cop_dist2d (cf,  2.0f, -2.0f, 0.0f, 0.0f);
    cop_dist2d (cf,  2.0f, -2.0f, 0.0f, 0.0f);
    /* ---- pass 2 : viewport extent (re-asserted) ---- */
    cop_f2int_raw(cf, COP_VIEWPORT_MAX);
    cop_f2int_raw(cf, COP_VIEWPORT_MAX);
    cop_int2f(cf, 3999u);
    cop_int2f(cf, 3999u);
    /* ---- pass 2 : axis frame (other axis) ---- */
    cop_azimuth(cf, 0.0f, 0.0f, -2.0f,  2.0f);
    cop_dist2d (cf,  2.0f, -2.0f, 0.0f, 0.0f);
}
#endif
