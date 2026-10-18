#!/usr/bin/env python3
"""tgpx4asm.py — assembler / disassembler for the Fujitsu MB86235 (TGPx4), Model 2C's COP.

The syntax is MAME's disassembler's (src/devices/cpu/mb86235/mb86235d.cpp), so a MAME `dasm`
listing of a 2C game's copro program reassembles. One instruction per line, its parts joined by
" : ":

    ALU : MUL : transfer|control    two operations (types 0, 1, 2)
    ALU|MUL : transfer|control      one operation (types 4, 5, 6)
    MOV3 #imm32, dst                type 7

    FADD AA0, AA1, AA0 : DRET
    NOP : MOV2 FI, AA0
    MOV3 #3F800000, AA2

Source also takes `label:`, `.equ NAME, value`, `.org addr`, `.word value64` (or `.word hi32, lo32`),
`; comments`, and `#f:<float>` for an immediate written as a float (MOV3 #f:1.5, AA0). Numbers are
hex as in MAME's listing (`DJMP 54C`, `#17H`, `0x17`) unless they are labels or .equ names.

The relative branches (DBLP, DBBC, DBBS) go to pc + 1 + rel in MAME's interpreter (mb86235ops.cpp
increments pc before the instruction runs), but the disassembler prints pc + rel. In source the
target is where the CPU really goes; only --selftest reads it the disassembler's way, so a listing
round-trips. All 4096 words of Dynamite Cop's program (dynamcopc) reassemble bit for bit.

    tgpx4asm.py src.s -o out.bin [--header out.h --name sym]   assemble
    tgpx4asm.py -d out.bin                                     disassemble
    tgpx4asm.py --selftest listing.dasm                        reassemble a MAME dasm listing
"""
import re
import struct
import sys

REG = ["MA0", "MA1", "MA2", "MA3", "MA4", "MA5", "MA6", "MA7",
       "AA0", "AA1", "AA2", "AA3", "AA4", "AA5", "AA6", "AA7",
       "EB", "EBU", "EBL", "EO", "SP", "ST", "MOD", "LRPC",
       "AR0", "AR1", "AR2", "AR3", "AR4", "AR5", "AR6", "AR7",
       "MB0", "MB1", "MB2", "MB3", "MB4", "MB5", "MB6", "MB7",
       "AB0", "AB1", "AB2", "AB3", "AB4", "AB5", "AB6", "AB7",
       "PR", "FI", "FO0", "FO1", "PDR", "DDR", "PRP", "PWP"] + ["???"] * 72
COND = ["MN", "MZ", "MV", "MU", "ZD", "NR", "IL", "ZC", "AN", "AZ", "AV", "AU", "MD", "AD", None, None,
        "F0", "F1", "F2", None, "IFF", "IFE", "OFF", "OFE", "IF"] + [None] * 39
MI1 = REG[0:8] + REG[32:40]
_C = ["-1.0E+0", "0.0E+0", "0.5E+0", "1.0E+0", "1.5E+0", "2.0E+0", "3.0E+0", "5.0E+0"]
MI2 = MI1 + ["PR", "PR++", "PR--", "PR#0", "???", "???", "???", "???"] + _C
MO = REG[0:8] + REG[32:40] + REG[8:16] + REG[40:48]
AI1 = REG[8:16] + REG[40:48]
AI2 = AI1 + ["PR", "PR++", "PR--", "PR#0", "???", "???", "???", "???", "0", "1", "-1"] + ["???"] * 5
AI2F = AI1 + ["PR", "PR++", "PR--", "PR#0", "???", "???", "???", "???"] + _C

# ALU ops: name -> (code, operand shape). 3 = i1,i2,o  2 = i1,i2  1 = i1,o  S = i1,#imm,o  0 = none
ALU = {"FADD": (0x00, "3f"), "FADDZ": (0x01, "3f"), "FSUB": (0x02, "3f"), "FSUBZ": (0x03, "3f"),
       "FCMP": (0x04, "2f"), "FABS": (0x05, "1"), "FABC": (0x06, "3f"), "NOP": (0x07, "0"),
       "FEA": (0x08, "S"), "FES": (0x09, "S"), "FRCP": (0x0a, "1"), "FRSQ": (0x0b, "1"),
       "FLOG": (0x0c, "1"), "CIF": (0x0d, "1"), "CFI": (0x0e, "1"), "CFIB": (0x0f, "1"),
       "ADD": (0x10, "3"), "ADDZ": (0x11, "3"), "SUB": (0x12, "3"), "SUBZ": (0x13, "3"),
       "CMP": (0x14, "2"), "ABS": (0x15, "1"), "ATR": (0x16, "1"), "ATRZ": (0x17, "1"),
       "AND": (0x18, "3"), "OR": (0x19, "3"), "XOR": (0x1a, "3"), "NOT": (0x1b, "1"),
       "LSR": (0x1c, "S"), "LSL": (0x1d, "S"), "ASR": (0x1e, "S"), "ASL": (0x1f, "S")}
ALU_NAME = {v[0]: (k, v[1]) for k, v in ALU.items()}
CTRL_BR = {0x10: "DB", 0x11: "DBN", 0x18: "DC", 0x19: "DCN"}


def sext(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


# ---------------------------------------------------------------- disassembler (mb86235d.cpp)

def d_ea(md, arx, ary, disp):
    s = "B(" if arx & 0x20 else "A("
    arx &= 7
    s += {0: "@AR%d" % arx, 1: "@AR%d++" % arx, 2: "@AR%d--" % arx, 3: "@AR%d++%XH" % (arx, disp),
          4: "@AR%d+AR%d" % (arx, ary), 5: "@AR%d+AR%d++" % (arx, ary), 6: "@AR%d+AR%d--" % (arx, ary),
          7: "@AR%d+AR%d++%XH" % (arx, ary, disp), 8: "@AR%d+AR%dU" % (arx, ary),
          9: "@AR%d+AR%dL" % (arx, ary), 10: "@AR%d+%XH" % (arx, disp),
          11: "@AR%d+AR%d+%XH" % (arx, ary, disp), 12: "%XH" % disp, 13: "@AR%d+[AR%d++]" % (arx, ary),
          14: "@AR%d+[AR%d--]" % (arx, ary), 15: "@AR%d+[AR%d++%XH]" % (arx, ary, disp)}[md]
    return s + ")"


def d_alu_mul(op, twoop):
    ma = (op >> 41) & 1
    o, i2, i1 = (op >> 42) & 0x1f, (op >> 47) & 0x1f, (op >> 52) & 0xf
    s = ""
    if twoop or ma:
        name, shape = ALU_NAME[(op >> 56) & 0x1f]
        a2 = AI2F if shape.endswith("f") else AI2
        s = {"3f": "%s %s, %s, %s" % (name, AI1[i1], a2[i2], MO[o]),
             "3": "%s %s, %s, %s" % (name, AI1[i1], a2[i2], MO[o]),
             "2f": "%s %s, %s" % (name, AI1[i1], a2[i2]), "2": "%s %s, %s" % (name, AI1[i1], a2[i2]),
             "1": "%s %s, %s" % (name, AI1[i1], MO[o]), "S": "%s %s, #%XH, %s" % (name, AI1[i1], i2, MO[o]),
             "0": "NOP"}[shape]
    if twoop:
        mi2, mi1, mo = (op >> 32) & 0x1f, (op >> 37) & 0xf, (op >> 27) & 0x1f
        s += " : %s %s, %s, %s" % ("FMUL" if (op >> 41) & 1 else "MUL", MI1[mi1], MI2[mi2], MO[mo])
    elif not ma:
        s += "%s %s, %s, %s" % ("FMUL" if (op >> 56) & 1 else "MUL", MI1[i1], MI2[i2], MO[o])
    return s


def d_dst(op, pc):
    ef2 = op & 0xffff
    r = REG[(ef2 >> 6) & 0x3f]
    m = (op >> 12) & 0xf
    return {0: "%03X" % (ef2 & 0xfff), 1: "%03X" % ((pc + sext(op, 12)) & 0xffffffff), 2: r, 3: "+" + r,
            4: r, 5: "+" + r, 6: r, 7: "+" + r, 8: "A(%03X)" % (ef2 & 0x3ff), 9: "+A(%03X)" % (ef2 & 0x3ff),
            10: "B(%03X)" % (ef2 & 0x3ff), 11: "+B(%03X)" % (ef2 & 0x3ff), 12: "A(%s)" % r,
            13: "+A(%s)" % r, 14: "B(%s)" % r, 15: "+B(%s)" % r}[m]


def d_control(op, pc):
    ef1, ef2, cop = (op >> 16) & 0x3f, op & 0xffff, (op >> 22) & 0x1f
    rel = "%03X" % ((pc + sext(op, 12)) & 0xffffffff)
    if cop == 0x00:
        return "NOP"
    if cop in (1, 2):
        n = "REP" if cop == 1 else "SETL"
        return "%s #%04XH" % (n, ef2) if ef1 == 0 else "%s AR%d" % (n, (ef2 >> 12) & 7)
    if cop == 3:
        return {1: "CLRFI", 2: "CLRFO", 3: "CLRF"}.get(ef1, "")
    if cop in (4, 5):
        return "%s %s" % ("PUSH" if cop == 4 else "POP", REG[(ef2 >> 6) & 0x3f])
    if cop == 8:
        return "SETM #%04XH" % ef2
    if cop == 9:
        return "SETM #%Xh, CBSA" % ((ef2 >> 12) & 7)
    if cop == 0xa:
        return "SETM #%Xh, CBSB" % ((ef2 >> 8) & 7)
    if cop == 0xb:
        return "SETM #%Xh, RF" % ((ef2 >> 7) & 1)
    if cop == 0xc:
        return "SETM #%Xh, RDY" % ((ef2 >> 4) & 1)
    if cop == 0xd:
        return "SETM #%Xh, WAIT" % (ef2 & 7)
    if cop == 0x13:
        return "DBLP " + rel
    if cop in (0x14, 0x15):
        return "%s AR%d:%d, %s" % ("DBBC" if cop == 0x14 else "DBBS", (op >> 13) & 7, (op >> 16) & 0xf, rel)
    if cop == 0x1b:
        return "DRET"
    if cop in CTRL_BR:
        c = COND[ef1]
        return "%s %s" % (CTRL_BR[cop] + c if c else "???", d_dst(op, pc))
    if cop == 0x12:
        return "DJMP " + d_dst(op, pc)
    if cop == 0x1a:
        return "DCALL " + d_dst(op, pc)
    return ""


_MVD_A = {0: "A(@AR{x})", 1: "A(@AR{x}++)", 2: "A(@AR{x}--)", 4: "A(@AR{x}+AR{y})", 5: "A(@AR{x}+AR{y}++)",
          6: "A(@AR{x}+AR{y}--)", 8: "A(@AR{x}+AR{y}U)", 9: "A(@AR{x}+AR{y}L)", 13: "A(@AR{x}+[AR{y}++])",
          14: "A(@AR{x}+[AR{y}--])"}
_MVD_B = {0: "B(@AR{x})", 1: "B(@AR{x}++)", 2: "B(@AR{x}--)", 4: "B(@AR{x}+AR{y})", 5: "B(@AR{x}+AR{y}++)",
          6: "B(@AR{x}+AR{y}--)", 8: "B(@AR{x}+AR{y}L)", 9: "B(@AR{x}+AR{y}U)", 13: "B(@AR{x}+[AR{y}++])",
          14: "B(@AR{x}+[AR{y}--])"}


def d_dxfer1(op):
    sd = (op >> 25) & 3
    if sd == 0:
        return "MVD1 %s, %s, %s, %s" % (REG[(op >> 20) & 0x1f], REG[(op >> 15) & 0x1f],
                                        REG[(op >> 10) & 0x1f], REG[(op >> 5) & 0x1f])
    if sd == 3:
        return "MVD1 ???"
    areg, ax, ay = REG[(op >> 20) & 0x1f], (op >> 17) & 7, (op >> 14) & 7
    breg, bx, by, md = REG[(op >> 10) & 0xf], (op >> 7) & 7, (op >> 4) & 7, op & 0xf
    if md not in _MVD_A:
        return "MVD1 ???"
    a, b = _MVD_A[md].format(x=ax, y=ay), _MVD_B[md].format(x=bx, y=by)
    return "MVD1 " + ("%s, %s, %s, %s" % (areg, a, breg, b) if sd == 1 else "%s, %s, %s, %s" % (a, areg, b, breg))


def _eoff(sr):
    return "E(@EB+EO++-%XH)" % (0x40 - (sr & 0x3f)) if sr & 0x20 else "E(@EB+EO++%XH)" % (sr & 0x3f)


def d_xfer1(op):
    dr, sr, md, ary, disp = (op >> 12) & 0x7f, (op >> 19) & 0x7f, op & 0xf, (op >> 4) & 7, (op >> 7) & 0x1f
    trm, dir_ = (op >> 26) & 1, (op >> 25) & 1
    rd = REG[dr] if not dr & 0x40 else d_ea(md, dr, ary, disp)
    if trm == 0:
        if sr == 0x58:
            return "MOV1 #%XH, %s" % (op & 0xfff, REG[dr])
        rs = REG[sr] if not sr & 0x40 else d_ea(md, sr, ary, disp)
        return "MOV1 %s, %s" % (rs, rd)
    if dir_ == 0:
        return "MOV1 " + (REG[dr] + ", " if not dr & 0x40 else rd) + _eoff(sr)
    return "MOV1 " + _eoff(sr) + ", " + rd


def d_dx2field(sd, isb, f):
    if sd == 0:
        s, d = (f >> 13) & 0x1f, (f >> 8) & 0x1f
        if isb:
            s, d = s | 0x20, d | 0x20
        return "%s, %s" % (REG[s], REG[d])
    if sd == 3:
        return "???"
    reg, x, y, md = REG[(f >> 13) & 0x1f], (f >> 10) & 7, (f >> 7) & 7, f & 0xf
    ea = {0: "A(@AR{x})", 1: "A(@AR{x}++)", 2: "A(@AR{x}--)", 4: "A(@AR{x}+AR{y}))", 5: "A(@AR{x}+AR{y}++)",
          6: "A(@AR{x}+AR{y}--)", 8: "A(@AR{x}+AR{y}U)", 9: "A(@AR{x}+AR{y}L)", 13: "A(@AR{x}+[AR{y}++])",
          14: "A(@AR{x}+[AR{y}--]"}.get(md)
    if ea is None:
        return "???"
    ea = ea.format(x=x, y=y)
    return "%s, %s" % (reg, ea) if sd == 1 else "%s, %s" % (ea, reg)


_MOVI_B = {0: "B(@AR{x})", 1: "B(@AR{x}++)", 2: "B(@AR{x}--)", 3: "B(@AR{x}++{d:X}H)", 4: "B(@AR{x}+AR{y})",
           5: "B(@AR{x}+AR{y}++)", 6: "B(@AR{x}+AR{y}--)", 7: "B(@AR{x}+AR{y}++{d:X}H)", 8: "B(@AR{x}+AR{y}U)",
           9: "B(@AR{x}+AR{y}L)", 10: "B(@AR{x}+{d:X}H)", 11: "B(@AR{x}+AR{y}+{d:X}H)", 12: "???",
           13: "B(@AR{x}+[AR{y}++])", 14: "B(@AR{x}+[AR{y}--])", 15: "B(@AR{x}+[AR{y}++{d:X}H])"}


def d_dxfer2(op):
    asd, bsd = (op >> 38) & 3, (op >> 18) & 3
    if asd == 3:
        b = _MOVI_B[op & 0xf].format(x=(op >> 10) & 7, y=(op >> 7) & 7, d=(op >> 4) & 7)
        return "MOVI %s, I(@AR%d++)" % (b, (op >> 30) & 7)
    return "MVD2 %s, %s" % (d_dx2field(asd, False, (op >> 20) & 0x3ffff), d_dx2field(bsd, True, op & 0x3ffff))


def d_xfer2(op):
    opx, trm, dir_ = (op >> 39) & 3, (op >> 38) & 1, (op >> 37) & 1
    sr, dr, ary, md, disp = (op >> 31) & 0x7f, (op >> 24) & 0x7f, (op >> 4) & 7, op & 0xf, (op >> 7) & 0x3fff
    rd = REG[dr] if not dr & 0x40 else d_ea(md, dr, ary, disp)
    if opx == 0:
        if trm == 0:
            if sr == 0x58:
                return "MOV2 #%06XH, %s" % (op & 0xffffff, REG[dr])
            rs = REG[sr] if not sr & 0x40 else d_ea(md, sr, ary, disp)
            return "MOV2 %s, %s" % (rs, rd)
        if dir_ == 0:
            return "MOV2 %s, %s" % (rd, _eoff(sr))
        return "MOV2 %s, %s" % (_eoff(sr), rd)
    if opx == 2:
        if trm == 0:
            rs = REG[sr] if not sr & 0x40 else ("#%06XH" % (op & 0xffffff) if sr == 0x58 else d_ea(md, sr, ary, disp))
            return "MOV4 %s, ICDTR%d" % (rs, dr & 7)
        if dir_ == 0:
            return "MOV4 ICDTR%d, %s" % (dr & 7, _eoff(sr))
        return "MOV4 %s, ICDTR%d" % (_eoff(sr), dr & 7)
    return ""


def d_xfer3(op):
    imm, dr = (op >> 27) & 0xffffffff, (op >> 19) & 0x7f
    d = REG[dr] if not dr & 0x40 else d_ea(op & 0xf, dr, (op >> 4) & 7, (op >> 7) & 0xfff)
    return "MOV3 #%08X, %s" % (imm, d)


def disasm(op, pc):
    t = (op >> 61) & 7
    if t == 0:
        return d_alu_mul(op, True) + " : " + d_dxfer1(op)
    if t == 1:
        return d_alu_mul(op, True) + " : " + d_xfer1(op)
    if t == 2:
        return d_alu_mul(op, True) + " : " + d_control(op, pc)
    if t == 4:
        return d_alu_mul(op, False) + " : " + d_dxfer2(op)
    if t == 5:
        return d_alu_mul(op, False) + " : " + d_xfer2(op)
    if t == 6:
        return d_alu_mul(op, False) + " : " + d_control(op, pc)
    if t == 7:
        return d_xfer3(op)
    return "???"


# ---------------------------------------------------------------- assembler

class AsmError(Exception):
    pass


_EA = [  # (regex, md) — the A(...)/B(...) contents
    (r"@AR(\d)\+\[AR(\d)\+\+([0-9A-F]+)H\]", 15), (r"@AR(\d)\+\[AR(\d)\+\+\]", 13), (r"@AR(\d)\+\[AR(\d)--\]", 14),
    (r"@AR(\d)\+AR(\d)\+\+([0-9A-F]+)H", 7), (r"@AR(\d)\+AR(\d)\+([0-9A-F]+)H", 11), (r"@AR(\d)\+AR(\d)\+\+", 5),
    (r"@AR(\d)\+AR(\d)--", 6), (r"@AR(\d)\+AR(\d)U", 8), (r"@AR(\d)\+AR(\d)L", 9), (r"@AR(\d)\+AR(\d)", 4),
    (r"@AR(\d)\+\+([0-9A-F]+)H", 3), (r"@AR(\d)\+([0-9A-F]+)H", 10), (r"@AR(\d)\+\+", 1), (r"@AR(\d)--", 2),
    (r"@AR(\d)", 0), (r"([0-9A-F]+)H", 12)]


class Asm:
    def __init__(self, listing=False):
        self.listing = listing     # bare branch numbers are MAME-dasm style (pc + rel)
        self.sym = {}

    # -- operand helpers
    def num(self, s):
        s = s.strip()
        if s in self.sym:
            return self.sym[s]
        if s.startswith("#"):
            s = s[1:]
        if s.lower().startswith("f:"):
            return struct.unpack("<I", struct.pack("<f", float(s[2:])))[0]
        neg = s.startswith("-")
        if neg:
            s = s[1:]
        if s.lower().startswith("0x"):
            v = int(s, 16)
        elif s[-1:] in "Hh" and re.fullmatch(r"[0-9A-Fa-f]+[Hh]", s):
            v = int(s[:-1], 16)
        elif s in self.sym:
            v = self.sym[s]
        else:
            try:
                v = int(s, 16)
            except ValueError:
                raise AsmError("bad number or unknown symbol %r" % s)
        return -v if neg else v

    def reg(self, s, table=REG):
        s = s.strip()
        if s not in table:
            raise AsmError("bad register %r" % s)
        return table.index(s)

    def ea(self, s):
        """'A(@AR7+8H)' -> (code with 0x40 | B*0x20 | arx, md, ary, disp)."""
        s = s.strip()
        m = re.fullmatch(r"([AB])\((.*?)\)*", s)
        if not m:
            raise AsmError("bad address %r" % s)
        bank, body = m.group(1), m.group(2)
        body = re.sub(r"\b[A-Za-z_][\w.]*\b",                      # .equ names and labels -> nnnH
                      lambda w: "%XH" % self.sym[w.group(0)] if w.group(0) in self.sym else w.group(0), body)
        for rx, md in _EA:
            mm = re.fullmatch(rx, body)
            if mm:
                g = mm.groups()
                if md == 12:
                    if not self.listing:   # mb86235ops.cpp decode_ea: fatalerror "illegal decode_ea type 0c"
                        raise AsmError("MAME has no absolute address mode: write A(@ARn+%s) with ARn = 0" % body)
                    arx, ary, disp = 0, 0, int(g[0], 16)
                else:
                    arx = int(g[0])
                    ary = int(g[1]) if md in (4, 5, 6, 7, 8, 9, 11, 13, 14, 15) else 0
                    disp = int(g[-1], 16) if md in (3, 7, 10, 11, 15) else 0
                return 0x40 | (0x20 if bank == "B" else 0) | arx, md, ary, disp
        raise AsmError("bad address %r" % s)

    def operand(self, s):
        """register -> (index, None); address -> (code, (md, ary, disp))."""
        s = s.strip()
        if s in REG:
            return REG.index(s), None
        c, md, ary, disp = self.ea(s)
        return c, (md, ary, disp)

    def eoff(self, s):
        m = re.fullmatch(r"E\(@EB\+EO\+\+(-?)([0-9A-F]+)H\)", s.strip())
        if not m:
            raise AsmError("bad E() %r" % s)
        v = int(m.group(2), 16)
        return (-v if m.group(1) else v) & 0x3f

    def target(self, s, pc, rel):
        s = s.strip()
        if rel:
            if s in self.sym or not self.listing:
                return (self.num(s) - (pc + 1)) & 0xfff
            return (self.num(s) - pc) & 0xfff
        return self.num(s) & 0xfff

    # -- ALU / MUL
    def alu(self, text, twoop):
        parts = text.split(None, 1)
        name, args = parts[0], ([a.strip() for a in parts[1].split(",")] if len(parts) > 1 else [])
        if name not in ALU:
            raise AsmError("bad ALU op %r" % name)
        code, shape = ALU[name]
        a2 = AI2F if shape.endswith("f") else AI2
        i1 = i2 = o = 0
        if shape in ("3", "3f"):
            i1, i2, o = self.reg(args[0], AI1), self.reg(args[1], a2), self.reg(args[2], MO)
        elif shape in ("2", "2f"):
            i1, i2 = self.reg(args[0], AI1), self.reg(args[1], a2)
        elif shape == "1":
            i1, o = self.reg(args[0], AI1), self.reg(args[1], MO)
        elif shape == "S":
            i1, i2, o = self.reg(args[0], AI1), self.num(args[1]) & 0x1f, self.reg(args[2], MO)
        w = code << 56 | i1 << 52 | i2 << 47 | o << 42
        return w if twoop else w | 1 << 41

    def mul(self, text, twoop):
        name, args = text.split(None, 1)
        if name not in ("MUL", "FMUL"):
            raise AsmError("bad MUL op %r" % name)
        a = [x.strip() for x in args.split(",")]
        mi1, mi2, mo = self.reg(a[0], MI1), self.reg(a[1], MI2), self.reg(a[2], MO)
        f = name == "FMUL"
        if twoop:
            return f << 41 | mi1 << 37 | mi2 << 32 | mo << 27
        return f << 56 | mi1 << 52 | mi2 << 47 | mo << 42

    # -- control
    def control(self, text, pc):
        parts = text.split(None, 1)
        name, rest = parts[0], (parts[1] if len(parts) > 1 else "")
        cop = ef1 = ef2 = 0
        if name == "NOP":
            pass
        elif name in ("REP", "SETL"):
            cop = 1 if name == "REP" else 2
            if rest.startswith("AR"):
                ef1, ef2 = 0x3f, int(rest[2]) << 12
            else:
                ef2 = self.num(rest) & 0xffff
        elif name in ("CLRFI", "CLRFO", "CLRF"):
            cop, ef1 = 3, {"CLRFI": 1, "CLRFO": 2, "CLRF": 3}[name]
        elif name in ("PUSH", "POP"):
            cop, ef2 = (4 if name == "PUSH" else 5), self.reg(rest) << 6
        elif name == "SETM":
            a = [x.strip() for x in rest.split(",")]
            v = self.num(a[0].rstrip("h"))
            if len(a) == 1:
                cop, ef2 = 8, v & 0xffff
            else:
                cop, sh, mask = {"CBSA": (9, 12, 7), "CBSB": (0xa, 8, 7), "RF": (0xb, 7, 1),
                                 "RDY": (0xc, 4, 1), "WAIT": (0xd, 0, 7)}[a[1]]
                ef2 = (v & mask) << sh
        elif name == "DBLP":
            cop, ef2 = 0x13, self.target(rest, pc, True)
        elif name in ("DBBC", "DBBS"):
            m = re.fullmatch(r"AR(\d):(\d+),\s*(.+)", rest.strip())
            if not m:
                raise AsmError("bad %s %r" % (name, rest))
            cop = 0x14 if name == "DBBC" else 0x15
            ef1, ef2 = int(m.group(2)) & 0xf, int(m.group(1)) << 13 | self.target(m.group(3), pc, True)
        elif name == "DRET":
            cop = 0x1b
        else:
            if name == "DJMP":
                cop, ef1 = 0x12, 0x3f
            elif name == "DCALL":
                cop, ef1 = 0x1a, 0x3f
            else:
                for c, pre in sorted(CTRL_BR.items(), key=lambda kv: -len(kv[1])):
                    if name.startswith(pre) and name[len(pre):] in COND:
                        cop, ef1 = c, COND.index(name[len(pre):])
                        break
                else:
                    raise AsmError("bad control op %r" % name)
            ef2 = self.dst(rest, pc)
        return cop << 22 | ef1 << 16 | ef2

    def dst(self, s, pc):
        s = s.strip()
        plus = s.startswith("+")
        if plus:
            s = s[1:]
        m = re.fullmatch(r"([AB])\((.*)\)", s)
        if m:
            inner = m.group(2)
            if inner in REG:
                mode = 0xc if m.group(1) == "A" else 0xe
                return (mode + plus) << 12 | self.reg(inner) << 6
            mode = 8 if m.group(1) == "A" else 0xa
            return (mode + plus) << 12 | (self.num(inner) & 0x3ff)
        if s in REG:
            # mb86235ops.cpp do_control_dst: bits 15:13 = 1 ARx, 2 AAx/ABx, 3 MAx/MBx (dasm prints all alike)
            r = self.reg(s)
            mode = 4 if (8 <= r < 16 or 40 <= r < 48) else (6 if (r < 8 or 32 <= r < 40) else 2)
            return (mode + plus) << 12 | r << 6
        return self.target(s, pc, False)

    # -- transfers
    def mov1(self, rest, pc):
        a = [x.strip() for x in rest.split(",")]
        if a[0].startswith("#"):
            return 0x58 << 19 | self.reg(a[1]) << 12 | (self.num(a[0]) & 0xfff)
        if a[0].startswith("E("):
            dr, ea = self.operand(a[1])
            w = 1 << 26 | (0x40 | self.eoff(a[0])) << 19 | dr << 12
        elif len(a) > 1 and a[1].startswith("E("):
            dr, ea = self.operand(a[0])
            w = 1 << 26 | self.eoff(a[1]) << 19 | dr << 12
        else:
            sr, e1 = self.operand(a[0])
            dr, e2 = self.operand(a[1])
            ea = e1 or e2
            w = sr << 19 | dr << 12
        if ea:
            md, ary, disp = ea
            w |= (disp & 0x1f) << 7 | ary << 4 | md
        return w

    def mov2(self, name, rest):
        a = [x.strip() for x in rest.split(",")]
        if name == "MOV4":
            if a[1].startswith("ICDTR"):
                d = int(a[1][5])
                if a[0].startswith("E("):
                    return 2 << 39 | 1 << 38 | (0x40 | self.eoff(a[0])) << 31 | d << 24
                if a[0].startswith("#"):
                    return 2 << 39 | 0x58 << 31 | d << 24 | (self.num(a[0]) & 0xffffff)
                sr, ea = self.operand(a[0])
                w = 2 << 39 | sr << 31 | d << 24
            else:
                return 2 << 39 | 1 << 38 | self.eoff(a[1]) << 31 | int(a[0][5]) << 24
        elif a[0].startswith("#"):
            return 0x58 << 31 | self.reg(a[1]) << 24 | (self.num(a[0]) & 0xffffff)
        elif a[0].startswith("E("):
            dr, ea = self.operand(a[1])
            w = 1 << 38 | (0x40 | self.eoff(a[0])) << 31 | dr << 24
        elif a[1].startswith("E("):
            dr, ea = self.operand(a[0])
            w = 1 << 38 | self.eoff(a[1]) << 31 | dr << 24
        else:
            sr, e1 = self.operand(a[0])
            dr, e2 = self.operand(a[1])
            ea = e1 or e2
            w = sr << 31 | dr << 24
        if ea:
            md, ary, disp = ea
            w |= (disp & 0x3fff) << 7 | ary << 4 | md
        return w

    def mov3(self, rest):
        a = [x.strip() for x in rest.split(",", 1)]
        imm = self.num(a[0]) & 0xffffffff
        dr, ea = self.operand(a[1])
        w = 7 << 61 | imm << 27 | dr << 19
        if ea:
            md, ary, disp = ea
            w |= (disp & 0xfff) << 7 | ary << 4 | md
        return w

    def mvd1(self, rest):
        a = [x.strip() for x in rest.split(",")]
        if all(x in REG for x in a):
            return self.reg(a[0]) << 20 | self.reg(a[1]) << 15 | self.reg(a[2]) << 10 | self.reg(a[3]) << 5
        sd = 1 if a[0] in REG else 2
        if sd == 1:
            areg, aea, breg, bea = a
        else:
            aea, areg, bea, breg = a
        ca, md, ay, _ = self.ea(aea)
        cb, _, by, _ = self.ea(bea)
        if md == 8 or md == 9:
            pass  # B side prints the other of U/L; md comes from the A side
        return (sd << 25 | (self.reg(areg) & 0x1f) << 20 | (ca & 7) << 17 | ay << 14 |
                (self.reg(breg) & 0xf) << 10 | (cb & 7) << 7 | by << 4 | md)

    def dx2field(self, a, isb):
        if a[0] in REG and a[1] in REG:
            return 0, (self.reg(a[0]) & 0x1f) << 13 | (self.reg(a[1]) & 0x1f) << 8
        sd = 1 if a[0] in REG else 2
        reg, ea = (a[0], a[1]) if sd == 1 else (a[1], a[0])
        c, md, ary, _ = self.ea(ea if ea.endswith(")") else ea + ")")
        return sd, (self.reg(reg) & 0x1f) << 13 | (c & 7) << 10 | ary << 7 | md

    def mvd2(self, rest):
        a = [x.strip() for x in rest.split(",")]
        asd, af = self.dx2field(a[0:2], False)
        bsd, bf = self.dx2field(a[2:4], True)
        return 4 << 61 | asd << 38 | af << 20 | bsd << 18 | bf

    def movi(self, rest):
        a = [x.strip() for x in rest.split(",")]
        c, md, ary, disp = self.ea(a[0])
        m = re.fullmatch(r"I\(@AR(\d)\+\+\)", a[1])
        return 4 << 61 | 3 << 38 | int(m.group(1)) << 30 | (c & 7) << 10 | ary << 7 | (disp & 7) << 4 | md

    # -- one instruction
    def encode(self, text, pc):
        parts = [p.strip() for p in text.split(" : ")]
        if len(parts) == 1:
            name, rest = parts[0].split(None, 1)
            if name != "MOV3":
                raise AsmError("one-part instruction must be MOV3: %r" % text)
            return self.mov3(rest)
        last = parts[-1]
        lname = last.split(None, 1)[0]
        lrest = last.split(None, 1)[1] if " " in last else ""
        if len(parts) == 3:
            w = self.alu(parts[0], True) | self.mul(parts[1], True)
            twoop = True
        elif len(parts) == 2:
            first = parts[0].split(None, 1)[0]
            w = self.mul(parts[0], False) if first in ("MUL", "FMUL") else self.alu(parts[0], False)
            twoop = False
        else:
            raise AsmError("too many parts: %r" % text)
        if lname in ("MVD1", "MVD2", "MOVI"):
            if lname == "MVD1":
                if not twoop:
                    raise AsmError("MVD1 needs ALU : MUL")
                return w | self.mvd1(lrest)
            if twoop:
                raise AsmError("%s takes one ALU or MUL op" % lname)
            return w | (self.mvd2(lrest) if lname == "MVD2" else self.movi(lrest))
        if lname == "MOV1":
            if not twoop:
                raise AsmError("MOV1 needs ALU : MUL (use MOV2)")
            return 1 << 61 | w | self.mov1(lrest, pc)
        if lname in ("MOV2", "MOV4"):
            if twoop:
                raise AsmError("%s takes one ALU or MUL op (use MOV1)" % lname)
            return 5 << 61 | w | self.mov2(lname, lrest)
        return (2 << 61 if twoop else 6 << 61) | w | self.control(last, pc)

    # -- a source file
    def assemble(self, src):
        lines = []
        pc = 0
        for n, raw in enumerate(src.splitlines(), 1):
            line = raw.split(";", 1)[0].strip()
            while True:
                m = re.match(r"([A-Za-z_.][\w.]*):(?!\S*\))\s*(.*)", line)
                if not m or m.group(1) in REG:
                    break
                self.sym[m.group(1)] = pc
                line = m.group(2)
            if not line:
                continue
            if line.startswith(".equ"):
                k, v = [x.strip() for x in line[4:].split(",", 1)]
                self.sym[k] = self.num(v)
                continue
            if line.startswith(".org"):
                pc = self.num(line[4:])
                continue
            lines.append((n, pc, line))
            pc += 1
        out = {}
        for n, pc, line in lines:
            try:
                if line.startswith(".word"):
                    a = [self.num(x) for x in line[5:].split(",")]
                    w = a[0] if len(a) == 1 else (a[0] << 32 | (a[1] & 0xffffffff))
                else:
                    w = self.encode(line, pc)
            except (AsmError, IndexError, ValueError, KeyError, AttributeError) as e:
                raise AsmError("line %d: %s: %s" % (n, line, e))
            out[pc] = w & 0xffffffffffffffff
        size = max(out) + 1 if out else 0
        return [out.get(i, 0) for i in range(size)]


def selftest(path):
    a = Asm(listing=True)
    bad_d = bad_a = n = 0
    for raw in open(path):
        m = re.match(r"\s*([0-9A-F]{3}): ([0-9A-F]{16}) (.*)$", raw.rstrip("\n"))
        if not m:
            continue
        pc, word, text = int(m.group(1), 16), int(m.group(2), 16), m.group(3).rstrip()
        n += 1
        if disasm(word, pc) != text:
            bad_d += 1
            if bad_d <= 10:
                print("dasm %03X: %016X\n  mame: %s\n  ours: %s" % (pc, word, text, disasm(word, pc)))
        try:
            w = a.encode(text, pc)
        except Exception as e:
            bad_a += 1
            if bad_a <= 10:
                print("asm  %03X: %s: %s" % (pc, text, e))
            continue
        if w != word and disasm(w, pc) != text:
            bad_a += 1
            if bad_a <= 10:
                print("asm  %03X: %016X -> %016X\n  %s\n  %s" % (pc, word, w, text, disasm(w, pc)))
    print("%d words: %d disassembly mismatches, %d assembly mismatches" % (n, bad_d, bad_a))
    return bad_d == 0 and bad_a == 0


def main(argv):
    if len(argv) >= 2 and argv[0] == "--selftest":
        return 0 if selftest(argv[1]) else 1
    if len(argv) >= 2 and argv[0] == "-d":
        data = open(argv[1], "rb").read()
        for i in range(len(data) // 8):
            w = struct.unpack_from("<Q", data, i * 8)[0]
            print("%03X: %016X %s" % (i, w, disasm(w, i)))
        return 0
    src, out, hdr, name = None, None, None, "tgpx4_program"
    i = 0
    while i < len(argv):
        if argv[i] == "-o":
            out = argv[i + 1]; i += 2
        elif argv[i] == "--header":
            hdr = argv[i + 1]; i += 2
        elif argv[i] == "--name":
            name = argv[i + 1]; i += 2
        else:
            src = argv[i]; i += 1
    if not src:
        print(__doc__)
        return 2
    try:
        words = Asm().assemble(open(src).read())
    except AsmError as e:
        print("%s: %s" % (src, e), file=sys.stderr)
        return 1
    if out:
        with open(out, "wb") as f:
            for w in words:
                f.write(struct.pack("<Q", w))
    if hdr:
        with open(hdr, "w") as f:
            f.write("/* generated by tools/tgpx4asm.py from %s — do not edit */\n" % src.replace("\\", "/").split("/")[-1])
            f.write("/* %d MB86235 words as (low 32, high 32) pairs, the order the COP FIFO takes them */\n" % len(words))
            f.write("static const u32 %s[%d] = {\n" % (name, 2 * len(words)))
            for w in words:
                f.write("    0x%08Xu, 0x%08Xu,\n" % (w & 0xffffffff, w >> 32))
            f.write("};\n")
    print("%s: %d words" % (src, len(words)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
