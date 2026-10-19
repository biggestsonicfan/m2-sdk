/* host harness: all.c natively, with the same fake TGP as tools/cvm.py */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef unsigned int u32;
u32 host_buf[0x8000];
static u32 dram[0x400], fin[256], nin, pin, fout[4096], nout;
static u32 dep, spx;
static u32 fb(float f) { u32 u; memcpy(&u, &f, 4); return u; }
static float bf(u32 u) { float f; memcpy(&f, &u, 4); return f; }
u32 vm_in(void) { return pin < nin ? fin[pin++] : 0; }
void vm_out(u32 v) { fout[nout++] = v; }
u32 vm_sin(u32 a) { return fb((float)sin((a & 0xffff) * (2 * M_PI / 65536.0))); }
u32 vm_cos(u32 a) { return fb((float)cos((a & 0xffff) * (2 * M_PI / 65536.0))); }
u32 vm_ird(u32 a) { return dram[a & 0x3ff]; }
void vm_iwr(u32 a, u32 v) { dram[a & 0x3ff] = v; }
void vm_push(void) { dep++; if (dep - 1 < 7) { memcpy(&dram[0x200 + spx], &dram[0x60], 48); spx += 12; } }
void vm_pop(void) { if (!dep) return; dep--; if (dep < 7) { spx -= 12; memcpy(&dram[0x60], &dram[0x200 + spx], 48); } }
u32 vm_rsqrt(u32 a) { return fb((float)(1.0 / sqrt((double)bf(a)))); }
void vm_main(u32 cmdno);
int main(void) {
    char line[4096];
    while (fgets(line, sizeof line, stdin)) {
        char *t = strtok(line, " \n");
        if (!t || *t == '#') continue;
        if (!strcmp(t, "buf")) {
            u32 w = strtoul(strtok(0, " \n"), 0, 16);
            while ((t = strtok(0, " \n"))) host_buf[w++ & 0x7fff] = strtoul(t, 0, 16);
            continue;
        }
        if (!strcmp(t, "mat")) { for (int i = 0; i < 12; i++) dram[0x60 + i] = strtoul(strtok(0, " \n"), 0, 16); continue; }
        u32 cmd = strtoul(t, 0, 16);
        nin = pin = nout = 0;
        while ((t = strtok(0, " \n"))) fin[nin++] = strtoul(t, 0, 16);
        if (cmd == 0) {             /* the kernels' own init: depth 0, the first slot's matrix */
            if (dep) memcpy(&dram[0x60], &dram[0x200], 48);
            dep = spx = 0;
        } else vm_main(cmd);
        printf("%02x:", cmd);
        for (u32 i = 0; i < nout; i++) printf(" %08x", fout[i]);
        printf(" | used %u/%u | mat", pin, nin);
        for (int i = 0; i < 12; i++) printf(" %08x", dram[0x60 + i]);
        printf("\n");
    }
    return 0;
}
