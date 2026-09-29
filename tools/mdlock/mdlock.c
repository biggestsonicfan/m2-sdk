/*
 * mdlock.c — check the 68000 core (src/m2_m68k.h) against Musashi, one instruction at a
 * time, while it runs a real game on src/md_hw.h.
 *
 * Every instruction (and every interrupt the core takes) runs twice: first on the core,
 * whose bus accesses are logged, then on Musashi from the same registers, with its memory
 * callbacks replaying the log (mus_glue.c). The registers, SR, both stack pointers, the
 * cycles charged and every write must match; the first difference stops the run.
 *
 *   git clone https://github.com/kstenerud/Musashi /tmp/Musashi && make -C /tmp/Musashi
 *   M=/tmp/Musashi; cc -O2 -Isrc -I$M -o mdlock tools/mdlock/mdlock.c tools/mdlock/mus_glue.c \
 *       $M/m68kcpu.o $M/m68kops.o $M/m68kdasm.o $M/softfloat/softfloat.o -lm
 *   ./mdlock [-f frames] [-i frame:pad]...          (same inputs as tools/mdhost.c)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
#include "sonic_rom.h"
#include "lock.h"

static void lk_log_acc(int w, int sz, u32 a, u32 v) {
    if (lk_n < LK_MAX) { lk_log[lk_n].w = (u8)w; lk_log[lk_n].sz = (u8)sz; lk_log[lk_n].used = 0;
                         lk_log[lk_n].addr = a & 0xffffff; lk_log[lk_n].val = v; lk_n++; }
}
static inline u32 lk_rd8(u32 a);
static inline u32 lk_rd16(u32 a);
static inline void lk_wr8(u32 a, u32 v);
static inline void lk_wr16(u32 a, u32 v);
#define M68K_RD8(a)      lk_rd8(a)
#define M68K_RD16(a)     lk_rd16(a)
#define M68K_WR8(a, v)   lk_wr8(a, v)
#define M68K_WR16(a, v)  lk_wr16(a, v)
#define M68K_FETCH16(a)  lk_rd16(a)
static void lk_interrupt(void);
static void lk_step(void);
#define MD_INTERRUPT() lk_interrupt()
#define MD_STEP()      lk_step()
#include "md_hw.h"

static inline u32 lk_rd8(u32 a)  { u32 v = md_rd8(a);  lk_log_acc(0, 1, a, v); return v; }
static inline u32 lk_rd16(u32 a) { u32 v = md_rd16(a); lk_log_acc(0, 2, a, v); return v; }
static inline void lk_wr8(u32 a, u32 v)  { lk_log_acc(1, 1, a, v & 0xff); md_wr8(a, v); }
static inline void lk_wr16(u32 a, u32 v) { lk_log_acc(1, 2, a, v & 0xffff); md_wr16(a, v); }

unsigned lk_peek(unsigned a, int sz) {
    a &= 0xffffff;
    if (a >= 0xe00000 || a < (MD_ROM_WORDS << 1)) return sz == 1 ? md_rd8(a) : md_rd16(a);
    return 0;
}

static unsigned long long lk_count;
static u32 lk_frame;

static void core_regs(lk_regs_t *r) {
    int i;
    for (i = 0; i < 8; i++) { r->d[i] = m68k.d[i]; r->a[i] = m68k.a[i]; }
    r->pc = m68k.pc; r->sr = m68k_get_sr();
    r->usp = m68k.s ? m68k.osp : m68k.a[7];
    r->ssp = m68k.s ? m68k.a[7] : m68k.osp;
}

static void dump(const char *what, const lk_regs_t *r) {
    int i;
    printf("  %-8s pc %06x sr %04x usp %08x ssp %08x\n", what, r->pc, r->sr, r->usp, r->ssp);
    printf("           d:"); for (i = 0; i < 8; i++) printf(" %08x", r->d[i]); printf("\n");
    printf("           a:"); for (i = 0; i < 8; i++) printf(" %08x", r->a[i]); printf("\n");
}

static void check(const char *kind, const lk_regs_t *pre, int core_cyc, int mus_cyc) {
    lk_regs_t c, m;
    int i, bad = lk_bad;
    core_regs(&c); mus_get(&m);
    if (memcmp(&c, &m, sizeof c)) bad = 1;
    if (core_cyc != mus_cyc) bad = 1;
    for (i = 0; i < lk_n; i++)
        if (!lk_log[i].used && lk_log[i].w) {
            bad = 1;
            if (!lk_bad) snprintf(lk_msg, sizeof lk_msg, "core wrote %x (%d bytes) at %06x, Musashi did not", lk_log[i].val, lk_log[i].sz, lk_log[i].addr);
            lk_bad = 1;
        }
    if (!bad) return;
    {
        char buf[128];
        mus_dasm(pre->pc, buf);
        printf("MISMATCH after %llu instructions, frame %u, %s at %06x: %s (op %04x)\n", lk_count, lk_frame, kind, pre->pc, buf, md_rd16(pre->pc));
        if (lk_bad) printf("  %s\n", lk_msg);
        printf("  cycles core %d, Musashi %d\n", core_cyc, mus_cyc);
        dump("before", pre); dump("core", &c); dump("Musashi", &m);
        for (i = 0; i < lk_n; i++)
            printf("  %s%d %06x = %x%s\n", lk_log[i].w ? "W" : "R", lk_log[i].sz, lk_log[i].addr, lk_log[i].val, lk_log[i].used ? "" : "  (unused)");
        exit(1);
    }
}

static void lk_step(void) {
    lk_regs_t pre;
    int c0 = m68k.cycles, mc;
    core_regs(&pre);
    lk_n = 0; lk_bad = 0;
    m68k_step();
    mus_set(&pre);
    mc = mus_step();
    /* the idle skip ends the slice (cycles = 0) on WaitForVBla's branch: not a timing */
    check("instruction", &pre, md_idle ? mc : c0 - m68k.cycles, mc);
    lk_count++;
}

static void lk_interrupt(void) {
    lk_regs_t pre;
    int lvl = (int)m68k.irq;
    core_regs(&pre);
    lk_n = 0; lk_bad = 0;
    m68k_interrupt();
    mus_set(&pre);
    mus_irq(lvl);
    check("interrupt", &pre, 0, 0);
}

int main(int argc, char **argv) {
    int frames = 600, i, f, inf[256], ni = 0;
    u8 inv[256];
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-i") && i + 1 < argc) {
            char *c = strchr(argv[++i], ':');
            if (!c || ni >= 256) return 2;
            inf[ni] = atoi(argv[i]); inv[ni++] = (u8)strtol(c + 1, 0, 16);
        } else { fprintf(stderr, "usage: see tools/mdlock/mdlock.c\n"); return 2; }
    }
    mus_init();
    md_reset();
    for (f = 1; f <= frames; f++) {
        lk_frame = (u32)f;
        for (i = 0; i < ni; i++) if (inf[i] <= f) md_pad[0] = inv[i];
        md_frame();
    }
    printf("OK: %llu instructions over %d frames match Musashi\n", lk_count, frames);
    return 0;
}
