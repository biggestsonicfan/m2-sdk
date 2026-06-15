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
 *   - the vblank pending bit (0xE80000 bit0, polled by m2_vsync) is only latched
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

/* ---- hardware addresses --------------------------------------------------- */
#define M2_TILE_FG  ((volatile u16 *)0x01000000u)  /* FG layer-0 tilemap (64 wide) */
#define M2_TILE_BG  ((volatile u16 *)0x01004000u)  /* BG layer-2 tilemap           */
#define M2_CHARGFX  ((volatile u8  *)0x01080000u)  /* 8x8 4bpp char gfx, idx*32     */
#define M2_PALETTE  ((volatile u16 *)0x01800000u)  /* BGR555, idx = palbank*16+pix  */

#define M2_IO_ENABLE (*(volatile u16 *)0x01C00040u)
#define M2_IO_BANK   (*(volatile u8  *)0x01C00000u)
#define M2_IN0       (*(volatile u8  *)0x01C00002u)
#define M2_IN1       (*(volatile u8  *)0x01C00004u)  /* P1 (active-low)             */
#define M2_IN2       (*(volatile u8  *)0x01C00006u)  /* P2 (active-low)             */

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
#define M2_UART_STAT (*(volatile u16 *)0x01C80002u)   /* read: 8251 status; bit0 = TxRDY */
static void m2_uart_putc(char c) {
    u32 g = 0;
    while (!(M2_UART_STAT & 0x01u) && ++g < 200000u) { }
    M2_UART_DATA = (u16)(u8)c;
}
static void m2_uart_puts(const char *s) { while (*s) m2_uart_putc(*s++); }
static void m2_uart_hex(u32 v) {           /* "XXXXXXXX" big-endian nibbles */
    int i; for (i = 28; i >= 0; i -= 4) m2_uart_putc("0123456789ABCDEF"[(v >> i) & 0xF]);
}

#define M2_W     64u           /* hardware tilemap stride (tiles)  */
#define M2_PRIO  0x8000u       /* tile entry bit15: above-3D category */
#define M2_RGB(r,g,b) ((u16)(((b) << 10) | ((g) << 5) | (r)))

/* Daytona's 128-glyph 8x8 4bpp font, already in hardware byte order. */
#include "m2font.h"            /* const u8 gFont[128*32] */

/* ---- linker stubs (referenced by the boot .s; defined once here) ---------- */
volatile u32 frameVBL = 0;     /* incremented by the (unused) vblank ISR */
void handleSerialIRQ(void) { }
void kickGEO(void) { }
void waitVBL(void) { }

/* ---- RNG: Sonic the Fighters' hardware-timer PRNG (m2_rand / m2_srand) ----- */
#include "m2_rand.h"

/* ---- frame pacing --------------------------------------------------------- */
/* Wait one 60 Hz vblank. Re-asserts the vblank enable each call (written twice
 * so gcc960 -O2 doesn't store g14=0), then polls + ACKs the pending bit. */
static void m2_vsync(void) {
    M2_IRQ_ENA = M2_IRQ_VBL;
    M2_IRQ_ENA = M2_IRQ_VBL;
    while (!(M2_IRQ_REQ & M2_IRQ_VBL)) { }
    M2_IRQ_REQ = ~M2_IRQ_VBL;
    /* armed by a launcher: P1 Start returns control to it (stop the app) */
    {
        volatile u32 *ex = (volatile u32 *)M2_EXIT_CTL;
        if (ex[0] == M2_EXIT_MAGIC) {
            M2_IO_BANK = 0;
            if (!(M2_IN0 & 0x10u)) ((void (*)(void))ex[1])();   /* never returns */
        }
    }
}

/* ---- palette / tiles / text ----------------------------------------------- */
static void m2_setpal(int idx, u16 bgr555) { M2_PALETTE[idx] = bgr555; }

/* char `idx` (use 0..15 for game graphics; the font occupies 0x20..0x7F) becomes
 * a solid 8x8 tile of pixel value `pix` -> pen (palbank*16 + pix). */
static void m2_solidtile(int idx, u8 pix) {
    volatile u8 *p = M2_CHARGFX + idx * 32;
    u8 v = (u8)((pix << 4) | pix);
    int i;
    for (i = 0; i < 32; i++) p[i] = v;
}

/* entry = M2_PRIO | (palbank<<7) | char */
static void m2_puttile(int col, int row, u16 entry) {
    M2_TILE_FG[row * M2_W + col] = entry;
}
static u16 m2_tile(int palbank, int chr) {
    return (u16)(M2_PRIO | ((palbank & 0x7f) << 7) | (chr & 0x7f));
}
static void m2_cleartiles(u16 entry) {
    int i;
    for (i = 0; i < (int)(M2_W * 64u); i++) M2_TILE_FG[i] = entry;
}

/* Print ASCII at (col,row) in colour group `palbank` (glyphs are pixel 1, so the
 * colour is pen palbank*16+1 — set it with m2_setpal(palbank*16+1, colour)). */
static void m2_print(int col, int row, const char *s, int palbank) {
    int i;
    for (i = 0; s[i]; i++)
        M2_TILE_FG[row * M2_W + col + i] = m2_tile(palbank, (u8)s[i]);
}
static void m2_textpal(int palbank, u16 colour) { m2_setpal(palbank * 16 + 1, colour); }

/* ---- input (315-5649) ----------------------------------------------------- */
enum { M2_UP = 1, M2_DOWN = 2, M2_LEFT = 4, M2_RIGHT = 8,
       M2_B1 = 16, M2_B2 = 32, M2_B3 = 64, M2_START = 128 };

/* Returns the pressed-button bitmask for player 0 (P1/IN1) or 1 (P2/IN2). */
static u32 m2_input(int player) {
    u8 v;
    u32 r = 0;
    M2_IO_BANK = 0;                 /* select digital bank 0 */
    v = player ? M2_IN2 : M2_IN1;   /* active-low */
    if (!(v & 0x20)) r |= M2_UP;
    if (!(v & 0x10)) r |= M2_DOWN;
    if (!(v & 0x80)) r |= M2_LEFT;
    if (!(v & 0x40)) r |= M2_RIGHT;
    if (!(v & 0x01)) r |= M2_B1;
    if (!(v & 0x02)) r |= M2_B2;
    if (!(v & 0x04)) r |= M2_B3;
    return r;
}
static u32 m2_start(void) {         /* IN0: bit4 = START1, bit5 = START2 */
    M2_IO_BANK = 0;
    return (!(M2_IN0 & 0x10) ? 1u : 0u) | (!(M2_IN0 & 0x20) ? 2u : 0u);
}

/* Launcher control of the m2_vsync P1-Start exit hook (see M2_EXIT_CTL). */
static void m2_exit_arm(void (*handler)(void)) {
    volatile u32 *ex = (volatile u32 *)M2_EXIT_CTL;
    ex[1] = (u32)handler; ex[0] = M2_EXIT_MAGIC;
}
static void m2_exit_disarm(void) { *(volatile u32 *)M2_EXIT_CTL = 0; }

/* ---- sound (sound board: i8251 USART -> SCSP MIDI) ------------------------- *
 * The i960 sends 3-byte sound commands to the sound 68000 via an i8251 USART:
 *   0x009C0000 = data  (each byte -> SCSP), 0x009C0004 = status(r)/control(w).
 * Reverse-engineered from STF init_sound / send_sound_code. m2_sound() sends a
 * 24-bit code MSB-first (e.g. 0xAE1231 -> 0xAE,0x12,0x31). Call m2_sound_init()
 * once after m2_init(); the sfight sound ROM's 68000 program interprets the codes. */
#define M2_SND_DATA (*(volatile u32 *)0x009C0000u)
#define M2_SND_CTL  (*(volatile u32 *)0x009C0004u)
static void m2__snd_delay(void) { volatile int i; for (i = 0; i < 80; i++) { } }
static void m2_sound_byte(u8 b) {
    u32 g = 0;
    while (!(M2_SND_CTL & 0x01u) && ++g < 100000u) { }   /* wait i8251 TxRDY (bounded) */
    M2_SND_DATA = b;
}
static void m2_sound(u32 code) {    /* 24-bit command, MSB-first */
    m2_sound_byte((u8)(code >> 16));
    m2_sound_byte((u8)(code >> 8));
    m2_sound_byte((u8)code);
}
static void m2_sound_init(void) {
    M2_SND_CTL = 0x00u; m2__snd_delay();    /* i8251 reset prep (x3) */
    M2_SND_CTL = 0x00u; m2__snd_delay();
    M2_SND_CTL = 0x00u; m2__snd_delay();
    M2_SND_CTL = 0x40u; m2__snd_delay();    /* internal reset           */
    M2_SND_CTL = 0x4Eu; m2__snd_delay();    /* mode: async 8-N-1, 16x   */
    M2_SND_CTL = 0x37u; m2__snd_delay();    /* cmd: TxEN|RxEN|DTR|RTS|err-reset */
    m2_sound(0x00A00001u);                  /* STF sound-program init handshake */
}

/* ---- board init ----------------------------------------------------------- */
static void m2__build_colorxlat(void) {
    int c;
    for (c = 0; c < 32; c++) {
        u16 v = (u16)((c << 3) | (c >> 2));   /* identity pal5bit expansion */
        *(volatile u16 *)(0x01810080u + (u32)c * 0x200u) = v;  /* R */
        *(volatile u16 *)(0x01814080u + (u32)c * 0x200u) = v;  /* G */
        *(volatile u16 *)(0x01818080u + (u32)c * 0x200u) = v;  /* B */
    }
}

/* Copy the 8x8 font into char RAM, one copy per palette bank (tile gfx is indexed by
 * (palbank<<7)|char, so each colour group needs its own copy at char-block palbank*128).
 * Call again after any CG/scroll load that overwrites the 0..0x3FF font tile range. */
static void m2_loadfont(void) {
    int bank, i;
    const int n = (int)sizeof(gFont);   /* gFont is 127*32=4064, not 128*32 — */
    for (bank = 0; bank < 8; bank++)     /* clamp so GCC11 can't exploit the OOB */
        for (i = 0; i < n; i++)
            M2_CHARGFX[bank * 128 * 32 + i] = gFont[i];
}

/* Bring up the board: colour-translation table, video timing, layer enable,
 * geometrizer parked, font loaded, I/O + vblank enabled. After this the game
 * sets its palette / solid tiles and runs an m2_vsync loop. */
static void m2_init(void) {
    int i;

    m2__build_colorxlat();
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

    /* clear both tile layers to the transparent space glyph (backdrop shows) */
    m2_cleartiles(m2_tile(0, 0x20));
    for (i = 0; i < (int)(M2_W * 64u); i++) M2_TILE_BG[i] = m2_tile(0, 0x20);

    /* I/O + serial bring-up, faithful to STF start_again_ip (_disable_ints).
     * REAL-HARDWARE init: in MAME the 8251 is a separate chip and the 315-5649's
     * extended regs (0x24 / 0x34-0x3A) are unmapped (and 0x40 is nopw), so all of
     * this is inert there — but silicon needs it. */
    {
        volatile u16 *uart_ctl = (volatile u16 *)0x01C80002u;  /* i8251 control reg */
        volatile u8  *io       = (volatile u8  *)0x01C00000u;  /* 315-5649 I/O chip */
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
        /* 315-5649 handshake: 0x40<-0, 0x24<-1, then the "SEGA" signature at
         * 0x34/0x36/0x38/0x3A (replaces the old M2_IO_ENABLE=1, which wrote 1 to
         * 0x40 — the wrong reg per STF). */
        io[0x40] = 0x00;
        io[0x24] = 0x01;
        io[0x34] = 'S'; io[0x36] = 'E'; io[0x38] = 'G'; io[0x3A] = 'A';
    }

    M2_IRQ_ENA = M2_IRQ_VBL;       /* enable vblank source (written twice — */
    M2_IRQ_ENA = M2_IRQ_VBL;       /* gcc960 -O2 g14 workaround)            */
}

#endif /* M2_H */
