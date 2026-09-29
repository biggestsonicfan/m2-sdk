/*
 * z80test.c — host check of src/m2_z80.h against the zexdoc/zexall CP/M exercisers.
 *
 *   cc -O2 -I../../src -o z80test z80test.c && ./z80test zexdoc.com
 *
 * The .com files are not bundled; zexdoc/zexall (Frank Cringle, GPL) are in many Z80
 * emulator repos, e.g. github.com/anotherlin/z80emu/tree/master/testfiles.
 * Only BDOS 2 (print char) and 9 (print $-string) are emulated, which is all they use.
 */
#include <stdio.h>
#include <stdlib.h>

static unsigned char mem[65536];
#define Z80_RD(a)     mem[(unsigned short)(a)]
#define Z80_WR(a, v)  (mem[(unsigned short)(a)] = (v))
#define Z80_IN(p)     0xff
#define Z80_OUT(p, v) ((void)(p), (void)(v))
#include "m2_z80.h"

int main(int argc, char **argv) {
    FILE *f;
    long long total = 0;
    if (argc < 2 || !(f = fopen(argv[1], "rb"))) { fprintf(stderr, "usage: z80test file.com\n"); return 2; }
    fread(mem + 0x100, 1, 0xff00, f);
    fclose(f);
    z80_reset();
    z80.pc = 0x100; z80.sp = 0xf000;
    mem[5] = 0xc9;                                   /* BDOS entry: RET after the trap */
    for (;;) {
        if (z80.pc == 0) break;                      /* warm boot = done */
        if (z80.pc == 5) {
            if (zC == 2) putchar(zE);
            else if (zC == 9) { unsigned a = zDE; while (mem[a] != '$') putchar(mem[a++ & 0xffff]); }
            fflush(stdout);
        }
        z80.cycles = 0;                              /* one instruction per step */
        z80_run(1);
        total++;
    }
    printf("\n[%lld instructions]\n", total);
    return 0;
}
