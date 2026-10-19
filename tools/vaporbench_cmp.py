#!/usr/bin/env python3
"""vaporbench_cmp.py - the boards' vaporbench results side by side (tools/vaporbench_dump.lua files).

    python3 tools/vaporbench_cmp.py 2A=out2A.txt 2B=out2B.txt 2C=out2C.txt

Prints a markdown table of microseconds per call: the native commands, then the ext commands
(cpres1 on 2B, the kernels' VM on 2A/2C). An ext time is marked * where the board's words
(results, then the matrix) differ from 2B's, cpres1's, by more than stftest_cmp.py allows:
floats within 1e-4 relative or 1e-5 absolute (sin/cos, divide and square root differ in the
last bits between the boards)."""
import math, struct, sys


def load(path):
    head, nat, ext = {}, {}, []
    for line in open(path):
        w = line.split()
        if w[0] == "frames":
            head = dict(zip(w[0::2], map(int, w[1::2])))
        elif w[0] == "nat":
            nat[w[1]] = (int(w[2]), w[3])
        elif w[0] == "ext":
            ext.append((int(w[1], 16), int(w[2]), [int(x, 16) for x in w[3:]]))
    return head, nat, ext


def f(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]


def close(a, b):
    if a == b:
        return True
    x, y = f(a), f(b)
    if x != x and y != y:
        return True
    if math.isinf(x) or math.isinf(y):
        return False
    return abs(x - y) <= 1e-5 or abs(x - y) <= 1e-4 * max(abs(x), abs(y))


def same(a, b):
    return len(a) == len(b) and all(close(x, y) for x, y in zip(a, b))


def us(ns):
    if ns == 0xFFFFFFFF:
        return "timeout"
    return f"{ns / 1000:.1f}" if ns < 10_000_000 else f"{ns / 1e6:.0f} ms"


def main():
    boards = [a.split("=", 1) for a in sys.argv[1:]]
    data = {b: load(p) for b, p in boards}
    names = [b for b, _ in boards]
    print("| command | " + " | ".join(names) + " |")
    print("|---|" + "---:|" * len(names))
    for k in data[names[0]][1]:
        cells = []
        for b in names:
            ns, ok = data[b][1][k]
            cells.append(us(ns) + ("" if ok == "ok" else " BAD"))
        print(f"| {k} | " + " | ".join(cells) + " |")
    print("| scene (us) | " + " | ".join(str(data[b][0].get("scene_us")) for b in names) + " |")
    ref = data.get("2B")
    print()
    print("| ext | " + " | ".join(names) + " |")
    print("|---|" + "---:|" * len(names))
    tot = {b: 0 for b in names}
    for i, (cmd, _, _) in enumerate(data[names[0]][2]):
        cells = []
        for b in names:
            _, ns, h = data[b][2][i]
            tot[b] += 0 if ns == 0xFFFFFFFF else ns
            diff = ref is not None and b != "2B" and not same(ref[2][i][2], h)
            cells.append(us(ns) + (" *" if diff else ""))
        print(f"| 0x{cmd:02x} | " + " | ".join(cells) + " |")
    print("| all | " + " | ".join(us(tot[b]) for b in names) + " |")


if __name__ == "__main__":
    main()
