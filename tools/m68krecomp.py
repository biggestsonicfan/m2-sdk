#!/usr/bin/env python3
"""m68krecomp.py <rom.h> <out.h> -p PROFILE [-p PROFILE]... [--budget N] [--exact]

Static recompiler for a Mega Drive game's 68000 code, for src/m2_m68k.h + src/md_hw.h
(the pattern of tools/z80recomp.py, Pac-Man's). Writes md_rc_run(): the most executed
instructions of the profile runs (tools/mdhost.c -P) translated to C, with operands,
addresses and the PC as constants, flags computed only where a later instruction reads
them, and the core's cycle counts (src/m2_m68k_cyc.h plus the same variable parts).

  - Direct branches between translated code are gotos; returns, indirect jumps and
    anything that lands elsewhere go through a hash of block entries (dispatch).
  - The cycles are charged per block; at each taken branch or dispatch the run ends if
    the slice is used up or an interrupt is pending, as the interpreter would between
    instructions (with --exact that check follows every instruction, so interrupts land
    on the same instruction as with the interpreter: tools/mdhost.c -m compares).
  - Instructions not translated here (privileged ones, BCD, exceptions, rare forms) and
    code the profile never ran go to the interpreter (m68k_step): the profile decides
    speed, never behaviour.

<rom.h> is the ROM header (src/sonic_rom.h, tools/mdrom.py). The output is derived from
the game: keep it out of git (src/sonic_recomp.h is gitignored).
"""
import argparse, array, os, re, sys

# ------------------------------------------------------------------------------------ input
def load_rom(path):
    text = open(path).read()
    body = text[text.index('{') + 1:text.rindex('}')]
    return [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', body)]


def load_cyc(path):
    """src/m2_m68k_cyc.h: m68k_cyc_idx[1024] into m68k_cyc_blk[][64] -> 65536 values"""
    text = open(path).read()
    idx = [int(x) for x in re.findall(r'\d+', text[text.index('{') + 1:text.index('}')])]
    blks = [[int(x) for x in b.split(',')] for b in re.findall(r'\{([\d,]+)\}', text[text.index('m68k_cyc_blk'):])]
    assert len(idx) == 1024
    return [blks[idx[op >> 6]][op & 63] for op in range(65536)]


def load_profile(prefix):
    pc = array.array('I'); pc.frombytes(open(prefix + '.pc', 'rb').read())
    ent = array.array('I'); ent.frombytes(open(prefix + '.ent', 'rb').read())
    return pc, ent


def s8(v): return v - 256 if v & 0x80 else v
def s16(v): return v - 65536 if v & 0x8000 else v
def hx(v): return '0x%xu' % (v & 0xffffffff)

MASK = {1: '0xffu', 2: '0xffffu', 4: '0xffffffffu'}
SH = {1: 24, 2: 16, 4: 0}
CT = {1: 'u8', 2: 'u16', 4: 'u32'}
FLAGS = 'xnzvc'


class Untranslated(Exception):
    pass


# ------------------------------------------------------------------------ one instruction
class Insn:
    """Decode + translate the instruction at `pc`. After translate():
       self.length, self.code (list of C lines), self.cyc (fixed cycles),
       self.kind: 'seq' | 'bcc' | 'bra' | 'bsr' | 'jmp' | 'jsr' | 'rts' | 'dbcc' | 'dyn' | 'exit'
       self.target (constant branch target or None), self.cond,
       self.reads / self.writes: flag sets (for liveness), self.live: flags needed after."""

    def __init__(self, rom, cyc, pc):
        self.rom, self.cycT, self.pc = rom, cyc, pc
        self.op = self.word(0)
        self.pos = 2                         # bytes consumed
        self.code = []
        self.tmp = 0
        self.target = None
        self.cond = None
        self.kind = 'seq'
        self.reads = set()
        self.writes = set()
        self.cyc = cyc[self.op]
        self.vcyc = []                       # runtime cycle expressions
        self.live = set(FLAGS)

    def word(self, off):
        a = self.pc + off
        if a // 2 >= len(self.rom): raise Untranslated('outside ROM')
        return self.rom[a // 2]

    def ext16(self):
        v = self.word(self.pos); self.pos += 2; return v

    def ext32(self):
        v = (self.word(self.pos) << 16) | self.word(self.pos + 2); self.pos += 4; return v

    def t(self, ctype='u32'):
        self.tmp += 1
        name = 't%d' % self.tmp
        return name, ctype

    def emit(self, s): self.code.append(s)

    def var(self, expr, ctype='u32'):
        n, _ = self.t()
        self.emit('%s %s = %s;' % (ctype, n, expr))
        return n

    # -------------------------------------------------------------- memory access
    def rd(self, sz, addr, const=None):
        """C expression reading sz bytes at addr (a C expression); const = known address"""
        if const is not None:
            a = const & 0xffffff
            if a >= 0xe00000:
                w = 'md_ram[0x%x]' % ((a & 0xffff) >> 1)
                if sz == 1: return '((u32)%s %s)' % (w, '& 0xffu' if a & 1 else '>> 8')
                if sz == 2: return '(u32)' + w
                if (a & 0xffff) == 0xfffe: return 'rc_rd32_o(%s, cyc_)' % hx(a)
                return '(((u32)%s << 16) | md_ram[0x%x])' % (w, ((a & 0xffff) >> 1) + 1)
            if a + sz <= len(self.rom) * 2:
                w = self.rom[a >> 1]
                if sz == 1: return hx((w & 0xff) if a & 1 else w >> 8)
                if sz == 2: return hx(w)
                return hx((w << 16) | self.rom[(a >> 1) + 1])
            return 'rc_rd%d_o(%s, cyc_)' % (sz * 8, hx(a))
        return {1: 'rc_rd8(%s)', 2: 'rc_rd16(%s)', 4: 'rc_rd32(%s)'}[sz] % addr

    def wr(self, sz, addr, val, const=None):
        if const is not None:
            a = const & 0xffffff
            if a >= 0xe00000:
                i = (a & 0xffff) >> 1
                if sz == 1:
                    if a & 1: self.emit('md_ram[0x%x] = (u16)((md_ram[0x%x] & 0xff00u) | ((%s) & 0xffu));' % (i, i, val))
                    else: self.emit('md_ram[0x%x] = (u16)((md_ram[0x%x] & 0x00ffu) | (((%s) & 0xffu) << 8));' % (i, i, val))
                elif sz == 2: self.emit('md_ram[0x%x] = (u16)(%s);' % (i, val))
                elif (a & 0xffff) == 0xfffe: self.emit('RC_WR_O(32, %s, %s);' % (hx(a), val))
                else:
                    v = self.var(val)
                    self.emit('md_ram[0x%x] = (u16)(%s >> 16); md_ram[0x%x] = (u16)%s;' % (i, v, i + 1, v))
                return
            if a < 0x400000: return                  # ROM: ignored
            self.emit('RC_WR_O(%d, %s, %s);' % (sz * 8, hx(a), val))
            return
        self.emit({1: 'rc_wr8(%s, %s);', 2: 'rc_wr16(%s, %s);', 4: 'rc_wr32(%s, %s);'}[sz] % (addr, val))

    # -------------------------------------------------------------- effective addresses
    def ea_addr(self, mode, reg, sz):
        """-> (C expression of the address, constant or None); emits the side effects"""
        if mode == 2: return self.var('m68k.a[%d]' % reg), None
        if mode == 3:
            inc = 2 if (reg == 7 and sz == 1) else sz
            a = self.var('m68k.a[%d]' % reg)
            self.emit('m68k.a[%d] = %s + %d;' % (reg, a, inc))
            return a, None
        if mode == 4:
            dec = 2 if (reg == 7 and sz == 1) else sz
            a = self.var('m68k.a[%d] - %d' % (reg, dec))
            self.emit('m68k.a[%d] = %s;' % (reg, a))
            return a, None
        if mode == 5:
            d = s16(self.ext16())
            return self.var('m68k.a[%d] + %s' % (reg, hx(d))), None
        if mode == 6:
            return self.index('m68k.a[%d]' % reg, None), None
        if reg == 0:
            a = s16(self.ext16()) & 0xffffffff
            return hx(a), a
        if reg == 1:
            a = self.ext32()
            return hx(a), a
        if reg == 2:
            base = self.pc + self.pos
            a = (base + s16(self.ext16())) & 0xffffffff
            return hx(a), a
        if reg == 3:
            base = self.pc + self.pos
            return self.index(None, base), None
        raise Untranslated('ea mode 7/%d' % reg)

    def index(self, base_expr, base_const):
        ext = self.ext16()
        r = (ext >> 12) & 7
        x = ('m68k.a[%d]' if ext & 0x8000 else 'm68k.d[%d]') % r
        if not ext & 0x800: x = '(u32)(s32)(s16)%s' % x
        d = s8(ext & 0xff)
        if base_const is not None:
            return self.var('%s + %s' % (hx(base_const + d), x))
        return self.var('%s + %s + %s' % (base_expr, x, hx(d)))

    def ea_read(self, mode, reg, sz):
        """-> C expression (zero-extended) of the operand value"""
        if mode == 0: return self.var('m68k.d[%d] & %s' % (reg, MASK[sz]))
        if mode == 1: return self.var('m68k.a[%d] & %s' % (reg, MASK[sz]))
        if mode == 7 and reg == 4:
            if sz == 4: return hx(self.ext32())
            return hx(self.ext16() & (0xff if sz == 1 else 0xffff))
        a, c = self.ea_addr(mode, reg, sz)
        return self.var(self.rd(sz, a, c))

    def setd(self, r, sz, val):
        if sz == 4: self.emit('m68k.d[%d] = %s;' % (r, val))
        elif sz == 2: self.emit('m68k.d[%d] = (m68k.d[%d] & 0xffff0000u) | ((%s) & 0xffffu);' % (r, r, val))
        else: self.emit('m68k.d[%d] = (m68k.d[%d] & 0xffffff00u) | ((%s) & 0xffu);' % (r, r, val))

    # -------------------------------------------------------------- flags
    def fl(self, name, expr):
        self.writes.add(name)
        if name in self.live: self.emit('m68k.%s = %s;' % (name, expr))

    def logic_flags(self, sz, r):
        R = self.var('(u32)(%s) << %d' % (r, SH[sz])) if SH[sz] else r
        self.fl('n', R); self.fl('z', R); self.fl('v', '0'); self.fl('c', '0')

    def add(self, sz, s, d, setx=True, addx=False):
        sh = SH[sz]
        S = self.var('(u32)(%s) << %d' % (s, sh)); D = self.var('(u32)(%s) << %d' % (d, sh))
        R = self.var('%s + %s%s' % (D, S, (' + ((m68k.x & 1u) << %d)' % sh) if addx else ''))
        if addx: self.reads.update('xz')
        self.fl('n', R)
        if addx: self.fl('z', 'm68k.z | ' + R)
        else: self.fl('z', R)
        self.fl('v', '(%s ^ %s) & (%s ^ %s)' % (S, R, D, R))
        c = '((%s & %s) | (~%s & (%s | %s))) >> 31' % (S, D, R, S, D)
        if setx and ('x' in self.live or 'c' in self.live):
            cv = self.var(c); self.fl('c', cv); self.fl('x', cv)
        else: self.fl('c', c)
        return '(%s >> %d)' % (R, sh) if sh else R

    def sub(self, sz, s, d, setx=True, subx=False, cmp=False):
        sh = SH[sz]
        S = self.var('(u32)(%s) << %d' % (s, sh)); D = self.var('(u32)(%s) << %d' % (d, sh))
        R = self.var('%s - %s%s' % (D, S, (' - ((m68k.x & 1u) << %d)' % sh) if subx else ''))
        if subx: self.reads.update('xz')
        self.fl('n', R)
        if subx: self.fl('z', 'm68k.z | ' + R)
        else: self.fl('z', R)
        self.fl('v', '(%s ^ %s) & (%s ^ %s)' % (S, D, R, D))
        c = '((%s & %s) | (~%s & (%s | %s))) >> 31' % (S, R, D, S, R)
        if setx and not cmp and ('x' in self.live or 'c' in self.live):
            cv = self.var(c); self.fl('c', cv); self.fl('x', cv)
        else: self.fl('c', c)
        return '(%s >> %d)' % (R, sh) if sh else R

    def cond_expr(self, cc):
        uses = {0: '', 1: '', 2: 'cz', 3: 'cz', 4: 'c', 5: 'c', 6: 'z', 7: 'z', 8: 'v', 9: 'v',
                10: 'n', 11: 'n', 12: 'nv', 13: 'nv', 14: 'nvz', 15: 'nvz'}[cc]
        self.reads.update(uses)
        return {0: '1', 1: '0',
                2: '(!(m68k.c & 1) && m68k.z)', 3: '((m68k.c & 1) || !m68k.z)',
                4: '!(m68k.c & 1)', 5: '(m68k.c & 1)', 6: '(m68k.z != 0)', 7: '(m68k.z == 0)',
                8: '!(m68k.v >> 31)', 9: '(m68k.v >> 31)', 10: '!(m68k.n >> 31)', 11: '(m68k.n >> 31)',
                12: '!((m68k.n ^ m68k.v) >> 31)', 13: '((m68k.n ^ m68k.v) >> 31)',
                14: '(m68k.z && !((m68k.n ^ m68k.v) >> 31))',
                15: '(!m68k.z || ((m68k.n ^ m68k.v) >> 31))'}[cc]

    # -------------------------------------------------------------- the instruction set
    def translate(self):
        op = self.op
        mode, reg, rx = (op >> 3) & 7, op & 7, (op >> 9) & 7
        top = op >> 12
        if top == 0: self.line0(op, mode, reg, rx)
        elif top in (1, 2, 3): self.move(op, mode, reg, rx)
        elif top == 4: self.line4(op, mode, reg, rx)
        elif top == 5: self.line5(op, mode, reg, rx)
        elif top == 6: self.line6(op)
        elif top == 7:
            if op & 0x100: raise Untranslated('moveq?')
            v = s8(op & 0xff) & 0xffffffff
            self.emit('m68k.d[%d] = %s;' % (rx, hx(v)))
            self.fl('n', hx(v)); self.fl('z', hx(v)); self.fl('v', '0'); self.fl('c', '0')
        elif top in (8, 0xc): self.line8c(op, mode, reg, rx)
        elif top in (9, 0xd): self.line9d(op, mode, reg, rx)
        elif top == 0xb: self.lineb(op, mode, reg, rx)
        elif top == 0xe: self.linee(op, mode, reg, rx)
        else: raise Untranslated('line A/F')
        self.length = self.pos

    def line0(self, op, mode, reg, rx):
        if op & 0x100:
            if mode == 1: raise Untranslated('movep')
            bit = self.var('m68k.d[%d]' % rx)
            return self.bitop(op, mode, reg, bit)
        if rx == 4:
            bit = hx(self.ext16() & 0xff)
            return self.bitop(op, mode, reg, bit)
        if (op & 0xff) in (0x3c, 0x7c): raise Untranslated('to ccr/sr')
        sz = 1 << ((op >> 6) & 3)
        if sz == 8 or rx == 7: raise Untranslated('illegal')
        s = hx(self.ext32()) if sz == 4 else hx(self.ext16() & (0xff if sz == 1 else 0xffff))
        if rx == 6:
            d = self.ea_read(mode, reg, sz)
            self.sub(sz, s, d, cmp=True)
            return
        if mode == 0:
            d = 'm68k.d[%d]' % reg; a = c = None
        else:
            if mode == 1 or (mode == 7 and reg >= 2): raise Untranslated('imm to An/pc')
            a, c = self.ea_addr(mode, reg, sz); d = self.var(self.rd(sz, a, c))
        if rx == 0: r = self.var('%s | %s' % (d, s)); self.logic_flags(sz, '%s & %s' % (r, MASK[sz]))
        elif rx == 1: r = self.var('%s & %s' % (d, s)); self.logic_flags(sz, '%s & %s' % (r, MASK[sz]))
        elif rx == 2: r = self.var(self.sub(sz, s, d))
        elif rx == 3: r = self.var(self.add(sz, s, d))
        else: r = self.var('%s ^ %s' % (d, s)); self.logic_flags(sz, '%s & %s' % (r, MASK[sz]))
        if mode == 0: self.setd(reg, sz, r)
        else: self.wr(sz, a, r, c)

    def bitop(self, op, mode, reg, bit):
        t = (op >> 6) & 3
        if mode == 0:
            b = self.var('1u << (%s & 31)' % bit)
            self.fl('z', 'm68k.d[%d] & %s' % (reg, b))
            if t == 1: self.emit('m68k.d[%d] ^= %s;' % (reg, b))
            elif t == 2: self.emit('m68k.d[%d] &= ~%s;' % (reg, b))
            elif t == 3: self.emit('m68k.d[%d] |= %s;' % (reg, b))
            return
        if mode == 1: raise Untranslated('bit An')
        b = self.var('1u << (%s & 7)' % bit)
        if t == 0:
            v = self.ea_read(mode, reg, 1)
            self.fl('z', '%s & %s' % (v, b))
            return
        a, c = self.ea_addr(mode, reg, 1)
        d = self.var(self.rd(1, a, c))
        self.fl('z', '%s & %s' % (d, b))
        r = {1: '%s ^ %s', 2: '%s & ~%s', 3: '%s | %s'}[t] % (d, b)
        self.wr(1, a, r, c)

    def move(self, op, mode, reg, rx):
        sz = {1: 1, 3: 2, 2: 4}[op >> 12]
        dm = (op >> 6) & 7
        s = self.ea_read(mode, reg, sz)
        if dm == 1:
            if sz == 1: raise Untranslated('movea.b')
            self.emit('m68k.a[%d] = %s;' % (rx, s if sz == 4 else '(u32)(s32)(s16)%s' % s))
            return
        self.logic_flags(sz, s)
        if dm == 0: self.setd(rx, sz, s)
        else:
            if dm == 7 and rx >= 2: raise Untranslated('move to pc/imm')
            a, c = self.ea_addr(dm, rx, sz)
            self.wr(sz, a, s, c)

    def line4(self, op, mode, reg, rx):
        if op & 0x100:
            if (op & 0x1c0) == 0x1c0:                              # LEA
                if mode in (0, 1, 3, 4) or (mode == 7 and reg == 4): raise Untranslated('lea mode')
                a, c = self.ea_addr(mode, reg, 4)
                self.emit('m68k.a[%d] = %s;' % (rx, a))
                return
            raise Untranslated('chk')
        sub = (op >> 8) & 0xf
        if sub in (0, 2, 4, 6):
            if ((op >> 6) & 3) == 3: raise Untranslated('sr/ccr moves')
            sz = 1 << ((op >> 6) & 3)
            if mode == 1 or (mode == 7 and reg >= 2): raise Untranslated('bad ea')
            if mode == 0: d = 'm68k.d[%d]' % reg; a = c = None
            else:
                a, c = self.ea_addr(mode, reg, sz)
                d = self.var(self.rd(sz, a, c)) if sub != 2 else '0'
            if sub == 0: r = self.var(self.sub(sz, d, '0', subx=True))          # NEGX
            elif sub == 2:                                                        # CLR
                r = '0'
                self.fl('n', '0'); self.fl('z', '0'); self.fl('v', '0'); self.fl('c', '0')
            elif sub == 4: r = self.var(self.sub(sz, d, '0'))                    # NEG
            else:
                r = self.var('~%s' % d); self.logic_flags(sz, '%s & %s' % (r, MASK[sz]))
            if mode == 0: self.setd(reg, sz, r)
            else: self.wr(sz, a, r, c)
            return
        if sub == 8:
            k = (op >> 6) & 3
            if k == 0: raise Untranslated('nbcd')
            if k == 1:
                if mode == 0:                                                     # SWAP
                    r = self.var('(m68k.d[%d] >> 16) | (m68k.d[%d] << 16)' % (reg, reg))
                    self.emit('m68k.d[%d] = %s;' % (reg, r))
                    self.fl('n', r); self.fl('z', r); self.fl('v', '0'); self.fl('c', '0')
                    return
                if mode in (1, 3, 4) or (mode == 7 and reg == 4): raise Untranslated('pea mode')
                a, c = self.ea_addr(mode, reg, 4)                                   # PEA
                self.push32(a)
                return
            if mode == 0:
                if k == 2:                                                        # EXT.W
                    r = self.var('(u32)(s32)(s8)m68k.d[%d] & 0xffffu' % reg)
                    self.setd(reg, 2, r); self.logic_flags(2, r)
                else:                                                             # EXT.L
                    r = self.var('(u32)(s32)(s16)m68k.d[%d]' % reg)
                    self.emit('m68k.d[%d] = %s;' % (reg, r)); self.logic_flags(4, r)
                return
            return self.movem(op, mode, reg)
        if sub == 0xa:
            if ((op >> 6) & 3) == 3: raise Untranslated('tas/illegal')
            sz = 1 << ((op >> 6) & 3)                                             # TST
            v = self.ea_read(mode, reg, sz)
            self.logic_flags(sz, v)
            return
        if sub == 0xc:
            if op & 0x80: return self.movem(op, mode, reg)
            raise Untranslated('mul.l')
        if sub == 0xe:
            if (op & 0xc0) == 0x80:                                               # JSR
                if mode in (0, 1, 3, 4) or (mode == 7 and reg == 4): raise Untranslated('jsr mode')
                a, c = self.ea_addr(mode, reg, 4)
                self.length = self.pos
                self.push32(hx(self.pc + self.pos))
                if c is not None: self.kind, self.target = 'jsr', c
                else: self.kind, self.dyn = 'dyn', a
                return
            if (op & 0xc0) == 0xc0:                                               # JMP
                if mode in (0, 1, 3, 4) or (mode == 7 and reg == 4): raise Untranslated('jmp mode')
                a, c = self.ea_addr(mode, reg, 4)
                if c is not None: self.kind, self.target = 'bra', c
                else: self.kind, self.dyn = 'dyn', a
                return
            if op == 0x4e75:                                                      # RTS
                a = self.var('rc_rd32(m68k.a[7])')
                self.emit('m68k.a[7] += 4;')
                self.kind, self.dyn = 'dyn', a
                return
            if op == 0x4e71: return                                               # NOP
            if (op & 0xfff8) == 0x4e50:                                           # LINK
                d = s16(self.ext16())
                self.push32('m68k.a[%d]' % reg)
                self.emit('m68k.a[%d] = m68k.a[7];' % reg)
                self.emit('m68k.a[7] += %s;' % hx(d))
                return
            if (op & 0xfff8) == 0x4e58:                                           # UNLK
                self.emit('m68k.a[7] = m68k.a[%d];' % reg)
                a = self.var('rc_rd32(m68k.a[7])')
                self.emit('m68k.a[7] += 4;')
                self.emit('m68k.a[%d] = %s;' % (reg, a))
                return
        raise Untranslated('line 4 %04x' % op)

    def push32(self, v):
        vv = self.var(v)
        self.emit('m68k.a[7] -= 4;')
        self.emit('rc_wr32(m68k.a[7], %s);' % vv)

    def movem(self, op, mode, reg):
        sz = 4 if op & 0x40 else 2
        lst = self.ext16()
        regs = [i for i in range(16) if lst & (1 << i)]
        self.cyc += len(regs) * (8 if sz == 4 else 4)
        tosz = lambda i: ('m68k.d[%d]' % i) if i < 8 else ('m68k.a[%d]' % (i - 8))
        if not op & 0x400:                                    # registers -> memory
            if mode == 4:
                a = self.var('m68k.a[%d]' % reg)
                for i in regs:
                    r = ('m68k.a[%d]' % (7 - i)) if i < 8 else ('m68k.d[%d]' % (15 - i))
                    self.emit('%s -= %d;' % (a, sz))
                    self.emit(('rc_wr32(%s, %s);' if sz == 4 else 'rc_wr16(%s, %s);') % (a, r))
                self.emit('m68k.a[%d] = %s;' % (reg, a))
                return
            if mode in (0, 1, 3) or (mode == 7 and reg >= 2): raise Untranslated('movem mode')
            a0, c = self.ea_addr(mode, reg, sz)
            a = self.var(a0)
            for i in regs:
                self.emit(('rc_wr32(%s, %s);' if sz == 4 else 'rc_wr16(%s, %s);') % (a, tosz(i)))
                self.emit('%s += %d;' % (a, sz))
            return
        if mode in (0, 1, 4) or (mode == 7 and reg == 4): raise Untranslated('movem mode')
        if mode == 3: a = self.var('m68k.a[%d]' % reg)
        else:
            a0, c = self.ea_addr(mode, reg, sz)
            a = self.var(a0)
        for i in regs:
            v = self.var('rc_rd32(%s)' % a if sz == 4 else '(u32)(s32)(s16)rc_rd16(%s)' % a)
            self.emit('%s = %s;' % (tosz(i), v))
            self.emit('%s += %d;' % (a, sz))
        if mode == 3: self.emit('m68k.a[%d] = %s;' % (reg, a))

    def line5(self, op, mode, reg, rx):
        if ((op >> 6) & 3) == 3:
            cc = (op >> 8) & 15
            if mode == 1:                                                         # DBcc
                disp = s16(self.ext16())
                self.kind = 'dbcc'
                self.target = (self.pc + 2 + disp) & 0xffffffff
                self.cond = self.cond_expr(cc)
                self.dreg = reg
                return
            if mode == 7 and reg >= 2: raise Untranslated('scc mode')
            c = self.cond_expr(cc)                                                # Scc
            if mode == 0:
                self.emit('if (%s) { m68k.d[%d] |= 0xffu; m68k.cycles -= 2; } else m68k.d[%d] &= 0xffffff00u;' % (c, reg, reg))
            else:
                v = self.var('(%s) ? 0xffu : 0u' % c)
                a, cst = self.ea_addr(mode, reg, 1)
                self.wr(1, a, v, cst)
            return
        s = rx if rx else 8
        sz = 1 << ((op >> 6) & 3)
        subq = op & 0x100
        if mode == 1:
            self.emit('m68k.a[%d] %s= %d;' % (reg, '-' if subq else '+', s))
            return
        if mode == 7 and reg >= 2: raise Untranslated('addq mode')
        if mode == 0: d = 'm68k.d[%d]' % reg; a = c = None
        else:
            a, c = self.ea_addr(mode, reg, sz); d = self.var(self.rd(sz, a, c))
        r = self.var(self.sub(sz, str(s), d) if subq else self.add(sz, str(s), d))
        if mode == 0: self.setd(reg, sz, r)
        else: self.wr(sz, a, r, c)

    def line6(self, op):
        cc = (op >> 8) & 15
        disp = op & 0xff
        base = self.pc + 2
        if disp == 0: disp = s16(self.ext16())
        else: disp = s8(disp)
        self.target = (base + disp) & 0xffffffff
        self.short = (op & 0xff) != 0
        if cc == 1:
            self.length = self.pos
            self.push32(hx(self.pc + self.pos))
            self.kind = 'bsr'
            return
        if cc == 0: self.kind = 'bra'; return
        self.kind = 'bcc'
        self.cond = self.cond_expr(cc)

    def line8c(self, op, mode, reg, rx):
        om = (op >> 6) & 7
        if om in (3, 7):
            if op & 0x4000: return self.mul(op, mode, reg, rx)
            return self.div(op, mode, reg, rx)
        if om == 4 and mode <= 1: raise Untranslated('bcd')
        if op & 0x4000:
            if (op & 0x1f8) == 0x140:
                self.emit('{ u32 x_ = m68k.d[%d]; m68k.d[%d] = m68k.d[%d]; m68k.d[%d] = x_; }' % (rx, rx, reg, reg)); return
            if (op & 0x1f8) == 0x148:
                self.emit('{ u32 x_ = m68k.a[%d]; m68k.a[%d] = m68k.a[%d]; m68k.a[%d] = x_; }' % (rx, rx, reg, reg)); return
            if (op & 0x1f8) == 0x188:
                self.emit('{ u32 x_ = m68k.d[%d]; m68k.d[%d] = m68k.a[%d]; m68k.a[%d] = x_; }' % (rx, rx, reg, reg)); return
        sz = 1 << (om & 3)
        opc = '&' if op & 0x4000 else '|'
        if not om & 4:
            s = self.ea_read(mode, reg, sz)
            r = self.var('(m68k.d[%d] %s %s) & %s' % (rx, opc, s, MASK[sz]))
            self.logic_flags(sz, r); self.setd(rx, sz, r)
        else:
            if mode <= 1 or (mode == 7 and reg >= 2): raise Untranslated('bad ea')
            a, c = self.ea_addr(mode, reg, sz)
            d = self.var(self.rd(sz, a, c))
            r = self.var('(%s %s m68k.d[%d]) & %s' % (d, opc, rx, MASK[sz]))
            self.logic_flags(sz, r); self.wr(sz, a, r, c)

    def mul(self, op, mode, reg, rx):
        s = self.ea_read(mode, reg, 2)
        if op & 0x100:
            r = self.var('(u32)((s32)(s16)%s * (s32)(s16)m68k.d[%d])' % (s, rx))
            self.emit('m68k.cycles -= rc_muls_cyc(%s);' % s)
        else:
            r = self.var('%s * (m68k.d[%d] & 0xffffu)' % (s, rx))
            self.emit('m68k.cycles -= rc_mulu_cyc(%s);' % s)
        self.emit('m68k.d[%d] = %s;' % (rx, r))
        self.fl('z', r); self.fl('n', r); self.fl('v', '0'); self.fl('c', '0')

    def div(self, op, mode, reg, rx):
        s = self.ea_read(mode, reg, 2)
        # divide by zero traps: leave that to the interpreter (nothing written yet but
        # the EA side effects, so only register/immediate/plain-memory sources here)
        if mode in (3, 4): raise Untranslated('div postinc/predec')
        self.emit('if (!%s) { m68k.pc = %s; RC_EXIT_FB }' % (s, hx(self.pc)))
        self.writes.add('v')          # on overflow only V is written: the rest may stay
        if op & 0x100:
            self.emit('{ s32 s_ = (s16)%s, q_, r_;' % s)
            self.emit('  if (m68k.d[%d] == 0x80000000u && s_ == -1) { m68k.z = 0; m68k.n = 0; m68k.v = 0; m68k.c = 0; m68k.d[%d] = 0; }' % (rx, rx))
            self.emit('  else { q_ = (s32)m68k.d[%d] / s_; r_ = (s32)m68k.d[%d] %% s_;' % (rx, rx))
            self.emit('    if (q_ == (s16)q_) { m68k.z = (u32)q_ & 0xffffu; m68k.n = (u32)q_ << 16; m68k.v = 0; m68k.c = 0;')
            self.emit('      m68k.d[%d] = ((u32)q_ & 0xffffu) | ((u32)r_ << 16); } else m68k.v = 0x80000000u; } }' % rx)
        else:
            self.emit('{ u32 q_ = m68k.d[%d] / %s, r_ = m68k.d[%d] %% %s;' % (rx, s, rx, s))
            self.emit('  if (q_ < 0x10000u) { m68k.z = q_; m68k.n = q_ << 16; m68k.v = 0; m68k.c = 0; m68k.d[%d] = (q_ & 0xffffu) | (r_ << 16); }' % rx)
            self.emit('  else m68k.v = 0x80000000u; }')

    def line9d(self, op, mode, reg, rx):
        om = (op >> 6) & 7
        add = (op >> 12) == 0xd
        if om in (3, 7):
            sz = 2 if om == 3 else 4
            s = self.ea_read(mode, reg, sz)
            if sz == 2: s = '(u32)(s32)(s16)%s' % s
            self.emit('m68k.a[%d] %s= %s;' % (rx, '+' if add else '-', s))
            return
        sz = 1 << (om & 3)
        if not om & 4:
            s = self.ea_read(mode, reg, sz)
            d = self.var('m68k.d[%d] & %s' % (rx, MASK[sz]))
            r = self.var(self.add(sz, s, d) if add else self.sub(sz, s, d))
            self.setd(rx, sz, r)
            return
        if mode <= 1:
            if mode == 1: raise Untranslated('addx mem')
            s = self.var('m68k.d[%d] & %s' % (reg, MASK[sz])); d = self.var('m68k.d[%d] & %s' % (rx, MASK[sz]))
            r = self.var(self.add(sz, s, d, addx=True) if add else self.sub(sz, s, d, subx=True))
            self.setd(rx, sz, r)
            return
        if mode == 7 and reg >= 2: raise Untranslated('bad ea')
        a, c = self.ea_addr(mode, reg, sz)
        d = self.var(self.rd(sz, a, c))
        s = self.var('m68k.d[%d] & %s' % (rx, MASK[sz]))
        r = self.var(self.add(sz, s, d) if add else self.sub(sz, s, d))
        self.wr(sz, a, r, c)

    def lineb(self, op, mode, reg, rx):
        om = (op >> 6) & 7
        if om in (3, 7):
            sz = 2 if om == 3 else 4
            s = self.ea_read(mode, reg, sz)
            if sz == 2: s = '(u32)(s32)(s16)%s' % s
            self.sub(4, s, 'm68k.a[%d]' % rx, cmp=True)
            return
        sz = 1 << (om & 3)
        if not om & 4:
            s = self.ea_read(mode, reg, sz)
            self.sub(sz, s, 'm68k.d[%d] & %s' % (rx, MASK[sz]), cmp=True)
            return
        if mode == 1:                                                             # CMPM
            a, c = self.ea_addr(3, reg, sz); s = self.var(self.rd(sz, a, None))
            a, c = self.ea_addr(3, rx, sz); d = self.var(self.rd(sz, a, None))
            self.sub(sz, s, d, cmp=True)
            return
        if mode == 0:
            r = self.var('(m68k.d[%d] ^ m68k.d[%d]) & %s' % (reg, rx, MASK[sz]))
            self.setd(reg, sz, r)
        else:
            if mode == 7 and reg >= 2: raise Untranslated('bad ea')
            a, c = self.ea_addr(mode, reg, sz)
            r = self.var('(%s ^ m68k.d[%d]) & %s' % (self.var(self.rd(sz, a, c)), rx, MASK[sz]))
            self.wr(sz, a, r, c)
        self.logic_flags(sz, r)

    def linee(self, op, mode, reg, rx):
        if ((op >> 6) & 3) == 3:
            if op & 0x800: raise Untranslated('bitfield')
            a, c = self.ea_addr(mode, reg, 2)
            d = self.var(self.rd(2, a, c))
            self.emit('(*M_).x = FX;')
            r = self.var('rc_shift(%d, %d, %d, %s, 1)' % (2, (op >> 9) & 3, (op >> 8) & 1, d))
            self.emit('FN = (*M_).n; FZ = (*M_).z; FV = (*M_).v; FC = (*M_).c; FX = (*M_).x;')
            self.wr(2, a, r, c)
            self.writes.update('xnzvc')
            if (op >> 9) & 3 == 2: self.reads.add('x')
            return
        sz = 1 << ((op >> 6) & 3)
        typ, left = (op >> 3) & 3, (op >> 8) & 1
        if op & 0x20:
            cnt = self.var('m68k.d[%d] & 63u' % rx)
            self.emit('m68k.cycles -= (int)%s * 2;' % cnt)
        else:
            cnt = str(rx if rx else 8)
            self.cyc += 2 * (rx if rx else 8)
        self.writes.update('nzvc')
        if typ != 3 and not op & 0x20: self.writes.add('x')   # a zero count keeps X
        if typ == 2: self.reads.add('x')
        # the common cases inline: LSL/LSR/ASL/ASR by a constant under the width
        if not op & 0x20 and typ in (0, 1) and int(cnt) < sz * 8 and not (typ == 0 and left):
            n = int(cnt); bits = sz * 8
            d = self.var('m68k.d[%d] & %s' % (reg, MASK[sz]))
            if left:                                                              # LSL
                r = self.var('(%s << %d) & %s' % (d, n, MASK[sz]))
                cf = '(%s >> %d) & 1u' % (d, bits - n)
            elif typ == 1:                                                        # LSR
                r = self.var('%s >> %d' % (d, n))
                cf = '(%s >> %d) & 1u' % (d, n - 1)
            else:                                                                 # ASR
                r = self.var('((u32)((s32)(%s << %d) >> %d) >> %d) & %s' % (d, 32 - bits, n, 32 - bits, MASK[sz]))
                cf = '(%s >> %d) & 1u' % (d, n - 1)
            self.setd(reg, sz, r)
            R = self.var('%s << %d' % (r, SH[sz])) if SH[sz] else r
            self.fl('n', R); self.fl('z', R); self.fl('v', '0')
            if 'c' in self.live or 'x' in self.live:
                cv = self.var(cf); self.fl('c', cv); self.fl('x', cv)
            return
        self.emit('(*M_).x = FX;')
        r = self.var('rc_shift(%d, %d, %d, m68k.d[%d], %s)' % (sz, typ, left, reg, cnt))
        self.emit('FN = (*M_).n; FZ = (*M_).z; FV = (*M_).v; FC = (*M_).c; FX = (*M_).x;')
        self.setd(reg, sz, r)


PROLOGUE = r"""
/* Inside md_rc_run the cycle count lives in a register (cyc_). The slow bus gets it by
 * value (the VDP's H counter reads it); a slow write returns 1 when it made an interrupt
 * pending (a VDP register), and the run then stops at the next block end, its remaining
 * cycles given back by md_run (md_cyc_owed). */
#define RC_LOAD do { D0 = m68k.d[0]; D1 = m68k.d[1]; D2 = m68k.d[2]; D3 = m68k.d[3]; D4 = m68k.d[4]; \
    D5 = m68k.d[5]; D6 = m68k.d[6]; D7 = m68k.d[7]; A0 = m68k.a[0]; A1 = m68k.a[1]; A2 = m68k.a[2]; \
    A3 = m68k.a[3]; A4 = m68k.a[4]; A5 = m68k.a[5]; A6 = m68k.a[6]; A7 = m68k.a[7]; \
    FX = m68k.x; FN = m68k.n; FZ = m68k.z; FV = m68k.v; FC = m68k.c; } while (0)
#define RC_SAVE do { m68k.d[0] = D0; m68k.d[1] = D1; m68k.d[2] = D2; m68k.d[3] = D3; m68k.d[4] = D4; \
    m68k.d[5] = D5; m68k.d[6] = D6; m68k.d[7] = D7; m68k.a[0] = A0; m68k.a[1] = A1; m68k.a[2] = A2; \
    m68k.a[3] = A3; m68k.a[4] = A4; m68k.a[5] = A5; m68k.a[6] = A6; m68k.a[7] = A7; \
    m68k.x = FX; m68k.n = FN; m68k.z = FZ; m68k.v = FV; m68k.c = FC; } while (0)
/* the registers and flags live in locals while the run lasts: RC_RET puts them back */
#if RC_LOCALS
#define RC_RET { RC_SAVE; m68k.cycles = cyc_; return 1; }
#else
#define RC_RET { m68k.cycles = cyc_; return 1; }
#endif
#define RC_WR_O(bits, a, v) do { if (rc_wr##bits##_o((a), (v), cyc_)) { md_cyc_owed += cyc_; cyc_ = 0; } } while (0)
static __attribute__((noinline)) u32 rc_rd8_o(u32 a, int cyc) { m68k.cycles = cyc; return md_rd8(a); }
static __attribute__((noinline)) u32 rc_rd16_o(u32 a, int cyc) { m68k.cycles = cyc; return md_rd16(a); }
static __attribute__((noinline)) u32 rc_rd32_o(u32 a, int cyc) { m68k.cycles = cyc; return M68K_RD32(a); }
static __attribute__((noinline)) int rc_wr8_o(u32 a, u32 v, int cyc) { m68k.cycles = cyc; md_irq_new = 0; md_wr8(a, v); return md_irq_new; }
static __attribute__((noinline)) int rc_wr16_o(u32 a, u32 v, int cyc) { m68k.cycles = cyc; md_irq_new = 0; md_wr16(a, v); return md_irq_new; }
static __attribute__((noinline)) int rc_wr32_o(u32 a, u32 v, int cyc) { m68k.cycles = cyc; md_irq_new = 0; M68K_WR32(a, v); return md_irq_new; }
/* the bus for addresses known only at run time: work RAM and ROM inline */
#define rc_rd8(a) ({ u32 a_ = (a); ((a_ & 0xe00000) == 0xe00000) ? (u32)(md_ram[(a_ & 0xffff) >> 1] >> ((~a_ & 1) << 3)) & 0xffu \
    : (a_ & 0xffffff) < (MD_ROM_WORDS << 1) ? (u32)(md_rom[(a_ & 0xffffff) >> 1] >> ((~a_ & 1) << 3)) & 0xffu \
    : rc_rd8_o(a_, cyc_); })
#define rc_rd16(a) ({ u32 a_ = (a); ((a_ & 0xe00000) == 0xe00000) ? (u32)md_ram[(a_ & 0xffff) >> 1] \
    : (a_ & 0xffffff) < (MD_ROM_WORDS << 1) ? (u32)md_rom[(a_ & 0xffffff) >> 1] : rc_rd16_o(a_, cyc_); })
#define rc_rd32(a) ({ u32 a_ = (a); ((a_ & 0xe00000) == 0xe00000 && (a_ & 0xffff) != 0xfffe) \
      ? ((u32)md_ram[(a_ & 0xffff) >> 1] << 16) | md_ram[((a_ & 0xffff) >> 1) + 1] : rc_rd32_o(a_, cyc_); })
#define rc_wr8(a, v) do { u32 a_ = (a), v_ = (v); if ((a_ & 0xe00000) == 0xe00000) { u16 *p_ = &md_ram[(a_ & 0xffff) >> 1]; \
    *p_ = (a_ & 1) ? (u16)((*p_ & 0xff00u) | (v_ & 0xffu)) : (u16)((*p_ & 0x00ffu) | ((v_ & 0xffu) << 8)); } \
    else RC_WR_O(8, a_, v_); } while (0)
#define rc_wr16(a, v) do { u32 a_ = (a), v_ = (v); if ((a_ & 0xe00000) == 0xe00000) md_ram[(a_ & 0xffff) >> 1] = (u16)v_; \
    else RC_WR_O(16, a_, v_); } while (0)
#define rc_wr32(a, v) do { u32 a_ = (a), v_ = (v); if ((a_ & 0xe00000) == 0xe00000 && (a_ & 0xffff) != 0xfffe) { \
    u16 *p_ = &md_ram[(a_ & 0xffff) >> 1]; p_[0] = (u16)(v_ >> 16); p_[1] = (u16)v_; } \
    else RC_WR_O(32, a_, v_); } while (0)
/* the shifts the translation leaves to the core (sets the flags as the interpreter) */
static __attribute__((noinline)) u32 rc_shift(int sz, int type, int left, u32 v, u32 cnt) {
    return m68k_shift(sz, type, left, v, cnt);
}
static __attribute__((noinline)) int rc_mulu_cyc(u32 s) { int c = 0; for (; s; s >>= 1) if (s & 1) c += 2; return c; }
static __attribute__((noinline)) int rc_muls_cyc(u32 s) {
    u32 y = (u32)(s32)(s16)s, f = 0; int c = 0;
    for (; y; y >>= 1) if ((y & 1) != f) { c += 2; f = 1 - f; }
    return c;
}
"""

# ------------------------------------------------------------------------------ the program
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom')
    ap.add_argument('out')
    ap.add_argument('-p', '--profile', action='append', required=True)
    ap.add_argument('--cyc', default=os.path.join(os.path.dirname(__file__), '..', 'src', 'm2_m68k_cyc.h'))
    ap.add_argument('--budget', type=int, default=4300, help='instructions to translate (the most run)')
    ap.add_argument('--exact', action='store_true', help='end the run after any instruction, as the interpreter')
    ap.add_argument('--locals', action='store_true',
                    help='the 68000 registers and flags in C locals (4%% faster, 20%% more code)')
    a = ap.parse_args()

    rom = load_rom(a.rom)
    cyc = load_cyc(a.cyc)
    counts, entries = None, None
    for p in a.profile:
        c, e = load_profile(p)
        if counts is None: counts, entries = list(c), list(e)
        else:
            counts = [x + y for x, y in zip(counts, c)]
            entries = [x + y for x, y in zip(entries, e)]
    romwords = len(rom)

    # the hot instructions (profile addresses are word indexes)
    hot = [(counts[i], i * 2) for i in range(min(romwords, len(counts))) if counts[i]]
    hot.sort(reverse=True)
    total = sum(c for c, _ in hot)
    chosen = {}
    covered = 0
    for c, pc in hot:
        if len(chosen) >= a.budget: break
        try:
            ins = Insn(rom, cyc, pc)
            ins.translate()
        except Untranslated as e:
            continue
        chosen[pc] = ins
        covered += c
    print('%d of %d executed instructions translated, %.1f%% of executions' %
          (len(chosen), len(hot), 100.0 * covered / total), file=sys.stderr)

    pcs = sorted(chosen)
    # leaders: entered other than by falling through (profile), branch targets, after an
    # untranslated predecessor, after calls (return addresses)
    lead = set()
    for pc in pcs:
        ins = chosen[pc]
        if entries[pc >> 1]: lead.add(pc)
        if ins.target is not None and ins.target in chosen: lead.add(ins.target)
    for pc in pcs:
        ins = chosen[pc]
        nxt = pc + ins.length
        if nxt in chosen and ins.kind in ('bsr', 'jsr', 'dyn', 'rts', 'bra', 'exit'): lead.add(nxt)
    for i, pc in enumerate(pcs):
        prev_falls = i > 0 and pcs[i - 1] + chosen[pcs[i - 1]].length == pc
        if not prev_falls: lead.add(pc)

    # flag liveness, backwards through each straight run (until a leader or a jump)
    for i in range(len(pcs) - 1, -1, -1):
        pc = pcs[i]
        ins = chosen[pc]
        nxt = pc + ins.length
        if ins.kind == 'seq' and nxt in chosen and nxt not in lead:
            after = chosen[nxt]
            live = (after.live - after.writes_final) | after.reads_final
        else:
            live = set(FLAGS)
        # re-translate with the live set to learn the exact reads/writes
        ins2 = Insn(rom, cyc, pc)
        ins2.live = live
        ins2.translate()
        ins2.writes_final = set(ins2.writes)
        # addx/subx/negx/div/shift-with-helper write flags conditionally: treat as reads
        ins2.reads_final = set(ins2.reads)
        chosen[pc] = ins2

    # -------------------------------------------------------------------- emit C
    out = []
    w = out.append
    w('/* Generated by tools/m68krecomp.py from %s + %s: %d instructions (%.1f%% of the\n'
      ' * profiled executions). Derived from the game ROM - do not commit. */'
      % (os.path.basename(a.rom), ' + '.join(os.path.basename(p) for p in a.profile), len(pcs), 100.0 * covered / total))
    w('#ifndef MD_RECOMP_H\n#define MD_RECOMP_H\n')
    w('#define RC_EXACT %d' % (1 if a.exact else 0))
    w('#define RC_LOCALS %d' % (0 if (not a.locals) else 1))
    w(PROLOGUE)
    leaders = sorted(lead)
    size = 1
    while size < len(leaders) * 2: size *= 2
    table = [None] * size
    for idx, pc in enumerate(leaders):
        h = ((pc >> 1) * 2654435761) & 0xffffffff
        h = (h >> 16) & (size - 1)
        while table[h] is not None: h = (h + 1) & (size - 1)
        table[h] = (pc, idx)
    w('#define RC_HASH_SIZE %d' % size)
    w('static const u32 rc_hash_pc[RC_HASH_SIZE] = {')
    row = []
    for e in table:
        row.append(hx(e[0]) if e else '0xffffffffu')
        if len(row) == 12: w('    ' + ', '.join(row) + ','); row = []
    if row: w('    ' + ', '.join(row) + ',')
    w('};')
    w('static const u16 rc_hash_idx[RC_HASH_SIZE] = {')
    row = []
    for e in table:
        row.append(str(e[1]) if e else '0')
        if len(row) == 24: w('    ' + ', '.join(row) + ','); row = []
    if row: w('    ' + ', '.join(row) + ',')
    w('};')
    w('#define RC_HASH(pc) ((((u32)(pc) >> 1) * 2654435761u) >> 16 & (RC_HASH_SIZE - 1))')
    w('')
    w('/* run translated code from m68k.pc; 0 = the PC is not translated (nothing done) */')
    w('static int md_rc_run(void) {')
    w('    static void *const lbl[%d] = {' % len(leaders))
    row = []
    for pc in leaders:
        row.append('&&L_%06x' % pc)
        if len(row) == 8: w('        ' + ', '.join(row) + ','); row = []
    if row: w('        ' + ', '.join(row) + ',')
    w('    };')
    w('    u32 pc_, h_;')
    w('    int cyc_;')
    if not (not a.locals):
        w('    u32 D0, D1, D2, D3, D4, D5, D6, D7, A0, A1, A2, A3, A4, A5, A6, A7, FX, FN, FZ, FV, FC;')
    w('    /* the CPU state through a base register: i960 loads/stores with a short offset are')
    w('     * half the size of absolute ones (GCC would otherwise fold the address back in) */')
    w('    m68k_t *M_;')
    w('    __asm__("" : "=r"(M_) : "0"(&m68k));')
    w('#define m68k (*M_)')
    w('    pc_ = m68k.pc;')
    w('    cyc_ = m68k.cycles;')
    w('    h_ = RC_HASH(pc_);')
    w('    while (rc_hash_pc[h_] != pc_) { if (rc_hash_pc[h_] == 0xffffffffu) return 0; h_ = (h_ + 1) & (RC_HASH_SIZE - 1); }')
    w('    RC_LOAD;')
    w('    goto *lbl[rc_hash_idx[h_]];')
    w('dispatch:')
    w('    if (m68k.cycles <= 0) { m68k.pc = pc_; return 1; }')
    w('    h_ = RC_HASH(pc_);')
    w('    while (rc_hash_pc[h_] != pc_) { if (rc_hash_pc[h_] == 0xffffffffu) { m68k.pc = pc_; return 1; } h_ = (h_ + 1) & (RC_HASH_SIZE - 1); }')
    w('    goto *lbl[rc_hash_idx[h_]];')

    def goto(target, acc_code):
        """jump to a constant target: charge, check, then goto or leave"""
        if target in lead:
            return '%s pc_ = %s; if (m68k.cycles <= 0) { m68k.pc = pc_; return 1; } goto L_%06x;' % (acc_code, hx(target), target)
        return '%s m68k.pc = %s; return 1;' % (acc_code, hx(target))

    idle_pc = None
    for i in range(romwords - 3):
        if rom[i] == 0x4a38 and rom[i + 2] == 0x66fa: idle_pc = i * 2; break

    acc = 0
    for i, pc in enumerate(pcs):
        ins = chosen[pc]
        if pc in lead:
            if acc: w('    m68k.cycles -= %d;' % acc)
            acc = 0
            w('L_%06x:' % pc)
        body = ' '.join(ins.code).replace('RC_EXIT_FB', 'm68k.cycles -= %d; return 1;' % acc)
        nxt = pc + ins.length
        falls = (i + 1 < len(pcs) and pcs[i + 1] == nxt)
        cyc_now = acc + ins.cyc
        charge = 'm68k.cycles -= %d;' % cyc_now if cyc_now else ''
        k = ins.kind
        line = '    /* %06x */ { %s' % (pc, body)
        if k == 'seq':
            acc = cyc_now
            if a.exact:
                w(line + ' m68k.cycles -= %d; } if (m68k.cycles <= 0) { m68k.pc = %s; return 1; }' % (acc, hx(nxt)))
                acc = 0
                if not falls: w('    m68k.pc = %s; return 1;' % hx(nxt))
                continue
            if falls: w(line + ' }')
            else: w(line + ' %s m68k.pc = %s; return 1; }' % (charge, hx(nxt))); acc = 0
        elif k in ('bra', 'bsr', 'jsr'):
            w(line + ' %s }' % goto(ins.target, charge)); acc = 0
        elif k == 'bcc':
            taken_cyc = cyc_now
            nt = cyc_now + (-2 if ins.short else 2)       # not taken: Bcc.S 2 less, Bcc.W 2 more
            idle = ' md_idle = 1; m68k.cycles = 0; m68k.pc = %s; return 1;' % hx(ins.target) if ins.target == idle_pc else ''
            tk = ('m68k.cycles -= %d;%s' % (taken_cyc, idle)) if idle else goto(ins.target, 'm68k.cycles -= %d;' % taken_cyc)
            if falls and not a.exact:
                w(line + ' if (%s) { %s } }' % (ins.cond, tk))
                acc = nt
            else:
                w(line + ' if (%s) { %s } m68k.cycles -= %d; m68k.pc = %s; return 1; }' % (ins.cond, tk, nt, hx(nxt)))
                acc = 0
        elif k == 'dbcc':
            r = ins.dreg
            w(line + ' if (!(%s)) { u32 r_ = (m68k.d[%d] - 1) & 0xffffu; m68k.d[%d] = (m68k.d[%d] & 0xffff0000u) | r_;'
              ' if (r_ != 0xffffu) { %s } m68k.cycles -= %d; } else m68k.cycles -= %d;'
              % (ins.cond, r, r, r, goto(ins.target, 'm68k.cycles -= %d;' % (cyc_now - 2)), cyc_now + 2, cyc_now))
            if falls and not a.exact:
                w('    }')
            else:
                w('    m68k.pc = %s; return 1; }' % hx(nxt))
            acc = 0
        elif k == 'dyn':
            w(line + ' %s pc_ = %s; goto dispatch; }' % (charge, ins.dyn)); acc = 0
        else:
            raise Exception(k)
    if acc: w('    m68k.cycles -= %d;' % acc)
    w('    m68k.pc = pc_; return 1;')
    w('#undef m68k')
    w('}')
    w('\n#endif')
    fa = next(i for i, l in enumerate(out) if l.startswith('static int md_rc_run'))
    for i in range(fa + 1, len(out)):
        l = out[i]
        if 'cyc_ = m68k.cycles;' in l or l.startswith('#'): continue
        l = l.replace('m68k.cycles', 'cyc_').replace('return 1;', 'RC_RET')
        if not (not a.locals):
            l = re.sub(r'm68k\.d\[(\d)\]', r'D\1', l)
            l = re.sub(r'm68k\.a\[(\d)\]', r'A\1', l)
            l = re.sub(r'm68k\.([xnzvc])\b', lambda m: 'F' + m.group(1).upper(), l)
        else:
            l = l.replace('RC_LOAD;', '').replace('(*M_).x = FX;', '').replace('FN = (*M_).n; FZ = (*M_).z; FV = (*M_).v; FC = (*M_).c; FX = (*M_).x;', '')
        out[i] = l
    text = '\n'.join(out) + '\n'
    text = re.sub(r'\b(s32|s16|s8)\b', r'm68k_\1', text)      # the core's signed types
    with open(a.out, 'w') as f: f.write(text)
    print('%d leaders, hash %d -> %s' % (len(leaders), size, a.out), file=sys.stderr)


if __name__ == '__main__':
    main()
