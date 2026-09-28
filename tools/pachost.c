/*
 * pachost.c — run src/pacman_hw.h on the host and write the screen as a PPM.
 *
 *   cc -O2 -Isrc -o pachost tools/pachost.c && ./pachost 300 out.ppm [in0hex]
 *
 * Builds against src/pacman_roms.h when it exists (tools/pacrom.py), else the bundled
 * test program src/pactest_rom.h — the same choice src/pacman.c makes.
 */
#include <stdio.h>
#include <stdlib.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
#if __has_include("pacman_roms.h")
#include "pacman_roms.h"
#else
#include "pactest_rom.h"
#endif
#include "pacman_hw.h"

int main(int argc, char **argv) {
    int frames = argc > 1 ? atoi(argv[1]) : 120, f, x, y;
    const char *out = argc > 2 ? argv[2] : "pac.ppm";
    FILE *o;
    pac_reset();
    for (f = 0; f < frames; f++) {
        pac_in0 = (argc > 3 && f > frames / 2) ? (u8)strtol(argv[3], 0, 16) : 0xff;
        pac_frame(1);
        pac_copy_done();
    }
    if (!(o = fopen(out, "wb"))) return 1;
    fprintf(o, "P6 %d %d 255\n", PAC_W, PAC_H);
    for (y = 0; y < PAC_H; y++)
        for (x = 0; x < PAC_W; x++) {
            u32 w = pac_fb[(y >> 3) * PAC_TW + (x >> 3)][y & 7];
            u8 r, g, b;
            pac_palette_rgb((w >> pac_nibshift(x & 7)) & 15, &r, &g, &b);
            fputc(r, o); fputc(g, o); fputc(b, o);
        }
    fclose(o);
    printf("%s: %d frames, pc=%04x\n", out, frames, z80.pc);
    return 0;
}
