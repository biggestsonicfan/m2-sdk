/*
 * m2_m68k.h — a Motorola 68000 interpreter for the i960 (guest-CPU core for emulated boards).
 *
 * A Mega Drive's 68000 has no path to the Model 2's video: the sound board's own 68000
 * talks to the i960 only over a ~3 KB/s serial line. So the Mega Drive CPU is interpreted
 * here, like the Z80 of src/m2_z80.h, and the sound board's 68000 stays what it is for
 * Pac-Man: the SCSP relay (snd/scsp_passthru.s).
 *
 * Header-only and bus-agnostic: define the bus hooks BEFORE including it:
 *     M68K_RD8(a)  M68K_RD16(a)  M68K_WR8(a, v)  M68K_WR16(a, v)
 * a is the full 32-bit address (the board masks it to 24 bits); word accesses are even.
 * Optional:
 *     M68K_RD32(a) / M68K_WR32(a, v)  a long access (default: two word accesses, high first)
 *     M68K_FETCH16(a)                 opcode and extension words (default M68K_RD16)
 *     M68K_IACK(level)                interrupt acknowledge: returns the vector number
 *                                     (default the autovector, 24 + level)
 *     M68K_RESET_HOOK()               the RESET instruction
 *     M68K_BCC_TAKEN(target)          a taken Bcc (an idle-loop hook)
 * The board sets m68k.irq (the level on the IPL lines, 0-7) between m68k_run slices, or from
 * a bus hook.
 *
 * Behaviour and timing follow Musashi (Karl Stenerud's 68000 core, MAME's 68000 for years),
 * instruction for instruction: flags, the undefined BCD flags, and the cycle counts
 * (tools/m68k_cyc.c derives src/m2_m68k_cyc.h, the per-opcode base times, from it;
 * tools/mdlock.c checks this core against it one instruction at a time). No address
 * errors, no trace mode, no prefetch.
 *
 * Portable GCC C with no i960 specifics, so the same core builds on the host for testing.
 */
#ifndef M2_M68K_H
#define M2_M68K_H

#include "m2_m68k_cyc.h"               /* M68K_CYC(op): base cycles per opcode */

#define M68K_INL static inline __attribute__((always_inline))

typedef unsigned int   m68k_u32;
typedef signed int     m68k_s32;
typedef unsigned short m68k_u16;
typedef signed short   m68k_s16;
typedef unsigned char  m68k_u8;
typedef signed char    m68k_s8;

#ifndef M68K_RD32
#define M68K_RD32(a) (((m68k_u32)M68K_RD16(a) << 16) | M68K_RD16((a) + 2))
#endif
#ifndef M68K_WR32
#define M68K_WR32(a, v) do { m68k_u32 a_ = (a), v_ = (v); \
        M68K_WR16(a_, (m68k_u16)(v_ >> 16)); M68K_WR16(a_ + 2, (m68k_u16)v_); } while (0)
#endif
#ifndef M68K_FETCH16
#define M68K_FETCH16(a) M68K_RD16(a)
#endif
#ifndef M68K_IACK
#define M68K_IACK(level) (24 + (level))
#endif
/* M68K_BCC_TAKEN(target): after a taken Bcc (not BRA/BSR), e.g. to end the slice in a known
 * wait-for-interrupt loop (the board's idle skip) */
#ifndef M68K_BCC_TAKEN
#define M68K_BCC_TAKEN(target) ((void)0)
#endif
#ifndef M68K_RESET_HOOK
#define M68K_RESET_HOOK() ((void)0)
#endif

/* Flags are kept apart, in the form each is cheapest to produce:
 *   n, v: bit 31      z: zero exactly when Z is set      c, x: bit 0
 * Sized results are worked on shifted to the top of 32 bits (a byte << 24), so one 32-bit
 * formula gives N, Z, V and C for every size. */
typedef struct {
    m68k_u32 d[8], a[8];               /* a[7] is the active stack pointer */
    m68k_u32 pc;
    m68k_u32 osp;                      /* the other one: USP in supervisor mode, SSP in user */
    m68k_u32 s, t, imask;              /* supervisor 0/1, trace 0/1, interrupt mask 0-7 */
    m68k_u32 x, n, z, v, c;
    m68k_u32 stopped;                  /* STOP: waiting for an interrupt */
    m68k_u32 irq;                      /* level on the IPL lines, set by the board */
    m68k_u32 ir;                       /* the opcode being run */
    m68k_u32 ppc;                      /* ... and its address */
    int      cycles;                   /* cycles left in the current m68k_run slice */
} m68k_t;

static m68k_t m68k;

#define M68K_SR_T 0x8000

M68K_INL m68k_u32 m68k_get_ccr(void) {
    return ((m68k.x & 1) << 4) | ((m68k.n >> 31) << 3) | ((m68k.z == 0) << 2)
         | ((m68k.v >> 31) << 1) | (m68k.c & 1);
}
M68K_INL m68k_u32 m68k_get_sr(void) {
    return (m68k.t << 15) | (m68k.s << 13) | (m68k.imask << 8) | m68k_get_ccr();
}
M68K_INL void m68k_set_ccr(m68k_u32 v) {
    m68k.x = (v >> 4) & 1;
    m68k.n = (v & 8) << 28;
    m68k.z = !(v & 4);
    m68k.v = (v & 2) << 30;
    m68k.c = v & 1;
}
/* entering or leaving supervisor mode swaps the stack pointers */
M68K_INL void m68k_set_s(m68k_u32 s) {
    if (s != m68k.s) { m68k_u32 t = m68k.a[7]; m68k.a[7] = m68k.osp; m68k.osp = t; m68k.s = s; }
}
M68K_INL void m68k_set_sr(m68k_u32 v) {
    m68k.t = (v >> 15) & 1;
    m68k.imask = (v >> 8) & 7;
    m68k_set_ccr(v);
    m68k_set_s((v >> 13) & 1);
}

/* ---- bus, by size (1, 2, 4 bytes) -------------------------------------------------- */
M68K_INL m68k_u32 m68k_rd(int sz, m68k_u32 a) {
    if (sz == 1) return M68K_RD8(a);
    if (sz == 2) return M68K_RD16(a);
    return M68K_RD32(a);
}
M68K_INL void m68k_wr(int sz, m68k_u32 a, m68k_u32 v) {
    if (sz == 1) M68K_WR8(a, (m68k_u8)v);
    else if (sz == 2) M68K_WR16(a, (m68k_u16)v);
    else M68K_WR32(a, v);
}
M68K_INL m68k_u32 m68k_imm16(void) { m68k_u32 v = M68K_FETCH16(m68k.pc); m68k.pc += 2; return v; }
M68K_INL m68k_u32 m68k_imm32(void) {
    m68k_u32 h = M68K_FETCH16(m68k.pc), l = M68K_FETCH16(m68k.pc + 2);
    m68k.pc += 4; return (h << 16) | l;
}
M68K_INL void m68k_push32(m68k_u32 v) { m68k.a[7] -= 4; M68K_WR32(m68k.a[7], v); }
M68K_INL void m68k_push16(m68k_u32 v) { m68k.a[7] -= 2; M68K_WR16(m68k.a[7], (m68k_u16)v); }
M68K_INL m68k_u32 m68k_pop32(void) { m68k_u32 v = M68K_RD32(m68k.a[7]); m68k.a[7] += 4; return v; }
M68K_INL m68k_u32 m68k_pop16(void) { m68k_u32 v = M68K_RD16(m68k.a[7]); m68k.a[7] += 2; return v; }

/* size helpers: mask, and the shift that moves a sized value to the top of 32 bits */
M68K_INL m68k_u32 m68k_mask(int sz) { return sz == 1 ? 0xffu : sz == 2 ? 0xffffu : 0xffffffffu; }
M68K_INL int m68k_sh(int sz) { return sz == 1 ? 24 : sz == 2 ? 16 : 0; }
/* sign-extend a sized value */
M68K_INL m68k_u32 m68k_sext(int sz, m68k_u32 v) {
    return sz == 1 ? (m68k_u32)(m68k_s32)(m68k_s8)v : sz == 2 ? (m68k_u32)(m68k_s32)(m68k_s16)v : v;
}
/* store a sized result in a data register, keeping the bits above it */
M68K_INL void m68k_setd(int r, int sz, m68k_u32 v) {
    if (sz == 4) m68k.d[r] = v;
    else if (sz == 2) m68k.d[r] = (m68k.d[r] & 0xffff0000u) | (v & 0xffffu);
    else m68k.d[r] = (m68k.d[r] & 0xffffff00u) | (v & 0xffu);
}

/* ---- effective addresses ------------------------------------------------------------- */
/* brief extension word: d8(base, Xn) */
M68K_INL m68k_u32 m68k_index(m68k_u32 base) {
    m68k_u32 ext = m68k_imm16();
    m68k_u32 xn = (ext & 0x8000) ? m68k.a[(ext >> 12) & 7] : m68k.d[(ext >> 12) & 7];
    if (!(ext & 0x800)) xn = (m68k_u32)(m68k_s32)(m68k_s16)xn;
    return base + xn + (m68k_u32)(m68k_s32)(m68k_s8)ext;
}
/* the address of a memory operand (modes 2-7); (An)+ / -(An) step A7 by 2 for a byte */
M68K_INL m68k_u32 m68k_ea(int mode, int reg, int sz) {
    m68k_u32 a;
    switch (mode) {
    case 2: return m68k.a[reg];
    case 3: a = m68k.a[reg]; m68k.a[reg] += (reg == 7 && sz == 1) ? 2 : sz; return a;
    case 4: m68k.a[reg] -= (reg == 7 && sz == 1) ? 2 : sz; return m68k.a[reg];
    case 5: a = m68k.a[reg]; return a + (m68k_u32)(m68k_s32)(m68k_s16)m68k_imm16();
    case 6: return m68k_index(m68k.a[reg]);
    default:
        switch (reg) {
        case 0: return (m68k_u32)(m68k_s32)(m68k_s16)m68k_imm16();
        case 1: return m68k_imm32();
        case 2: a = m68k.pc; return a + (m68k_u32)(m68k_s32)(m68k_s16)m68k_imm16();
        default: return m68k_index(m68k.pc);             /* 3: d8(PC,Xn) */
        }
    }
}
/* read an operand of any mode, zero-extended to 32 bits */
M68K_INL m68k_u32 m68k_rd_ea(int mode, int reg, int sz) {
    if (mode == 0) return m68k.d[reg] & m68k_mask(sz);
    if (mode == 1) return m68k.a[reg] & m68k_mask(sz);
    if (mode == 7 && reg == 4) {                          /* #imm */
        if (sz == 4) return m68k_imm32();
        return m68k_imm16() & m68k_mask(sz);
    }
    return m68k_rd(sz, m68k_ea(mode, reg, sz));
}

/* ---- ALU: flags from values shifted to the top of 32 bits ------------------------------ */
M68K_INL m68k_u32 m68k_add(int sz, m68k_u32 s, m68k_u32 d, m68k_u32 xin, int setx, int addx) {
    int sh = m68k_sh(sz);
    m68k_u32 S = s << sh, D = d << sh, R = D + S + (xin << sh);
    m68k.n = R;
    if (addx) m68k.z |= R; else m68k.z = R;
    m68k.v = (S ^ R) & (D ^ R);
    m68k.c = ((S & D) | (~R & (S | D))) >> 31;
    if (setx) m68k.x = m68k.c;
    return R >> sh;
}
M68K_INL m68k_u32 m68k_sub(int sz, m68k_u32 s, m68k_u32 d, m68k_u32 xin, int setx, int subx) {
    int sh = m68k_sh(sz);
    m68k_u32 S = s << sh, D = d << sh, R = D - S - (xin << sh);
    m68k.n = R;
    if (subx) m68k.z |= R; else m68k.z = R;
    m68k.v = (S ^ D) & (R ^ D);
    m68k.c = ((S & R) | (~D & (S | R))) >> 31;
    if (setx) m68k.x = m68k.c;
    return R >> sh;
}
M68K_INL void m68k_logic(int sz, m68k_u32 r) {
    m68k_u32 R = r << m68k_sh(sz);
    m68k.n = m68k.z = R; m68k.v = 0; m68k.c = 0;
}

M68K_INL int m68k_cond(int cc) {
    switch (cc & 15) {
    case 0:  return 1;                                                  /* T  */
    case 1:  return 0;                                                  /* F  */
    case 2:  return !(m68k.c & 1) && m68k.z;                            /* HI */
    case 3:  return (m68k.c & 1) || !m68k.z;                            /* LS */
    case 4:  return !(m68k.c & 1);                                      /* CC */
    case 5:  return m68k.c & 1;                                         /* CS */
    case 6:  return m68k.z != 0;                                        /* NE */
    case 7:  return m68k.z == 0;                                        /* EQ */
    case 8:  return !(m68k.v >> 31);                                    /* VC */
    case 9:  return m68k.v >> 31;                                       /* VS */
    case 10: return !(m68k.n >> 31);                                    /* PL */
    case 11: return m68k.n >> 31;                                       /* MI */
    case 12: return !((m68k.n ^ m68k.v) >> 31);                         /* GE */
    case 13: return (m68k.n ^ m68k.v) >> 31;                            /* LT */
    case 14: return m68k.z && !((m68k.n ^ m68k.v) >> 31);               /* GT */
    default: return !m68k.z || ((m68k.n ^ m68k.v) >> 31);              /* LE */
    }
}

/* ---- exceptions ------------------------------------------------------------------------ */
static const m68k_u8 m68k_exc_cyc[16] = { 40, 4, 50, 50, 34, 38, 40, 34, 34, 34, 34, 34, 4, 4, 4, 44 };

static __attribute__((noinline)) void m68k_exception(m68k_u32 vec) {
    m68k_u32 sr = m68k_get_sr();
    m68k_set_s(1);
    m68k.t = 0;
    m68k_push32(m68k.pc);
    m68k_push16(sr);
    m68k.pc = M68K_RD32(vec << 2);
    m68k.cycles -= vec < 16 ? m68k_exc_cyc[vec] : vec >= 24 && vec < 32 ? 44 : 34;
}
/* take a pending interrupt, if the mask allows it (level 7 is not maskable) */
static __attribute__((noinline)) void m68k_interrupt(void) {
    m68k_u32 lvl = m68k.irq & 7, vec;
    m68k.stopped = 0;
    vec = M68K_IACK(lvl);
    m68k_exception(vec);                               /* trap-style frame: PC then SR */
    m68k.imask = lvl;
}
M68K_INL int m68k_irq_pending(void) {
    return m68k.irq > m68k.imask || m68k.irq == 7;
}

static void m68k_reset(void) {
    int i;
    for (i = 0; i < 8; i++) { m68k.d[i] = 0; m68k.a[i] = 0; }
    m68k.s = 1; m68k.t = 0; m68k.imask = 7; m68k.osp = 0;
    m68k.x = m68k.c = 0; m68k.n = m68k.v = 0; m68k.z = 1;
    m68k.stopped = 0; m68k.irq = 0;
    m68k.a[7] = M68K_RD32(0);
    m68k.pc = M68K_RD32(4);
}

/* ---- shifts (register and memory forms), by type: 0 AS, 1 LS, 2 ROX, 3 RO ---------------- */
M68K_INL m68k_u32 m68k_shift(int sz, int type, int left, m68k_u32 v, m68k_u32 cnt) {
    int bits = sz * 8;
    m68k_u32 mask = m68k_mask(sz), msb = 1u << (bits - 1), r = v & mask;
    m68k.v = 0;
    if (cnt == 0) {
        m68k.c = type == 2 ? m68k.x : 0;
        m68k_u32 R = r << m68k_sh(sz); m68k.n = m68k.z = R;
        return r;
    }
    switch (type) {
    case 0:                                                 /* ASL / ASR */
        if (left) {
            if (cnt >= (m68k_u32)bits) {
                m68k.c = m68k.x = cnt == (m68k_u32)bits ? v & 1 : 0;
                m68k.v = r ? 0x80000000u : 0;
                r = 0;
            } else {
                /* V: the top cnt+1 bits were not all equal */
                m68k_u32 top = (mask << (bits - 1 - cnt)) & mask;   /* bits that pass the sign */
                m68k_u32 t = r & top;
                m68k.v = (t != 0 && t != top) ? 0x80000000u : 0;
                m68k.c = m68k.x = (r >> (bits - cnt)) & 1;
                r = (r << cnt) & mask;
            }
        } else {
            m68k_s32 sv = (m68k_s32)(r << (32 - bits));             /* sign at bit 31 */
            if (cnt >= (m68k_u32)bits) {
                m68k.c = m68k.x = (r & msb) ? 1 : 0;
                r = (r & msb) ? mask : 0;
            } else {
                m68k.c = m68k.x = (r >> (cnt - 1)) & 1;
                r = ((m68k_u32)(sv >> cnt) >> (32 - bits)) & mask;
            }
        }
        break;
    case 1:                                                 /* LSL / LSR */
        if (cnt > (m68k_u32)bits) { m68k.c = m68k.x = 0; r = 0; }
        else if (left) { m68k.c = m68k.x = (r >> (bits - cnt)) & 1; r = cnt == (m68k_u32)bits ? 0 : (r << cnt) & mask; }
        else { m68k.c = m68k.x = (r >> (cnt - 1)) & 1; r = cnt == (m68k_u32)bits ? 0 : r >> cnt; }
        break;
    case 2: {                                               /* ROXL / ROXR: through X, bits+1 wide */
        m68k_u32 n = cnt % (m68k_u32)(bits + 1), i, x = m68k.x & 1;
        for (i = 0; i < n; i++) {
            if (left) { m68k_u32 o = (r >> (bits - 1)) & 1; r = ((r << 1) | x) & mask; x = o; }
            else      { m68k_u32 o = r & 1; r = (r >> 1) | (x << (bits - 1)); x = o; }
        }
        m68k.c = m68k.x = x;
        break;
    }
    default: {                                              /* ROL / ROR */
        m68k_u32 n = cnt & (m68k_u32)(bits - 1);
        if (n) r = left ? ((r << n) | (r >> (bits - n))) & mask : ((r >> n) | (r << (bits - n))) & mask;
        m68k.c = left ? r & 1 : (r >> (bits - 1)) & 1;
        break;
    }
    }
    { m68k_u32 R = r << m68k_sh(sz); m68k.n = m68k.z = R; }
    return r;
}

/* ---- MOVEM ------------------------------------------------------------------------------- */
static __attribute__((noinline)) void m68k_movem(m68k_u32 op) {
    int sz = (op & 0x40) ? 4 : 2, mode = (op >> 3) & 7, reg = op & 7, i, cnt = 0;
    m68k_u32 list = m68k_imm16(), a;
    if (!(op & 0x400)) {                                    /* registers -> memory */
        if (mode == 4) {                                    /* -(An): list reversed, A7..D0 */
            a = m68k.a[reg];
            for (i = 0; i < 16; i++)
                if (list & (1u << i)) {
                    m68k_u32 v = i < 8 ? m68k.a[7 - i] : m68k.d[15 - i];
                    a -= sz; m68k_wr(sz, a, v); cnt++;
                }
            m68k.a[reg] = a;
        } else {
            a = m68k_ea(mode, reg, sz);
            for (i = 0; i < 16; i++)
                if (list & (1u << i)) {
                    m68k_wr(sz, a, i < 8 ? m68k.d[i] : m68k.a[i - 8]); a += sz; cnt++;
                }
        }
    } else {                                                /* memory -> registers */
        a = mode == 3 ? m68k.a[reg] : m68k_ea(mode, reg, sz);
        for (i = 0; i < 16; i++)
            if (list & (1u << i)) {
                m68k_u32 v = m68k_rd(sz, a);
                if (sz == 2) v = (m68k_u32)(m68k_s32)(m68k_s16)v;
                if (i < 8) m68k.d[i] = v; else m68k.a[i - 8] = v;
                a += sz; cnt++;
            }
        if (mode == 3) m68k.a[reg] = a;
    }
    m68k.cycles -= cnt << (sz == 4 ? 3 : 2);
}

/* ---- the rarely used instructions, out of line -------------------------------------------- */
static __attribute__((noinline)) void m68k_bcd(m68k_u32 op) {
    /* ABCD (C1xx) / SBCD (81xx), Dy,Dx or -(Ay),-(Ax), as Musashi computes them (the undefined
     * N and V included) */
    int rx = (op >> 9) & 7, ry = op & 7, mem = op & 8, add = (op & 0xf000) == 0xc000;
    m68k_u32 src, dst, res, ea = 0, v;
    if (mem) {
        src = m68k_rd(1, m68k_ea(4, ry, 1));
        ea = m68k_ea(4, rx, 1);
        dst = m68k_rd(1, ea);
    } else { src = m68k.d[ry] & 0xff; dst = m68k.d[rx] & 0xff; }
    if (add) {
        res = (src & 15) + (dst & 15) + (m68k.x & 1);
        v = ~res;
        if (res > 9) res += 6;
        res += (src & 0xf0) + (dst & 0xf0);
        m68k.x = m68k.c = res > 0x99;
        if (m68k.c) res -= 0xa0;
        v &= res;
    } else {
        res = (dst & 15) - (src & 15) - (m68k.x & 1);
        v = ~res;
        if (res > 9) res -= 6;
        res += (dst & 0xf0) - (src & 0xf0);
        m68k.x = m68k.c = res > 0x99;
        if (m68k.c) res += 0xa0;
        res &= 0xff;
        v &= res;
    }
    m68k.v = (v & 0x80) << 24;
    m68k.n = (res & 0x80) << 24;
    res &= 0xff;
    m68k.z |= res;
    if (mem) M68K_WR8(ea, (m68k_u8)res); else m68k_setd(rx, 1, res);
}

static __attribute__((noinline)) void m68k_nbcd(m68k_u32 op) {
    int mode = (op >> 3) & 7, reg = op & 7;
    m68k_u32 ea = 0, dst, res;
    if (mode == 0) dst = m68k.d[reg] & 0xff; else { ea = m68k_ea(mode, reg, 1); dst = M68K_RD8(ea); }
    res = (0x9a - dst - (m68k.x & 1)) & 0xff;
    if (res != 0x9a) {
        m68k_u32 v = ~res;
        if ((res & 0x0f) == 0xa) res = (res & 0xf0) + 0x10;
        res &= 0xff;
        m68k.v = ((v & res) & 0x80) << 24;
        if (mode == 0) m68k_setd(reg, 1, res); else M68K_WR8(ea, (m68k_u8)res);
        m68k.z |= res;
        m68k.c = m68k.x = 1;
    } else {
        m68k.v = 0; m68k.c = m68k.x = 0;
    }
    m68k.n = (res & 0x80) << 24;
}

static __attribute__((noinline)) void m68k_movep(m68k_u32 op) {
    int dx = (op >> 9) & 7, ay = op & 7;
    m68k_u32 ea = m68k.a[ay] + (m68k_u32)(m68k_s32)(m68k_s16)m68k_imm16(), v;
    switch ((op >> 6) & 3) {
    case 0: v = (M68K_RD8(ea) << 8) | M68K_RD8(ea + 2); m68k_setd(dx, 2, v); break;
    case 1: m68k.d[dx] = (M68K_RD8(ea) << 24) | (M68K_RD8(ea + 2) << 16) | (M68K_RD8(ea + 4) << 8) | M68K_RD8(ea + 6); break;
    case 2: v = m68k.d[dx]; M68K_WR8(ea, (m68k_u8)(v >> 8)); M68K_WR8(ea + 2, (m68k_u8)v); break;
    default: v = m68k.d[dx]; M68K_WR8(ea, (m68k_u8)(v >> 24)); M68K_WR8(ea + 2, (m68k_u8)(v >> 16));
             M68K_WR8(ea + 4, (m68k_u8)(v >> 8)); M68K_WR8(ea + 6, (m68k_u8)v); break;
    }
}

/* DIVU / DIVS <ea>,Dn (Musashi: overflow sets V only; divide by zero traps) */
static __attribute__((noinline)) void m68k_div(m68k_u32 op) {
    int dn = (op >> 9) & 7, sgn = op & 0x100;
    m68k_u32 src = m68k_rd_ea((op >> 3) & 7, op & 7, 2);
    if (src == 0) { m68k_exception(5); return; }
    if (!sgn) {
        m68k_u32 q = m68k.d[dn] / src, r = m68k.d[dn] % src;
        if (q < 0x10000) {
            m68k.z = q; m68k.n = q << 16; m68k.v = 0; m68k.c = 0;
            m68k.d[dn] = (q & 0xffff) | (r << 16);
        } else m68k.v = 0x80000000u;
    } else {
        m68k_s32 s = (m68k_s16)src, q, r;
        if (m68k.d[dn] == 0x80000000u && s == -1) {
            m68k.z = 0; m68k.n = 0; m68k.v = 0; m68k.c = 0; m68k.d[dn] = 0; return;
        }
        q = (m68k_s32)m68k.d[dn] / s; r = (m68k_s32)m68k.d[dn] % s;
        if (q == (m68k_s16)q) {
            m68k.z = (m68k_u32)q & 0xffff; m68k.n = (m68k_u32)q << 16; m68k.v = 0; m68k.c = 0;
            m68k.d[dn] = ((m68k_u32)q & 0xffff) | ((m68k_u32)r << 16);
        } else m68k.v = 0x80000000u;
    }
}

/* MULU / MULS <ea>,Dn: 38 + 2 per 1 bit (MULU) / per 01 or 10 pair (MULS) of the source */
static __attribute__((noinline)) void m68k_mul(m68k_u32 op) {
    int dn = (op >> 9) & 7;
    m68k_u32 src = m68k_rd_ea((op >> 3) & 7, op & 7, 2), r, y, c = 0;
    if (op & 0x100) {
        m68k_u32 x = (m68k_u32)(m68k_s32)(m68k_s16)src, f = 0;
        for (y = x; y; y >>= 1) if ((y & 1) != f) { c += 2; f = 1 - f; }
        r = (m68k_u32)((m68k_s32)(m68k_s16)src * (m68k_s32)(m68k_s16)m68k.d[dn]);
    } else {
        for (y = src; y; y >>= 1) if (y & 1) c += 2;
        r = src * (m68k.d[dn] & 0xffff);
    }
    m68k.cycles -= (int)c;
    m68k.d[dn] = r;
    m68k.z = r; m68k.n = r; m68k.v = 0; m68k.c = 0;
}

static __attribute__((noinline)) void m68k_illegal(m68k_u32 op) {
    m68k.pc = m68k.ppc;                /* the frame holds the illegal opcode's address */
    m68k_exception((op >> 12) == 0xa ? 10 : (op >> 12) == 0xf ? 11 : 4);
}
static __attribute__((noinline)) void m68k_privilege(void) {
    m68k.pc = m68k.ppc;
    m68k_exception(8);
}

/* ---- one instruction ---------------------------------------------------------------------- */
static void m68k_step(void) {
    m68k_u32 op = M68K_FETCH16(m68k.pc), s, d, r, ea;
    int mode = (op >> 3) & 7, reg = op & 7, rx = (op >> 9) & 7, sz;
    m68k.ir = op;
    m68k.ppc = m68k.pc;
    m68k.pc += 2;
    m68k.cycles -= M68K_CYC(op);

    switch (op >> 12) {
    case 0x0:
        if (op & 0x100) {                                       /* BTST/BCHG/BCLR/BSET Dn,<ea>; MOVEP */
            if (mode == 1) { m68k_movep(op); break; }
            s = m68k.d[rx];
            goto bitop;
        }
        if (rx == 4) {                                          /* bit ops, #imm */
            s = m68k_imm16() & 0xff;
        bitop:
            if (mode == 0) {
                m68k_u32 bit = 1u << (s & 31);
                m68k.z = m68k.d[reg] & bit;
                switch ((op >> 6) & 3) {
                case 1: m68k.d[reg] ^= bit; break;
                case 2: m68k.d[reg] &= ~bit; break;
                case 3: m68k.d[reg] |= bit; break;
                }
            } else {
                m68k_u32 bit = 1u << (s & 7), t = (op >> 6) & 3;
                if (t == 0) { m68k.z = m68k_rd_ea(mode, reg, 1) & bit; break; }
                ea = m68k_ea(mode, reg, 1);
                d = M68K_RD8(ea);
                m68k.z = d & bit;
                d = t == 1 ? d ^ bit : t == 2 ? d & ~bit : d | bit;
                M68K_WR8(ea, (m68k_u8)d);
            }
            break;
        }
        if ((op & 0xff) == 0x3c || (op & 0xff) == 0x7c) {       /* ORI/ANDI/EORI to CCR / SR */
            m68k_u32 v = m68k_imm16(), cur;
            if (op & 0x40) {
                if (!m68k.s) { m68k_privilege(); break; }
                cur = m68k_get_sr();
            } else { cur = m68k_get_ccr(); v &= 0xff; }
            switch (rx) {
            case 0: cur |= v; break;
            case 1: cur &= v; break;
            case 5: cur ^= v; break;
            default: m68k_illegal(op); goto done;
            }
            if (op & 0x40) m68k_set_sr(cur); else m68k_set_ccr(cur);
            break;
        }
        sz = 1 << ((op >> 6) & 3);
        if (sz == 8 || rx == 7) { m68k_illegal(op); break; }
        s = sz == 4 ? m68k_imm32() : m68k_imm16() & m68k_mask(sz);
        if (rx == 6) {                                          /* CMPI */
            d = m68k_rd_ea(mode, reg, sz);
            m68k_u32 x = m68k.x;
            m68k_sub(sz, s, d, 0, 0, 0);
            m68k.x = x;
            break;
        }
        if (mode == 0) { d = m68k.d[reg]; ea = 0; }
        else { ea = m68k_ea(mode, reg, sz); d = m68k_rd(sz, ea); }
        switch (rx) {
        case 0: r = d | s; m68k_logic(sz, r); break;            /* ORI  */
        case 1: r = d & s; m68k_logic(sz, r); break;            /* ANDI */
        case 2: r = m68k_sub(sz, s, d, 0, 1, 0); break;         /* SUBI */
        case 3: r = m68k_add(sz, s, d, 0, 1, 0); break;         /* ADDI */
        default: r = d ^ s; m68k_logic(sz, r); break;           /* EORI */
        }
        if (mode == 0) m68k_setd(reg, sz, r); else m68k_wr(sz, ea, r);
        break;

    case 0x1: case 0x2: case 0x3: {                             /* MOVE, MOVEA */
        int dm = (op >> 6) & 7;
        sz = (op >> 12) == 1 ? 1 : (op >> 12) == 3 ? 2 : 4;
        s = m68k_rd_ea(mode, reg, sz);
        if (dm == 1) { m68k.a[rx] = m68k_sext(sz, s); break; }
        m68k_logic(sz, s);
        if (dm == 0) m68k_setd(rx, sz, s);
        else m68k_wr(sz, m68k_ea(dm, rx, sz), s);
        break;
    }

    case 0x4:
        if (op & 0x100) {
            if ((op & 0x1c0) == 0x1c0) { m68k.a[rx] = m68k_ea(mode, reg, 4); break; }     /* LEA */
            if ((op & 0x1c0) == 0x180) {                                                  /* CHK.W */
                m68k_s32 bound = (m68k_s16)m68k_rd_ea(mode, reg, 2), v = (m68k_s16)m68k.d[rx];
                m68k.z = v & 0xffff; m68k.v = 0; m68k.c = 0;
                if (v >= 0 && v <= bound) break;
                m68k.n = v < 0 ? 0x80000000u : 0;
                m68k_exception(6);
                break;
            }
            m68k_illegal(op); break;
        }
        switch ((op >> 8) & 0xf) {
        case 0x0: case 0x2: case 0x4: case 0x6:
            if (((op >> 6) & 3) == 3) {
                switch ((op >> 8) & 0xf) {
                case 0x0:                                       /* MOVE from SR */
                    if (mode == 0) m68k_setd(reg, 2, m68k_get_sr());
                    else M68K_WR16(m68k_ea(mode, reg, 2), (m68k_u16)m68k_get_sr());
                    break;
                case 0x4: m68k_set_ccr(m68k_rd_ea(mode, reg, 2)); break;          /* MOVE to CCR */
                case 0x6:                                                          /* MOVE to SR */
                    if (!m68k.s) { m68k_privilege(); break; }
                    m68k_set_sr(m68k_rd_ea(mode, reg, 2));
                    break;
                default: m68k_illegal(op); break;               /* MOVE from CCR: 68010+ */
                }
                break;
            }
            sz = 1 << ((op >> 6) & 3);
            if (mode == 0) { d = m68k.d[reg]; ea = 0; }
            else { ea = m68k_ea(mode, reg, sz); if (((op >> 8) & 0xf) != 2) d = m68k_rd(sz, ea); else d = 0; }
            switch ((op >> 8) & 0xf) {
            case 0x0: r = m68k_sub(sz, d, 0, m68k.x & 1, 1, 1); break;             /* NEGX */
            case 0x2:                                                              /* CLR */
                /* the 68000 reads before it clears: Musashi does not, nor do we */
                r = 0; m68k.n = 0; m68k.z = 0; m68k.v = 0; m68k.c = 0; break;
            case 0x4: r = m68k_sub(sz, d, 0, 0, 1, 0); break;                      /* NEG */
            default:  r = ~d; m68k_logic(sz, r & m68k_mask(sz)); break;            /* NOT */
            }
            if (mode == 0) m68k_setd(reg, sz, r); else m68k_wr(sz, ea, r);
            break;
        case 0x8:
            switch ((op >> 6) & 3) {
            case 0: m68k_nbcd(op); break;
            case 1:
                if (mode == 0) {                                                   /* SWAP */
                    r = (m68k.d[reg] >> 16) | (m68k.d[reg] << 16);
                    m68k.d[reg] = r; m68k.n = m68k.z = r; m68k.v = 0; m68k.c = 0;
                } else { ea = m68k_ea(mode, reg, 4); m68k_push32(ea); }            /* PEA */
                break;
            case 2:
                if (mode == 0) {                                                   /* EXT.W */
                    r = (m68k_u32)(m68k_s32)(m68k_s8)m68k.d[reg] & 0xffff;
                    m68k_setd(reg, 2, r); m68k_logic(2, r);
                } else m68k_movem(op);
                break;
            default:
                if (mode == 0) {                                                   /* EXT.L */
                    r = (m68k_u32)(m68k_s32)(m68k_s16)m68k.d[reg];
                    m68k.d[reg] = r; m68k_logic(4, r);
                } else m68k_movem(op);
                break;
            }
            break;
        case 0xa:
            if (((op >> 6) & 3) == 3) {
                if (op == 0x4afc) { m68k_illegal(op); break; }                     /* ILLEGAL */
                if (mode == 0) { d = m68k.d[reg] & 0xff; m68k_logic(1, d); m68k_setd(reg, 1, d | 0x80); }
                else { ea = m68k_ea(mode, reg, 1); d = M68K_RD8(ea); m68k_logic(1, d); M68K_WR8(ea, (m68k_u8)(d | 0x80)); }
                break;                                                             /* TAS */
            }
            sz = 1 << ((op >> 6) & 3);                                             /* TST */
            m68k_logic(sz, m68k_rd_ea(mode, reg, sz));
            break;
        case 0xc:
            if (op & 0x80) m68k_movem(op); else m68k_illegal(op);
            break;
        case 0xe:
            if ((op & 0xc0) == 0x80) {                                             /* JSR */
                ea = m68k_ea(mode, reg, 4); m68k_push32(m68k.pc); m68k.pc = ea; break;
            }
            if ((op & 0xc0) == 0xc0) { m68k.pc = m68k_ea(mode, reg, 4); break; }    /* JMP */
            switch ((op >> 3) & 0xf) {
            case 0x8: case 0x9: m68k_exception(32 + (op & 15)); break;             /* TRAP #n */
            case 0xa:                                                              /* LINK */
                m68k_push32(m68k.a[reg]);
                m68k.a[reg] = m68k.a[7];
                m68k.a[7] += (m68k_u32)(m68k_s32)(m68k_s16)m68k_imm16();
                break;
            case 0xb:                                                              /* UNLK */
                m68k.a[7] = m68k.a[reg];
                m68k.a[reg] = m68k_pop32();
                break;
            case 0xc:                                                              /* MOVE An,USP */
                if (!m68k.s) { m68k_privilege(); break; }
                m68k.osp = m68k.a[reg]; break;
            case 0xd:                                                              /* MOVE USP,An */
                if (!m68k.s) { m68k_privilege(); break; }
                m68k.a[reg] = m68k.osp; break;
            case 0xe:
                switch (op & 7) {
                case 0: if (!m68k.s) { m68k_privilege(); break; } M68K_RESET_HOOK(); break;  /* RESET */
                case 1: break;                                                     /* NOP */
                case 2:                                                            /* STOP */
                    if (!m68k.s) { m68k_privilege(); break; }
                    m68k_set_sr(m68k_imm16());
                    m68k.stopped = 1;
                    break;
                case 3:                                                            /* RTE */
                    if (!m68k.s) { m68k_privilege(); break; }
                    s = m68k_pop16(); m68k.pc = m68k_pop32(); m68k_set_sr(s);
                    break;
                case 5: m68k.pc = m68k_pop32(); break;                             /* RTS */
                case 6: if (m68k.v >> 31) m68k_exception(7); break;                /* TRAPV */
                case 7: s = m68k_pop16(); m68k.pc = m68k_pop32(); m68k_set_ccr(s); break;  /* RTR */
                default: m68k_illegal(op); break;
                }
                break;
            default: m68k_illegal(op); break;
            }
            break;
        default: m68k_illegal(op); break;
        }
        break;

    case 0x5:
        if (((op >> 6) & 3) == 3) {
            int cc = (op >> 8) & 15;
            if (mode == 1) {                                                       /* DBcc */
                if (m68k_cond(cc)) { m68k.pc += 2; break; }
                r = (m68k.d[reg] - 1) & 0xffff;
                m68k.d[reg] = (m68k.d[reg] & 0xffff0000u) | r;
                if (r != 0xffff) {
                    m68k.pc += (m68k_u32)(m68k_s32)(m68k_s16)M68K_FETCH16(m68k.pc);
                    m68k.cycles += 2;
                } else { m68k.pc += 2; m68k.cycles -= 2; }
                break;
            }
            r = m68k_cond(cc) ? 0xff : 0;                                          /* Scc */
            if (mode == 0) { m68k_setd(reg, 1, r); if (r) m68k.cycles -= 2; }
            else M68K_WR8(m68k_ea(mode, reg, 1), (m68k_u8)r);
            break;
        }
        s = rx ? rx : 8;                                                           /* ADDQ / SUBQ */
        sz = 1 << ((op >> 6) & 3);
        if (mode == 1) { m68k.a[reg] += (op & 0x100) ? -s : s; break; }
        if (mode == 0) { d = m68k.d[reg]; ea = 0; }
        else { ea = m68k_ea(mode, reg, sz); d = m68k_rd(sz, ea); }
        r = (op & 0x100) ? m68k_sub(sz, s, d, 0, 1, 0) : m68k_add(sz, s, d, 0, 1, 0);
        if (mode == 0) m68k_setd(reg, sz, r); else m68k_wr(sz, ea, r);
        break;

    case 0x6: {                                                                    /* Bcc, BRA, BSR */
        int cc = (op >> 8) & 15;
        m68k_u32 base = m68k.pc, disp = op & 0xff;
        if (disp == 0) { disp = (m68k_u32)(m68k_s32)(m68k_s16)M68K_FETCH16(base); m68k.pc += 2; }
        else disp = (m68k_u32)(m68k_s32)(m68k_s8)disp;
        if (cc == 1) { m68k_push32(m68k.pc); m68k.pc = base + disp; break; }
        if (m68k_cond(cc)) { m68k.pc = base + disp; if (cc) M68K_BCC_TAKEN(m68k.pc); }
        else m68k.cycles -= (op & 0xff) ? -2 : 2;
        break;
    }

    case 0x7:                                                                      /* MOVEQ */
        if (op & 0x100) { m68k_illegal(op); break; }
        r = (m68k_u32)(m68k_s32)(m68k_s8)op;
        m68k.d[rx] = r; m68k.n = m68k.z = r; m68k.v = 0; m68k.c = 0;
        break;

    case 0x8: case 0xc: {                                                          /* OR / AND, MUL, DIV, BCD, EXG */
        int om = (op >> 6) & 7;
        if (om == 3 || om == 7) { if (op & 0x4000) m68k_mul(op); else m68k_div(op); break; }
        if (om == 4 && mode <= 1) { m68k_bcd(op); break; }
        if (op & 0x4000) {                                                         /* EXG */
            if ((op & 0x1f8) == 0x140) { r = m68k.d[rx]; m68k.d[rx] = m68k.d[reg]; m68k.d[reg] = r; break; }
            if ((op & 0x1f8) == 0x148) { r = m68k.a[rx]; m68k.a[rx] = m68k.a[reg]; m68k.a[reg] = r; break; }
            if ((op & 0x1f8) == 0x188) { r = m68k.d[rx]; m68k.d[rx] = m68k.a[reg]; m68k.a[reg] = r; break; }
        }
        sz = 1 << (om & 3);
        if (!(om & 4)) {                                                           /* <ea> op Dn -> Dn */
            s = m68k_rd_ea(mode, reg, sz);
            r = (op & 0x4000) ? m68k.d[rx] & s : m68k.d[rx] | s;
            r &= m68k_mask(sz);
            m68k_logic(sz, r); m68k_setd(rx, sz, r);
        } else {                                                                   /* Dn op <ea> -> <ea> */
            if (mode <= 1) { m68k_illegal(op); break; }
            ea = m68k_ea(mode, reg, sz);
            d = m68k_rd(sz, ea);
            r = (op & 0x4000) ? d & m68k.d[rx] : d | m68k.d[rx];
            r &= m68k_mask(sz);
            m68k_logic(sz, r); m68k_wr(sz, ea, r);
        }
        break;
    }

    case 0x9: case 0xd: {                                                          /* SUB / ADD, SUBA/ADDA, SUBX/ADDX */
        int om = (op >> 6) & 7, add = (op >> 12) == 0xd;
        if (om == 3 || om == 7) {                                                  /* ADDA / SUBA */
            s = m68k_sext(om == 3 ? 2 : 4, m68k_rd_ea(mode, reg, om == 3 ? 2 : 4));
            m68k.a[rx] = add ? m68k.a[rx] + s : m68k.a[rx] - s;
            break;
        }
        sz = 1 << (om & 3);
        if (!(om & 4)) {                                                           /* <ea>,Dn */
            s = m68k_rd_ea(mode, reg, sz);
            d = m68k.d[rx] & m68k_mask(sz);
            r = add ? m68k_add(sz, s, d, 0, 1, 0) : m68k_sub(sz, s, d, 0, 1, 0);
            m68k_setd(rx, sz, r);
            break;
        }
        if (mode <= 1) {                                                           /* ADDX / SUBX */
            if (mode == 0) { s = m68k.d[reg] & m68k_mask(sz); d = m68k.d[rx] & m68k_mask(sz); ea = 0; }
            else { s = m68k_rd(sz, m68k_ea(4, reg, sz)); ea = m68k_ea(4, rx, sz); d = m68k_rd(sz, ea); }
            r = add ? m68k_add(sz, s, d, m68k.x & 1, 1, 1) : m68k_sub(sz, s, d, m68k.x & 1, 1, 1);
            if (mode == 0) m68k_setd(rx, sz, r); else m68k_wr(sz, ea, r);
            break;
        }
        ea = m68k_ea(mode, reg, sz);                                               /* Dn,<ea> */
        d = m68k_rd(sz, ea);
        s = m68k.d[rx] & m68k_mask(sz);
        r = add ? m68k_add(sz, s, d, 0, 1, 0) : m68k_sub(sz, s, d, 0, 1, 0);
        m68k_wr(sz, ea, r);
        break;
    }

    case 0xb: {                                                                    /* CMP, CMPA, CMPM, EOR */
        int om = (op >> 6) & 7;
        m68k_u32 x = m68k.x;
        if (om == 3 || om == 7) {                                                  /* CMPA */
            s = m68k_sext(om == 3 ? 2 : 4, m68k_rd_ea(mode, reg, om == 3 ? 2 : 4));
            m68k_sub(4, s, m68k.a[rx], 0, 0, 0);
            m68k.x = x;
            break;
        }
        sz = 1 << (om & 3);
        if (!(om & 4)) {                                                           /* CMP <ea>,Dn */
            s = m68k_rd_ea(mode, reg, sz);
            m68k_sub(sz, s, m68k.d[rx] & m68k_mask(sz), 0, 0, 0);
            m68k.x = x;
            break;
        }
        if (mode == 1) {                                                           /* CMPM (Ay)+,(Ax)+ */
            s = m68k_rd(sz, m68k_ea(3, reg, sz));
            d = m68k_rd(sz, m68k_ea(3, rx, sz));
            m68k_sub(sz, s, d, 0, 0, 0);
            m68k.x = x;
            break;
        }
        if (mode == 0) { r = (m68k.d[reg] ^ m68k.d[rx]) & m68k_mask(sz); m68k_setd(reg, sz, r); }   /* EOR */
        else { ea = m68k_ea(mode, reg, sz); r = (m68k_rd(sz, ea) ^ m68k.d[rx]) & m68k_mask(sz); m68k_wr(sz, ea, r); }
        m68k_logic(sz, r);
        break;
    }

    case 0xe:
        if (((op >> 6) & 3) == 3) {                                                /* memory shift, word, by 1 */
            if (op & 0x800) { m68k_illegal(op); break; }
            ea = m68k_ea(mode, reg, 2);
            r = m68k_shift(2, (op >> 9) & 3, (op >> 8) & 1, M68K_RD16(ea), 1);
            M68K_WR16(ea, (m68k_u16)r);
            break;
        }
        sz = 1 << ((op >> 6) & 3);
        s = (op & 0x20) ? m68k.d[rx] & 63 : (rx ? rx : 8);
        r = m68k_shift(sz, (op >> 3) & 3, (op >> 8) & 1, m68k.d[reg], s);
        m68k_setd(reg, sz, r);
        m68k.cycles -= (int)s * 2;
        break;

    default:                                                                       /* line A, line F */
        m68k_illegal(op);
        break;
    }
done:;
}

/* Run for (at least) `cycles` cycles: whole instructions, interrupts taken between them. */
static void m68k_run(int cycles) {
    m68k.cycles += cycles;
    while (m68k.cycles > 0) {
        if (m68k_irq_pending()) m68k_interrupt();
        if (m68k.stopped) { m68k.cycles = 0; break; }
        m68k_step();
    }
}

#endif /* M2_M68K_H */
