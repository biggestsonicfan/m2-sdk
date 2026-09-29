/*
 * mus_glue.c — Musashi's side of tools/mdlock: its memory callbacks replay the bus log
 * of the instruction src/m2_m68k.h just ran (reads return what that core read, writes
 * are checked against what it wrote), so Musashi runs the same instruction on the same
 * data with no side effects. A separate file: Musashi's m68k.h and m2_m68k.h share names.
 */
#include <stdio.h>
#include "m68kcpu.h"                  /* m68ki_cpu: Musashi internals */
#include "lock.h"

lk_acc_t lk_log[LK_MAX];
int lk_n, lk_bad;
char lk_msg[256];

static unsigned lk_rd(unsigned a, int sz) {
    int i;
    a &= 0xffffff;
    if (sz == 4) return (lk_rd(a, 2) << 16) | lk_rd(a + 2, 2);
    for (i = 0; i < lk_n; i++)
        if (!lk_log[i].used && !lk_log[i].w && lk_log[i].sz == sz && lk_log[i].addr == a) {
            lk_log[i].used = 1; return lk_log[i].val;
        }
    if (!lk_bad) { lk_bad = 1; snprintf(lk_msg, sizeof lk_msg, "Musashi reads %d bytes at %06x, not in the log", sz, a); }
    return lk_peek(a, sz);
}
static void lk_wr(unsigned a, unsigned v, int sz) {
    int i;
    a &= 0xffffff;
    if (sz == 4) { lk_wr(a, v >> 16, 2); lk_wr(a + 2, v & 0xffff, 2); return; }
    for (i = 0; i < lk_n; i++)
        if (!lk_log[i].used && lk_log[i].w && lk_log[i].sz == sz && lk_log[i].addr == a) {
            lk_log[i].used = 1;
            if (lk_log[i].val != v && !lk_bad) { lk_bad = 1; snprintf(lk_msg, sizeof lk_msg, "write %d bytes at %06x: core %x, Musashi %x", sz, a, lk_log[i].val, v); }
            return;
        }
    if (!lk_bad) { lk_bad = 1; snprintf(lk_msg, sizeof lk_msg, "Musashi writes %x (%d bytes) at %06x, not in the log", v, sz, a); }
}
unsigned int m68k_read_memory_8(unsigned int a)  { return lk_rd(a, 1); }
unsigned int m68k_read_memory_16(unsigned int a) { return lk_rd(a, 2); }
unsigned int m68k_read_memory_32(unsigned int a) { return lk_rd(a, 4); }
void m68k_write_memory_8(unsigned int a, unsigned int v)  { lk_wr(a, v & 0xff, 1); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { lk_wr(a, v & 0xffff, 2); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { lk_wr(a, v, 4); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return lk_peek(a, 2); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return (lk_peek(a, 2) << 16) | lk_peek(a + 2, 2); }

void mus_init(void) {
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); m68k_pulse_reset();
    m68ki_cpu.reset_cycles = 0;        /* the reset is the core's; charge none here */
}

void mus_set(const lk_regs_t *r) {
    int i;
    m68k_set_reg(M68K_REG_SR, r->sr);
    m68k_set_reg(M68K_REG_USP, r->usp);
    m68k_set_reg(M68K_REG_ISP, r->ssp);
    for (i = 0; i < 8; i++) { m68k_set_reg(M68K_REG_D0 + i, r->d[i]); m68k_set_reg(M68K_REG_A0 + i, r->a[i]); }
    m68k_set_reg(M68K_REG_PC, r->pc);
}
void mus_get(lk_regs_t *r) {
    int i;
    for (i = 0; i < 8; i++) { r->d[i] = m68k_get_reg(0, M68K_REG_D0 + i); r->a[i] = m68k_get_reg(0, M68K_REG_A0 + i); }
    r->pc = m68k_get_reg(0, M68K_REG_PC);
    r->sr = m68k_get_reg(0, M68K_REG_SR);
    r->usp = m68k_get_reg(0, M68K_REG_USP);
    r->ssp = m68k_get_reg(0, M68K_REG_ISP);
}
int mus_step(void) { return m68k_execute(1); }
void mus_irq(int level) { m68ki_exception_interrupt((unsigned)level); }   /* take it now */
void mus_dasm(unsigned pc, char *buf) { m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000); }
