/*
 * m2_workaround.h — the three-target quirk shims, in ONE place.
 *
 * Everything in this SDK runs on up to three implementations of the same underspecified
 * board, and each has bugs/holes the code must dodge:
 *   1. REAL SILICON      — the ground truth (i960KB + real SHARCs + real VRAM).
 *   2. MAME (claude_mame)— accurate bus/CPU; some regions UNMAPPED (COPRO_RESET_REG,
 *                          COP_BOOTSTAT_REG read 0 — see m2_constants.h), GEO optionally
 *                          HLE'd (geo_parse) with its own divergences.
 *   3. m2emulator (HLE)  — fast bring-up; NO i960 FPU, a MEMB instruction-decoder hole,
 *                          late BUFF_RAM mapping, fragile IRQ/register-cache handling.
 *
 * Every workaround that is CODE lives here as a named shim, so a quirk can be found,
 * reasoned about, and eventually deleted (when a target is dropped) in one place.
 * Divergences that are POLICY rather than code stay where they act, but are indexed
 * here so this header is the one-stop quirks page:
 *
 *   - m2emu has NO i960 FPU: build -msoft-float or the first FP opcode is an invalid
 *     instruction (black screen). MAME/silicon accept hard float but the KERNEL stays
 *     soft-float regardless (memory: build-must-be-softfloat). Build-level, build_*.bat.
 *   - m2emu maps BUFF_RAM late: an early 0x900000 write black-screens the HLE
 *     (geo_buffram_clear placement note, m2_geo.h).
 *   - MAME HLE COP maps opcode indices 0x30/0x5A/0x5B to DIFFERENT ops than the real
 *     cpres1 firmware (norm3/norm2/rot2d note, m2_math.h).
 *   - MAME HLE GEO renders direct_data; m2emu + silicon do not (m2_geo.h).
 *   - m2emu crashes on a vblank IRQ inside a deep soft-float chain (register-cache
 *     spill) — keep per-frame float out of IRQ windows (m2_geo.h geo_quad_matrix note).
 *   - m2emu masks shift counts to 5 bits (an i960 shift by 32+ gives 0). GCC's i960
 *     `x == 0` is `shro x,1,y`, so any non-zero multiple of 32 tested as zero (Pac-Man's
 *     Z80 flag tables: Z set for 0x20, 0x40, ...). Build-level: tools/emu_shro.py, the
 *     compiler launcher CMakeLists.txt installs (M2_EMU_SHRO, default ON).
 *   - m2emu's sound UART (0x9C0004) always reads 0x07 (RxRDY set) and its data 0xFF, and
 *     nothing sent reaches the sound board: bound every RX drain (m2_scsp_probe), and
 *     the scsp_passthru sound path is silent there.
 *
 * ---- shim 1: m2emu MEMB-decoder hole -----------------------------------------------
 * m2emu's instruction decoder rejects some 2-word MEMB absolute-displacement forms
 * (`ld/lda ABS, reg` — the displacement word parses as an invalid opcode; first seen on
 * `ld 0x980010`). gcc CONSTANT-FOLDS every C pointer expression back to exactly that
 * absolute form, so the only reliable C-level dodge is to force the address through a
 * REGISTER: a noinline function argument. These accessors are that dodge — use them for
 * one-off MMIO touches in code that must boot on m2emu (hot paths keep their local
 * `volatile u32 *p` pointer variables, which gcc also keeps in registers).
 * (Runtime-computed addresses — PEEK/POKE, pointer walks — are naturally safe.)
 */
#ifndef M2_WORKAROUND_H
#define M2_WORKAROUND_H

static u32 __attribute__((noinline, noclone)) m2_rd32(u32 a) { return *(volatile u32 *)a; }
static u32 __attribute__((noinline, noclone)) m2_rd16(u32 a) { return (u32)*(volatile u16 *)a; }
static u32 __attribute__((noinline, noclone)) m2_rd8 (u32 a) { return (u32)*(volatile u8  *)a; }
static void __attribute__((noinline, noclone)) m2_wr32(u32 a, u32 v) { *(volatile u32 *)a = v; }
static void __attribute__((noinline, noclone)) m2_wr16(u32 a, u32 v) { *(volatile u16 *)a = (u16)v; }
static void __attribute__((noinline, noclone)) m2_wr8 (u32 a, u32 v) { *(volatile u8  *)a = (u8)v; }

/* ---- shim 2: gcc960 -O2 "g14" store bug ---------------------------------------------
 * gcc960 -O2 compiled a LONE trailing constant store into `st g14` with g14 = 0 (g14 is
 * the compiler's arg-block register; the boot asm zeroes it), silently writing 0 instead
 * of the value. Writing the register twice generates a real store for at least the
 * second write. The current i960-elf GCC 11 toolchain is not known to have the bug, but
 * the affected writes are boot-critical one-time MMIO config (IRQ enable, 315-5649
 * mode), so the belt-and-braces double write is kept — through this macro, so every
 * site is findable and the workaround is deletable in one place. Harmless on all
 * targets (idempotent config registers). */
#define M2_WRITE_TWICE(lv, v) do { (lv) = (v); (lv) = (v); } while (0)

#endif /* M2_WORKAROUND_H */
