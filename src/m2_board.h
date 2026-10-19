/*
 * m2_board.h — which Model 2 board the build targets, and the addresses that differ.
 *
 * Pick one at build time (CMake: -DM2_BOARD=2O|2A|2B|2C, which defines the macro):
 *   M2_BOARD_2O  original Model 2 (MAME model2o): TGP coprocessor, Model 1 IO board
 *                                                          test set: desert (or daytona)
 *   M2_BOARD_2A  Model 2A-CRX: TGP (Fujitsu MB86234) coprocessor     test set: dynamcop
 *   M2_BOARD_2B  Model 2B-CRX: SHARC ADSP-21062 COP + SHARC GEO      test set: sfight (default)
 *   M2_BOARD_2C  Model 2C-CRX: TGPx4 (Fujitsu MB86235) coprocessor   test set: dynamcopc
 *
 * The i960 side of the three boards is the same machine except for what is listed here.
 * Dynamite Cop shipped on all three with one program image per board; its 1 MB upper ROM
 * (epr-20932/20933) is byte-identical on all three, and its lower program differs only in
 * these places (offsets in the joined lower image, ROM_LOAD32_WORD; docs/model2-boards.md):
 *
 *   what               2A (dynamcop)             2B (dynamcopb)            2C (dynamcopc)
 *   render mode        0x10000000 = 4  @6ee10   = 0x4004                  = 0x4004
 *   texture RAM        0x12000000 / 0x12400000   0x11000000 / 0x11200000   same as 2B
 *   luma RAM           0x12800000, 1 byte per u32  0x11400000, 1 byte per u16  same as 2B
 *   copro program      TGP, u32 words @b6084     two SHARCs, IOP DMA @be090  TGPx4, u32 pairs @ba260
 *
 * MAME (src/mame/sega/model2.cpp) agrees: model2_tgp_mem maps texture RAM at 0x12000000/
 * 0x12400000 through tex0_w/tex1_w (one 16-bit halfword per 32-bit bus word) and luma RAM at
 * 0x12800000 with umask32 0x000000ff; model2b_crx_mem / model2c_crx_mem map 0x11000000..
 * 0x1140ffff (luma umask16 0x00ff). Render mode bit 2 = m_render_mode on every board.
 *
 * Pure integer literals only (included from m2_constants.h). The access helpers that hide the
 * 2A bus layout (m2_tex_wr32 / m2_tex_rd32 / m2_luma_wr) are in m2font.h and m2_color.h.
 */
#ifndef M2_BOARD_H
#define M2_BOARD_H

#if defined(M2_BOARD_2O) + defined(M2_BOARD_2A) + defined(M2_BOARD_2B) + defined(M2_BOARD_2C) > 1
#error "define only one of M2_BOARD_2O / M2_BOARD_2A / M2_BOARD_2B / M2_BOARD_2C"
#endif
#if !defined(M2_BOARD_2O) && !defined(M2_BOARD_2A) && !defined(M2_BOARD_2C) && !defined(M2_BOARD_2B)
#define M2_BOARD_2B 1
#endif

/* The original Model 2 (Daytona USA, MAME model2o) is a 2A on the i960 side: the same TGP
 * (model2_tgp_state; its program is uploaded the same way, so the SDK's 2A TGP kernel runs),
 * the same texture/luma map and render mode 4 (DaytonaTestRom main.c writes 0x04). What differs
 * (model2o_mem):
 *   - the program ROM pair is 2 x 128 KB (desert epr-16976.12 / .13, daytona epr-16722a.12 /
 *     .13): a 256 KB image;
 *   - 0x200000 is 128 KB of RAM (2A: 256 KB) and 0x220000.. maps program ROM +0x20000;
 *   - 0x01C00000 is the MB8421 dual-port RAM to the Model 1 IO board (umask32 0x00ff00ff:
 *     dpram byte n at 0x01C00000 + 2*n), not the 315-5649 (m2_io.h reads it for you).
 * Credit: DaytonaTestRom (Steve Jones, MIT, github.com/stevej0/DaytonaTestRom). */
#if defined(M2_BOARD_2O)
#define M2_BOARD_2A_TGP      1            /* TGP board: 2A's texture map and kernel             */
#define M2_BOARD_MODEL1IO    1            /* inputs through the Model 1 IO board dpram          */
#else
#define M2_BOARD_MODEL1IO    0
#endif
#if defined(M2_BOARD_2A)
#define M2_BOARD_2A_TGP      1
#endif

#if defined(M2_BOARD_2A_TGP)
#if defined(M2_BOARD_2O)
#define M2_BOARD_NAME        "Model 2"
#else
#define M2_BOARD_NAME        "Model 2A"
#endif
#define M2_BOARD_TEXRAM0     0x12000000u  /* texture RAM 0 (tex0_w)                            */
#define M2_BOARD_TEXRAM1     0x12400000u  /* texture RAM 1 (tex1_w)                            */
#define M2_BOARD_TEX_STRIDE  2u           /* bus words per 32 bits of texels: one halfword each */
#define M2_BOARD_LUMARAM     0x12800000u  /* luma RAM, entry i at +4*i (umask32 0x000000ff)     */
#define M2_BOARD_LUMA_STRIDE 4u
#define M2_BOARD_RENDERMODE  0x0004u      /* dynamcop 2A @6ee10 writes 4 (2B/2C write 0x4004)   */
#define M2_BOARD_SHARC       0            /* no SHARC COP / GEO to boot                         */
#else /* 2B and 2C share the texture/luma map */
#if defined(M2_BOARD_2C)
#define M2_BOARD_NAME        "Model 2C"
#define M2_BOARD_SHARC       0            /* TGPx4 COP; the GEO is a fixed-function chip        */
#else
#define M2_BOARD_NAME        "Model 2B"
#define M2_BOARD_SHARC       1            /* cpres1 COP + cpres2 GEO SHARCs (m2_3d.h)            */
#endif
#define M2_BOARD_TEXRAM0     0x11000000u
#define M2_BOARD_TEXRAM1     0x11200000u
#define M2_BOARD_TEX_STRIDE  1u
#define M2_BOARD_LUMARAM     0x11400000u  /* luma RAM, entry i at +2*i (umask16 0x00ff)         */
#define M2_BOARD_LUMA_STRIDE 2u
#define M2_BOARD_RENDERMODE  0x4004u      /* STF start_again_ip; dynamcopb/dynamcopc the same   */
#endif

/* Only STF's own data ROMs (the sfight set, 2B) hold the luma table, polygon palette and model
 * table that m2_color.h / m2_obj.h read by default. On another board the data ROMs are another
 * game's, so build STF's polygon luma curve instead of copying it (m2_color.h M2_LUMA_STF_CURVE). */
#if !defined(M2_BOARD_2B) && !defined(M2_LUMA_COMPUTED) && !defined(M2_LUMA_STF_CURVE)
#define M2_LUMA_STF_CURVE 1
#endif

#endif /* M2_BOARD_H */
