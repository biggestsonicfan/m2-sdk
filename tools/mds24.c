/*
 * mds24.c — check src/md_s24.h on the host: run the game (src/md_hw.h), let md_s24.h build
 * the System 24 tile / char / palette RAM in arrays, draw those as MAME's model2
 * screen_update + segaic24 do (window masks, per-line scroll, opaque layer B, the two
 * priority passes), and compare the 320x224 picture with the reference renderer
 * (src/md_render.h), pixel for pixel.
 *
 *   cc -O2 -Isrc -o mds24 tools/mds24.c
 *   ./mds24 [-f frames] [-p frame,prefix]... [-i frame:pad]... [-e every]
 *
 *   -p F,prefix  after frame F write prefix.m2.ppm (the whole 496x384 Model 2 screen),
 *                prefix.ref.ppm (reference) and prefix.diff.ppm (differences in red)
 *   -e N         compare every N frames and print the number of differing pixels
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
#include "sonic_rom.h"
#include "m2font.h"
#include "md_hw.h"
#include "md_render.h"

static u16 tile_ram[0x8000], char_ram[0x40000], pal_ram[0x1000];
#define S24_TILE tile_ram
#define S24_CHAR char_ram
#define S24_PAL  pal_ram
#include "md_s24.h"

static u32 screen[384][496];

static u32 bgr555_rgb(u16 c) {
    u32 r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    return ((r * 255 / 31) << 16) | ((g * 255 / 31) << 8) | (b * 255 / 31);
}

/* a layer's pixel at screen (x,y): -1 if the mask gives the other layer of the pair,
 * else (category << 16) | (pen ? palette index : 0x8000 | palette index) */
static int layer_pix(int layer, int x, int y) {
    u16 m = tile_ram[(layer < 2 ? 0x6000 : 0x6800) + y * 4 + (x >> 7)];
    int bit = (m >> (15 - ((x >> 3) & 15))) & 1, tx, ty, win = layer & 1;
    u16 hs = tile_ram[0x5000 + layer], vs = tile_ram[0x5004 + layer], e, w;
    u32 ch, pen, bank;
    if (bit != win) return -1;
    if (vs & 0x8000) return -1;                        /* disabled */
    if (hs & 0x8000) tx = (x - tile_ram[0x4000 + 0x200 * layer + y]) & 511;
    else tx = (x - hs) & 511;
    ty = (vs + y) & 511;
    e = tile_ram[layer * 0x1000 + (ty >> 3) * 64 + (tx >> 3)];
    ch = e & 0x3fff; bank = (e >> 7) & 0xff;
    w = char_ram[ch * 16 + (ty & 7) * 2 + ((tx & 7) >> 2)];
    pen = (w >> (12 - 4 * (tx & 3))) & 15;
    return (int)(((u32)(e >> 15) << 16) | (pen ? 0 : 0x8000) | (bank * 16 + pen));
}

static void draw_m2(void) {
    int x, y, l, pass;
    for (y = 0; y < 384; y++)
        for (x = 0; x < 496; x++) {
            u32 c = bgr555_rgb(pal_ram[0]);
            /* pass 1: layers 3, 2 opaque, then 1, 0 category 0; pass 2: 3..0 category 1 */
            for (pass = 0; pass < 2; pass++)
                for (l = 3; l >= 0; l--) {
                    int p = layer_pix(l, x, y);
                    if (p < 0) continue;
                    if (pass == 0) {
                        if (l >= 2) c = bgr555_rgb(pal_ram[p & 0xfff]);
                        else if (!(p & 0x8000) && !(p >> 16)) c = bgr555_rgb(pal_ram[p & 0xfff]);
                    } else if (!(p & 0x8000) && (p >> 16)) c = bgr555_rgb(pal_ram[p & 0xfff]);
                }
            screen[y][x] = c;
        }
}

static void ppm(const char *path, int w, int h, u32 (*get)(int, int)) {
    FILE *o = fopen(path, "wb");
    int x, y;
    fprintf(o, "P6 %d %d 255\n", w, h);
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) { u32 c = get(x, y); fputc(c >> 16, o); fputc((c >> 8) & 255, o); fputc(c & 255, o); }
    fclose(o);
}
static u8 ref[MDR_W * MDR_H];
static u32 ref_rgb(int x, int y) { u16 c = md_cram[ref[y * MDR_W + x]]; return bgr555_rgb(s24_rgb(c)); }
static u32 m2_rgb(int x, int y) { return screen[y][x]; }
static u32 diff_rgb(int x, int y) {
    u32 a = ref_rgb(x, y), b = screen[y + S24_Y0][x + S24_X0];
    return a == b ? (a >> 2) & 0x3f3f3f : 0xff0000;
}

static int compare(void) {
    int x, y, n = 0;
    mdr_frame(ref);
    draw_m2();
    for (y = 0; y < MDR_H; y++) for (x = 0; x < MDR_W; x++) if (ref_rgb(x, y) != screen[y + S24_Y0][x + S24_X0]) n++;
    return n;
}

int main(int argc, char **argv) {
    int frames = 600, i, f, every = 0, pf[64], np = 0, inf[256], ni = 0;
    const char *pp[64];
    u8 inv[256];
    char name[512];
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-e") && i + 1 < argc) every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            char *c = strchr(argv[++i], ','); if (!c) return 2; pf[np] = atoi(argv[i]); pp[np++] = c + 1;
        } else if (!strcmp(argv[i], "-i") && i + 1 < argc) {
            char *c = strchr(argv[++i], ':'); if (!c) return 2; inf[ni] = atoi(argv[i]); inv[ni++] = (u8)strtol(c + 1, 0, 16);
        } else { fprintf(stderr, "usage: see tools/mds24.c\n"); return 2; }
    }
    md_reset();
    s24_init();
    for (f = 1; f <= frames; f++) {
        for (i = 0; i < ni; i++) if (inf[i] <= f) md_pad[0] = inv[i];
        md_frame();
        s24_update();
        if (every && f % every == 0)
            printf("frame %5d: %6d pixels differ, %3u composite cells, %2d banks, %2d char groups\n",
                   f, compare(), s24_ncomposited, s24_nbanks, s24_ngroups);
        for (i = 0; i < np; i++) if (pf[i] == f) {
            int n = compare();
            snprintf(name, sizeof name, "%s.m2.ppm", pp[i]); ppm(name, 496, 384, m2_rgb);
            snprintf(name, sizeof name, "%s.ref.ppm", pp[i]); ppm(name, MDR_W, MDR_H, ref_rgb);
            snprintf(name, sizeof name, "%s.diff.ppm", pp[i]); ppm(name, MDR_W, MDR_H, diff_rgb);
            printf("frame %d: %d pixels differ -> %s.*.ppm\n", f, n, pp[i]);
        }
    }
    return 0;
}
