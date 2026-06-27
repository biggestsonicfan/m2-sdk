#ifndef STF_COP_PREAMBLE_H
#define STF_COP_PREAMBLE_H
/* stf_cop_preamble.h - STF poly_test_camera's COP-FIFO BASIS+VIEWPORT preamble for an object,
 * captured verbatim from real STF (tools/cop_polytest_obj1.txt, poly-test obj1, write-positions 43-88).
 * These OP0x18(viewport=3999.99)/OP0x17(0xF9F)/OP0x2f+OP0x2b(orthonormal basis, +/-2.0 unit vecs) ops
 * set the COP's render/projection state. STF runs this BEFORE every object transform+submit; omitting
 * it is why our COP reported ready (COP_STAT=1) but never committed (COP_WPOS frozen). All operands are
 * FIXED (no read-drains in this pure-write window), so it replays straight to the COP FIFO (0x884000). */
#define COP_PREAMBLE_N 46u
static const u32 cop_obj_preamble[46] = {
    0x0C001818u, 0x4579FFFFu, 0x0C001818u, 0x4579FFFFu, 0x0B801717u, 0x00000F9Fu,
    0x0B801717u, 0x00000F9Fu, 0x17802F2Fu, 0x00000000u, 0x00000000u, 0x40000000u,
    0xC0000000u, 0x15802B2Bu, 0xC0000000u, 0x40000000u, 0x00000000u, 0x00000000u,
    0x15802B2Bu, 0x40000000u, 0xC0000000u, 0x00000000u, 0x00000000u, 0x15802B2Bu,
    0x40000000u, 0xC0000000u, 0x00000000u, 0x00000000u, 0x0C001818u, 0x4579FFFFu,
    0x0C001818u, 0x4579FFFFu, 0x0B801717u, 0x00000F9Fu, 0x0B801717u, 0x00000F9Fu,
    0x17802F2Fu, 0x00000000u, 0x00000000u, 0xC0000000u, 0x40000000u, 0x15802B2Bu,
    0x40000000u, 0xC0000000u, 0x00000000u, 0x00000000u,
};
#endif
