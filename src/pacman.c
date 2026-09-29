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
 * src/puckman.c is the same program on the Namco set (src/puckman_roms.h).
 *
 * Controls (Model 2 -> Pac-Man): P1 stick, P2 stick, COIN1/COIN2 = coins, START1/2,
 * SERVICE = credit. No sound yet (Namco WSG), no cocktail flip.
 *
 * Build:  cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-i960-elf.cmake -DM2_GAME=pacman
 */
#include "m2.h"
#include "m2_tilefb.h"

/* Boot the SHARC like any sfight-based game: the COP (cpres1) and GEO (cpres2) firmware,
 * i.e. Sonic the Fighters' SHARC programs (src/cpres*.h: tools/sharc2h.py from the
 * stf-sharc reassembly, or a ROM extract — docs/firmware-extraction.md). Pac-Man draws
 * nothing through it; each frame just commits an empty GEO display list, so the real
 * geometrizer (MAME M2_HLE_GEO_OFF, silicon) runs its frame loop alongside the game.
 * -DPAC_NO_SHARC, or no firmware headers, builds without it. */
#if !defined(PAC_NO_SHARC) && __has_include("cpres1.h") && __has_include("cpres2.h")
#define PAC_SHARC 1
#include "m2_obj.h"                    /* m2_silicon_boot, m2_frame_begin/commit/end */
#endif

#ifndef PAC_ROMS                       /* src/puckman.c picks the other set */
#define PAC_ROMS  "pacman_roms.h"
#define PAC_NAME  "PAC-MAN"
#endif
#if __has_include(PAC_ROMS)
#include PAC_ROMS
#define PAC_TITLE PAC_NAME
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

/* A tile-palette write goes through the colour-translation table: each 5-bit channel c
 * reads colorxlat row c, pen 0x40 (MAME sega/model2.cpp palette_w). m2_init's STF table
 * holds clamp(row * pen) there, which saturates any channel >= 4/31 to full, fine for
 * pure colours but it turns Pac-Man's blue maze white. Pac-Man draws no polygons, so
 * make that one column a straight 0..255 ramp. Must run BEFORE the palette writes
 * (MAME converts a colour when it is written). */
static void pac_linear_tilepal(void) {
    volatile u16 *R = (volatile u16 *)0x01810000u;
    volatile u16 *G = (volatile u16 *)0x01814000u;
    volatile u16 *B = (volatile u16 *)0x01818000u;
    u32 c;
    for (c = 0; c < 32u; c++) {
        u16 v = (u16)((c * 255u + 15u) / 31u);
        R[c * 0x100u + 0x40u] = v; G[c * 0x100u + 0x40u] = v; B[c * 0x100u + 0x40u] = v;
    }
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
#ifdef PAC_SHARC
    m2_silicon_boot();                 /* SHARC firmware upload + colour pipeline + GEO seed */
    for (i = 0; i < 8; i++) { m2_frame_begin(); m2_frame_end(); }   /* prime the GEO */
#endif
    pac_linear_tilepal();              /* after m2_silicon_boot: m2_color_init rebuilds colorxlat */
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
#ifdef PAC_SHARC
    tfb_text(8, 96, "STF SHARC", (u8)ink, -1);
#endif
    tfb_text(8, 112, "SPEED", (u8)ink, -1);

    last = t0 = frameVBL;
    for (;;) {
        /* frameskip: after a frame that overran its vblank, skip drawing one (never two
         * in a row), so a busy Z80 keeps the game at full speed */
        int render = !(late && !skipped);
        pac_read_inputs();
#ifdef PAC_SHARC
        m2_frame_begin();              /* an empty GEO frame: keeps the SHARC's frame loop going */
        m2_frame_commit();
#endif
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
