/*
 * mdhost.c — run src/md_hw.h (the Mega Drive on the 68000 core) on the host, with the
 * reference renderer src/md_render.h, and write pictures and memory.
 *
 *   python3 tools/mdrom.py sonic.bin                     # -> src/sonic_rom.h
 *   cc -O2 -Isrc -o mdhost tools/mdhost.c
 *   ./mdhost [-f frames] [-p frame,ppm]... [-i frame:pad]... [-r ramdump] [-s]
 *
 *   -f N         run N frames (default 600)
 *   -p F,out.ppm write the picture after frame F (repeatable)
 *   -i F:hex     from frame F on, pad 1 = hex (active-low, MAME's PAD1 bits: 01 up, 02 down,
 *                04 left, 08 right, 10 B, 20 C, 40 A, 80 start; ff = nothing). Repeatable.
 *   -r out.bin   write 68000 RAM (64 KB, big-endian) after the last frame
 *   -m out.txt   write a per-frame RAM checksum (lockstep with MAME: tools/mdref.lua)
 *   -s           print statistics: 68000 instructions per frame, idle lines
 *   -P prefix    profile: write prefix.op (65536 x u32 executions per opcode) and
 *                prefix.pc (8M x u32 executions per word address), for tools/m68kgen.py
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
#include "sonic_rom.h"

static unsigned long long md_insns;
static u32 *prof_op, *prof_pc;          /* -P: executions per opcode and per PC */
static void prof_step(void);
#define MD_INTERRUPT() m68k_interrupt()
#define MD_STEP()      (md_insns++, prof_op ? prof_step() : m68k_step())
#include "md_hw.h"
#include "md_render.h"

static void prof_step(void) {
    prof_pc[(m68k.pc & 0xffffff) >> 1]++;
    prof_op[md_rd16(m68k.pc)]++;
    m68k_step();
}

static void write_ppm(const char *path) {
    static u8 pic[MDR_W * MDR_H];
    FILE *o = fopen(path, "wb");
    int i;
    if (!o) { perror(path); exit(1); }
    mdr_frame(pic);
    fprintf(o, "P6 %d %d 255\n", MDR_W, MDR_H);
    for (i = 0; i < MDR_W * MDR_H; i++) {
        u8 r, g, b;
        mdr_rgb(md_cram[pic[i]], &r, &g, &b);
        fputc(r, o); fputc(g, o); fputc(b, o);
    }
    fclose(o);
}

int main(int argc, char **argv) {
    int frames = 600, i, f, stats = 0;
    int pf[64], np = 0, inf[256], ni = 0;
    const char *pp[64], *ramout = 0, *sumout = 0, *profout = 0;
    unsigned long long fmax = 0;
    u8 inv[256];
    FILE *sums = 0;
    clock_t t0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            char *c = strchr(argv[++i], ',');
            if (!c || np >= 64) return 2;
            pf[np] = atoi(argv[i]); pp[np++] = c + 1;
        } else if (!strcmp(argv[i], "-i") && i + 1 < argc) {
            char *c = strchr(argv[++i], ':');
            if (!c || ni >= 256) return 2;
            inf[ni] = atoi(argv[i]); inv[ni++] = (u8)strtol(c + 1, 0, 16);
        } else if (!strcmp(argv[i], "-r") && i + 1 < argc) ramout = argv[++i];
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) sumout = argv[++i];
        else if (!strcmp(argv[i], "-s")) stats = 1;
        else if (!strcmp(argv[i], "-P") && i + 1 < argc) profout = argv[++i];
        else { fprintf(stderr, "usage: see tools/mdhost.c\n"); return 2; }
    }
    if (sumout && !(sums = fopen(sumout, "w"))) { perror(sumout); return 1; }
    if (profout) { prof_op = calloc(65536, 4); prof_pc = calloc(1 << 23, 4); }
    md_reset();
    t0 = clock();
    for (f = 1; f <= frames; f++) {
        unsigned long long before = md_insns;
        for (i = 0; i < ni; i++) if (inf[i] <= f) md_pad[0] = inv[i];
        md_frame();
        if (md_insns - before > fmax) fmax = md_insns - before;
        if (stats && (f % 60 == 0 || f == frames))
            printf("frame %5d: pc %06x  %llu insns this frame\n", f, m68k.pc, md_insns - before);
        if (sums) {
            u32 s = 0; int k;
            for (k = 0; k < 0x8000; k++) s = s * 31u + md_ram[k];
            fprintf(sums, "%d %08x\n", f, s);
        }
        for (i = 0; i < np; i++) if (pf[i] == f) write_ppm(pp[i]);
    }
    if (ramout) {
        FILE *o = fopen(ramout, "wb");
        for (i = 0; i < 0x8000; i++) { fputc(md_ram[i] >> 8, o); fputc(md_ram[i] & 0xff, o); }
        fclose(o);
    }
    if (sums) fclose(sums);
    if (profout) {
        char n[512]; FILE *o;
        snprintf(n, sizeof n, "%s.op", profout); o = fopen(n, "wb"); fwrite(prof_op, 4, 65536, o); fclose(o);
        snprintf(n, sizeof n, "%s.pc", profout); o = fopen(n, "wb"); fwrite(prof_pc, 4, 1 << 23, o); fclose(o);
    }
    printf("busiest frame: %llu instructions\n", fmax);
    printf("%d frames, %llu instructions (%.0f/frame), %.2f s host\n", frames, md_insns,
           (double)md_insns / frames, (double)(clock() - t0) / CLOCKS_PER_SEC);
    return 0;
}
