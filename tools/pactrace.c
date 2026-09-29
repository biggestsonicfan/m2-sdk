/*
 * pactrace.c — record every Z80 instruction address Pac-Man executes, for z80recomp.py.
 *
 *   cc -O2 -Isrc -o pactrace tools/pactrace.c && ./pactrace minutes > trace.txt
 *
 * Runs the board on the host through boot, attract and several credits of play with a
 * pseudo-random stick (coins and starts on a schedule), single-stepping the interpreter,
 * and prints the sorted set of opcode addresses that ran from ROM. Builds against
 * src/pacman_roms.h (or -DPAC_ROMS='"puckman_roms.h"').
 */
#include <stdio.h>
#include <stdlib.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
#ifndef PAC_ROMS
#define PAC_ROMS "pacman_roms.h"
#endif
#include PAC_ROMS
#include "pacman_hw.h"

static u8 seen[0x4000];

int main(int argc, char **argv) {
    int minutes = argc > 1 ? atoi(argv[1]) : 20, f, frames, i, n = 0;
    u32 rng = 12345, dir = 0;
    frames = minutes * 60 * 61;
    pac_reset();
    for (f = 0; f < frames; f++) {
        int left = PAC_CYCLES_PER_FRAME;
        pac_in0 = 0xff; pac_in1 = 0xff;
        if (f % 3000 >= 700 && f % 3000 < 706) pac_in0 &= ~0x20;      /* coin */
        if (f % 3000 >= 760 && f % 3000 < 766) pac_in1 &= ~0x20;      /* start */
        if (f % 20 == 0) { rng = rng * 1103515245u + 12345u; dir = (rng >> 16) & 3; }
        pac_in0 &= (u8)~(1u << dir);                                   /* up/left/right/down */
        while (left > 0) {
            if (z80.pc < 0x4000 && !z80.halted) seen[z80.pc] = 1;
            z80.cycles = 0;
            z80_run(1);
            left -= 1 - z80.cycles;
            z80.cycles = 0;
        }
        if (pac_irq_mask) { z80.irq_line = 1; z80.irq_vec = pac_vector; }
    }
    for (i = 0; i < 0x4000; i++) if (seen[i]) { printf("%04x\n", i); n++; }
    fprintf(stderr, "%d instruction addresses over %d frames\n", n, frames);
    return 0;
}
