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

# The afterimage engine (0x80..0x87, cpres1 PM 0x208E1..0x20A8E) as STF drives it: 0x80 lays
# a fighter's trail into the 128-slot ring, so the readers above only ever saw an empty one.
# Each frame: move some parts' unit matrices (0x35), reserve (0x80: 4 header words, then per
# part its index and three object numbers, then -1 and a turn angle), snapshot this frame as
# the next one's previous (0x7F), age the ring (0x82) and read it back (0x83..0x87).
u = lambda v: v & 0xffffffff
units = [[None] * 16 for _ in range(2)]
def put_unit(p, b, m):
    units[p][b] = m
    print("mat " + " ".join("%08x" % fb(x) for x in m))
    print("35 %08x %08x" % (p, 12 * b))
print("81")
print("49 %08x %08x" % (0x32181, fb(0.1)))        # spacing: one copy per 0.1 units
print("49 %08x %08x" % (0x32182, 30))             # the life a fresh trail starts at
for p in range(2):
    for b in range(16):
        put_unit(p, b, [random.uniform(-1, 1) for _ in range(9)] + [random.uniform(-5, 5) for _ in range(3)])
    print("7f %08x" % p)
written = 0
for f in range(8):
    for p in range(2):
        npart = random.choice((1, 2, 3, 5, 8, 11, 16))
        mask = sum(1 << b for b in random.sample(range(16), npart))
        far = f == 5 and p == 0                   # one teleport: farther than 13 squared, no trail
        for b in range(16):
            if mask >> b & 1:
                m = units[p][b]
                put_unit(p, b, [x + random.uniform(-0.05, 0.05) for x in m[:9]] +
                         [x + (6.0 if far else random.uniform(-0.4, 0.4)) for x in m[9:]])
        step = -random.randrange(1, 5)
        ang = random.choice((0, random.randrange(1, 0x10000)))
        recs = []
        for b in reversed(range(16)):              # the i960 scanbits, highest part first
            if mask >> b & 1: recs += [b, random.randrange(1, 0x400), random.randrange(1, 0x400), random.randrange(1, 0x400)]
        print("80 " + " ".join("%08x" % u(w) for w in [p, mask, u(step), fb(random.uniform(0.2, 1.0))] + recs + [0xffffffff, ang]))
        print("7f %08x" % p)
    print("82")
    for s in random.sample(range(0x80), 6):
        print("85 %08x" % s)
    print("83 %08x" % random.randrange(0x80))
    print("84 %08x" % random.randrange(0x80))
    if f % 3 == 2:
        print("87 %08x" % random.randrange(0x80))
    if f == 6:
        print("86 %08x" % random.randrange(2))
for s in range(0x80):                              # the whole ring at the end
    print("85 %08x" % s)
