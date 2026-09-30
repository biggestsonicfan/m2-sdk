/*
 * sonic.c — Sonic the Hedgehog (Mega Drive) on the Model 2B: MAME's Mega Drive ported to
 * the i960.
 *
 * The Mega Drive's 68000 is emulated on the i960 (src/m2_m68k.h; its hot code statically
 * recompiled when src/sonic_recomp.h exists, tools/m68krecomp.py); the rest of the console
 * as far as the game sees it (memory map, VDP ports, DMA, interrupts, pads) is
 * src/md_hw.h. The picture goes onto the Model 2's System 24 tilemaps (src/md_s24.h):
 * plane B and plane A on the two scrolling layers, the sprites composited into plane A's
 * cells. The 320x224 screen sits in the middle of the 496x384 one.
 * The sound board's own 68000 is left as it is for Pac-Man: the SCSP relay
 * (snd/scsp_passthru.s), which Sonic's sound will play through (not yet).
 *
 * ROM: `python3 tools/mdrom.py sonic.bin` writes src/sonic_rom.h (gitignored, Sega data).
 *
 * Controls (Model 2 -> Mega Drive pad 1): P1 stick = D-pad, button 1 = A, 2 = B, 3 = C,
 * START 1 = START.
 *
 * Build:  cmake ... -DM2_GAME=sonic   ->  roms/sonic/ (the three EPROMs over sfight)
 */
#include "m2.h"

#if !__has_include("sonic_rom.h")
#error "src/sonic_rom.h missing: python3 tools/mdrom.py <Sonic the Hedgehog ROM>"
#endif
#include "sonic_rom.h"

static u32 md_insns;                    /* 68000 instructions run (the panel's statistics) */
#if __has_include("sonic_recomp.h") && !defined(SONIC_NO_RECOMP)
#define SONIC_RECOMP 1
#define MD_RECOMP "sonic_recomp.h"      /* tools/m68krecomp.py: md_rc_run ahead of the interpreter */
#endif
#define MD_INTERRUPT() m68k_interrupt()
#define MD_STEP()      (md_insns++, m68k_step())
#include "md_hw.h"

#define S24_TILE ((volatile u16 *)0x01000000u)
#define S24_CHAR ((volatile u16 *)0x01080000u)
#define S24_PAL  ((volatile u16 *)0x01800000u)
#include "md_s24.h"

#define MD_HZ    59923u                 /* Mega Drive NTSC frame rate, mHz (53.693175 MHz / 3420 / 262) */
#define M2_HZ    57524u                 /* Model 2 vblank rate, mHz (16 MHz / 656 / 424) */
#define MD_MAX_SKIP 3                   /* draw at least every 4th vblank */

/* The tile palette goes through the colour-translation table: each 5-bit channel c reads
 * colorxlat row c, pen 0x40 (MAME sega/model2.cpp palette_w). m2_init's table saturates
 * low channels (see pacman.c); make that column a ramp. MAME then applies its monitor
 * gamma, max((v - 64) * 255 / 191, 0), which would crush the Mega Drive's dark shades to
 * black: the ramp starts at 64 so the picture comes out linear. Before any palette write
 * (MAME converts a colour as it is written). */
static void sonic_tilepal_ramp(void) {
    volatile u16 *R = (volatile u16 *)0x01810000u;
    volatile u16 *G = (volatile u16 *)0x01814000u;
    volatile u16 *B = (volatile u16 *)0x01818000u;
    u32 c;
    for (c = 0; c < 32u; c++) {
        u16 v = (u16)(64u + (c * 191u + 15u) / 31u);
        R[c * 0x100u + 0x40u] = v; G[c * 0x100u + 0x40u] = v; B[c * 0x100u + 0x40u] = v;
    }
}

/* Model 2 inputs -> Mega Drive pad 1 (active-low on both) */
static void sonic_read_inputs(void) {
    u8 sys, p1, pad = 0xff;
    M2_IO.bank = 0;
    sys = M2_IO.in0; p1 = M2_IO.in1;
    if (!(p1 & M2_INP_UP))    pad &= (u8)~0x01;
    if (!(p1 & M2_INP_DOWN))  pad &= (u8)~0x02;
    if (!(p1 & M2_INP_LEFT))  pad &= (u8)~0x04;
    if (!(p1 & M2_INP_RIGHT)) pad &= (u8)~0x08;
    if (!(p1 & M2_INP_B2))    pad &= (u8)~0x10;    /* B */
    if (!(p1 & M2_INP_B3))    pad &= (u8)~0x20;    /* C */
    if (!(p1 & M2_INP_B1))    pad &= (u8)~0x40;    /* A */
    if (!(sys & M2_IN0_START1)) pad &= (u8)~0x80;  /* START */
    md_pad[0] = pad;
}

/* Model 2 timer 3 (MAME sega/model2.cpp timers_r: counts down at 25 MHz, the i960's clock):
 * the panel's split of the i960's time between the 68000 and the video */
#define M2_TIMER3 (*(volatile u32 *)0x00f0000cu)
static u32 t_cpu, t_vid;

static void num(char *s, u32 v, int w) {
    int i;
    for (i = w - 1; i >= 0; i--) { s[i] = (char)(v ? '0' + v % 10 : (i == w - 1 ? '0' : ' ')); v /= 10; }
    s[w] = 0;
}

int main(void) {
    u32 last, t0, frames = 0, tick = 0, ins0 = 0;
    int late = 0, skipped = 0;
    char buf[12];

    m2_init();
    sonic_tilepal_ramp();
    s24_init();
    md_reset();

    s24_text(11, 3, "SONIC THE HEDGEHOG");
#ifdef SONIC_RECOMP
    s24_text(11, 5, "68000 ON I960, RECOMPILED");
#else
    s24_text(11, 5, "68000 ON I960");
#endif
    s24_text(11, 40, "SPEED");
    s24_text(28, 40, "68K/FRAME");
    s24_text(11, 42, "68K K");
    s24_text(28, 42, "VIDEO K");
    M2_TIMER3 = 0xffffffffu;

    last = t0 = frameVBL;
    for (;;) {
        /* the Mega Drive runs at 59.92 Hz, the Model 2 refreshes at 57.52 Hz: about every
         * 24th vblank runs two Mega Drive frames. Frameskip as pacman.c: after a vblank
         * that overran, the next one skips drawing (at most MD_MAX_SKIP in a row). */
        int render = !(late && skipped < MD_MAX_SKIP), n, k;
        tick += MD_HZ;
        for (n = 0; tick >= M2_HZ; n++) tick -= M2_HZ;
        for (k = 0; k < n; k++) {
            u32 t = M2_TIMER3;
            sonic_read_inputs();
            md_frame();
            t_cpu += t - M2_TIMER3;
            frames++;
        }
        if (render && n) { u32 t = M2_TIMER3; s24_update(); t_vid += t - M2_TIMER3; }
        skipped = render ? 0 : skipped + 1;
        late = frameVBL != last;
#ifndef SONIC_BENCH                     /* -DSONIC_BENCH: flat out, SPEED shows the headroom */
        while (frameVBL == last) { }
#endif
        last = frameVBL;
        if (last - t0 >= 60) {          /* speed = emulated frames against real Mega Drive time */
            num(buf, frames * (100000u * (M2_HZ / 8u) / (MD_HZ / 8u)) / ((last - t0) * 1000u), 3);
            buf[3] = '%'; buf[4] = 0;
            s24_text(17, 40, buf);
            num(buf, (md_insns - ins0) / (frames ? frames : 1), 6);
            s24_text(38, 40, buf);
            /* i960 cycles per Mega Drive frame, in thousands (417 = all of it at 59.92 Hz) */
            num(buf, t_cpu / 1000u / (frames ? frames : 1), 4); s24_text(17, 42, buf);
            num(buf, t_vid / 1000u / (frames ? frames : 1), 4); s24_text(38, 42, buf);
            M2_TIMER3 = 0xffffffffu;
            frames = 0; t0 = last; ins0 = md_insns; t_cpu = t_vid = 0;
        }
    }
}
