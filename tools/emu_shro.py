#!/usr/bin/env python3
"""
emu_shro.py — CMake C compiler launcher: rewrite GCC's `x == 0` idiom for m2emulator.

GCC's i960 backend (i960.md *equals_zero_insn0) computes `x == 0` as `shro x,1,y`
(1 >> x): right on an i960, where a shift count above 31 gives 0, but m2emulator masks
the count to 5 bits like x86, so 1 >> 32 = 1 and every x that is a non-zero multiple
of 32 tests as zero. (Pac-Man: the Z80 flag tables set Z for 0x20, 0x40, ...)

Used as CMAKE_C_COMPILER_LAUNCHER: compiles to assembly, rewrites every `shro a,1,b`
(register count, constant 1), assembles. Replacements give the same result on all
three targets:
  a != b:  subo a,0,b / or a,b,b / shro 31,b,b / xor 1,b,b    (no condition codes)
  a == b:  scanbit a,a / shro 31,a,a                         (0 -> 0xFFFFFFFF -> 1)
scanbit sets the condition codes, which the original shro does not; the rewrite checks
that nothing reads them before they are set again, and fails the build otherwise.
"""
import os
import re
import subprocess
import sys

SHRO = re.compile(r'^(\s*)shro\s+(\w+),1,(\w+)\s*$')
INSN = re.compile(r'^\s*([a-z][\w.]*)\s*(.*)$')
LABEL = re.compile(r'^([\w.$]+):')
CC_SET = re.compile(r'^(cmp\w*|concmp\w*|chkbit|scanbit|spanbit|scanbyte|bbs|bbc)(\.[tf])?$')
CC_READ = re.compile(r'^(b(e|ne|l|le|g|ge|o|no)|test\w+|alterbit|addc|subc|sel\w+|fault\w+)(\.[tf])?$')
CC_DEAD = re.compile(r'^(ret|call|callx|calls|bal|balx|bx)$')


def cc_dead_after(lines, i, labels, seen=None):
    """True if the condition codes are set (or dead) before being read after line i."""
    seen = seen if seen is not None else set()
    for j in range(i + 1, len(lines)):
        m = INSN.match(lines[j])
        if not m or lines[j].lstrip().startswith(('.', '#')) or LABEL.match(lines[j].strip()):
            continue
        op, args = m.group(1), m.group(2)
        if CC_SET.match(op):
            return True
        if CC_READ.match(op):
            return False
        if CC_DEAD.match(op):
            return True
        if op in ('b', 'b.t', 'b.f'):
            tgt = args.split()[0] if args else ''
            if tgt in seen or tgt not in labels:
                return tgt in seen
            seen.add(tgt)
            return cc_dead_after(lines, labels[tgt], labels, seen)
    return True


def rewrite(path):
    lines = open(path).read().split('\n')
    labels = {}
    for i, l in enumerate(lines):
        m = LABEL.match(l.strip())
        if m:
            labels[m.group(1)] = i
    out = []
    for i, l in enumerate(lines):
        m = SHRO.match(l)
        if not m:
            out.append(l)
            continue
        ind, a, b = m.groups()
        if a != b:
            out += [f'{ind}subo\t{a},0,{b}', f'{ind}or\t{a},{b},{b}',
                    f'{ind}shro\t31,{b},{b}', f'{ind}xor\t1,{b},{b}']
        else:
            if not cc_dead_after(lines, i, labels):
                sys.exit(f'emu_shro: {path}:{i + 1}: condition codes live across "{l.strip()}"')
            out += [f'{ind}scanbit\t{a},{a}', f'{ind}shro\t31,{a},{a}']
    open(path, 'w').write('\n'.join(out))


def main():
    cmd = sys.argv[1:]
    if '-c' not in cmd or '-o' not in cmd or not any(a.endswith('.c') for a in cmd):
        sys.exit(subprocess.call(cmd))
    o = cmd.index('-o')
    obj = cmd[o + 1]
    asm = obj + '.emu.s'
    scmd = [('-S' if a == '-c' else a) for a in cmd]
    scmd[o + 1] = asm
    r = subprocess.call(scmd)
    if r:
        sys.exit(r)
    rewrite(asm)
    # assemble: the same driver, without the preprocessor/dependency flags
    acmd, skip = [], False
    for a in cmd:
        if skip:
            skip = False
            continue
        if a in ('-MT', '-MF', '-MQ'):
            skip = True
            continue
        if a.startswith(('-M', '-D', '-I', '-W')) or a.endswith('.c'):
            continue
        acmd.append(a)
    acmd.append(asm)
    r = subprocess.call(acmd)
    if r == 0 and not os.environ.get('EMU_SHRO_KEEP'):
        os.remove(asm)
    sys.exit(r)


if __name__ == '__main__':
    main()
