#!/usr/bin/env python3
"""stftest_cmp.py - compare two runs of src/stftest.c, per command.

  stftest_cmp.py CASES A B [-v]

CASES is the tools/cop_ext_gen.py script the cases came from (t1.txt). A and B are each a dump
of g_stf_out (first line "done N ...", then one hex word per line: per case its word count,
then the words) or the host model's output for CASES (src/cop_ext/build.sh --check writes
h1.txt). A word matches exactly, or is close (both floats within 1e-4 relative, or 1e-5
absolute: the boards' sin/cos, divide and square root differ in the last bits), or differs.
A case whose word counts differ is counted as such and its words are not compared. -v lists
every word that differs and every count that does.
"""
import math, struct, sys

def load(path, cases_path):
    """per case, the list of words the COP sent back (results, then the matrix)"""
    lines = open(path).read().split("\n")
    if lines[0].startswith("done"):
        words = [int(w, 16) for w in lines[1:] if w.strip()]
        out, o = [], 0
        for _ in range(int(lines[0].split()[1])):
            n = words[o]
            out.append(words[o + 1:o + 1 + n])
            o += 1 + n
        return out
    # host format: one line per non-mat case: "cc: outs | used | mat m0..m11"
    res = iter(l for l in lines if ":" in l)
    out = []
    for line in open(cases_path):
        t = line.split()
        if not t or t[0].startswith("#") or t[0] == "buf": continue
        if t[0] == "mat":
            out.append([int(w, 16) for w in t[1:]])
            continue
        l = next(res)
        outs = l.split("|")[0].split(":")[1].split()
        out.append([int(w, 16) for w in outs + l.split("mat")[1].split()])
    return out

def f(u): return struct.unpack("<f", struct.pack("<I", u))[0]

def close(a, b):
    if a == b: return True
    x, y = f(a), f(b)
    if x != x and y != y: return True                     # NaN either way
    if math.isinf(x) or math.isinf(y): return False
    return abs(x - y) <= 1e-5 or abs(x - y) <= 1e-4 * max(abs(x), abs(y))

cases_path = sys.argv[1]
names = [("04" if l.split()[0] == "mat" else l.split()[0]) for l in open(cases_path)
         if l.split() and not l.startswith("#") and l.split()[0] != "buf"]
A = load(sys.argv[2], cases_path)
B = load(sys.argv[3], cases_path)
verbose = "-v" in sys.argv
stats = {}
n = min(len(A), len(B))
for ci in range(n):
    key, a, b = names[ci], A[ci], B[ci]
    s = stats.setdefault(key, [0, 0, 0, 0])
    if len(a) != len(b):
        s[3] += 1
        if verbose:
            print("case %d cmd %s: %d words vs %d" % (ci, key, len(a), len(b)))
        continue
    nr = len(a) - 12
    for j, (x, y) in enumerate(zip(a, b)):
        if x == y: s[0] += 1
        elif close(x, y): s[1] += 1
        else:
            s[2] += 1
            if verbose:
                print("case %d cmd %s %s %d: %08x %08x (%g %g)" % (ci, key, "res" if j < nr else "mat",
                      j if j < nr else j - nr, x, y, f(x), f(y)))
tot = [sum(v[i] for v in stats.values()) for i in range(4)]
print("cases %d / %d, %d commands: %d exact, %d close, %d differ, %d cases with other counts"
      % (n, len(names), len(stats), *tot))
for k in sorted(k for k, v in stats.items() if v[2] or v[3]):
    print("  %s: %d exact, %d close, %d differ, %d counts" % (k, *stats[k]))
