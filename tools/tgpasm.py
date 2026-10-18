#!/usr/bin/env python3
"""tgpasm.py - assembler/disassembler for the Fujitsu MB86233/MB86234 "TGP" (Model 2A coprocessor).

There is no public assembler for this DSP. The syntax here is exactly what MAME's disassembler
prints (src/devices/cpu/mb86233/mb86233d.cpp), so a MAME `dasm` listing reassembles, plus:
labels (`name:`), `#label` branch targets, `.equ NAME, value`, `.word v`, `.org addr`,
`.fill COUNT, instruction` (COUNT copies) and `;` comments.
The encodings come from mb86233d.cpp (and the semantics from mb86233.cpp); `--selftest LISTING`
reassembles a MAME dasm listing (lines `ADDR: OPCODE text`) and checks every word.

  tgpasm.py prog.s -o prog.bin            # little-endian u32 words
  tgpasm.py prog.s --header out.h --name m2_tgp_kernel
  tgpasm.py -d prog.bin                   # disassemble
  tgpasm.py --selftest dc2a.dasm          # round-trip a MAME listing
"""
import argparse, re, struct, sys

REGS = ["b0", "b1", "x0", "x1", "x2", "i0", "i1", "i2", "sp", "pag", "vsm", "dmc", "c0", "c1", "pc", "-",
        "a", "ah", "al", "b", "bh", "bl", "c", "ch", "cl", "d", "dh", "dl", "p", "ph", "pl", "sft",
        "rf0", "rf1", "rf2", "rf3", "rf4", "rf5", "rf6", "rf7", "rf8", "rf9", "rfa", "rfb", "rfc", "rfd", "rfe", "rff",
        "sio0", "si1", "pio", "pioa", "rpc", "r?35", "r?36", "r?37", "pad", "mod", "ear", "st", "mask", "tim", "cx", "dx"]
REGNUM = {n: i for i, n in enumerate(REGS)}

CONDS = {0x00: "zrd", 0x01: "ged", 0x02: "led", 0x0a: "gpio0", 0x0b: "gpio1", 0x0c: "gpio2",
         0x10: "zc0", 0x11: "zc1", 0x12: "gpio3", 0x16: "alw"}
CONDNUM = {v: k for k, v in CONDS.items()}

ALU = {0x01: "andd", 0x02: "orad", 0x03: "eord", 0x04: "notd", 0x05: "fcpd", 0x06: "fadd", 0x07: "fsbd",
       0x08: "fml", 0x09: "fmsd", 0x0a: "fmrd", 0x0b: "fabd", 0x0c: "fsmd", 0x0d: "fspd", 0x0e: "cxfd",
       0x0f: "cfxd", 0x10: "fdvd", 0x11: "fned", 0x13: "d=b+a", 0x14: "d=b-a", 0x16: "lsrd", 0x17: "lsld",
       0x18: "asrd", 0x19: "asld", 0x1a: "addd", 0x1b: "subd"}
ALUNUM = {v: k for k, v in ALU.items()}

CLR0 = ["?0", "?1", "a", "b", "d", "?5", "?6", "?7", "?8", "?9", "?a", "?b", "?c", "?d", "?e", "?f"]
ROUND = ["rn", "rp", "rm", "rz"]
LDI = ["lipl", "lia", "lib", "lid"]

G_MOV = 0x07 << 26          # 0x1c000000
G_STM = 0x36000000          # group 0x0d, bit 25 set as in shipped code
G_LI = 0x0e << 26           # 0x38000000
G_CTL = 0x0f << 26          # 0x3c000000
G_BR = 0xbe000000           # group 0x2f, bit 25 set as in shipped code
NOP_LOW = 0x1f1e10          # "mov a, -": the ALU-only form


# ---------------------------------------------------------------- disassembler (port of mb86233d.cpp)

def _num(v):
    return "%d" % v if v < 10 else "0x%x" % v


def dmem(reg, x1, bank):
    x = "x1" if x1 else "x0"
    k = reg & 0x180
    a = reg & 0x7f
    if k in (0x000, 0x080, 0x100):
        s = ("$0x%x" % (0x200 | a)) if bank else ("$" + _num(a))
        if k == 0x000:
            return s
        pre = s if (bank or a) else ""
        return pre + ("(%s)" % x if k == 0x080 else "(%s+)" % x)
    o = "[" if reg & 0x40 else "("
    o += x if reg & 0x20 else "b" + x
    if reg & 0x10:
        o += "-" + _num(0x10 - (reg & 0xf))
    elif reg & 0xf:
        o += "+" + _num(reg & 0xf)
    o += "]" if reg & 0x40 else ")"
    if bank:
        o += "+0x200"
    return o


def disasm(op):
    g = (op >> 26) & 0x3f
    if g == 0x00:
        r1, r2, alu, sub = op & 0x1ff, (op >> 9) & 0x1ff, (op >> 21) & 0x1f, (op >> 18) & 7
        s = (ALU.get(alu, "alu0_func(%02x)" % alu) + " : ") if alu else ""
        if sub in (0, 1):
            return s + "lab %s, %s (e)" % (dmem(r1, 0, 0), dmem(r2, 1, 0))
        if sub == 3:
            return s + "lab %s, %s" % (dmem(r1, 0, 0), dmem(r2, 1, 1))
        if sub == 4:
            return s + "lab %s, %s" % (dmem(r1, 0, 1), dmem(r2, 1, 0))
        return s + "lab {%d} %s, %s" % (sub, dmem(r1, 0, 0), dmem(r2, 1, 0))
    if g == 0x07:
        r1, r2, alu, sub = op & 0x1ff, (op >> 9) & 0x1ff, (op >> 21) & 0x1f, (op >> 18) & 7
        s = ""
        if alu:
            s = ALU.get(alu, "alu0_func(%02x)" % alu)
            if (op & 0x1fffff) == NOP_LOW:
                return s
            s += " : "
        if sub == 0:
            return s + "mov {0} %s, %s (e)" % (dmem(r1, 0, 0), dmem(r2, 1, 0))
        if sub == 1:
            return s + "mov %s, %s (e)" % (dmem(r1, 0, 0), dmem(r2, 1, 0))
        if sub == 2:
            return s + "mov %s (e), %s" % (dmem(r1, 0, 0), dmem(r2, 1, 0))
        if sub == 3:
            return s + "mov %s, %s" % (dmem(r1, 0, 0), dmem(r2, 1, 1))
        if sub == 4:
            return s + "mov %s, %s" % (dmem(r1, 0, 1), dmem(r2, 1, 0))
        if sub == 5:
            return s + "mov %s (o), %s" % (dmem(r1, 0, 0), dmem(r2, 1, 0))
        if sub == 7:
            k, r = r2 >> 6, REGS[r2 & 0x3f]
            if k == 0: return s + "mov %s, %s" % (r, dmem(r1, 1, 0))
            if k == 1: return s + "mov %s, %s (e)" % (r, dmem(r1, 1, 0))
            if k == 2: return s + "mov %s, %s" % (dmem(r1, 1, 1), r)
            if k == 3: return s + "mov %s, %s" % (dmem(r1, 1, 0), r)
            if k == 4: return s + "mov %s (e), %s" % (dmem(r1, 1, 0), r)
            if k == 5: return s + "mov %s (o), %s" % (dmem(r1, 0, 0), r)
            if k == 6:
                if r1 >> 6:
                    return s + "mov {r1 %d} %s, %s" % (r1 >> 6, REGS[r1 & 0x3f], r)
                return s + "mov %s, %s" % (REGS[r1 & 0x3f], r)
            return s + "mov {r2 %d} %s, %s" % (k, dmem(r1, 0, 0), r)
        return s + "mov {%d} %s, %s" % (sub, dmem(r1, 0, 0), dmem(r2, 1, 0))
    if g == 0x0d:
        if (op >> 17) & 7 == 5:
            return "stmh" + (" fp" if op & 1 else "") + " " + ROUND[(op >> 1) & 3]
        return "unk %02x.%x" % (op >> 26, (op >> 17) & 7)
    if g == 0x0e:
        return "%s #0x%x" % (LDI[(op >> 24) & 3], op & 0xffffff)
    if g == 0x0f:
        alu, sub = (op >> 20) & 0x1f, (op >> 17) & 7
        s = (ALU.get(alu, "alu0_func(%02x)" % alu) + " : ") if alu else ""
        if sub == 0:
            l = [CLR0[i] for i in range(16) if op & (1 << i)]
            return s + "clr0" + (" " + ", ".join(l) if l else "")
        if sub == 1: return s + "clr1 #0x%04x" % (op & 0xffff)
        if sub == 2:
            if op & 0x8000: return s + "rep " + REGS[op & 0x3f]
            if not op & 0xff: return s + "rep #0x100"
            return s + "rep #" + _num(op & 0xff)
        if sub == 3: return s + "set #0x%04x" % (op & 0xffff)
        return s + "unk %02x.%x" % (op >> 26, sub)
    if 0x10 <= g <= 0x1f:
        return "ldi #0x%x, %s" % (op & 0xffffff, REGS[(op >> 24) & 0x3f])
    if g in (0x2f, 0x3f):
        cond, sub, data = (op >> 20) & 0x1f, (op >> 17) & 7, op & 0xffff
        c = ("!" if op & 0x40000000 else "") + CONDS.get(cond, "cond(%02x)" % cond)
        tgt = lambda: REGS[op & 0x1f] if op & 0x4000 else "(%s)" % dmem(op & 0x1ff, 0, 0)
        if sub == 0: return "brif %s #0x%x" % (c, data)
        if sub == 1: return "brul %s %s" % (c, tgt())
        if sub == 2: return "bsif %s #0x%x" % (c, data)
        if sub == 3: return "bsul %s %s" % (c, tgt())
        if sub == 5: return "rtif %s" % c
        if sub == 6: return "ldif %s %s, %s" % (c, dmem(data & 0x1ff, 0, 0), REGS[(data >> 9) & 0x3f])
        if sub == 7: return "iret"
        return "unk %02x.%d" % (op >> 26, sub)
    return "unk %02x" % (op >> 26)


# ---------------------------------------------------------------- assembler

class AsmError(Exception):
    pass


class Asm:
    def __init__(self):
        self.syms = {}

    def val(self, s):
        s = s.strip()
        if s.startswith("#"):
            s = s[1:]
        try:
            return int(eval(s, {"__builtins__": {}}, dict(self.syms)))
        except Exception:
            raise AsmError("bad value %r" % s)

    def fval(self, s):
        """#1.5f style float immediates -> IEEE bits (for lid/ldi of float constants)."""
        return struct.unpack("<I", struct.pack("<f", float(s)))[0]

    def mem(self, s, x1):
        """Parse a memory operand for the x0 (x1=False) or x1 slot. Returns (9-bit field, bank)."""
        s = s.strip()
        bank = False
        if s.endswith("+0x200"):
            s, bank = s[:-6], True
        xr = "x1" if x1 else "x0"
        m = re.fullmatch(r"([\[(])(b?)(x[01])([+-][0-9a-fx]+)?([\])])", s)
        if m:
            if m.group(3) != xr:
                raise AsmError("operand %r must use %s here" % (s, xr))
            r = 0x180 | (0x40 if m.group(1) == "[" else 0) | (0 if m.group(2) else 0x20)
            off = self.val(m.group(4)) if m.group(4) else 0
            if not -16 <= off <= 15:
                raise AsmError("offset out of range in %r" % s)
            return r | (off & 0x1f), bank
        m = re.fullmatch(r"(\$[^()]+)?(?:\((x[01])(\+?)\))?", s)
        if not m or (not m.group(1) and not m.group(2)):
            raise AsmError("bad memory operand %r" % s)
        a = self.val(m.group(1)[1:]) if m.group(1) else 0
        if a & 0x200:
            bank = True
        if a & ~0x27f:
            raise AsmError("address out of range in %r" % s)
        a &= 0x7f
        if m.group(2):
            if m.group(2) != xr:
                raise AsmError("operand %r must use %s here" % (s, xr))
            return (0x100 if m.group(3) else 0x080) | a, bank
        return a, bank

    @staticmethod
    def ismem(s):
        return s.strip()[:1] in "$(["

    def reg(self, s):
        s = s.strip()
        if s not in REGNUM:
            raise AsmError("unknown register %r" % s)
        return REGNUM[s]

    def cond(self, s):
        inv = s.startswith("!")
        c = s[1:] if inv else s
        if c in CONDNUM:
            n = CONDNUM[c]
        else:
            m = re.fullmatch(r"cond\(([0-9a-f]+)\)", c)
            if not m:
                raise AsmError("unknown condition %r" % s)
            n = int(m.group(1), 16)
        return (0x40000000 if inv else 0) | (n << 20)

    @staticmethod
    def split_ops(s):
        out, depth, cur = [], 0, ""
        for ch in s:
            if ch in "([": depth += 1
            if ch in ")]": depth -= 1
            if ch == "," and depth == 0:
                out.append(cur.strip()); cur = ""
            else:
                cur += ch
        if cur.strip():
            out.append(cur.strip())
        return out

    def encode(self, text):
        t = text.strip()
        alu = 0
        m = re.match(r"^([a-z0-9=+\-]+)\s*:\s*(.*)$", t)
        if m and m.group(1) in ALUNUM:
            alu, t = ALUNUM[m.group(1)], m.group(2)
        elif t in ALUNUM:
            return G_MOV | (ALUNUM[t] << 21) | NOP_LOW
        if t == "nop":
            return G_MOV | (alu << 21) | NOP_LOW
        mn, _, rest = t.partition(" ")
        rest = rest.strip()

        if mn in ("mov", "lab"):
            sub = None
            m = re.match(r"^\{(\d+)\}\s*(.*)$", rest)
            if m:
                sub, rest = int(m.group(1)), m.group(2)
            ops = self.split_ops(rest)
            if len(ops) != 2:
                raise AsmError("%s needs two operands: %r" % (mn, text))
            a, b = ops
            ae = a.endswith("(e)"); a = a[:-3].strip() if ae else a
            ao = a.endswith("(o)"); a = a[:-3].strip() if ao else a
            be = b.endswith("(e)"); b = b[:-3].strip() if be else b
            base = (G_MOV if mn == "mov" else 0) | (alu << 21)
            if mn == "lab":
                r1, k1 = self.mem(a, False)
                r2, k2 = self.mem(b, True)
                if be:
                    s = 0 if sub is None else sub
                elif k2:
                    s = 3
                elif k1:
                    s = 4
                else:
                    s = sub if sub is not None else 0
                return base | (s << 18) | (r2 << 9) | r1
            am, bm = self.ismem(a), self.ismem(b)
            if am and bm:
                if sub is not None and not (ae or be):
                    r1, _ = self.mem(a, False); r2, _ = self.mem(b, True)
                    return base | (sub << 18) | (r2 << 9) | r1
                if ao:
                    r1, _ = self.mem(a, False); r2, _ = self.mem(b, True); s = 5
                elif be:
                    r1, _ = self.mem(a, False); r2, _ = self.mem(b, True); s = 0 if sub == 0 else 1
                elif ae:
                    r1, _ = self.mem(a, False); r2, _ = self.mem(b, True); s = 2
                else:
                    r1, k1 = self.mem(a, False); r2, k2 = self.mem(b, True)
                    if k2 and not k1: s = 3
                    elif k1 and not k2: s = 4
                    else: raise AsmError("mem->mem mov needs exactly one +0x200 operand: %r" % text)
                return base | (s << 18) | (r2 << 9) | r1
            if not am and bm:            # reg -> mem / io
                r = self.reg(a)
                m1, k = self.mem(b, True)
                if k: raise AsmError("reg -> banked memory is not encodable: %r" % text)
                return base | (7 << 18) | (((1 if be else 0) << 6 | r) << 9) | m1
            if am and not bm:            # mem / io / prog -> reg
                r = self.reg(b)
                m2 = re.match(r"^\{r2 (\d+)\}\s*(.*)$", a)
                if ao:
                    m1, _ = self.mem(a, False); k = 5
                elif ae:
                    m1, _ = self.mem(a, True); k = 4
                else:
                    m1, bk = self.mem(a, True); k = 2 if bk else 3
                return base | (7 << 18) | ((k << 6 | r) << 9) | m1
            m = re.match(r"^\{r1 (\d+)\}\s*(.*)$", rest)
            hi = 0
            if m:
                hi = int(m.group(1)); a = self.split_ops(m.group(2))[0]
            return base | (7 << 18) | ((6 << 6 | self.reg(b)) << 9) | (hi << 6) | self.reg(a)

        if mn == "stmh":
            w = rest.split()
            fp = 1 if "fp" in w else 0
            rnd = [x for x in w if x in ROUND]
            return G_STM | (5 << 17) | ((ROUND.index(rnd[0]) if rnd else 0) << 1) | fp
        if mn in LDI:
            if alu: raise AsmError("no ALU op on %s" % mn)
            return G_LI | (LDI.index(mn) << 24) | (self.imm24(rest) & 0xffffff)
        if mn == "clr0":
            bits = 0
            for x in self.split_ops(rest):
                bits |= 1 << CLR0.index(x)
            return G_CTL | (alu << 20) | bits
        if mn == "clr1":
            return G_CTL | (alu << 20) | (1 << 17) | (self.val(rest) & 0xffff)
        if mn == "set":
            return G_CTL | (alu << 20) | (3 << 17) | (self.val(rest) & 0xffff)
        if mn == "rep":
            if rest.startswith("#"):
                n = self.val(rest)
                if not 1 <= n <= 0x100: raise AsmError("rep count 1..256")
                return G_CTL | (alu << 20) | (2 << 17) | (n & 0xff)
            return G_CTL | (alu << 20) | (2 << 17) | 0x8000 | self.reg(rest)
        if mn == "ldi":
            if alu: raise AsmError("no ALU op on ldi")
            v, r = self.split_ops(rest)
            return 0x40000000 | (self.reg(r) << 24) | (self.imm24(v) & 0xffffff)
        if mn in ("brif", "bsif", "brul", "bsul", "rtif", "ldif", "iret"):
            if alu: raise AsmError("no ALU op on a branch")
            if mn == "iret":
                return G_BR | (0x16 << 20) | (7 << 17)
            c, _, arg = rest.partition(" ")
            base = G_BR | self.cond(c)
            if mn in ("brif", "bsif"):
                v = self.val(arg)
                if not 0 <= v <= 0xffff: raise AsmError("branch target out of range")
                return base | ((0 if mn == "brif" else 2) << 17) | v
            if mn in ("brul", "bsul"):
                sub = 1 if mn == "brul" else 3
                arg = arg.strip()
                if arg.startswith("(") and arg.endswith(")"):
                    r1, _ = self.mem(arg[1:-1], False)
                    return base | (sub << 17) | r1
                r = self.reg(arg)
                if r > 0x1f: raise AsmError("brul/bsul register must be 0..0x1f")
                return base | (sub << 17) | 0x4000 | r
            if mn == "rtif":
                return base | (5 << 17)
            mo, rg = self.split_ops(arg)
            r1, _ = self.mem(mo, False)
            return base | (6 << 17) | (self.reg(rg) << 9) | r1
        raise AsmError("unknown instruction %r" % text)

    def imm24(self, s):
        s = s.strip()
        if s.startswith("#"):
            s = s[1:]
        if s.startswith("f:"):                     # #f:1.5 = float bits (the low 24 bits only!)
            return self.fval(s[2:])
        return self.val(s)

    def assemble(self, src):
        lines = []
        for n, raw in enumerate(src.splitlines(), 1):
            t = raw.split(";", 1)[0].strip()
            if t:
                lines.append((n, t))
        # pass 1: labels / equs
        pc = 0
        for n, t in lines:
            while True:
                m = re.match(r"^([A-Za-z_.][\w.]*):\s*(.*)$", t)
                if not m: break
                self.syms[m.group(1)] = pc
                t = m.group(2)
            if not t: continue
            if t.startswith(".equ"):
                k, v = self.split_ops(t[4:])
                self.syms[k] = self.val(v)
                continue
            if t.startswith(".org"):
                pc = self.val(t[4:]); continue
            if t.startswith(".fill"):
                pc += self.val(self.split_ops(t[5:])[0]); continue
            pc += 1
        # pass 2
        out, pc = {}, 0
        for n, t in lines:
            t = re.sub(r"^(?:[A-Za-z_.][\w.]*:\s*)+", "", t)
            if not t or t.startswith(".equ"): continue
            if t.startswith(".org"):
                pc = self.val(t[4:]); continue
            try:
                if t.startswith(".fill"):
                    cnt, _, ins = t[5:].partition(",")
                    w = self.encode(ins)
                    for _ in range(self.val(cnt)):
                        out[pc] = w; pc += 1
                    continue
                w = self.val(t[5:]) & 0xffffffff if t.startswith(".word") else self.encode(t)
            except AsmError as e:
                raise AsmError("line %d: %s  [%s]" % (n, e, t))
            out[pc] = w
            pc += 1
        size = max(out) + 1 if out else 0
        return [out.get(i, 0) for i in range(size)]


def selftest(path):
    a = Asm()
    bad = enc = 0
    for line in open(path):
        m = re.match(r"^([0-9A-F]{4}): ([0-9A-F]{8}) (.*)$", line.rstrip("\n"))
        if not m:
            continue
        word, text = int(m.group(2), 16), m.group(3)
        if disasm(word) != text:
            bad += 1
            if bad < 20: print("DASM  %s %08X  mame=%r  ours=%r" % (m.group(1), word, text, disasm(word)))
            continue
        try:
            w2 = a.encode(text)
        except AsmError as e:
            enc += 1
            if enc < 20: print("ASM   %s %08X  %r: %s" % (m.group(1), word, text, e))
            continue
        if w2 != word and disasm(w2) != text:
            enc += 1
            if enc < 20: print("ASM   %s %08X -> %08X  %r" % (m.group(1), word, w2, text))
    print("selftest: %d disassembly mismatches, %d assembly mismatches" % (bad, enc))
    return 1 if bad or enc else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", nargs="?")
    ap.add_argument("-o", "--out")
    ap.add_argument("--header")
    ap.add_argument("--name", default="m2_tgp_program")
    ap.add_argument("-d", "--disasm", action="store_true")
    ap.add_argument("--selftest")
    a = ap.parse_args()
    if a.selftest:
        return selftest(a.selftest)
    if a.disasm:
        data = open(a.src, "rb").read()
        for i in range(len(data) // 4):
            w = struct.unpack_from("<I", data, i * 4)[0]
            print("%04X: %08X %s" % (i, w, disasm(w)))
        return 0
    try:
        words = Asm().assemble(open(a.src).read())
    except AsmError as e:
        print("%s: %s" % (a.src, e), file=sys.stderr)
        return 1
    if a.out:
        open(a.out, "wb").write(b"".join(struct.pack("<I", w) for w in words))
    if a.header:
        with open(a.header, "w", newline="\n") as f:
            f.write("/* Generated by tools/tgpasm.py from %s - do not edit. */\n" % a.src.replace("\\", "/").split("/")[-1])
            f.write("static const u32 %s[%d] = {\n" % (a.name, len(words)))
            for i in range(0, len(words), 6):
                f.write("    " + ", ".join("0x%08Xu" % w for w in words[i:i + 6]) + ",\n")
            f.write("};\n")
    if not a.out and not a.header:
        for i, w in enumerate(words):
            print("%04X: %08X %s" % (i, w, disasm(w)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
