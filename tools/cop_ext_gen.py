import random, struct, sys
random.seed(int(sys.argv[1]) if len(sys.argv) > 1 else 1)
native = {1,2,3,5,6,7,8,9,10,0x13,0x14,0x15,0x16,0x17,0x18,0x1a,0x21,0x22,0x23,0x24,0x25,0x2b,0x2c,0x2d,0x2f,0x59,0x5c,0x5d,0x5e,0x78}
counts = {}
for l in open(sys.argv[2] if len(sys.argv) > 2 else "argc.txt"):
    a, b = l.split(); counts[int(a, 16)] = int(b)
fb = lambda f: struct.unpack("<I", struct.pack("<f", f))[0]
def arg():
    r = random.random()
    if r < 0.6: return fb(random.uniform(-100, 100))
    if r < 0.8: return fb(random.uniform(-1, 1))
    # an index or a slot offset (12 words a slot): the firmware has no bounds check, and past
    # its 16-entry tables it reads (and on sfight DMAs) whatever lies there
    return random.choice((random.randrange(16), 12 * random.randrange(16)))
# the arguments that are table indices or buffer RAM offsets, per command: the firmware has no
# bound on them, so an out-of-range one reads (and writes) whatever lies there on each board.
# Offsets fall in buffer RAM words 0x3800..0x38ff: the `buf` lines below fill them, and 0x4a
# (osage) walks the record stream at 0x3900.
player = lambda: random.randrange(2)
slot = lambda: 12 * random.randrange(16)
inner = lambda: random.randrange(16)
off = lambda: 0x3800 + random.randrange(0xf0)
ang = lambda: random.randrange(0x10000)
typed = {0x36: (player, slot), 0x37: (player, slot), 0x43: (inner,), 0x44: (inner,), 0x45: (inner,),
         0x46: (inner,), 0x67: (off,), 0x68: (off,), 0x69: (off, ang),
         0x74: (player, slot, lambda: random.randrange(8), arg, arg, arg, inner), 0x4a: (lambda: 0x3900,)}
# `buf WORD w...`: buffer RAM words the cases read, written before the first one
fl = lambda lo, hi, n: [fb(random.uniform(lo, hi)) for _ in range(n)]
print("buf 3800 " + " ".join("%08x" % w for w in fl(-2, 2, 0x100)))
# osage records (exec.inc sharc_osage): 1 the matrices, 2 the limits, 3 no-limit flag and damping,
# 4 the chain's start, two 5 segments (a point, its velocity, a length...), 0 the end
osage = ([1] + fl(-2, 2, 36) + [2] + fl(-2, 2, 30) + [3, random.randrange(2), fb(random.uniform(0, 1))] +
         [4] + fl(-2, 2, 3) + [5] + fl(-2, 2, 11) + [5] + fl(-2, 2, 11) + [0])
print("buf 3900 " + " ".join("%08x" % w for w in osage))
# every command starts from a matrix of its own, so one that differs doesn't carry into the next
mat = lambda: print("mat " + " ".join("%08x" % fb(random.uniform(-2, 2)) for _ in range(12)))
for rep in range(3):
    for n in range(0x88):
        if n in native or counts[n] < 0: continue
        mat()
        gen = typed.get(n, (arg,) * counts[n])
        print("%02x " % n + " ".join("%08x" % g() for g in gen))
