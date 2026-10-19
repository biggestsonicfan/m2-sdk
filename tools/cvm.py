#!/usr/bin/env python3
"""cvm.py - reference interpreter for the COP-VM bytecode (tools/rv2cvm.py), with a fake TGP.

Runs the same test script as host.c: lines `cmd arg...` (hex) and `mat w0..w11`.
"""
import math, struct, sys
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from rv2cvm import OPS

M = 0xffffffff
def f2u(f):
    if f != f: return 0xffc00000
    try:
        return struct.unpack("<I", struct.pack("<f", f))[0]
    except OverflowError:
        return 0xff800000 if f < 0 else 0x7f800000
def u2f(u): return struct.unpack("<f", struct.pack("<I", u & M))[0]
def s32(v): v &= M; return v - (1 << 32) if v >> 31 else v
def sx(v, b): v &= (1 << b) - 1; return v - (1 << b) if v >> (b - 1) else v

class VM:
    def __init__(self, image, base=0x4000, stack=0x1001f600):   # image[0] is the mark, _start the next word
        self.buf = [0] * 0x8000
        self.buf[base:base + len(image)] = image
        self.base, self.stack = base, stack
        self.dram = [0] * 0x400
        self.dep = self.spx = 0
        self.steps = 0

    def run(self, cmd, fin):
        x = [0] * 32; f = [0] * 32
        x[10] = cmd; x[2] = self.stack
        pc = self.base + 1; out = []; fin = list(fin); used = 0
        buf, dram = self.buf, self.dram
        while True:
            w = buf[pc]; self.steps += 1
            if self.steps - getattr(self, "s0", 0) > 3000000: raise SystemExit("runaway: cmd %x pc %x" % (cmd, pc))
            op = OPS[(w >> 23) & 0xff]; A = (w >> 18) & 31; B = (w >> 13) & 31; C = sx(w, 13); R2 = w & 31
            npc = pc + 1
            def wx(v):
                if A: x[A] = v & M
            if op == "NOP": pass
            elif op == "LI": wx(buf[w & 0x3ffff & 0x7fff])
            elif op == "ADD": wx(x[B] + x[R2])
            elif op == "SUB": wx(x[B] - x[R2])
            elif op == "AND": wx(x[B] & x[R2])
            elif op == "OR": wx(x[B] | x[R2])
            elif op == "XOR": wx(x[B] ^ x[R2])
            elif op == "SLL": wx(x[B] << (x[R2] & 31))
            elif op == "SRL": wx(x[B] >> (x[R2] & 31))
            elif op == "SRA": wx(s32(x[B]) >> (x[R2] & 31))
            elif op == "SLT": wx(int(s32(x[B]) < s32(x[R2])))
            elif op == "SLTU": wx(int(x[B] < x[R2]))
            elif op == "ADDI": wx(x[B] + C)
            elif op == "ANDI": wx(x[B] & (C & M))
            elif op == "ORI": wx(x[B] | (C & M))
            elif op == "XORI": wx(x[B] ^ (C & M))
            elif op == "SLLI": wx(x[B] << (C & 31))
            elif op == "SRLI": wx(x[B] >> (C & 31))
            elif op == "SRAI": wx(s32(x[B]) >> (C & 31))
            elif op == "SLTI": wx(int(s32(x[B]) < C))
            elif op == "SLTIU": wx(int(x[B] < (C & M)))
            elif op in ("LW", "FLW"):
                v = buf[((x[B] + C) & M) >> 2 & 0x7fff]
                if op == "LW": wx(v)
                else: f[A] = v
            elif op in ("SW", "FSW"):
                buf[((x[B] + C) & M) >> 2 & 0x7fff] = x[A] if op == "SW" else f[A]
            elif op == "LBU":
                a = (x[B] + C) & M; wx(buf[a >> 2 & 0x7fff] >> (8 * (a & 3)) & 0xff)
            elif op == "SB":
                a = (x[B] + C) & M; i = a >> 2 & 0x7fff; s = 8 * (a & 3)
                buf[i] = buf[i] & ~(0xff << s) & M | (x[A] & 0xff) << s
            elif op in ("BEQ", "BNE", "BLT", "BGE", "BLTU", "BGEU"):
                a, b = x[B], x[A]
                t = {"BEQ": a == b, "BNE": a != b, "BLT": s32(a) < s32(b), "BGE": s32(a) >= s32(b),
                     "BLTU": a < b, "BGEU": a >= b}[op]
                if t: npc = pc + C
            elif op in ("J", "JAL"):
                wx(0x10000000 | (pc + 1) << 2); npc = pc + sx(w, 18)
            elif op in ("JR", "JALR"):
                t = ((x[B] + C) & M) >> 2 & 0x7fff; wx(0x10000000 | (pc + 1) << 2); npc = t
            elif op in ("FADD", "FSUB", "FMUL", "FDIV"):
                a, b = u2f(f[B]), u2f(f[R2])
                if op == "FADD": r = a + b
                elif op == "FSUB": r = a - b
                elif op == "FMUL": r = a * b
                elif b: r = a / b
                else: r = math.nan if a == 0 or a != a else math.copysign(math.inf, a) * math.copysign(1, b)
                f[A] = f2u(r)
            elif op == "FSQRT":
                v = u2f(f[B]); f[A] = f[B] if not f[B] & 0x7fffffff else (f2u(math.sqrt(v)) if v > 0 or v != v and False else (0xffc00000 if v == v and v < 0 else f2u(math.sqrt(v)) if v == v else 0xffc00000))
            elif op == "FMOV": f[A] = f[B]
            elif op == "FNEG": f[A] = f[B] ^ 0x80000000
            elif op == "FABS": f[A] = f[B] & 0x7fffffff
            elif op in ("FLT", "FLE", "FEQ"):
                a, b = u2f(f[B]), u2f(f[R2]); wx(int({"FLT": a < b, "FLE": a <= b, "FEQ": a == b}[op]))
            elif op == "FCVTWS":
                v = u2f(f[B]); wx(int(v) if abs(v) < 2**31 else 0x80000000)
            elif op == "FCVTSW": f[A] = f2u(float(s32(x[B])))
            elif op == "FMAX": f[A] = f[B] if u2f(f[B]) >= u2f(f[R2]) else f[R2]
            elif op == "FMVXW": wx(f[B])
            elif op == "FMVWX": f[A] = x[B]
            elif op == "IN":
                v = fin[used] if used < len(fin) else 0; used += 1; wx(v)
            elif op == "OUT": out.append(x[B])
            elif op == "SIN": wx(f2u(math.sin((x[B] & 0xffff) * 2 * math.pi / 65536)))
            elif op == "COS": wx(f2u(math.cos((x[B] & 0xffff) * 2 * math.pi / 65536)))
            elif op == "IRD": wx(dram[x[B] & 0x3ff])
            elif op == "IWR": dram[x[B] & 0x3ff] = x[R2]
            elif op == "PUSH":
                self.dep += 1
                if self.dep - 1 < 7: dram[0x200 + self.spx:0x200 + self.spx + 12] = dram[0x60:0x6c]; self.spx += 12
            elif op == "POP":
                if self.dep:
                    self.dep -= 1
                    if self.dep < 7: self.spx -= 12; dram[0x60:0x6c] = dram[0x200 + self.spx:0x200 + self.spx + 12]
            elif op == "RSQRT":
                v = u2f(x[B]); wx(f2u(1 / math.sqrt(v)) if v > 0 and v != math.inf else (0 if v == math.inf else 0xffc00000 if v < 0 or v != v else 0x7f800000))
            elif op == "HALT": return out, min(used, len(fin))
            else: raise SystemExit("bad op %s at %x" % (op, pc))
            pc = npc

def main():
    img = open(sys.argv[1], "rb").read()
    vm = VM(list(struct.unpack("<%dI" % (len(img) // 4), img)))
    for line in sys.stdin:
        t = line.split()
        if not t or t[0].startswith("#"): continue
        if t[0] == "buf":
            w = int(t[1], 16)
            for k, v in enumerate(t[2:]): vm.buf[w + k] = int(v, 16)
            continue
        if t[0] == "mat":
            vm.dram[0x60:0x6c] = [int(v, 16) for v in t[1:13]]; continue
        cmd = int(t[0], 16); fin = [int(v, 16) for v in t[1:]]
        s0 = vm.s0 = vm.steps
        if cmd == 0:                # the kernels' own init: depth 0, the first slot's matrix
            if vm.dep: vm.dram[0x60:0x6c] = vm.dram[0x200:0x20c]
            vm.dep = vm.spx = 0; out, used = [], 0
        else: out, used = vm.run(cmd, fin)
        print("%02x:%s | used %d/%d | mat%s" % (cmd, "".join(" %08x" % v for v in out), used, len(fin),
              "".join(" %08x" % v for v in vm.dram[0x60:0x6c])))
        print("  steps %d" % (vm.steps - s0), file=sys.stderr)

if __name__ == "__main__":
    main()
