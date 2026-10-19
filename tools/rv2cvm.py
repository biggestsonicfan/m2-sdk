#!/usr/bin/env python3
"""rv2cvm.py - translate an rv32if ELF (the COP extension program) into COP-VM bytecode.

The 2A TGP and 2C TGPx4 kernels run the commands they have no native code for in a small
virtual machine whose program lives in buffer RAM. Its instruction set is RV32I + F, one
bytecode word per RISC-V instruction (so branch offsets and return addresses carry over),
re-encoded so the DSP can decode it cheaply:

    bit 31 = 0 | op (8) 30:23 | A (5) 22:18 | B (5) 17:13 | C (13, signed) 12:0

A = rd (rs2 for stores and branches), B = rs1, C = immediate, or rs2 in its low 5 bits.
J/JAL: A = rd, bits 17:0 = signed word offset. LI (lui; auipc, whose value is known):
A = rd, bits 17:0 = buffer-RAM word of the value in the constant pool. An address a in the
VM is buffer RAM word (a >> 2) & 0x7fff; the image is linked at 0x10010004 (word 0x4001), and
word 0x4000 (image word 0) is the mark MARK, which the kernels check before they run the VM.

  rv2cvm.py vm.elf --header out.h --name m2_cop_ext     # C array + layout constants
  rv2cvm.py vm.elf --bin out.bin                         # words from 0x4000, little-endian
"""
import argparse, struct, subprocess, sys, tempfile

OPS = """NOP LI ADD SUB AND OR XOR SLL SRL SRA SLT SLTU ADDI ANDI ORI XORI SLLI SRLI SRAI SLTI SLTIU
LW SW LBU SB FLW FSW BEQ BNE BLT BGE BLTU BGEU JAL J JALR JR
FADD FSUB FMUL FDIV FSQRT FMOV FNEG FABS FLT FLE FEQ FCVTWS FCVTSW FMAX FMVXW FMVWX
IN OUT SIN COS IRD IWR PUSH POP RSQRT HALT""".split()
OP = {n: i for i, n in enumerate(OPS)}

BASE = 0x10000000
MARK = 0x4d32564d                       # "M2VM", image word 0


def word_of(addr):
    return (addr >> 2) & 0x7fff


def syms(elf):
    out = subprocess.run(["llvm-nm-18", elf], capture_output=True, text=True, check=True).stdout
    s = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3:
            s[p[2]] = int(p[0], 16)
    return s


def sext(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


def enc(op, a=0, b=0, c=0):
    assert 0 <= a < 32 and 0 <= b < 32 and -4096 <= c < 4096, (op, a, b, c)
    return OP[op] << 23 | a << 18 | b << 13 | (c & 0x1fff)


def enc18(op, a, v):
    assert -(1 << 17) <= v < (1 << 17)
    return OP[op] << 23 | a << 18 | (v & 0x3ffff)


class Tr:
    def __init__(self, pool_word, pool_cap):
        self.pool, self.pool_idx = [], {}
        self.pool_word, self.pool_cap = pool_word, pool_cap

    def li(self, rd, value):
        value &= 0xffffffff
        if value not in self.pool_idx:
            self.pool_idx[value] = len(self.pool)
            self.pool.append(value)
            if len(self.pool) > self.pool_cap:
                raise SystemExit("constant pool full")
        return enc18("LI", rd, self.pool_word + self.pool_idx[value])

    def one(self, pc, w):
        opc, rd, f3 = w & 0x7f, (w >> 7) & 31, (w >> 12) & 7
        rs1, rs2, f7 = (w >> 15) & 31, (w >> 20) & 31, w >> 25
        immi = sext(w >> 20, 12)
        imms = sext((w >> 25) << 5 | (w >> 7) & 31, 12)
        def bad():
            raise SystemExit("0x%08x: unsupported instruction 0x%08x" % (pc, w))
        if opc == 0x37:                                   # lui
            return enc("NOP") if rd == 0 else self.li(rd, w & 0xfffff000)
        if opc == 0x17:                                   # auipc
            return enc("NOP") if rd == 0 else self.li(rd, pc + (w & 0xfffff000))
        if opc == 0x6f:                                   # jal
            off = sext((w >> 31) << 20 | ((w >> 12) & 0xff) << 12 | ((w >> 20) & 1) << 11 | ((w >> 21) & 0x3ff) << 1, 21)
            assert off % 4 == 0
            return enc18("J" if rd == 0 else "JAL", rd, off // 4)
        if opc == 0x67:                                   # jalr
            return enc("JR" if rd == 0 else "JALR", rd, rs1, immi)
        if opc == 0x63:                                   # branches
            off = sext((w >> 31) << 12 | ((w >> 7) & 1) << 11 | ((w >> 25) & 0x3f) << 5 | ((w >> 8) & 0xf) << 1, 13)
            assert off % 4 == 0
            name = {0: "BEQ", 1: "BNE", 4: "BLT", 5: "BGE", 6: "BLTU", 7: "BGEU"}.get(f3) or bad()
            return enc(name, rs2, rs1, off // 4)
        if opc == 0x03:                                   # loads
            name = {2: "LW", 4: "LBU"}.get(f3) or bad()
            return enc("NOP") if rd == 0 else enc(name, rd, rs1, immi)
        if opc == 0x23:
            name = {2: "SW", 0: "SB"}.get(f3) or bad()
            return enc(name, rs2, rs1, imms)
        if opc == 0x07 and f3 == 2:
            return enc("FLW", rd, rs1, immi)
        if opc == 0x27 and f3 == 2:
            return enc("FSW", rs2, rs1, imms)
        if opc == 0x13:                                   # op-imm
            if rd == 0:
                return enc("NOP")
            if f3 in (1, 5):
                sh = (w >> 20) & 31
                name = "SLLI" if f3 == 1 else ("SRAI" if f7 == 0x20 else "SRLI")
                return enc(name, rd, rs1, sh)
            name = {0: "ADDI", 2: "SLTI", 3: "SLTIU", 4: "XORI", 6: "ORI", 7: "ANDI"}[f3]
            return enc(name, rd, rs1, immi)
        if opc == 0x33:                                   # op
            if f7 not in (0, 0x20):
                bad()                                     # no M extension
            if rd == 0:
                return enc("NOP")
            name = {(0, 0): "ADD", (0, 0x20): "SUB", (1, 0): "SLL", (2, 0): "SLT", (3, 0): "SLTU",
                    (4, 0): "XOR", (5, 0): "SRL", (5, 0x20): "SRA", (6, 0): "OR", (7, 0): "AND"}.get((f3, f7)) or bad()
            return enc(name, rd, rs1, rs2)
        if opc == 0x53:                                   # OP-FP, single precision
            f5 = f7 >> 2
            if f7 & 3:
                bad()
            if f5 in (0, 1, 2, 3):
                return enc(("FADD", "FSUB", "FMUL", "FDIV")[f5], rd, rs1, rs2)
            if f5 == 0x0b and rs2 == 0:
                return enc("FSQRT", rd, rs1)
            if f5 == 0x04:
                if rs1 != rs2:
                    bad()                                 # copysign: the C never needs it
                return enc(("FMOV", "FNEG", "FABS")[f3], rd, rs1)
            if f5 == 0x05 and f3 == 1:
                return enc("FMAX", rd, rs1, rs2)
            if f5 == 0x14:
                return enc("NOP") if rd == 0 else enc(("FLE", "FLT", "FEQ")[f3], rd, rs1, rs2)
            if f5 == 0x18 and rs2 == 0 and f3 == 1:       # fcvt.w.s, rtz
                return enc("NOP") if rd == 0 else enc("FCVTWS", rd, rs1)
            if f5 == 0x1a and rs2 == 0:                   # fcvt.s.w (exact for the C's small ints)
                return enc("FCVTSW", rd, rs1)
            if f5 == 0x1c and f3 == 0 and rs2 == 0:
                return enc("NOP") if rd == 0 else enc("FMVXW", rd, rs1)
            if f5 == 0x1e and f3 == 0 and rs2 == 0:
                return enc("FMVWX", rd, rs1)
            bad()
        if opc == 0x0b:                                   # custom-0: the natives (compat.h)
            key = (f3, f7)
            name = {(0, 0): "IN", (1, 0): "OUT", (2, 0): "SIN", (3, 0): "COS", (4, 0): "IRD", (5, 0): "IWR",
                    (6, 0): "PUSH", (7, 0): "POP", (0, 1): "RSQRT", (0, 2): "HALT"}.get(key) or bad()
            return enc(name, rd, rs1, rs2)
        if w == 0x0000000f or opc == 0x0f:                # fence
            return enc("NOP")
        bad()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--header")
    ap.add_argument("--name", default="m2_cop_ext")
    ap.add_argument("--bin")
    o = ap.parse_args()
    s = syms(o.elf)
    org, text_end, image_end = s["_start"], s["_text_end"], s["_image_end"]
    pool, pool_end, bss_end, stack = s["_pool"], s["_pool_end"], s["_bss_end"], s["_stack_top"]
    with tempfile.TemporaryDirectory() as td:
        subprocess.run(["llvm-objcopy-18", "-O", "binary", "--only-section=.text", "--only-section=.rodata",
                        "--only-section=.data", o.elf, td + "/flat.bin"], check=True)
        flat = open(td + "/flat.bin", "rb").read()
    flat += b"\0" * (-len(flat) % 4)
    words = list(struct.unpack("<%dI" % (len(flat) // 4), flat))
    assert org + 4 * len(words) == image_end + (-image_end % 4), (hex(org), len(words), hex(image_end))
    tr = Tr(word_of(pool), (pool_end - pool) // 4)
    out = [tr.one(org + 4 * i, w) if org + 4 * i < text_end else w for i, w in enumerate(words)]
    out += [0] * (word_of(pool) - word_of(org) - len(out))
    out += tr.pool
    out = [MARK] + out                  # word 0x4000: the kernels run the VM only when it is there
    print("rv2cvm: %d code words, %d data words, %d pool constants, bss to word 0x%x, stack top 0x%x"
          % ((text_end - org) // 4, len(words) - (text_end - org) // 4, len(tr.pool), word_of(bss_end), stack),
          file=sys.stderr)
    if o.bin:
        open(o.bin, "wb").write(struct.pack("<%dI" % len(out), *out))
    if o.header:
        n = o.name
        with open(o.header, "w") as f:
            f.write("/* %s - generated by tools/rv2cvm.py from the COP extension program; do not edit. */\n"
                    % o.header.replace("\\", "/").rsplit("/", 1)[-1])
            f.write("#ifndef %s_IMAGE_H\n#define %s_IMAGE_H\n" % (n.upper(), n.upper()))
            f.write("#define %s_BASE      0x%04xu  /* buffer RAM word of image[0], the mark */\n" % (n.upper(), word_of(org) - 1))
            f.write("#define %s_WORDS     %du\n" % (n.upper(), len(out)))
            f.write("#define %s_BSS_END   0x%04xu  /* zero image end .. here */\n" % (n.upper(), (word_of(bss_end - 1) + 1)))
            f.write("#define %s_STACK_TOP 0x%08xu\n" % (n.upper(), stack))
            f.write("#define %s_ENTRY     0x%04xu  /* VPC of _start */\n" % (n.upper(), word_of(org)))
            f.write("static const unsigned int %s_image[%d] = {\n" % (n, len(out)))
            for i in range(0, len(out), 8):
                f.write("    " + ", ".join("0x%08x" % v for v in out[i:i + 8]) + ",\n")
            f.write("};\n#endif\n")


if __name__ == "__main__":
    main()
