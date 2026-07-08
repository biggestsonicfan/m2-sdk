/*
 * m2_io.h — the Sega 315-5649 on-board I/O chip, mapped at 0x01C00000.
 *
 * One struct for the whole chip: byte registers on a 2-byte stride (each real
 * register sits at an even offset; the _pNN fields are the odd-address gaps).
 * Access via M2_IO.<reg>. Covers the digital input bank + player ports and the
 * RS-422 host link (m2_rs422.h). The chip decodes 16 registers (0x00..0x1E);
 * an STF-derived "SEGA" bring-up at 0x24/0x34-0x3A/0x40 was removed as
 * unverified (those addresses are unmapped/nopw in MAME).
 *
 * Include via m2.h (needs u8 + M2_API). NB the i8251 debug UART is a SEPARATE
 * chip at 0x01C80000 (see m2.h M2_UART_*), not part of this I/O chip.
 *
 * Register/bit layout verified against MAME (sega/315_5649.cpp device offsets
 * = byte addr / 2, and sega/model2.cpp model2b hookup + INPUT_PORTS(model2)):
 *   port A (0x00) OUT eeprom/ctrl-mode, port B (0x02) IN0 system,
 *   port C (0x04) IN1 = P1, port D (0x06) IN2 = P2. All inputs ACTIVE-LOW.
 */
#ifndef M2_IO_H
#define M2_IO_H

typedef struct {
    u8 bank, _p01;    /* 0x00  W port A: bit0=ctrl-mode (1 banks in0 to EEPROM
                              status; write 0 for normal inputs), bit5=EEPROM DI,
                              bit6=EEPROM CS, bit7=EEPROM CLK                 */
    u8 in0,  _p03;    /* 0x02  R port B: system inputs, active-low (M2_IN0_*) */
    u8 in1,  _p05;    /* 0x04  R port C: P1 stick+buttons, active-low (M2_INP_*) */
    u8 in2,  _p07;    /* 0x06  R port D: P2 stick+buttons, active-low (M2_INP_*) */
    u8 _r08[0x0A];    /* 0x08..0x11                                */
    u8 txd1, _p13;    /* 0x12  W: RS-422 command / strobe          */
    u8 txd2, _p15;    /* 0x14  W: RS-422 data byte                 */
    u8 rxd1, _p17;    /* 0x16  R: RS-422 status (bit0 = valid)     */
    u8 rxd2, _p19;    /* 0x18  R: RS-422 inbound data byte         */
    u8 flag, _p1b;    /* 0x1A  R: RS-422 TX/RX buffer flags        */
    u8 mode;          /* 0x1C  W: RS-422 LOOP / satellite / sat#   */
} m2_io_t;

#define M2_IO (*(volatile m2_io_t *)0x01C00000u)

/* sanity: catch any struct padding that would slide a register off its offset. */
_Static_assert(__builtin_offsetof(m2_io_t, txd1)   == 0x12, "m2_io_t.txd1");
_Static_assert(__builtin_offsetof(m2_io_t, mode)   == 0x1C, "m2_io_t.mode");

/* ---- input bit masks (raw registers, ACTIVE-LOW: 0 = pressed) -------------- */

/* in0 — system inputs (MAME "IN0", port B) */
#define M2_IN0_COIN1   0x01u
#define M2_IN0_COIN2   0x02u
#define M2_IN0_TEST    0x04u
#define M2_IN0_SERVICE 0x08u
#define M2_IN0_START1  0x10u
#define M2_IN0_START2  0x20u

/* in1/in2 — per-player stick+buttons (MAME "IN1"/"IN2", ports C/D).
 * sfight/schamp wiring: b1=Punch, b2=Kick, b3=Barrier, b4 unused. */
#define M2_INP_B1      0x01u
#define M2_INP_B2      0x02u
#define M2_INP_B3      0x04u
#define M2_INP_B4      0x08u
#define M2_INP_DOWN    0x10u
#define M2_INP_UP      0x20u
#define M2_INP_RIGHT   0x40u
#define M2_INP_LEFT    0x80u

/* ---- per-player decoded snapshot ------------------------------------------- */

/* One player's inputs, decoded to active-HIGH flags: each field is 1 while held,
 * 0 otherwise. start comes from in0 (START1/START2); everything else from the
 * player's in1/in2 register. */
typedef struct {
    u8 up, down, left, right;
    u8 b1, b2, b3, b4;
    u8 start;
} m2_player_t;

/* Snapshot player 0 (P1) or 1 (P2). Selects ctrl-mode 0 first so in0 reads the
 * real system inputs (not EEPROM status). */
M2_API m2_player_t m2_player(int player) {
    m2_player_t p;
    u8 v, s;
    M2_IO.bank = 0;                       /* normal-input mode */
    s = M2_IO.in0;
    v = player ? M2_IO.in2 : M2_IO.in1;
    p.up    = !(v & M2_INP_UP);
    p.down  = !(v & M2_INP_DOWN);
    p.left  = !(v & M2_INP_LEFT);
    p.right = !(v & M2_INP_RIGHT);
    p.b1    = !(v & M2_INP_B1);
    p.b2    = !(v & M2_INP_B2);
    p.b3    = !(v & M2_INP_B3);
    p.b4    = !(v & M2_INP_B4);
    p.start = !(s & (player ? M2_IN0_START2 : M2_IN0_START1));
    return p;
}

/* ---- decoded bitmask (the other input ergonomics: OR of M2_* flags) -------- */

/* Pressed-button flags. Pairs with m2_input()/m2_start(); a superset-friendly bitmask
 * for `if (in & M2_LEFT)` style. NB M2_UP..M2_B3 map to the per-player in1/in2 bits and
 * M2_START to the system in0 start bit (folded in by m2_input below). */
enum { M2_UP = 1, M2_DOWN = 2, M2_LEFT = 4, M2_RIGHT = 8,
       M2_B1 = 16, M2_B2 = 32, M2_B3 = 64, M2_START = 128 };

/* Both players' start, from in0: bit0 = START1, bit1 = START2 (active-low -> 1 = held). */
M2_API u32 m2_start(void) {
    M2_IO.bank = 0;
    return (!(M2_IO.in0 & M2_IN0_START1) ? 1u : 0u)
         | (!(M2_IO.in0 & M2_IN0_START2) ? 2u : 0u);
}

/* Pressed-button bitmask for player 0 (P1/IN1) or 1 (P2/IN2). Includes M2_START for this
 * player: START is a SYSTEM input on in0 (start1/start2), NOT in the per-player in1/in2
 * byte — folding it in here makes the M2_START enum bit honest, so `m2_input()&M2_START`
 * works as callers expect (before, that check was silently dead). m2_start() still reads
 * both players' start at once; m2_player() is the same decode as a per-field struct. */
M2_API u32 m2_input(int player) {
    u8 v;
    u32 r = 0;
    M2_IO.bank = 0;                 /* normal-input mode */
    v = player ? M2_IO.in2 : M2_IO.in1;   /* active-low */
    if (!(v & M2_INP_UP))    r |= M2_UP;
    if (!(v & M2_INP_DOWN))  r |= M2_DOWN;
    if (!(v & M2_INP_LEFT))  r |= M2_LEFT;
    if (!(v & M2_INP_RIGHT)) r |= M2_RIGHT;
    if (!(v & M2_INP_B1))    r |= M2_B1;
    if (!(v & M2_INP_B2))    r |= M2_B2;
    if (!(v & M2_INP_B3))    r |= M2_B3;
    if (m2_start() & (player ? 2u : 1u)) r |= M2_START;   /* in0 start1/start2 */
    return r;
}

#endif /* M2_IO_H */
