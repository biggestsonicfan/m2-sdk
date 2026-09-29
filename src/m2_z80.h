/*
 * m2_z80.h — a Zilog Z80 interpreter for the i960 (guest-CPU core for emulated boards).
 *
 * A guest board built on chips the Model 2 also has (i960; the sound board's 68000/SCSP)
 * could in principle hand its code to the matching part. A Z80 board (Pac-Man,
 * src/pacman.c) has no match here, so its CPU is interpreted by this core.
 *
 * Header-only and bus-agnostic: define the four bus hooks BEFORE including it, e.g.
 *     static u8   pac_rd(u16 a);  ...
 *     #define Z80_RD(a)     pac_rd(a)
 *     #define Z80_WR(a, v)  pac_wr(a, v)
 *     #define Z80_IN(p)     pac_in(p)
 *     #define Z80_OUT(p, v) pac_out(p, v)
 * Optional: Z80_FETCH(a) for opcode/operand fetches (defaults to Z80_RD) — point it at the
 * ROM array when code only ever runs from ROM, which skips the bus decode.
 * Optional: Z80_JP_TAKEN(target), an idle-loop hook (see below).
 * The hooks must not touch `z80`: z80_run works on a local copy and writes it back at the
 * end. Between z80_run calls the board may set z80.irq_line / z80.irq_vec.
 *
 * Documented behaviour and flags (passes zexdoc, tools/z80test); the undocumented X/Y flag
 * bits are tracked for the common cases only. Cycle counts are the standard T-states,
 * which is all a video-timed board needs to pace its frames. The R register is not
 * counted per M1 cycle (that costs every opcode); LD A,R returns an approximation.
 *
 * GCC C (always_inline, anonymous struct) with no i960 specifics, so the same core builds on
 * the host for testing.
 */
#ifndef M2_Z80_H
#define M2_Z80_H

#ifndef Z80_FETCH
#define Z80_FETCH(a) Z80_RD(a)
#endif
/* Z80_JP_TAKEN(target): run after a taken JP cc. A board can use it to end the slice
 * (z80.cycles = 0) when the jump closes a known wait-for-interrupt loop: time spent
 * spinning there changes nothing, so skipping it is free speed (MAME's idle skip). */
#ifndef Z80_JP_TAKEN
#define Z80_JP_TAKEN(target) ((void)0)
#endif

/* the hot helpers must inline: an i960 call+ret costs ~16 cycles, more than most Z80 ops */
#define Z80_INL static inline __attribute__((always_inline))

typedef unsigned char  z80_u8;
typedef unsigned short z80_u16;

/* register file: r[] is B C D E H L F A, so r[op & 7] is the operand register for
 * every 8-bit encoding except 6 = (HL) */
enum { Z80_B, Z80_C, Z80_D, Z80_E, Z80_H, Z80_L, Z80_F, Z80_A };

typedef struct {
    z80_u8  r[8];
    z80_u8  alt[8];               /* B' C' D' E' H' L' F' A' */
    z80_u16 ix, iy, sp, pc;
    z80_u8  i, rr, iff1, iff2, im;
    z80_u8  irq_vec;              /* the byte the board puts on the bus for IM 0/2 */
    union {                       /* anything non-zero here sends z80_run down its slow path */
        unsigned int events;
        struct { z80_u8 irq_line, halted, ei_delay, ev_pad_; };   /* irq_line: level-held INT */
    };
    int     cycles;               /* T-states left in the current z80_run slice */
} z80_t;

static z80_t z80;

#define Z80_CF 0x01
#define Z80_NF 0x02
#define Z80_PF 0x04
#define Z80_XF 0x08
#define Z80_HF 0x10
#define Z80_YF 0x20
#define Z80_ZF 0x40
#define Z80_SF 0x80

static z80_u8 z80_sz[256], z80_szp[256];

#define zA  z80.r[Z80_A]
#define zF  z80.r[Z80_F]
#define zB  z80.r[Z80_B]
#define zC  z80.r[Z80_C]
#define zD  z80.r[Z80_D]
#define zE  z80.r[Z80_E]
#define zH  z80.r[Z80_H]
#define zL  z80.r[Z80_L]
#define zBC ((z80_u16)((zB << 8) | zC))
#define zDE ((z80_u16)((zD << 8) | zE))
#define zHL ((z80_u16)((zH << 8) | zL))
#define zSET_BC(v) do { z80_u16 t_ = (v); zB = (z80_u8)(t_ >> 8); zC = (z80_u8)t_; } while (0)
#define zSET_DE(v) do { z80_u16 t_ = (v); zD = (z80_u8)(t_ >> 8); zE = (z80_u8)t_; } while (0)
#define zSET_HL(v) do { z80_u16 t_ = (v); zH = (z80_u8)(t_ >> 8); zL = (z80_u8)t_; } while (0)

static void z80_reset(void) {
    int i;
    for (i = 0; i < 256; i++) {
        z80_sz[i] = (z80_u8)((i & (Z80_SF | Z80_XF | Z80_YF)) | (i ? 0 : Z80_ZF));
        { int b, n = 0; for (b = 0; b < 8; b++) n += (i >> b) & 1;
          z80_szp[i] = (z80_u8)(z80_sz[i] | ((n & 1) ? 0 : Z80_PF)); }
    }
    for (i = 0; i < 8; i++) { z80.r[i] = 0xff; z80.alt[i] = 0xff; }
    z80.ix = z80.iy = z80.sp = 0xffff;
    z80.pc = 0; z80.i = 0; z80.rr = 0;
    z80.iff1 = z80.iff2 = 0; z80.im = 0; z80.halted = 0; z80.ei_delay = 0;
    z80.irq_line = 0; z80.irq_vec = 0xff;
}

/* Everything below runs on the CPU state through `zp`: z80_run points it at a local copy
 * of z80, so once inlined GCC keeps PC, SP and the cycle count in registers instead of
 * loading and storing the global around every opcode. */
#define z80 (*zp)

/* ---- bus: the hooks wrapped once, so a hook macro may evaluate its argument twice */
Z80_INL z80_u8 z80_bus_fetch(z80_u16 a)       { return Z80_FETCH(a); }
Z80_INL z80_u8 z80_bus_rd(z80_u16 a)          { return Z80_RD(a); }
Z80_INL void   z80_bus_wr(z80_u16 a, z80_u8 v) { Z80_WR(a, v); }
Z80_INL z80_u8 z80_bus_in(z80_u16 p)          { return Z80_IN(p); }
Z80_INL void   z80_bus_out(z80_u16 p, z80_u8 v) { Z80_OUT(p, v); }

/* ---- register by index (B C D E H L F A) --------------------------------------
 * Only through these switches is r[] indexed at run time, so GCC can keep the eight
 * registers in i960 registers instead of the array in memory (a load is 4 cycles, a
 * store 2 in MAME's i960 model). The hot main-page ops use constant indexes instead. */
Z80_INL z80_u8 z80_getr(z80_t *zp, int i) {
    switch (i & 7) {
    case 0: return zB; case 1: return zC; case 2: return zD; case 3: return zE;
    case 4: return zH; case 5: return zL; case 6: return zF; default: return zA;
    }
}
Z80_INL void z80_setr(z80_t *zp, int i, z80_u8 v) {
    switch (i & 7) {
    case 0: zB = v; break; case 1: zC = v; break; case 2: zD = v; break; case 3: zE = v; break;
    case 4: zH = v; break; case 5: zL = v; break; case 6: zF = v; break; default: zA = v; break;
    }
}

/* ---- fetch / stack helpers ------------------------------------------------- */
Z80_INL z80_u8 z80_imm8(z80_t *zp) { return z80_bus_fetch(z80.pc++); }
Z80_INL z80_u16 z80_imm16(z80_t *zp) {
    z80_u16 lo = z80_bus_fetch(z80.pc); z80_u16 hi = z80_bus_fetch((z80_u16)(z80.pc + 1));
    z80.pc += 2; return (z80_u16)(lo | (hi << 8));
}
Z80_INL z80_u16 z80_rd16(z80_t *zp, z80_u16 a) {
    (void)zp;
    return (z80_u16)(z80_bus_rd(a) | (z80_bus_rd((z80_u16)(a + 1)) << 8));
}
Z80_INL void z80_wr16(z80_t *zp, z80_u16 a, z80_u16 v) {
    (void)zp;
    z80_bus_wr(a, (z80_u8)v); z80_bus_wr((z80_u16)(a + 1), (z80_u8)(v >> 8));
}
Z80_INL void z80_push(z80_t *zp, z80_u16 v) {
    z80.sp -= 2; z80_wr16(zp, z80.sp, v);
}
Z80_INL z80_u16 z80_pop(z80_t *zp) {
    z80_u16 v = z80_rd16(zp, z80.sp); z80.sp += 2; return v;
}

/* ---- ALU ------------------------------------------------------------------- */
Z80_INL void z80_add8(z80_t *zp, z80_u8 v, int c) {
    unsigned r = (unsigned)zA + v + c;
    zF = (z80_u8)(z80_sz[r & 0xff] | ((r >> 8) & Z80_CF) | ((zA ^ v ^ r) & Z80_HF)
                  | (((v ^ zA ^ 0x80) & (v ^ r) & 0x80) >> 5));
    zA = (z80_u8)r;
}
Z80_INL void z80_sub8(z80_t *zp, z80_u8 v, int c) {
    unsigned r = (unsigned)zA - v - c;
    zF = (z80_u8)(z80_sz[r & 0xff] | ((r >> 8) & Z80_CF) | Z80_NF | ((zA ^ v ^ r) & Z80_HF)
                  | (((v ^ zA) & (zA ^ r) & 0x80) >> 5));
    zA = (z80_u8)r;
}
Z80_INL void z80_cp8(z80_t *zp, z80_u8 v) {
    unsigned r = (unsigned)zA - v;
    zF = (z80_u8)((z80_sz[r & 0xff] & ~(Z80_XF | Z80_YF)) | (v & (Z80_XF | Z80_YF))
                  | ((r >> 8) & Z80_CF) | Z80_NF | ((zA ^ v ^ r) & Z80_HF)
                  | (((v ^ zA) & (zA ^ r) & 0x80) >> 5));
}
Z80_INL void z80_alu(z80_t *zp, int op, z80_u8 v) {
    switch (op & 7) {
    case 0: z80_add8(zp, v, 0); break;
    case 1: z80_add8(zp, v, zF & Z80_CF); break;
    case 2: z80_sub8(zp, v, 0); break;
    case 3: z80_sub8(zp, v, zF & Z80_CF); break;
    case 4: zA &= v; zF = (z80_u8)(z80_szp[zA] | Z80_HF); break;
    case 5: zA ^= v; zF = z80_szp[zA]; break;
    case 6: zA |= v; zF = z80_szp[zA]; break;
    default: z80_cp8(zp, v); break;
    }
}
Z80_INL z80_u8 z80_inc8(z80_t *zp, z80_u8 v) {
    z80_u8 r = (z80_u8)(v + 1);
    zF = (z80_u8)((zF & Z80_CF) | z80_sz[r] | (r == 0x80 ? Z80_PF : 0) | ((r & 0x0f) ? 0 : Z80_HF));
    return r;
}
Z80_INL z80_u8 z80_dec8(z80_t *zp, z80_u8 v) {
    z80_u8 r = (z80_u8)(v - 1);
    zF = (z80_u8)((zF & Z80_CF) | Z80_NF | z80_sz[r] | (r == 0x7f ? Z80_PF : 0)
                  | ((r & 0x0f) == 0x0f ? Z80_HF : 0));
    return r;
}
Z80_INL z80_u16 z80_add16(z80_t *zp, z80_u16 a, z80_u16 v) {
    unsigned r = (unsigned)a + v;
    zF = (z80_u8)((zF & (Z80_SF | Z80_ZF | Z80_PF)) | (((a ^ v ^ r) >> 8) & Z80_HF)
                  | ((r >> 16) & Z80_CF) | ((r >> 8) & (Z80_XF | Z80_YF)));
    return (z80_u16)r;
}
Z80_INL void z80_adc16(z80_t *zp, z80_u16 v) {
    unsigned a = zHL, r = a + v + (zF & Z80_CF);
    zF = (z80_u8)(((r >> 8) & (Z80_SF | Z80_XF | Z80_YF)) | ((r & 0xffff) ? 0 : Z80_ZF)
                  | (((a ^ v ^ r) >> 8) & Z80_HF) | (((v ^ a ^ 0x8000) & (v ^ r) & 0x8000) >> 13)
                  | ((r >> 16) & Z80_CF));
    zSET_HL(r);
}
Z80_INL void z80_sbc16(z80_t *zp, z80_u16 v) {
    unsigned a = zHL, r = a - v - (zF & Z80_CF);
    zF = (z80_u8)(Z80_NF | ((r >> 8) & (Z80_SF | Z80_XF | Z80_YF)) | ((r & 0xffff) ? 0 : Z80_ZF)
                  | (((a ^ v ^ r) >> 8) & Z80_HF) | (((v ^ a) & (a ^ r) & 0x8000) >> 13)
                  | ((r >> 16) & Z80_CF));
    zSET_HL(r);
}

/* CB-page rotate/shift (op bits 5..3) — returns the result, sets F */
Z80_INL z80_u8 z80_rot(z80_t *zp, int op, z80_u8 v) {
    z80_u8 c, r;
    switch ((op >> 3) & 7) {
    case 0:  c = v >> 7; r = (z80_u8)((v << 1) | c); break;                 /* RLC */
    case 1:  c = v & 1;  r = (z80_u8)((v >> 1) | (c << 7)); break;          /* RRC */
    case 2:  c = v >> 7; r = (z80_u8)((v << 1) | (zF & Z80_CF)); break;     /* RL  */
    case 3:  c = v & 1;  r = (z80_u8)((v >> 1) | ((zF & Z80_CF) << 7)); break; /* RR */
    case 4:  c = v >> 7; r = (z80_u8)(v << 1); break;                       /* SLA */
    case 5:  c = v & 1;  r = (z80_u8)((v >> 1) | (v & 0x80)); break;        /* SRA */
    case 6:  c = v >> 7; r = (z80_u8)((v << 1) | 1); break;                 /* SLL (undoc) */
    default: c = v & 1;  r = (z80_u8)(v >> 1); break;                       /* SRL */
    }
    zF = (z80_u8)(z80_szp[r] | c);
    return r;
}
Z80_INL void z80_bit(z80_t *zp, int b, z80_u8 v, z80_u8 xy) {
    z80_u8 m = (z80_u8)(v & (1u << b));
    zF = (z80_u8)((zF & Z80_CF) | Z80_HF | (m ? (m & Z80_SF) : (Z80_ZF | Z80_PF)) | (xy & (Z80_XF | Z80_YF)));
}

Z80_INL int z80_cond(z80_t *zp, int cc) {
    switch (cc & 7) {
    case 0: return !(zF & Z80_ZF);
    case 1: return   zF & Z80_ZF;
    case 2: return !(zF & Z80_CF);
    case 3: return   zF & Z80_CF;
    case 4: return !(zF & Z80_PF);
    case 5: return   zF & Z80_PF;
    case 6: return !(zF & Z80_SF);
    default: return  zF & Z80_SF;
    }
}

/* 16-bit pair by the op's bits 5..4: BC DE HL SP (HL replaced by `hl` for DD/FD) */
Z80_INL z80_u16 z80_rp(z80_t *zp, int p, z80_u16 hl) {
    switch (p & 3) { case 0: return zBC; case 1: return zDE; case 2: return hl; default: return z80.sp; }
}

/* ---- ED page --------------------------------------------------------------- */
Z80_INL int z80_exec_ed(z80_t *zp) {
    z80_u8 op = z80_imm8(zp), v;
    z80_u16 t;
    switch (op) {
    case 0x40: case 0x48: case 0x50: case 0x58: case 0x60: case 0x68: case 0x70: case 0x78:
        v = z80_bus_in(zBC);                                   /* IN r,(C) */
        zF = (z80_u8)((zF & Z80_CF) | z80_szp[v]);
        if (op != 0x70) z80_setr(zp, op >> 3, v);
        z80.cycles -= 12; return -1;
    case 0x41: case 0x49: case 0x51: case 0x59: case 0x61: case 0x69: case 0x71: case 0x79:
        z80_bus_out(zBC, op == 0x71 ? 0 : z80_getr(zp, op >> 3)); /* OUT (C),r */
        z80.cycles -= 12; return -1;
    case 0x42: case 0x52: case 0x62: case 0x72:
        z80_sbc16(zp, z80_rp(zp, op >> 4, zHL)); z80.cycles -= 15; return -1;
    case 0x4a: case 0x5a: case 0x6a: case 0x7a:
        z80_adc16(zp, z80_rp(zp, op >> 4, zHL)); z80.cycles -= 15; return -1;
    case 0x43: case 0x53: case 0x63: case 0x73:
        z80_wr16(zp, z80_imm16(zp), z80_rp(zp, op >> 4, zHL)); z80.cycles -= 20; return -1;
    case 0x4b: case 0x5b: case 0x6b: case 0x7b:
        t = z80_rd16(zp, z80_imm16(zp));
        switch ((op >> 4) & 3) { case 0: zSET_BC(t); break; case 1: zSET_DE(t); break;
                                 case 2: zSET_HL(t); break; default: z80.sp = t; }
        z80.cycles -= 20; return -1;
    case 0x44: case 0x4c: case 0x54: case 0x5c: case 0x64: case 0x6c: case 0x74: case 0x7c:
        v = zA; zA = 0; z80_sub8(zp, v, 0); z80.cycles -= 8; return -1;        /* NEG */
    case 0x45: case 0x55: case 0x65: case 0x75: case 0x4d: case 0x5d: case 0x6d: case 0x7d:
        z80.iff1 = z80.iff2; z80.pc = z80_pop(zp); z80.cycles -= 14; return -2; /* RETN/RETI */
    case 0x46: case 0x4e: case 0x66: case 0x6e: z80.im = 0; z80.cycles -= 8; return -1;
    case 0x56: case 0x76: z80.im = 1; z80.cycles -= 8; return -1;
    case 0x5e: case 0x7e: z80.im = 2; z80.cycles -= 8; return -1;
    case 0x47: z80.i = zA; z80.cycles -= 9; return -1;
    case 0x4f: z80.rr = zA; z80.cycles -= 9; return -1;
    case 0x57: zA = z80.i;
        zF = (z80_u8)((zF & Z80_CF) | z80_sz[zA] | (z80.iff2 ? Z80_PF : 0)); z80.cycles -= 9; return -1;
    case 0x5f: zA = (z80_u8)((z80.rr & 0x80) | ((z80.rr + (z80.cycles >> 2)) & 0x7f)); /* R, approximated */
        zF = (z80_u8)((zF & Z80_CF) | z80_sz[zA] | (z80.iff2 ? Z80_PF : 0)); z80.cycles -= 9; return -1;
    case 0x67: v = z80_bus_rd(zHL);                                         /* RRD */
        z80_bus_wr(zHL, (z80_u8)((zA << 4) | (v >> 4)));
        zA = (z80_u8)((zA & 0xf0) | (v & 0x0f));
        zF = (z80_u8)((zF & Z80_CF) | z80_szp[zA]); z80.cycles -= 18; return -1;
    case 0x6f: v = z80_bus_rd(zHL);                                         /* RLD */
        z80_bus_wr(zHL, (z80_u8)((v << 4) | (zA & 0x0f)));
        zA = (z80_u8)((zA & 0xf0) | (v >> 4));
        zF = (z80_u8)((zF & Z80_CF) | z80_szp[zA]); z80.cycles -= 18; return -1;

    case 0xa0: case 0xb0: case 0xa8: case 0xb8: {                       /* LDI LDIR LDD LDDR */
        int d = (op & 8) ? -1 : 1;
        z80_u8 n;
        v = z80_bus_rd(zHL); z80_bus_wr(zDE, v);
        zSET_HL(zHL + d); zSET_DE(zDE + d); zSET_BC(zBC - 1);
        n = (z80_u8)(v + zA);
        zF = (z80_u8)((zF & (Z80_SF | Z80_ZF | Z80_CF)) | (zBC ? Z80_PF : 0)
                      | (n & Z80_XF) | ((n << 4) & Z80_YF));
        if ((op & 0x10) && zBC) { z80.pc -= 2; z80.cycles -= 21; } else z80.cycles -= 16;
        return -1;
    }
    case 0xa1: case 0xb1: case 0xa9: case 0xb9: {                       /* CPI CPIR CPD CPDR */
        int d = (op & 8) ? -1 : 1;
        z80_u8 r, n;
        v = z80_bus_rd(zHL); r = (z80_u8)(zA - v);
        zSET_HL(zHL + d); zSET_BC(zBC - 1);
        zF = (z80_u8)((zF & Z80_CF) | Z80_NF | (z80_sz[r] & ~(Z80_XF | Z80_YF))
                      | ((zA ^ v ^ r) & Z80_HF) | (zBC ? Z80_PF : 0));
        n = (z80_u8)(r - ((zF & Z80_HF) ? 1 : 0));
        zF |= (z80_u8)((n & Z80_XF) | ((n << 4) & Z80_YF));
        if ((op & 0x10) && zBC && r) { z80.pc -= 2; z80.cycles -= 21; } else z80.cycles -= 16;
        return -1;
    }
    case 0xa2: case 0xb2: case 0xaa: case 0xba: {                       /* INI INIR IND INDR */
        int d = (op & 8) ? -1 : 1;
        v = z80_bus_in(zBC); z80_bus_wr(zHL, v);
        zSET_HL(zHL + d); zB--;
        zF = (z80_u8)(Z80_NF | z80_sz[zB]);
        if ((op & 0x10) && zB) { z80.pc -= 2; z80.cycles -= 21; } else z80.cycles -= 16;
        return -1;
    }
    case 0xa3: case 0xb3: case 0xab: case 0xbb: {                       /* OUTI OTIR OUTD OTDR */
        int d = (op & 8) ? -1 : 1;
        v = z80_bus_rd(zHL); zB--; z80_bus_out(zBC, v);
        zSET_HL(zHL + d);
        zF = (z80_u8)(Z80_NF | z80_sz[zB]);
        if ((op & 0x10) && zB) { z80.pc -= 2; z80.cycles -= 21; } else z80.cycles -= 16;
        return -1;
    }
    default: z80.cycles -= 8; return -1;                                   /* ED NOP */
    }
}

/* ---- CB page (plain, on B..A and (HL)) -------------------------------------- */
Z80_INL void z80_exec_cb(z80_t *zp) {
    z80_u8 op = z80_imm8(zp), v;
    int reg = op & 7, b = (op >> 3) & 7;
    v = (reg == 6) ? z80_bus_rd(zHL) : z80_getr(zp, reg);
    switch (op >> 6) {
    case 0: v = z80_rot(zp, op, v); break;
    case 1: z80_bit(zp, b, v, reg == 6 ? (z80_u8)(zHL >> 8) : v);
            z80.cycles -= (reg == 6) ? 12 : 8; return;
    case 2: v = (z80_u8)(v & ~(1u << b)); break;
    default: v = (z80_u8)(v | (1u << b)); break;
    }
    if (reg == 6) { z80_bus_wr(zHL, v); z80.cycles -= 15; }
    else          { z80_setr(zp, reg, v); z80.cycles -= 8; }
}

/* ---- DD/FD page: `xy` is IX or IY ------------------------------------------ */
Z80_INL int z80_exec_xy(z80_t *zp, z80_u16 *xy) {
    z80_u8 op = z80_imm8(zp), v;
    z80_u16 a;
#define XH ((z80_u8)(*xy >> 8))
#define XL ((z80_u8)*xy)
#define SET_XH(v_) (*xy = (z80_u16)((*xy & 0x00ff) | ((v_) << 8)))
#define SET_XL(v_) (*xy = (z80_u16)((*xy & 0xff00) | (z80_u8)(v_)))
#define XD() ((z80_u16)(*xy + (signed char)z80_imm8(zp)))
    switch (op) {
    case 0x09: case 0x19: case 0x29: case 0x39:
        *xy = z80_add16(zp, *xy, z80_rp(zp, op >> 4, *xy)); z80.cycles -= 15; return -1;
    case 0x21: *xy = z80_imm16(zp); z80.cycles -= 14; return -1;
    case 0x22: z80_wr16(zp, z80_imm16(zp), *xy); z80.cycles -= 20; return -1;
    case 0x2a: *xy = z80_rd16(zp, z80_imm16(zp)); z80.cycles -= 20; return -1;
    case 0x23: (*xy)++; z80.cycles -= 10; return -1;
    case 0x2b: (*xy)--; z80.cycles -= 10; return -1;
    case 0x24: SET_XH(z80_inc8(zp, XH)); z80.cycles -= 8; return -1;
    case 0x25: SET_XH(z80_dec8(zp, XH)); z80.cycles -= 8; return -1;
    case 0x26: SET_XH(z80_imm8(zp)); z80.cycles -= 11; return -1;
    case 0x2c: SET_XL(z80_inc8(zp, XL)); z80.cycles -= 8; return -1;
    case 0x2d: SET_XL(z80_dec8(zp, XL)); z80.cycles -= 8; return -1;
    case 0x2e: SET_XL(z80_imm8(zp)); z80.cycles -= 11; return -1;
    case 0x34: a = XD(); z80_bus_wr(a, z80_inc8(zp, z80_bus_rd(a))); z80.cycles -= 23; return -1;
    case 0x35: a = XD(); z80_bus_wr(a, z80_dec8(zp, z80_bus_rd(a))); z80.cycles -= 23; return -1;
    case 0x36: a = XD(); z80_bus_wr(a, z80_imm8(zp)); z80.cycles -= 19; return -1;
    case 0xe1: *xy = z80_pop(zp); z80.cycles -= 14; return -1;
    case 0xe5: z80_push(zp, *xy); z80.cycles -= 15; return -1;
    case 0xe3: a = z80_rd16(zp, z80.sp); z80_wr16(zp, z80.sp, *xy); *xy = a; z80.cycles -= 23; return -1;
    case 0xe9: z80.pc = *xy; z80.cycles -= 8; return -1;
    case 0xf9: z80.sp = *xy; z80.cycles -= 10; return -1;
    case 0xcb: {                                          /* DDCB d op */
        z80_u8 cop;
        int reg, b;
        a = XD(); cop = z80_imm8(zp); reg = cop & 7; b = (cop >> 3) & 7;
        v = z80_bus_rd(a);
        switch (cop >> 6) {
        case 0: v = z80_rot(zp, cop, v); break;
        case 1: z80_bit(zp, b, v, (z80_u8)(a >> 8)); z80.cycles -= 20; return -1;
        case 2: v = (z80_u8)(v & ~(1u << b)); break;
        default: v = (z80_u8)(v | (1u << b)); break;
        }
        z80_bus_wr(a, v);
        if (reg != 6) z80_setr(zp, reg, v);               /* undocumented copy to reg */
        z80.cycles -= 23; return -1;
    }
    default: break;
    }
    if (op >= 0x40 && op < 0x80 && op != 0x76) {          /* LD r,r' with IXH/IXL/(IX+d) */
        int dst = (op >> 3) & 7, src = op & 7;
        if (src == 6) { v = z80_bus_rd(XD()); z80_setr(zp, dst, v); z80.cycles -= 19; return -1; }
        if (dst == 6) { a = XD(); z80_bus_wr(a, z80_getr(zp, src)); z80.cycles -= 19; return -1; }
        v = (src == 4) ? XH : (src == 5) ? XL : z80_getr(zp, src);
        if (dst == 4) SET_XH(v); else if (dst == 5) SET_XL(v); else z80_setr(zp, dst, v);
        z80.cycles -= 8; return -1;
    }
    if (op >= 0x80 && op < 0xc0) {                         /* ALU A,IXH/IXL/(IX+d) */
        int src = op & 7;
        if (src == 6) { z80_alu(zp, op >> 3, z80_bus_rd(XD())); z80.cycles -= 19; return -1; }
        v = (src == 4) ? XH : (src == 5) ? XL : z80_getr(zp, src);
        z80_alu(zp, op >> 3, v); z80.cycles -= 8; return -1;
    }
#undef XH
#undef XL
#undef SET_XH
#undef SET_XL
#undef XD
    z80.cycles -= 4;                                       /* prefix ignored */
    return op;
}

/* ---- main page ---------------------------------------------------------------
 * Returns -1, -2 after an op that changed the interrupt state (EI, HALT, RETN/RETI: the
 * run loop re-checks it), or an opcode the caller must run next: DD/FD followed by an
 * opcode that doesn't use HL (the prefix only costs its 4 T-states). */
Z80_INL int z80_exec_main(z80_t *zp, z80_u8 op) {
    z80_u8 v;
    z80_u16 a;
    switch (op) {
    /* LD r,r' and ALU A,r: one case per opcode with constant operands, so each compiles to
     * a couple of loads/stores instead of a runtime decode of the register fields */
#define Z80_LD(o) case (o): \
        if (((o) & 7) == 6 && (((o) >> 3) & 7) == 6) { z80.halted = 1; z80.cycles -= 4; return -2; } /* HALT */ \
        else if (((o) & 7) == 6) { z80.r[((o) >> 3) & 7] = z80_bus_rd(zHL); z80.cycles -= 7; } \
        else if ((((o) >> 3) & 7) == 6) { z80_bus_wr(zHL, z80.r[(o) & 7]); z80.cycles -= 7; } \
        else { z80.r[((o) >> 3) & 7] = z80.r[(o) & 7]; z80.cycles -= 4; } \
        return -1;
#define Z80_ALU(o) case (o): \
        if (((o) & 7) == 6) { z80_alu(zp, ((o) >> 3) & 7, z80_bus_rd(zHL)); z80.cycles -= 7; } \
        else { z80_alu(zp, ((o) >> 3) & 7, z80.r[(o) & 7]); z80.cycles -= 4; } \
        return -1;
#define Z80_X8(m, b) m(b) m(b + 1) m(b + 2) m(b + 3) m(b + 4) m(b + 5) m(b + 6) m(b + 7)
    Z80_X8(Z80_LD, 0x40) Z80_X8(Z80_LD, 0x48) Z80_X8(Z80_LD, 0x50) Z80_X8(Z80_LD, 0x58)
    Z80_X8(Z80_LD, 0x60) Z80_X8(Z80_LD, 0x68) Z80_X8(Z80_LD, 0x70) Z80_X8(Z80_LD, 0x78)
    Z80_X8(Z80_ALU, 0x80) Z80_X8(Z80_ALU, 0x88) Z80_X8(Z80_ALU, 0x90) Z80_X8(Z80_ALU, 0x98)
    Z80_X8(Z80_ALU, 0xa0) Z80_X8(Z80_ALU, 0xa8) Z80_X8(Z80_ALU, 0xb0) Z80_X8(Z80_ALU, 0xb8)
#undef Z80_LD
#undef Z80_ALU
#undef Z80_X8
    case 0x00: z80.cycles -= 4; return -1;
    case 0x01: a = z80_imm16(zp); zSET_BC(a); z80.cycles -= 10; return -1;
    case 0x11: a = z80_imm16(zp); zSET_DE(a); z80.cycles -= 10; return -1;
    case 0x21: a = z80_imm16(zp); zSET_HL(a); z80.cycles -= 10; return -1;
    case 0x31: z80.sp = z80_imm16(zp); z80.cycles -= 10; return -1;
    case 0x02: z80_bus_wr(zBC, zA); z80.cycles -= 7; return -1;
    case 0x12: z80_bus_wr(zDE, zA); z80.cycles -= 7; return -1;
    case 0x0a: zA = z80_bus_rd(zBC); z80.cycles -= 7; return -1;
    case 0x1a: zA = z80_bus_rd(zDE); z80.cycles -= 7; return -1;
    case 0x22: z80_wr16(zp, z80_imm16(zp), zHL); z80.cycles -= 16; return -1;
    case 0x2a: a = z80_rd16(zp, z80_imm16(zp)); zSET_HL(a); z80.cycles -= 16; return -1;
    case 0x32: z80_bus_wr(z80_imm16(zp), zA); z80.cycles -= 13; return -1;
    case 0x3a: zA = z80_bus_rd(z80_imm16(zp)); z80.cycles -= 13; return -1;
    case 0x03: zSET_BC(zBC + 1); z80.cycles -= 6; return -1;
    case 0x13: zSET_DE(zDE + 1); z80.cycles -= 6; return -1;
    case 0x23: zSET_HL(zHL + 1); z80.cycles -= 6; return -1;
    case 0x33: z80.sp++; z80.cycles -= 6; return -1;
    case 0x0b: zSET_BC(zBC - 1); z80.cycles -= 6; return -1;
    case 0x1b: zSET_DE(zDE - 1); z80.cycles -= 6; return -1;
    case 0x2b: zSET_HL(zHL - 1); z80.cycles -= 6; return -1;
    case 0x3b: z80.sp--; z80.cycles -= 6; return -1;
    case 0x09: case 0x19: case 0x29: case 0x39:
        a = z80_add16(zp, zHL, z80_rp(zp, op >> 4, zHL)); zSET_HL(a); z80.cycles -= 11; return -1;
#define Z80_INCDEC(o) \
    case (o):     z80.r[((o) >> 3) & 7] = z80_inc8(zp, z80.r[((o) >> 3) & 7]); z80.cycles -= 4; return -1; \
    case (o) + 1: z80.r[((o) >> 3) & 7] = z80_dec8(zp, z80.r[((o) >> 3) & 7]); z80.cycles -= 4; return -1; \
    case (o) + 2: z80.r[((o) >> 3) & 7] = z80_imm8(zp); z80.cycles -= 7; return -1;
    Z80_INCDEC(0x04) Z80_INCDEC(0x0c) Z80_INCDEC(0x14) Z80_INCDEC(0x1c)
    Z80_INCDEC(0x24) Z80_INCDEC(0x2c) Z80_INCDEC(0x3c)
#undef Z80_INCDEC
    case 0x34: z80_bus_wr(zHL, z80_inc8(zp, z80_bus_rd(zHL))); z80.cycles -= 11; return -1;
    case 0x35: z80_bus_wr(zHL, z80_dec8(zp, z80_bus_rd(zHL))); z80.cycles -= 11; return -1;
    case 0x36: z80_bus_wr(zHL, z80_imm8(zp)); z80.cycles -= 10; return -1;
    case 0x07: zA = (z80_u8)((zA << 1) | (zA >> 7));
        zF = (z80_u8)((zF & (Z80_SF | Z80_ZF | Z80_PF)) | (zA & (Z80_XF | Z80_YF | Z80_CF)));
        z80.cycles -= 4; return -1;
    case 0x0f: zF = (z80_u8)((zF & (Z80_SF | Z80_ZF | Z80_PF)) | (zA & Z80_CF));
        zA = (z80_u8)((zA >> 1) | (zA << 7)); zF |= (z80_u8)(zA & (Z80_XF | Z80_YF));
        z80.cycles -= 4; return -1;
    case 0x17: v = zA >> 7; zA = (z80_u8)((zA << 1) | (zF & Z80_CF));
        zF = (z80_u8)((zF & (Z80_SF | Z80_ZF | Z80_PF)) | (zA & (Z80_XF | Z80_YF)) | v);
        z80.cycles -= 4; return -1;
    case 0x1f: v = zA & 1; zA = (z80_u8)((zA >> 1) | ((zF & Z80_CF) << 7));
        zF = (z80_u8)((zF & (Z80_SF | Z80_ZF | Z80_PF)) | (zA & (Z80_XF | Z80_YF)) | v);
        z80.cycles -= 4; return -1;
    case 0x27: {                                           /* DAA */
        z80_u8 c = zF & Z80_CF, n = zF & Z80_NF, diff = 0, a0 = zA;
        if ((zF & Z80_HF) || (zA & 0x0f) > 9) diff = 6;
        if (c || zA > 0x99) { diff |= 0x60; c = Z80_CF; }
        zA = n ? (z80_u8)(zA - diff) : (z80_u8)(zA + diff);
        zF = (z80_u8)(z80_szp[zA] | c | n | ((a0 ^ zA) & Z80_HF));
        z80.cycles -= 4; return -1;
    }
    case 0x2f: zA ^= 0xff;
        zF = (z80_u8)((zF & (Z80_SF | Z80_ZF | Z80_PF | Z80_CF)) | Z80_HF | Z80_NF | (zA & (Z80_XF | Z80_YF)));
        z80.cycles -= 4; return -1;
    case 0x37: zF = (z80_u8)((zF & (Z80_SF | Z80_ZF | Z80_PF)) | Z80_CF | (zA & (Z80_XF | Z80_YF)));
        z80.cycles -= 4; return -1;
    case 0x3f: zF = (z80_u8)(((zF & (Z80_SF | Z80_ZF | Z80_PF | Z80_CF)) | ((zF & Z80_CF) << 4)
                              | (zA & (Z80_XF | Z80_YF))) ^ Z80_CF);
        z80.cycles -= 4; return -1;
    case 0x08: v = zF; zF = z80.alt[Z80_F]; z80.alt[Z80_F] = v;
        v = zA; zA = z80.alt[Z80_A]; z80.alt[Z80_A] = v;
        z80.cycles -= 4; return -1;                           /* EX AF,AF' */
    case 0xd9:
#define Z80_SWAP(i) v = z80.r[i]; z80.r[i] = z80.alt[i]; z80.alt[i] = v;
        Z80_SWAP(Z80_B) Z80_SWAP(Z80_C) Z80_SWAP(Z80_D) Z80_SWAP(Z80_E) Z80_SWAP(Z80_H) Z80_SWAP(Z80_L)
#undef Z80_SWAP
        z80.cycles -= 4; return -1;                           /* EXX */
    case 0xeb: v = zD; zD = zH; zH = v; v = zE; zE = zL; zL = v; z80.cycles -= 4; return -1;
    case 0xe3: a = z80_rd16(zp, z80.sp); z80_wr16(zp, z80.sp, zHL); zSET_HL(a); z80.cycles -= 19; return -1;
    case 0x10: v = z80_imm8(zp);                             /* DJNZ */
        if (--zB) { z80.pc = (z80_u16)(z80.pc + (signed char)v); z80.cycles -= 13; } else z80.cycles -= 8;
        return -1;
    case 0x18: v = z80_imm8(zp); z80.pc = (z80_u16)(z80.pc + (signed char)v); z80.cycles -= 12; return -1;
    case 0x20: case 0x28: case 0x30: case 0x38:
        v = z80_imm8(zp);
        if (z80_cond(zp, (op >> 3) & 3)) { z80.pc = (z80_u16)(z80.pc + (signed char)v); z80.cycles -= 12; }
        else z80.cycles -= 7;
        return -1;
    case 0xc3: z80.pc = z80_imm16(zp); z80.cycles -= 10; return -1;
    case 0xc2: case 0xca: case 0xd2: case 0xda: case 0xe2: case 0xea: case 0xf2: case 0xfa:
        a = z80_imm16(zp);
        if (z80_cond(zp, op >> 3)) { z80.pc = a; z80.cycles -= 10; Z80_JP_TAKEN(a); }
        else z80.cycles -= 10;
        return -1;
    case 0xe9: z80.pc = zHL; z80.cycles -= 4; return -1;
    case 0xcd: a = z80_imm16(zp); z80_push(zp, z80.pc); z80.pc = a; z80.cycles -= 17; return -1;
    case 0xc4: case 0xcc: case 0xd4: case 0xdc: case 0xe4: case 0xec: case 0xf4: case 0xfc:
        a = z80_imm16(zp);
        if (z80_cond(zp, op >> 3)) { z80_push(zp, z80.pc); z80.pc = a; z80.cycles -= 17; } else z80.cycles -= 10;
        return -1;
    case 0xc9: z80.pc = z80_pop(zp); z80.cycles -= 10; return -1;
    case 0xc0: case 0xc8: case 0xd0: case 0xd8: case 0xe0: case 0xe8: case 0xf0: case 0xf8:
        if (z80_cond(zp, op >> 3)) { z80.pc = z80_pop(zp); z80.cycles -= 11; } else z80.cycles -= 5;
        return -1;
    case 0xc7: case 0xcf: case 0xd7: case 0xdf: case 0xe7: case 0xef: case 0xf7: case 0xff:
        z80_push(zp, z80.pc); z80.pc = (z80_u16)(op & 0x38); z80.cycles -= 11; return -1;
    case 0xc1: a = z80_pop(zp); zSET_BC(a); z80.cycles -= 10; return -1;
    case 0xd1: a = z80_pop(zp); zSET_DE(a); z80.cycles -= 10; return -1;
    case 0xe1: a = z80_pop(zp); zSET_HL(a); z80.cycles -= 10; return -1;
    case 0xf1: a = z80_pop(zp); zA = (z80_u8)(a >> 8); zF = (z80_u8)a; z80.cycles -= 10; return -1;
    case 0xc5: z80_push(zp, zBC); z80.cycles -= 11; return -1;
    case 0xd5: z80_push(zp, zDE); z80.cycles -= 11; return -1;
    case 0xe5: z80_push(zp, zHL); z80.cycles -= 11; return -1;
    case 0xf5: z80_push(zp, (z80_u16)((zA << 8) | zF)); z80.cycles -= 11; return -1;
    case 0xc6: case 0xce: case 0xd6: case 0xde: case 0xe6: case 0xee: case 0xf6: case 0xfe:
        z80_alu(zp, op >> 3, z80_imm8(zp)); z80.cycles -= 7; return -1;
    case 0xd3: v = z80_imm8(zp); z80_bus_out((z80_u16)((zA << 8) | v), zA); z80.cycles -= 11; return -1;
    case 0xdb: v = z80_imm8(zp); zA = z80_bus_in((z80_u16)((zA << 8) | v)); z80.cycles -= 11; return -1;
    case 0xf9: z80.sp = zHL; z80.cycles -= 6; return -1;
    case 0xf3: z80.iff1 = z80.iff2 = 0; z80.cycles -= 4; return -1;
    case 0xfb: z80.iff1 = z80.iff2 = 1; z80.ei_delay = 1; z80.cycles -= 4; return -2;
    case 0xcb: z80_exec_cb(zp); return -1;
    case 0xed: return z80_exec_ed(zp);
    case 0xdd: return z80_exec_xy(zp, &z80.ix);
    case 0xfd: return z80_exec_xy(zp, &z80.iy);
    default: z80.cycles -= 4; return -1;
    }
}

/* Accept a pending maskable interrupt (the board has set irq_line, and irq_vec for
 * IM 0/2). The line is level-held: the board clears it (e.g. Pac-Man's IRQ-enable latch)
 * — z80_run also drops it on acknowledge so a board that never clears it can't storm. */
Z80_INL void z80_take_irq(z80_t *zp) {
    z80.halted = 0;
    z80.iff1 = z80.iff2 = 0;
    z80.irq_line = 0;
    switch (z80.im) {
    case 2: z80_push(zp, z80.pc);
        z80.pc = z80_rd16(zp, (z80_u16)((z80.i << 8) | (z80.irq_vec & 0xfe)));
        z80.cycles -= 19; break;
    case 1: z80_push(zp, z80.pc); z80.pc = 0x38; z80.cycles -= 13; break;
    default: z80_push(zp, z80.pc); z80.pc = (z80_u16)(z80.irq_vec & 0x38); z80.cycles -= 13; break; /* RST only */
    }
}

#undef z80

/* Run for (at least) `cycles` T-states. The interrupt/HALT/EI state only changes between
 * slices (the board raises irq_line) or on the ops that return -2, so the inner loop
 * only tests the cycle count. */
static void z80_run(int cycles) {
    z80_t s = z80, *zp = &s;
    int op;
    s.cycles += cycles;
    while (s.cycles > 0) {
        if (s.events) {
            if (s.irq_line && s.iff1 && !s.ei_delay) { z80_take_irq(zp); continue; }
            if (s.halted) {                                /* burn the rest in HALT NOPs */
                if (s.irq_line && s.iff1) continue;
                s.cycles = 0; break;
            }
            if (s.ei_delay) {                              /* one op after EI, then look again */
                s.ei_delay = 0;
                op = z80_imm8(zp);
                do op = z80_exec_main(zp, (z80_u8)op); while (op >= 0);
                continue;
            }
        }
        do {
            op = z80_imm8(zp);
            do op = z80_exec_main(zp, (z80_u8)op); while (op >= 0);
        } while (op == -1 && s.cycles > 0);
    }
    z80 = s;
}

#endif /* M2_Z80_H */
