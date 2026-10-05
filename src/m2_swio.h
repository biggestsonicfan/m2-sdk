/*
 * m2_swio.h — Sonic the Fighters' switch/analog I/O frame subsystem, ported
 * verbatim from rom_code1.s: read_sw, write_sw, io_request_chk, read_analog_to_ram.
 *
 * Unlike the stateless m2_input()/m2_player() (which live-read the port each call),
 * this maintains STF's edge-detected input model — HELD / MOMENTARY (newly pressed)
 * / MOMEN_ON_REL (released) — refreshed once per vblank. The vblank ISR (i_handle.s
 * VsyncScr) calls read_sw then write_sw every frame, exactly as STF's VsyncScr does
 * at +48/+4C. Storage is at STF's fixed work-RAM addresses (M2_MEM.in_held @0x500700
 * … analog_ram @0x50071C — see m2_memory.h).
 *
 * Include via m2.h AFTER m2_memory.h and m2_io.h (uses M2_MEM + the M2_INP_* masks).
 * read_sw/write_sw are called from asm, so the kernel build (-DM2_API=) must export
 * them as globals; deunderscore.py lists them so `call _read_sw` resolves in the ELF
 * build. seclet_command_check (STF's debug-code cheat sequence) is omitted — our
 * kernel has no debug_flag consumer.
 */
#ifndef M2_SWIO_H
#define M2_SWIO_H

/* 315-5649 registers read_sw/write_sw/io_request_chk touch that the m2_io_t struct
 * doesn't name (they sit in its _r08 gap or beyond .mode). Verbatim STF addresses. */
#define M2_IO_COINOUT (*(volatile u8  *)0x01C0000Au) /* write_sw: coin-counter / lamp out      */
#define M2_IO_INMISC  (*(volatile u8  *)0x01C0000Cu) /* read_sw: extra digital in (inverted)   */
#define M2_IO_PORTCFG (*(volatile u8  *)0x01C00010u) /* io_request_chk: port config = 0x4F     */
#define M2_IO_LATCH   (*(volatile u16 *)0x01C00040u) /* read_sw: input-latch strobe = 1        */
#define M2_IO_ADC     (*(volatile u8  *)0x01C0001Eu) /* read_analog_to_ram: 8-ch ADC mux       */

/* STF read_analog_to_ram @0x1954: reset the ADC channel mux (write 0), then read 8
 * channels — each read auto-advances the channel — into M2_MEM.analog_ram. sfight has
 * no analog sticks, but STF reads them unconditionally; kept faithful. */
M2_API void read_analog_to_ram(void) {
    int i;
    M2_IO_ADC = 0;                          /* stib 0 -> 0x1C0001E: reset mux */
    for (i = 0; i < 8; i++)
        M2_MEM.analog_ram[i] = M2_IO_ADC;   /* each read advances the channel */
}

/* STF read_sw @0x18A0: snapshot the digital inputs into a 32-bit ACTIVE-LOW word
 * (byte0 = system/IN0, byte1 = P1/IN1, byte2 = P2/IN2, byte3 = coin/service read
 * from bank 1), apply STF's start-bit canonicalisation, then derive HELD (active-
 * high) plus the MOMENTARY (newly-pressed) and MOMEN_ON_REL (released) edge sets
 * against last frame. seclet_command_check is intentionally omitted. */
M2_API void read_sw(void) {
    volatile u8 *io = (volatile u8 *)0x01C00000u;   /* IO_PORTS */
    u32 held_prev, word, top, held, misc;
    u8  b;

    M2_IO_LATCH = 1u;                       /* stos 1,0x40: latch the inputs */

    held_prev = M2_MEM.in_held;
    M2_MEM.in_held_prev = held_prev;

    /* ~IN[0x0C] masked by 8 — STF's non-debug default; the dip-switch 0xFF mask path
     * is gated on debug_flag bit14, which we don't model. Kept as a 2-frame history. */
    misc = (~(u32)M2_IO_INMISC) & 8u;
    M2_MEM.io_prev1 = M2_MEM.io_prev0;
    M2_MEM.io_prev0 = misc;

    io[0x00] = 0;                           /* bank 0: real inputs           */
    word  = (u32)io[0x02];                  /* IN0 system  -> byte0          */
    word |= (u32)io[0x04] << 8;             /* IN1 P1      -> byte1          */
    word |= (u32)io[0x06] << 16;            /* IN2 P2      -> byte2          */

    io[0x00] = 1;                           /* bank 1: coin / service on IN0 */
    b   = io[0x02];
    top = 0xFFu;
    if (!(b & 0x80u)) top &= ~(1u << 2);
    if (!(b & 0x40u)) top &= ~(1u << 3);
    word |= top << 24;                      /* -> byte3                      */

    /* STF start-bit canonicalisation: a press on bit11 (P1) / bit19 (P2) moves to bit10 / bit18. */
    if (!(word & (1u << 11))) { word &= ~(1u << 10); word |= (1u << 11); }
    if (!(word & (1u << 19))) { word &= ~(1u << 18); word |= (1u << 19); }

    held = ~word;                                   /* active-high held           */
    M2_MEM.in_held         = held;
    M2_MEM.in_momentary    = held & ~held_prev;     /* newly pressed this frame   */
    M2_MEM.in_momen_on_rel = held_prev & word;      /* released this frame        */

    read_analog_to_ram();
}

/* STF write_sw @0x19B2: push the coin-counter / lamp state to the I/O chip. */
M2_API void write_sw(void) {
    M2_IO_COINOUT = M2_MEM.coin_flags;
}

/* STF io_request_chk @0x1877 (called once at boot, ROM:00006AC4): configure the
 * 315-5649 port and clear the interrupt-flag state. Replaces the bare 0x4F poke. */
M2_API void io_request_chk(void) {
    M2_IO_PORTCFG = 0x4Fu;                  /* port config (was: *0x01C00010 = 0x4F) */
    M2_MEM.in_momentary    = 0;
    M2_MEM.in_momen_on_rel = 0;
    M2_MEM.in_held         = 0;
    M2_MEM.in_held_prev    = 0;
    M2_MEM.coin_flags      = 0;             /* 0x00 = INSERT COINS */
}

/* ---- app-facing accessors (STF edge model) --------------------------------
 * One player's held / newly-pressed / just-released byte, active-HIGH, indexed with
 * the M2_INP_* masks (m2_io.h): B1/B2/B3/B4, DOWN/UP/RIGHT/LEFT. player: 0=P1, 1=P2.
 * (System bits — coin/test/service/start — are byte0/byte3 of M2_MEM.in_*.) */
M2_API u32 m2_held(int player)     { return (M2_MEM.in_held         >> (player ? 16 : 8)) & 0xFFu; }
M2_API u32 m2_pressed(int player)  { return (M2_MEM.in_momentary    >> (player ? 16 : 8)) & 0xFFu; }
M2_API u32 m2_released(int player) { return (M2_MEM.in_momen_on_rel >> (player ? 16 : 8)) & 0xFFu; }

/* System inputs (byte0 = IN0), same edge model, indexed with the M2_IN0_* masks
 * (COIN1/COIN2/TEST/SERVICE/START1/START2). */
M2_API u32 m2_sys_held(void)     { return M2_MEM.in_held         & 0xFFu; }
M2_API u32 m2_sys_pressed(void)  { return M2_MEM.in_momentary    & 0xFFu; }
M2_API u32 m2_sys_released(void) { return M2_MEM.in_momen_on_rel & 0xFFu; }

#endif /* M2_SWIO_H */
