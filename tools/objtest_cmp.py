#!/usr/bin/env python3
"""Compare objtest's results from two boards (e.g. dynamcop 2A and sfight 2B).

Each input is a dump of i960 RAM from g_obj_done on: one "ADDR WORD" pair of hex numbers per
line, at least 44 words (done, bad, counts[2][2], words[2][19]), as a MAME Lua script that
reads program space and writes "%08X %08X" lines produces. Find the start address with
`i960-elf-nm game.elf | grep _g_obj_done`.

Integer words (commands, header words, END mark, polygon counts) must match exactly. The 12
matrix words of each object must agree within 1.7e-6 of the largest element of their column
(T, words 10-12, is compared exactly: objtest sets it before any rotation). The TGP's and cpres1's
sine tables differ in the last bits, so an element near zero can differ by ~1e-6 absolute.

    python3 tools/objtest_cmp.py 2A/dump.txt 2B/dump.txt
"""
import struct
import sys

TOL = 1.7e-6


def words(path):
    return [int(line.split()[1], 16) for line in open(path) if line.strip()]


def f(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]


def main(a_path, b_path):
    a, b = words(a_path), words(b_path)
    bad = 0
    if a[1] or b[1]:
        print(f"g_obj_bad: {a[1]} / {b[1]} (a board's own check failed)")
        bad += 1
    for k in range(2):
        for j in range(2):
            i = 2 + 2 * k + j
            if a[i] != b[i]:
                print(f"obj{k + 1} count {j}: {a[i]:08X} / {b[i]:08X}")
                bad += 1
        base = 6 + 19 * k
        worst = 0.0
        for i in range(19):
            x, y = a[base + i], b[base + i]
            if 1 <= i <= 9:   # rotation columns
                c = (i - 1) // 3
                col = [f(a[base + 1 + 3 * c + r]) for r in range(3)]
                scale = max(abs(v) for v in col) or 1.0
                rel = abs(f(x) - f(y)) / scale
                worst = max(worst, rel)
                if rel > TOL:
                    print(f"obj{k + 1} word {i}: {f(x):+.9g} / {f(y):+.9g}, {rel:.2e} of the column")
                    bad += 1
            elif x != y:
                print(f"obj{k + 1} word {i}: {x:08X} / {y:08X}")
                bad += 1
        print(f"obj{k + 1}: matrix columns agree to {worst:.2e}")
    print("SAME" if not bad else f"{bad} DIFFERENCES")
    return 1 if bad else 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1], sys.argv[2]))
