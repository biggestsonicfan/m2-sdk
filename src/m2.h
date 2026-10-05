/*
 * m2.h — Sega Model 2 (model2b) homebrew SDK.
 *
 * Header-only board layer extracted from the Snake homebrew. Include from
 * exactly ONE .c per game (it defines the linker stubs the boot code needs).
 * Runs on m2-hle2 (the `m2snake` pure-data profile) and on real MAME via an
 * sfight/Daytona loose-ROM override.
 *
 * Quick start:
 *     #include "m2.h"
 *     int main(void){
 *         m2_init();                       // board + font ready
 *         m2_setpal(1, M2_RGB(31,0,0));    // pen 1 = red
 *         m2_solidtile(1, 1);              // char 1 = solid pen-1
 *         for(;;){ m2_vsync(); u32 in=m2_input(0); ... draw ...; }
 *     }
 *
 * Hard-won board facts baked in here (do not re-derive):
 *   - colorxlat MUST be built or every pen renders black (model2 maps each pen
 *     through colorxlat@0x1810000 then gamma).
 *   - the vblank pending bit (0xE80000 bit0, acked by the vblank ISR) is only latched
 *     when the source is enabled in 0xE80004 — and that enable MUST be written
 *     twice (gcc960 -O2 turns a lone tail constant store into `st g14`, g14=0).
 *   - park the geometrizer on an END instr or its garbage 3D clobbers the tiles.
 *   - tile entry = PRIO(bit15) | (palbank<<7) | char ; pen = palbank*16 + pixel.
 *   - input is the 315-5649: write 0 to IO_BANK, then read active-low IN1/IN2.
 */
#ifndef M2_H
#define M2_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#include "m2_debug.h"   /* M2_DBG / M2_DBGH serial trace (no-op unless -DM2_DEBUG) */
#include "m2_workaround.h" /* three-target quirk shims (m2emu MEMB dodge, g14 double-write) */
#include "m2_wait.h"    /* instrumented hardware waits (m2_waitdbg record + M2W_* sites) */
#include "m2_post.h"    /* POST-code boot progress at fixed 0x5F0000 (M2POST_* stages) */
#include "m2_fault.h"   /* fault record @0x5F0040 + opt-in recovery hook (kx_ftbl.s)   */

/* Pure-C Newton sqrt — the SDK's one sqrt (was duplicated as g2d__sqrt/geo__sqrt).
 * Do NOT route this through the COP (m2_cop_sqrt): COP_SQRT is always "defined"
 * (an opcode macro, not a feature flag), and waiting on the COP math FIFO can hang
 * the render loop. The COP is only an accelerator; the C path is correct under
 * soft-float and needs no libm. */
static float m2_sqrtf(float x) {
    float g; int i;
    if (x <= 0.0f) return 0.0f;
    g = x; for (i = 0; i < 8; i++) g = 0.5f * (g + x / g);
    return g;
}

/* SDK linkage: defaults to `static` (existing builds unchanged). Build the
 * main/'kernel' program with -DM2_API= to emit these as GLOBAL symbols so
 * uploaded subprograms can resolve them via the kernel's map (ld --just-symbols).
 * Apps never include this header; they get generated extern prototypes. */
#ifndef M2_API
#define M2_API static
#endif

/* ---- hardware addresses --------------------------------------------------- */
#define M2_TILE_FG  ((volatile u16 *)0x01000000u)  /* FG layer-0 tilemap (64 wide) */
#define M2_TILE_BG  ((volatile u16 *)0x01004000u)  /* BG layer-2 tilemap           */
#define M2_CHARGFX  ((volatile u8  *)0x01080000u)  /* 8x8 4bpp char gfx, idx*32     */
#define M2_PALETTE  ((volatile u16 *)0x01800000u)  /* BGR555, idx = palbank*16+pix  */

#include "m2_io.h"    /* the 315-5649 I/O chip as one struct: M2_IO.bank/in0/in1/in2/... */

/* Launcher app-exit hook (a fixed cell in real work RAM, above the heap and below
 * the i960 fault/interrupt tables at 0x5ff000): when armed, m2_vsync() returns to
 * the handler on P1 Start, so a launcher can stop ANY app. A launcher arms it
 * (m2_exit_arm) before running an app and disarms it for its own UI. MUST be
 * disarmed once at boot before the first m2_vsync (work RAM powers up with
 * garbage). [0]=magic, [1]=handler addr.
 * (Was 0x043FE020 in the 8 MB ext-RAM window — that address is unmapped on real
 * hardware, so m2_vsync read garbage and jumped to a bad handler = invalid op.) */
#define M2_EXIT_CTL   0x005F8000u
#define M2_EXIT_MAGIC 0x45584954u    /* 'EXIT' */
typedef struct { u32 magic; u32 handler; } m2_exit_t;
#define M2_EXIT (*(volatile m2_exit_t *)M2_EXIT_CTL)
_Static_assert(__builtin_offsetof(m2_exit_t, magic)   == 0, "m2_exit_t.magic");
_Static_assert(__builtin_offsetof(m2_exit_t, handler) == 4, "m2_exit_t.handler");

#define M2_RENDERMODE (*(volatile u16 *)0x10000000u)
#define M2_HSYNC      (*(volatile u16 *)0x01040000u)
#define M2_VSYNC      (*(volatile u16 *)0x01060000u)
#define M2_L1_HPOS    (*(volatile u16 *)0x0100A000u)
#define M2_L2_HPOS    (*(volatile u16 *)0x0100A004u)
#define M2_L1_VPOS    (*(volatile u16 *)0x0100A008u)
#define M2_L2_VPOS    (*(volatile u16 *)0x0100A00Cu)
#define M2_VIDEO_CTRL (*(volatile u32 *)0x0098000Cu)
#define M2_HSCRTB_1   ((volatile u16 *)0x01008000u)
#define M2_HSCRTB_2   ((volatile u16 *)0x01008400u)

#define M2_GEO_WADDR  (*(volatile u32 *)0x00801008u)
#define M2_GEO_END    (*(volatile u32 *)0x008000F0u)
#define M2_GEO_RADDR  (*(volatile u32 *)0x00803008u)
#define M2_ZCLIP      (*(volatile u32 *)0x0181C000u)

#define M2_IRQ_REQ (*(volatile u32 *)0x00E80000u)
#define M2_IRQ_ENA (*(volatile u32 *)0x00E80004u)
#define M2_IRQ_VBL 0x00000001u

/* ---- i8251 aux UART (board -> host debug serial) -------------------------- *
 * The standard async UART at SERIAL_START=0x1C80000 (data) / 0x1C80002 (cmd on
 * write, status on read). m2_init runs House of the Dead's _InitSerial sequence
 * (...0x4E mode, 0x37 command = TxEN|RxEN), so after m2_init these transmit a
 * byte/string to a host terminal. TxRDY = status bit0; the wait is bounded so a
 * disconnected UART can't hang the board. This is the developer feedback channel
 * (the 315-5649 at 0x1C00000 is the on-board I/O link, not a path to a PC). */
#define M2_UART_DATA (*(volatile u16 *)0x01C80000u)
#define M2_UART_CTL_ADDR 0x01C80002u                  /* write: 8251 mode/command; read: status */
#define M2_UART_STAT (*(volatile u16 *)M2_UART_CTL_ADDR) /* read: 8251 status; bit0 = TxRDY */
M2_API void m2_uart_putc(char c) {
    (void)m2_wait_mask16(M2W_UART_TX, M2_UART_CTL_ADDR, 0x01u, 0x01u, 200000u);  /* TxRDY */
    M2_UART_DATA = (u16)(u8)c;
}
M2_API void m2_uart_puts(const char *s) { while (*s) m2_uart_putc(*s++); }
M2_API void m2_uart_hex(u32 v) {           /* "XXXXXXXX" big-endian nibbles */
    int i; for (i = 28; i >= 0; i -= 4) m2_uart_putc("0123456789ABCDEF"[(v >> i) & 0xF]);
}

#define M2_W     64u           /* hardware tilemap stride (tiles)  */
#define M2_PRIO  0x8000u       /* tile entry bit15: above-3D category */
#define M2_RGB(r,g,b) ((u16)(((b) << 10) | ((g) << 5) | (r)))

/* Daytona's 128-glyph 8x8 4bpp font, already in hardware byte order. */
#include "m2font.h"            /* const u8 gFont[128*32] */

/* ---- linker stubs (referenced by the boot .s; defined once here) ---------- */
/* vblank frame counter — a FIXED platform cell (the M2_EXIT_CTL pattern), incremented
 * by the vblank ISR (i_handle.s _irq_vblank, .set to the same address). Was a kernel
 * .bss global, whose floating address forced apps into --just-symbols lockstep with
 * the kernel; fixed, it is shareable forever (apps get it via PROVIDE in lib/app.ld).
 * NOT auto-zeroed (outside .bss/.data) — m2_init resets it; consumers only delta it. */
#define M2_FRAMEVBL_ADDR 0x005F00F0u
#define frameVBL (*(volatile u32 *)M2_FRAMEVBL_ADDR)
/* STF work-RAM globals (M2_MEM.vsync = RAMBASE_START etc.) — the vblank/timer ISRs in
 * i_handle.s still reach them via the linker symbols _RAMBASE_START / _timerFlag. */
#include "m2_memory.h"
/* STF read_sw/write_sw/io_request_chk/read_analog_to_ram + the HELD/pressed/released
 * edge model. AFTER m2_memory.h (uses M2_MEM) and m2_io.h (M2_INP_* masks). read_sw/
 * write_sw are called from the vblank ISR (i_handle.s VsyncScr). */
#include "m2_swio.h"
/* M2_API so these are GLOBAL in the kernel (the boot .s references them by linker
 * symbol) but `static` in any app TU that includes m2.h directly (e.g. platformer):
 * that keeps a GS_STANDALONE build — app + kernel linked into one image — from
 * hitting a multiple-definition collision on them. */
M2_API void handleSerialIRQ(void) { }
M2_API void kickGEO(void) { }
M2_API void waitVBL(void) { }

/* Mask / unmask maskable interrupts via the i960 process priority (modpc: 0x1F=31
 * masks vblank etc.; 0 accepts them). Wrap a brief soft-float critical section in
 * m2_irq_off()/m2_irq_on() so a vblank IRQ can't fire inside a deep soft-float call
 * chain (which crashes m2emu via the register-cache spill). Supervisor-mode only. */
M2_API void m2_irq_off(void) {
    __asm__ volatile ("shlo 0x10,0x1f,r4\n\tmov r4,r5\n\tmodpc r4,r4,r5" ::: "r4","r5");
}
M2_API void m2_irq_on(void) {
    __asm__ volatile ("shlo 0x10,0x1f,r4\n\tmov 0,r5\n\tmodpc r4,r4,r5" ::: "r4","r5");
}

/* ---- RNG: Sonic the Fighters' hardware-timer PRNG (m2_rand / m2_srand) ----- */
#include "m2_rand.h"

/* ---- frame pacing --------------------------------------------------------- */
/* Wait one 60 Hz vblank. IRQ-driven now (STF model): the vblank ISR (_irq_vblank,
 * vector 12) ticks frameVBL each vsync and ACKs the pending bit, so we just wait
 * for the counter to advance. (The old poll+ACK of 0xE80000 here would race the
 * ISR, which now owns the ACK.) */
M2_API void m2_vsync(void) {
    u32 start = frameVBL;
    while (frameVBL == start) { }
    /* armed by a launcher: P1 Start returns control to it (stop the app) */
    {
        volatile m2_exit_t *ex = &M2_EXIT;
        if (ex->magic == M2_EXIT_MAGIC) {
            M2_IO.bank = 0;
            if (!(M2_IO.in0 & M2_IN0_START1)) ((void (*)(void))ex->handler)();   /* never returns */
        }
    }
}

/* ---- palette / tiles / text ----------------------------------------------- */
M2_API void m2_setpal(int idx, u16 bgr555) { M2_PALETTE[idx] = bgr555; }

/* char `idx` (use 0..15 for game graphics; the font occupies 0x20..0x7F) becomes
 * a solid 8x8 tile of pixel value `pix` -> pen (palbank*16 + pix). */
M2_API void m2_solidtile(int idx, u8 pix) {
    volatile u8 *p = M2_CHARGFX + idx * 32;
    u8 v = (u8)((pix << 4) | pix);
    int i;
    for (i = 0; i < 32; i++) p[i] = v;
}

/* entry = M2_PRIO | (palbank<<7) | char */
M2_API void m2_puttile(int col, int row, u16 entry) {
    M2_TILE_FG[row * M2_W + col] = entry;
}
M2_API u16 m2_tile(int palbank, int chr) {
    return (u16)(M2_PRIO | ((palbank & 0x7f) << 7) | (chr & 0x7f));
}
M2_API void m2_cleartiles(u16 entry) {
    int i;
    for (i = 0; i < (int)(M2_W * 64u); i++) M2_TILE_FG[i] = entry;
}

/* Opaque background field of `colour` on the FG layer, below the 3D layer.
 * Pen-0 (a bare palette[0] write) is TRANSPARENT by tile convention (invisible on
 * m2emu), and the BG/layer-2 tilemap composites its palette inconsistently across
 * emulators (MAME->black, m2emu->white) — so neither is reliable. The FG layer (the
 * one the coloured text proves renders palram correctly everywhere) is used instead:
 * fill it with a SOLID tile (char 1 = pixel-1, palbank 0, pen 1 = colour) at NO
 * priority, so it sits below the 3D layer (polys draw on top) but above the backdrop.
 * Call right after m2_init; text drawn afterwards (PRIO bit) lands above it.
 *
 * palette[0] (the hw backdrop) is ALSO set to `colour`: text drawn over this backdrop
 * is transparent (its glyph gaps are pixel-value 0 = transparent and fall through to
 * palette[0]), so matching palette[0] to the field makes that transparency read cleanly
 * (gaps == field) on MAME/silicon instead of showing a black box behind every glyph. */
M2_API void m2_backdrop(u16 colour) {
    m2_setpal(0, colour);                 /* hw backdrop = field, so text gaps match (transparent text) */
    m2_setpal(1, colour);                 /* palbank 0, pixel 1 = colour */
    m2_solidtile(1, 1);                   /* char 1 = solid pixel-1 (bank 0 gfx) */
    m2_cleartiles((u16)((0 << 7) | 1));   /* FG, palbank 0, char 1, NO PRIO -> below 3D */
}

/* Print ASCII at (col,row) in colour group `palbank` (glyphs are pixel 1, so the
 * colour is pen palbank*16+1 — set it with m2_setpal(palbank*16+1, colour)). */
M2_API void m2_print(int col, int row, const char *s, int palbank) {
    int i;
    for (i = 0; s[i]; i++)
        M2_TILE_FG[row * M2_W + col + i] = m2_tile(palbank, (u8)s[i]);
}
/* Set a colour group's INK colour (font pixel value 1 -> pen palbank*16+1) and reset its
 * OUTLINE to black. The gFont glyphs are TWO-LAYER: value 1 = ink, value 2 = an
 * outline/shadow rendered through pen palbank*16+2. We set that pen to black (0) here so
 * the outline is a reliable black on every target (palram may be uninitialised on silicon,
 * where leaving it unset gave a garbage-coloured outline). To use a different outline
 * colour, call m2_textedge(palbank, colour) AFTER this. */
M2_API void m2_textpal(int palbank, u16 colour) {
    m2_setpal(palbank * 16 + 1, colour);   /* ink     (font value 1)               */
    m2_setpal(palbank * 16 + 2, 0);        /* outline (font value 2) = black default */
}

/* Override the font outline/shadow colour for a colour group (font value 2 -> pen
 * palbank*16+2). Call AFTER m2_textpal, which resets the outline to black. */
M2_API void m2_textedge(int palbank, u16 colour) { m2_setpal(palbank * 16 + 2, colour); }

/* ---- input (315-5649) -----------------------------------------------------
 * The input decoders — the M2_UP.. bitmask enum, m2_input(), m2_start(), and the
 * m2_player() struct snapshot — all live in m2_io.h now, next to the raw M2_IO
 * registers and the M2_INP_ / M2_IN0_ masks they decode (included above). */

/* Launcher control of the m2_vsync P1-Start exit hook (see M2_EXIT_CTL). */
M2_API void m2_exit_arm(void (*handler)(void)) {
    volatile m2_exit_t *ex = &M2_EXIT;
    ex->handler = (u32)handler; ex->magic = M2_EXIT_MAGIC;
}
M2_API void m2_exit_disarm(void) { M2_EXIT.magic = 0; }

/* ---- sound (sound board: i8251 USART -> SCSP MIDI) ------------------------- *
 * The i960 sends 3-byte sound commands to the sound 68000 via an i8251 USART:
 *   0x009C0000 = data  (each byte -> SCSP), 0x009C0004 = status(r)/control(w).
 * Reverse-engineered from STF init_sound / send_sound_code. m2_sound() sends a
 * 24-bit code MSB-first (e.g. 0xAE1231 -> 0xAE,0x12,0x31). Call m2_sound_init()
 * once after m2_init(); the sfight sound ROM's 68000 program interprets the codes. */
#define M2_SND_DATA (*(volatile u32 *)0x009C0000u)
#define M2_SND_CTL  (*(volatile u32 *)0x009C0004u)
M2_API void m2__snd_delay(void) { volatile int i; for (i = 0; i < 80; i++) { } }
M2_API void m2_sound_byte(u8 b) {
    (void)m2_wait_mask32(M2W_SND_TX, 0x009C0004u, 0x01u, 0x01u, 100000u);   /* i8251 TxRDY */
    M2_SND_DATA = b;
}
M2_API void m2_sound(u32 code) {    /* 24-bit command, MSB-first */
    m2_sound_byte((u8)(code >> 16));
    m2_sound_byte((u8)(code >> 8));
    m2_sound_byte((u8)code);
}
M2_API void m2_sound_init(void) {
    M2_SND_CTL = 0x00u; m2__snd_delay();    /* i8251 reset prep (x3) */
    M2_SND_CTL = 0x00u; m2__snd_delay();
    M2_SND_CTL = 0x00u; m2__snd_delay();
    M2_SND_CTL = 0x40u; m2__snd_delay();    /* internal reset           */
    M2_SND_CTL = 0x4Eu; m2__snd_delay();    /* mode: async 8-N-1, 16x   */
    M2_SND_CTL = 0x37u; m2__snd_delay();    /* cmd: TxEN|RxEN|DTR|RTS|err-reset */
    m2_sound(0x00A00001u);                  /* STF sound-program init handshake */
}

/* ---- board init ----------------------------------------------------------- */
M2_API void m2__build_colorxlat(void) {
    /* STF start_again_ip @0x2E8: build the FULL color-translation table — 32 intensity
     * rows x 128 pens, row stride 0x200 bytes (0x100 shorts), R/G/B planes at
     * 0x1810000/0x1814000/0x1818000. colorxlat[i][p] = clamp((MUL*(i*p))>>8 + ADD, 0xFF)
     * with default test values MUL=0x100 (identity), ADD=0 -> clamp(i*p, 0xFF). The GEO
     * samples this for every polygon pixel; the old sparse table (1 pen/row) left garbage
     * entries that render as screen noise on silicon (MAME zero-inits it, hid the bug). */
    volatile u16 *R = (volatile u16 *)0x01810000u;
    volatile u16 *G = (volatile u16 *)0x01814000u;
    volatile u16 *B = (volatile u16 *)0x01818000u;
    u32 inten, pen;
    for (inten = 0; inten < 32u; inten++) {
        u32 row = inten * 0x100u;                 /* 0x200 bytes = 0x100 shorts */
        for (pen = 0; pen < 128u; pen++) {
            u32 v = inten * pen;                   /* integer mul (no FP) */
            if (v > 0xFFu) v = 0xFFu;
            R[row + pen] = (u16)v; G[row + pen] = (u16)v; B[row + pen] = (u16)v;
        }
    }
}

/* STF start_again_ip @0x488: LUMA2 ramp at 0x12800000 — LUMA2[i] = (i+1)>>1 for 128
 * shorts (0,1,1,2,2,...,64). The polygon pipeline reads this luminance ramp; left
 * uninitialised it's garbage. */
M2_API void m2__build_luma2(void) {
    volatile u16 *luma2 = (volatile u16 *)0x12800000u;
    u32 i;
    for (i = 0; i < 128u; i++) luma2[i] = (u16)((i + 1u) >> 1);
}

/* Copy the 8x8 font into char RAM, one copy per palette bank (tile gfx is indexed by
 * (palbank<<7)|char, so each colour group needs its own copy at char-block palbank*128).
 * Call again after any CG/scroll load that overwrites the 0..0x3FF font tile range. */
M2_API void m2_loadfont(void) {
    int bank, i;
    const int n = (int)sizeof(gFont);   /* gFont is 127*32=4064, not 128*32 — */
    for (bank = 0; bank < 8; bank++)     /* clamp so GCC11 can't exploit the OOB */
        for (i = 0; i < n; i++)
            M2_CHARGFX[bank * 128 * 32 + i] = gFont[i];
}

/* Bring up the board: colour-translation table, video timing, layer enable,
 * geometrizer parked, font loaded, I/O + vblank enabled. After this the game
 * sets its palette / solid tiles and runs an m2_vsync loop. */
M2_API void m2_init(void) {
    int i;

    frameVBL = 0;             /* fixed cell (not .bss) — powers up as garbage */
    m2__build_colorxlat();
    m2__build_luma2();        /* STF start_again_ip: LUMA2 (0x12800000) ramp */
    m2_setpal(0, 0);          /* default backdrop = black (games may override) */

    M2_RENDERMODE = 0x4004;     /* STF start_again_ip writes 0x4004 to 0x10000000 (was 0x04) */
    M2_HSYNC = (u16)-84;        /* 0xFFAC — matches STF start_again_ip */
    M2_VSYNC = (u16)-3;         /* 0xFFFD — STF's value (was -2/0xFFFE, one line off) */
    M2_VIDEO_CTRL = 0;
    M2_L1_HPOS = 0; M2_L1_VPOS = 0;            /* FG layer A: on, no scroll  */
    M2_L2_HPOS = 0; M2_L2_VPOS = 0x8000;       /* BG layer B: off            */
    for (i = 0; i < 0x200; i++) { M2_HSCRTB_1[i] = 0; M2_HSCRTB_2[i] = 0; }

    /* park the geometrizer on an END instruction (no stray 3D) */
    M2_GEO_WADDR = 0x00000000;
    M2_GEO_END   = 0x00010000;
    M2_GEO_WADDR = 0x00010000;
    M2_GEO_END   = 0x00000000;
    M2_GEO_RADDR = 0x00900000;
    M2_ZCLIP     = 0xff;

    m2_loadfont();   /* font into char RAM, one copy per palette bank */

    /* Clear the FULL FG+BG tilemap RAM (0x01000000..0x01008000 = 0x2000 u16 each) to
     * the transparent space glyph, NOT just the 64x64 active region (good hygiene). */
    { u16 sp = m2_tile(0, 0x20); int k;
      for (k = 0; k < 0x2000; k++) { M2_TILE_FG[k] = sp; M2_TILE_BG[k] = sp; } }

    /* ⭐ Clear the SEGAS24 WINDOW MASK at 0x0100C000 (0x1000 u16). This mask selects, per
     * screen region, which tilemap layer (A/FG vs B/BG) the hardware displays (see MAME
     * segaic24.cpp draw_rect's per-8px mask; STF's set_window_bit @0x2EE70 ORs/clears bits
     * here, and STF zeroes it in init_scroll/scroll_all_init). Left UNINITIALISED it told
     * the hardware to show the garbage B layer in random regions on SILICON = the
     * screen-edge "bars" AND the manager's clipped text. MAME zero-inits it, hiding the
     * bug. This is THE real fix (the tilemap clear above alone did NOT fix it). */
    { volatile u16 *w = (volatile u16 *)0x0100C000u; int k;
      for (k = 0; k < 0x1000; k++) w[k] = 0u; }

    /* Aux-serial bring-up (i8251 at 0x01C80000), faithful to STF start_again_ip.
     * Inert in MAME's default hookup but needed on real hardware. (An STF-derived
     * 315-5649 "SEGA" bring-up sequence — writes to unmapped regs 0x24/0x34-0x3A/
     * 0x40 — used to live here too; removed as unverified, MAME ignores it and it
     * has never been tested on silicon.) */
    {
        volatile u16 *uart_ctl = (volatile u16 *)M2_UART_CTL_ADDR;  /* i8251 control reg */
        volatile int d;
        /* i8251 aux UART, matching House of the Dead _InitSerial exactly: 3 null/
         * sync writes, internal reset (0x40), mode 0x4E (async x16, 8-N-1), then the
         * COMMAND byte 0x37 (TxEN|DTR|RxEN|ErrReset|RTS) — we were MISSING 0x37,
         * which is what actually enables the transmitter/receiver. */
        *uart_ctl = 0x00; for (d = 4; d > 0; d--) { }
        *uart_ctl = 0x00; for (d = 4; d > 0; d--) { }
        *uart_ctl = 0x00; for (d = 4; d > 0; d--) { }
        *uart_ctl = 0x40; for (d = 4; d > 0; d--) { }   /* internal reset  */
        *uart_ctl = 0x4E; for (d = 4; d > 0; d--) { }   /* mode: 8-N-1 x16 */
        *uart_ctl = 0x37;                                /* command: TxEN|RxEN */
    }

    M2_WRITE_TWICE(M2_IRQ_ENA, M2_IRQ_VBL);  /* enable vblank source (g14 shim, m2_workaround.h) */
}

#endif /* M2_H */
