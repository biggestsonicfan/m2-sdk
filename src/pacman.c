/*
 * pacman.c — Namco Pac-Man on the Model 2B: the MAME pacman driver ported to the i960.
 *
 * The board's Z80 is interpreted (src/m2_z80.h); the rest of the board — memory map,
 * IRQ, inputs, tile + sprite video — is src/pacman_hw.h. Pac-Man's screen (224x288,
 * portrait) sits cell-aligned in the middle of the 496x384 System 24 tile plane
 * (m2_tilefb.h: one char block per cell), and each frame only the cells that changed
 * are copied to char RAM. Pac-Man's 16 colours go straight into the tile palette.
 * The panel on the left shows how fast the emulation runs (100% = 60 frames a second);
 * when the Z80 is flat out, every other frame may skip drawing to keep the game speed.
 *
 * ROMs: `python3 tools/pacrom.py pacman.zip` writes src/pacman_roms.h (gitignored, Namco
 * data). Without it this builds the homebrew board test from src/pactest_rom.h.
 *
 * Controls (Model 2 -> Pac-Man): P1 stick, P2 stick, COIN1/COIN2 = coins, START1/2,
 * SERVICE = credit. No sound yet (Namco WSG), no cocktail flip.
 *
 * Build:  cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-i960-elf.cmake -DM2_GAME=pacman
 */
#include "m2.h"
#include "m2_tilefb.h"

#if __has_include("pacman_roms.h")
#include "pacman_roms.h"
#define PAC_TITLE "PAC-MAN"
#else
#include "pactest_rom.h"
#define PAC_TITLE "TEST ROM"
#endif
#include "pacman_hw.h"

#define PAC_COL0 17                    /* Pac-Man screen origin on the tile plane, in cells */
#define PAC_ROW0 6

/* Model 2 inputs -> Pac-Man IN0/IN1 (all active-low on both boards) */
static void pac_read_inputs(void) {
    u8 sys, p1, p2, in0 = 0xff, in1 = 0xff;
    M2_IO.bank = 0;
    sys = M2_IO.in0; p1 = M2_IO.in1; p2 = M2_IO.in2;
    if (!(p1 & M2_INP_UP))    in0 &= ~0x01;
    if (!(p1 & M2_INP_LEFT))  in0 &= ~0x02;
    if (!(p1 & M2_INP_RIGHT)) in0 &= ~0x04;
    if (!(p1 & M2_INP_DOWN))  in0 &= ~0x08;
    if (!(sys & M2_IN0_COIN1))   in0 &= ~0x20;
    if (!(sys & M2_IN0_COIN2))   in0 &= ~0x40;
    if (!(sys & M2_IN0_SERVICE)) in0 &= ~0x80;
    if (!(p2 & M2_INP_UP))    in1 &= ~0x01;
    if (!(p2 & M2_INP_LEFT))  in1 &= ~0x02;
    if (!(p2 & M2_INP_RIGHT)) in1 &= ~0x04;
    if (!(p2 & M2_INP_DOWN))  in1 &= ~0x08;
    if (!(sys & M2_IN0_START1)) in1 &= ~0x20;
    if (!(sys & M2_IN0_START2)) in1 &= ~0x40;
    pac_in0 = in0;
    pac_in1 = in1;                     /* bit 4 test off, bit 7 = upright cabinet */
}

/* copy the changed cells of pac_fb into their char blocks */
static void pac_blit(void) {
    int i, y;
    for (i = 0; i < pac_cn; i++) {
        int c = pac_clist[i], tx = c % PAC_TW, ty = c / PAC_TW;
        volatile u32 *d = (volatile u32 *)(M2_CHARGFX + ((PAC_ROW0 + ty) * TFB_CELLS_W + PAC_COL0 + tx) * 32);
        for (y = 0; y < 8; y++) d[y] = pac_fb[c][y];
    }
    pac_copy_done();
}

static void pac_num(char *s, u32 v, int w) {
    int i;
    for (i = w - 1; i >= 0; i--) { s[i] = (char)(v ? '0' + v % 10 : (i == w - 1 ? '0' : ' ')); v /= 10; }
    s[w] = 0;
}

int main(void) {
    int i, ink = 1, best = -1, late = 0, skipped = 0;
    u32 last, t0, frames = 0;
    char buf[8];

    m2_init();
    tfb_init();
    pac_reset();
    for (i = 0; i < 16; i++) {         /* Pac-Man palette -> tile palette (every FB bank) */
        u8 r, g, b;
        pac_palette_rgb(i, &r, &g, &b);
        tfb_setcolor((u8)i, M2_RGB(r >> 3, g >> 3, b >> 3));
        if (r + g + b > best) { best = r + g + b; ink = i; }
    }

    tfb_text(8, 64, PAC_TITLE, (u8)ink, -1);
    tfb_text(8, 80, "Z80 ON I960", (u8)ink, -1);
    tfb_text(8, 112, "SPEED", (u8)ink, -1);

    last = t0 = frameVBL;
    for (;;) {
        /* frameskip: after a frame that overran its vblank, skip drawing one (never two
         * in a row), so a busy Z80 keeps the game at full speed */
        int render = !(late && !skipped);
        pac_read_inputs();
        pac_frame(render);
        if (render) pac_blit();
        skipped = !render;
        frames++;
        late = frameVBL != last;
#ifndef PAC_BENCH                      /* -DPAC_BENCH: run flat out, SPEED shows the headroom */
        while (frameVBL == last) { }   /* at most one Pac-Man frame per vblank */
#endif
        last = frameVBL;
        if (last - t0 >= 60) {         /* speed = emulated frames per 60 vblanks */
            pac_num(buf, frames * 100 / (last - t0), 3);
            buf[3] = '%'; buf[4] = 0;
            tfb_fillrect(8, 128, 5 * 8, 8, 0);
            tfb_text(8, 128, buf, (u8)ink, -1);
            frames = 0; t0 = last;
        }
    }
}
