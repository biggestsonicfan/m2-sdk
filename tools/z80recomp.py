#!/usr/bin/env python3
"""z80recomp.py <rom.h> <trace.txt> <out.h> [--func z80_run_rc]

Static recompiler for a Z80 program in ROM, for src/m2_z80.h. Writes a run function that
replaces z80_run: a switch with one case per instruction address in the trace
(tools/pactrace.c prints them), in address order. Each case is that instruction
translated to C with its operands and the PC as constants, using the core's own inline
helpers (z80_alu, z80_inc8, z80_add16, z80_rot, z80_bit, z80_push, ...) and its cycle
counts, so flags and timing are the interpreter's. A case falls through to the next
traced address when the PC lands there; anything else goes back to the switch, whose
default is the interpreter. Forms not translated here (DAA, EI, HALT, the exchanges, the
block moves, IXH/IXL, rare ED ops) call one out-of-line interpreter step. So the trace
and this translator only decide speed, never behaviour.

<rom.h> is the ROM header the program is built with (the pac_rom[] array is read here
to decode instructions; it is not copied into the output). Include the output AFTER
m2_z80.h and the bus hooks. It is derived from the game ROM: keep it out of git.
"""
import argparse, os, re

R8 = {0: 'zB', 1: 'zC', 2: 'zD', 3: 'zE', 4: 'zH', 5: 'zL', 7: 'zA'}
RP_GET = {0: 'zBC', 1: 'zDE', 2: 'zHL', 3: 'z80.sp'}
RP_SET = {0: 'zSET_BC(%s);', 1: 'zSET_DE(%s);', 2: 'zSET_HL(%s);', 3: 'z80.sp = %s;'}
PUSH = {0: 'zBC', 1: 'zDE', 2: 'zHL', 3: '(z80_u16)((zA << 8) | zF)'}
POP = {0: 'zSET_BC(a);', 1: 'zSET_DE(a);', 2: 'zSET_HL(a);', 3: 'zA = (z80_u8)(a >> 8); zF = (z80_u8)a;'}


def s8(b):
    return b - 256 if b >= 128 else b


def translate(rom, p):
    """-> (length or None, C code or None). None code = use the interpreter step."""
    rd = lambda o: rom[(p + o) & 0x3fff]
    op = rd(0)
    n = rd(1)
    nn = rd(1) | rd(2) << 8

    def done(length, body, cyc):
        return length, 'z80.pc = 0x%04x; %s z80.cycles -= %d;' % ((p + length) & 0xffff, body, cyc)

    # ---------------------------------------------------------------- main page
    if op == 0x00: return done(1, '', 4)
    if 0x40 <= op < 0x80 and op != 0x76:
        d, s_ = (op >> 3) & 7, op & 7
        if s_ == 6: return done(1, '%s = z80_bus_rd(zHL);' % R8[d], 7)
        if d == 6: return done(1, 'z80_bus_wr(zHL, %s);' % R8[s_], 7)
        return done(1, '%s = %s;' % (R8[d], R8[s_]), 4)
    if 0x80 <= op < 0xc0:
        s_ = op & 7
        if s_ == 6: return done(1, 'z80_alu(zp, %d, z80_bus_rd(zHL));' % ((op >> 3) & 7), 7)
        return done(1, 'z80_alu(zp, %d, %s);' % ((op >> 3) & 7, R8[s_]), 4)
    if (op & 0xcf) == 0x01: return done(3, RP_SET[op >> 4] % ('0x%04x' % nn), 10)
    if op == 0x02: return done(1, 'z80_bus_wr(zBC, zA);', 7)
    if op == 0x12: return done(1, 'z80_bus_wr(zDE, zA);', 7)
    if op == 0x0a: return done(1, 'zA = z80_bus_rd(zBC);', 7)
    if op == 0x1a: return done(1, 'zA = z80_bus_rd(zDE);', 7)
    if op == 0x22: return done(3, 'z80_wr16(zp, 0x%04x, zHL);' % nn, 16)
    if op == 0x2a: return done(3, 'a = z80_rd16(zp, 0x%04x); zSET_HL(a);' % nn, 16)
    if op == 0x32: return done(3, 'z80_bus_wr(0x%04x, zA);' % nn, 13)
    if op == 0x3a: return done(3, 'zA = z80_bus_rd(0x%04x);' % nn, 13)
    if (op & 0xcf) == 0x03: return done(1, RP_SET[op >> 4] % ('(z80_u16)(%s + 1)' % RP_GET[op >> 4]), 6)
    if (op & 0xcf) == 0x0b: return done(1, RP_SET[op >> 4] % ('(z80_u16)(%s - 1)' % RP_GET[op >> 4]), 6)
    if (op & 0xcf) == 0x09: return done(1, 'a = z80_add16(zp, zHL, %s); zSET_HL(a);' % RP_GET[op >> 4], 11)
    if (op & 0xc7) == 0x04 and op != 0x34:
        r = R8[(op >> 3) & 7]; return done(1, '%s = z80_inc8(zp, %s);' % (r, r), 4)
    if (op & 0xc7) == 0x05 and op != 0x35:
        r = R8[(op >> 3) & 7]; return done(1, '%s = z80_dec8(zp, %s);' % (r, r), 4)
    if (op & 0xc7) == 0x06 and op != 0x36: return done(2, '%s = 0x%02x;' % (R8[(op >> 3) & 7], n), 7)
    if op == 0x34: return done(1, 'z80_bus_wr(zHL, z80_inc8(zp, z80_bus_rd(zHL)));', 11)
    if op == 0x35: return done(1, 'z80_bus_wr(zHL, z80_dec8(zp, z80_bus_rd(zHL)));', 11)
    if op == 0x36: return done(2, 'z80_bus_wr(zHL, 0x%02x);' % n, 10)
    if op == 0x07: return done(1, 'zA = (z80_u8)((zA << 1) | (zA >> 7)); '
                               'zF = (z80_u8)((zF & (Z80_SF | Z80_ZF | Z80_PF)) | (zA & (Z80_XF | Z80_YF | Z80_CF)));', 4)
    if op == 0x0f: return done(1, 'zF = (z80_u8)((zF & (Z80_SF | Z80_ZF | Z80_PF)) | (zA & Z80_CF)); '
                               'zA = (z80_u8)((zA >> 1) | (zA << 7)); zF |= (z80_u8)(zA & (Z80_XF | Z80_YF));', 4)
    if op == 0x17: return done(1, 'v = zA >> 7; zA = (z80_u8)((zA << 1) | (zF & Z80_CF)); '
                               'zF = (z80_u8)((zF & (Z80_SF | Z80_ZF | Z80_PF)) | (zA & (Z80_XF | Z80_YF)) | v);', 4)
    if op == 0x1f: return done(1, 'v = zA & 1; zA = (z80_u8)((zA >> 1) | ((zF & Z80_CF) << 7)); '
                               'zF = (z80_u8)((zF & (Z80_SF | Z80_ZF | Z80_PF)) | (zA & (Z80_XF | Z80_YF)) | v);', 4)
    if op == 0x2f: return done(1, 'zA ^= 0xff; zF = (z80_u8)((zF & (Z80_SF | Z80_ZF | Z80_PF | Z80_CF)) '
                               '| Z80_HF | Z80_NF | (zA & (Z80_XF | Z80_YF)));', 4)
    if op == 0x37: return done(1, 'zF = (z80_u8)((zF & (Z80_SF | Z80_ZF | Z80_PF)) | Z80_CF | (zA & (Z80_XF | Z80_YF)));', 4)
    if op == 0x3f: return done(1, 'zF = (z80_u8)(((zF & (Z80_SF | Z80_ZF | Z80_PF | Z80_CF)) | ((zF & Z80_CF) << 4) '
                               '| (zA & (Z80_XF | Z80_YF))) ^ Z80_CF);', 4)
    if op == 0xeb: return done(1, 'v = zD; zD = zH; zH = v; v = zE; zE = zL; zL = v;', 4)
    if op == 0x10:
        t = (p + 2 + s8(n)) & 0xffff
        return 2, ('if (--zB) { z80.pc = 0x%04x; z80.cycles -= 13; } else { z80.pc = 0x%04x; z80.cycles -= 8; }'
                   % (t, (p + 2) & 0xffff))
    if op == 0x18: return 2, 'z80.pc = 0x%04x; z80.cycles -= 12;' % ((p + 2 + s8(n)) & 0xffff)
    if op in (0x20, 0x28, 0x30, 0x38):
        t = (p + 2 + s8(n)) & 0xffff
        return 2, ('if (z80_cond(zp, %d)) { z80.pc = 0x%04x; z80.cycles -= 12; } else { z80.pc = 0x%04x; z80.cycles -= 7; }'
                   % ((op >> 3) & 3, t, (p + 2) & 0xffff))
    if op == 0xc3: return 3, 'z80.pc = 0x%04x; z80.cycles -= 10;' % nn
    if (op & 0xc7) == 0xc2:
        return 3, ('z80.cycles -= 10; if (z80_cond(zp, %d)) { z80.pc = 0x%04x; Z80_JP_TAKEN(0x%04x); } else z80.pc = 0x%04x;'
                   % ((op >> 3) & 7, nn, nn, (p + 3) & 0xffff))
    if op == 0xe9: return 1, 'z80.pc = zHL; z80.cycles -= 4;'
    if op == 0xcd: return 3, 'z80_push(zp, 0x%04x); z80.pc = 0x%04x; z80.cycles -= 17;' % ((p + 3) & 0xffff, nn)
    if (op & 0xc7) == 0xc4:
        return 3, ('if (z80_cond(zp, %d)) { z80_push(zp, 0x%04x); z80.pc = 0x%04x; z80.cycles -= 17; } '
                   'else { z80.pc = 0x%04x; z80.cycles -= 10; }' % ((op >> 3) & 7, (p + 3) & 0xffff, nn, (p + 3) & 0xffff))
    if op == 0xc9: return 1, 'z80.pc = z80_pop(zp); z80.cycles -= 10;'
    if (op & 0xc7) == 0xc0:
        return 1, ('if (z80_cond(zp, %d)) { z80.pc = z80_pop(zp); z80.cycles -= 11; } '
                   'else { z80.pc = 0x%04x; z80.cycles -= 5; }' % ((op >> 3) & 7, (p + 1) & 0xffff))
    if (op & 0xc7) == 0xc7: return 1, 'z80_push(zp, 0x%04x); z80.pc = 0x%04x; z80.cycles -= 11;' % ((p + 1) & 0xffff, op & 0x38)
    if (op & 0xcf) == 0xc1: return done(1, 'a = z80_pop(zp); ' + POP[(op >> 4) & 3], 10)
    if (op & 0xcf) == 0xc5: return done(1, 'z80_push(zp, %s);' % PUSH[(op >> 4) & 3], 11)
    if (op & 0xc7) == 0xc6: return done(2, 'z80_alu(zp, %d, 0x%02x);' % ((op >> 3) & 7, n), 7)
    if op == 0xd3: return done(2, 'z80_bus_out((z80_u16)((zA << 8) | 0x%02x), zA);' % n, 11)
    if op == 0xdb: return done(2, 'zA = z80_bus_in((z80_u16)((zA << 8) | 0x%02x));' % n, 11)
    if op == 0xf9: return done(1, 'z80.sp = zHL;', 6)
    if op == 0xf3: return done(1, 'z80.iff1 = z80.iff2 = 0;', 4)

    # ---------------------------------------------------------------- CB page
    if op == 0xcb:
        c = n; reg, b = c & 7, (c >> 3) & 7
        src = 'z80_bus_rd(zHL)' if reg == 6 else R8[reg]
        if (c >> 6) == 1:
            xy = '(z80_u8)(zHL >> 8)' if reg == 6 else 'v'
            return done(2, 'v = %s; z80_bit(zp, %d, v, %s);' % (src, b, xy), 12 if reg == 6 else 8)
        expr = {0: 'z80_rot(zp, 0x%02x, v)' % c, 2: '(z80_u8)(v & ~%du)' % (1 << b),
                3: '(z80_u8)(v | %du)' % (1 << b)}[c >> 6]
        if reg == 6: return done(2, 'v = %s; v = %s; z80_bus_wr(zHL, v);' % (src, expr), 15)
        return done(2, 'v = %s; %s = %s;' % (src, R8[reg], expr.replace('v', 'v')), 8)

    # ---------------------------------------------------------------- ED page
    if op == 0xed:
        e = n
        if (e & 0xc7) == 0x42:
            fn = 'z80_adc16' if e & 8 else 'z80_sbc16'
            rp = 'zHL' if ((e >> 4) & 3) == 2 else RP_GET[(e >> 4) & 3]
            return done(2, '%s(zp, %s);' % (fn, rp), 15)
        if (e & 0xc7) == 0x43:
            a16 = rd(2) | rd(3) << 8
            if e & 8: return done(4, 'a = z80_rd16(zp, 0x%04x); %s' % (a16, RP_SET[(e >> 4) & 3] % 'a'), 20)
            return done(4, 'z80_wr16(zp, 0x%04x, %s);' % (a16, RP_GET[(e >> 4) & 3]), 20)
        if (e & 0xc7) == 0x44: return done(2, 'v = zA; zA = 0; z80_sub8(zp, v, 0);', 8)
        if e == 0x47: return done(2, 'z80.i = zA;', 9)
        if e in (0x46, 0x4e, 0x66, 0x6e): return done(2, 'z80.im = 0;', 8)
        if e in (0x56, 0x76): return done(2, 'z80.im = 1;', 8)
        if e in (0x5e, 0x7e): return done(2, 'z80.im = 2;', 8)
        return None, None

    # ---------------------------------------------------------------- DD / FD
    if op in (0xdd, 0xfd):
        X = 'z80.ix' if op == 0xdd else 'z80.iy'
        x = n; d = s8(rd(2))
        XD = '(z80_u16)(%s + %d)' % (X, d)
        if (x & 0xcf) == 0x09:
            rp = X if ((x >> 4) & 3) == 2 else RP_GET[(x >> 4) & 3]
            return done(2, '%s = z80_add16(zp, %s, %s);' % (X, X, rp), 15)
        if x == 0x21: return done(4, '%s = 0x%04x;' % (X, rd(2) | rd(3) << 8), 14)
        if x == 0x22: return done(4, 'z80_wr16(zp, 0x%04x, %s);' % (rd(2) | rd(3) << 8, X), 20)
        if x == 0x2a: return done(4, '%s = z80_rd16(zp, 0x%04x);' % (X, rd(2) | rd(3) << 8), 20)
        if x == 0x23: return done(2, '%s++;' % X, 10)
        if x == 0x2b: return done(2, '%s--;' % X, 10)
        if x == 0x34: return done(3, 'a = %s; z80_bus_wr(a, z80_inc8(zp, z80_bus_rd(a)));' % XD, 23)
        if x == 0x35: return done(3, 'a = %s; z80_bus_wr(a, z80_dec8(zp, z80_bus_rd(a)));' % XD, 23)
        if x == 0x36: return done(4, 'z80_bus_wr(%s, 0x%02x);' % (XD, rd(3)), 19)
        if x == 0xe1: return done(2, '%s = z80_pop(zp);' % X, 14)
        if x == 0xe5: return done(2, 'z80_push(zp, %s);' % X, 15)
        if x == 0xe9: return 2, 'z80.pc = %s; z80.cycles -= 8;' % X
        if x == 0xf9: return done(2, 'z80.sp = %s;' % X, 10)
        if 0x40 <= x < 0x80 and x != 0x76:
            dst, src = (x >> 3) & 7, x & 7
            if src == 6:                                       # LD r,(IX+d): real H/L
                return done(3, '%s = z80_bus_rd(%s);' % (R8[dst], XD), 19)
            if dst == 6 and src != 6:
                return done(3, 'z80_bus_wr(%s, %s);' % (XD, R8[src]), 19)
            return None, None                                  # IXH/IXL forms
        if 0x80 <= x < 0xc0 and (x & 7) == 6:
            return done(3, 'z80_alu(zp, %d, z80_bus_rd(%s));' % ((x >> 3) & 7, XD), 19)
        if x == 0xcb:
            c = rd(3); reg, b = c & 7, (c >> 3) & 7
            if (c >> 6) == 1:
                return done(4, 'a = %s; v = z80_bus_rd(a); z80_bit(zp, %d, v, (z80_u8)(a >> 8));' % (XD, b), 20)
            expr = {0: 'z80_rot(zp, 0x%02x, v)' % c, 2: '(z80_u8)(v & ~%du)' % (1 << b),
                    3: '(z80_u8)(v | %du)' % (1 << b)}[c >> 6]
            copy = '' if reg == 6 else ' %s = v;' % R8[reg]
            return done(4, 'a = %s; v = z80_bus_rd(a); v = %s; z80_bus_wr(a, v);%s' % (XD, expr, copy), 23)
        return None, None
    return None, None                                          # DAA, EI, HALT, EX, ...


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('rom_h')
    ap.add_argument('trace')
    ap.add_argument('out')
    ap.add_argument('--func', default='z80_run_rc')
    ap.add_argument('--array', default='pac_rom')
    a = ap.parse_args()
    src = open(a.rom_h).read()
    body = src[src.index(a.array):]
    body = body[body.index('{') + 1:body.index('}')]
    rom = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})\b', body))
    assert len(rom) == 0x4000, len(rom)
    pcs = sorted({int(l, 16) for l in open(a.trace) if l.strip()})
    pcs = [p for p in pcs if p < 0x4000]
    guard = os.path.basename(a.out).upper().replace('.', '_')
    o = ['/* Generated by tools/z80recomp.py from %s + %s: %d instruction addresses.'
         % (os.path.basename(a.rom_h), os.path.basename(a.trace), len(pcs)),
         ' * Derived from the game ROM - do not commit. */',
         '#ifndef %s\n#define %s\n' % (guard, guard),
         '#define z80 (*zp)',
         '',
         '/* one interpreted instruction at z80.pc (out of line: one copy of the interpreter) */',
         'static __attribute__((noinline)) int z80rc_interp1(z80_t *zp) {',
         '    int op = z80_m1(zp);',
         '    do op = z80_exec_main(zp, (z80_u8)op); while (op >= 0);',
         '    return op;',
         '}',
         '',
         'static void %s(int cycles) {' % a.func,
         '    z80_t s = z80, *zp = &s;',
         '    z80_u16 a;',
         '    z80_u8 v;',
         '    int op;',
         '    (void)a; (void)v;',
         '    z80.cycles += cycles;',
         '    while (z80.cycles > 0) {',
         '#ifdef Z80_EXT_IRQ',
         '        z80.irq_line = (z80_u8)(Z80_EXT_IRQ ? 1 : 0);',
         '#endif',
         '        if (z80.events) {                                /* as z80_run */',
         '            if (z80.irq_line && z80.iff1 && !z80.ei_delay) { z80_take_irq(zp); continue; }',
         '            if (z80.halted) {',
         '                if (z80.ei_delay) { z80.ei_delay = 0; z80.rr++; z80.cycles -= 4; continue; }',
         '                if (z80.irq_line && z80.iff1) continue;',
         '                z80.rr = (z80_u8)(z80.rr + ((z80.cycles + 3) >> 2));   /* an M1 each */',
         '                z80.cycles -= (z80.cycles + 3) & ~3; break;   /* whole HALT NOPs */',
         '            }',
         '            if (z80.ei_delay) { z80.ei_delay = 0; z80rc_interp1(zp); continue; }',
         '        }',
         '        switch (z80.pc) {']
    nt = 0
    for i, p in enumerate(pcs):
        nxt = pcs[i + 1] if i + 1 < len(pcs) else -1
        length, code = translate(rom, p)
        if code is None:
            o.append('        case 0x%04x: z80.pc = 0x%04x; op = z80rc_interp1(zp); if (op != -1 || z80.pc != 0x%04x || z80.cycles <= 0) continue;'
                     % (p, p, nxt & 0xffff))
        else:
            nt += 1
            m1 = 2 if rom[p & 0x3fff] in (0xcb, 0xed, 0xdd, 0xfd) else 1     # R: M1 cycles of the op
            o.append('        case 0x%04x: z80.rr += %d; %s if (z80.pc != 0x%04x || z80.cycles <= 0) continue;'
                     % (p, m1, code, nxt & 0xffff))
    o += ['        default:                                         /* not traced: interpret */',
          '            z80rc_interp1(zp);',
          '            continue;',
          '        }',
          '    }',
          '    z80 = s;',
          '}',
          '',
          '#undef z80',
          '#endif']
    # `z80 = s;` must name the global: undo the macro just for that line
    text = '\n'.join(o) + '\n'
    text = text.replace('    z80 = s;\n}', '#undef z80\n    z80 = s;\n#define z80 (*zp)\n}')
    text = text.replace('    z80_t s = z80, *zp = &s;', '#undef z80\n    z80_t s = z80, *zp = &s;\n#define z80 (*zp)')
    open(a.out, 'w').write(text)
    print('%s: %s(), %d cases, %d translated, %d via the interpreter'
          % (a.out, a.func, len(pcs), nt, len(pcs) - nt))


if __name__ == '__main__':
    main()
