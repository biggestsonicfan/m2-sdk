#!/usr/bin/env python3
"""compare.py <dir> <pacman_roms.h>: lockstep MAME pacman (ref.bin, ref_*.raw) against the
i960 port (port.bin, port_*.fb), frame by frame.

Memory: 0x4000-0x4FFF (0x4800-0x4BFF is unmapped in MAME: skipped) + 0x5060-0x506F.
A difference that lasts one frame in at most two bytes (a push/call) is the instruction
astride the frame edge:
MAME's Z80 is cycle-stepped, so at its frame end that instruction is part done (its PC not
yet advanced, its write maybe not yet made), where the port finishes it. Anything longer
lived, or wider, is a real divergence.
Pictures: the port's picture of frame k against MAME's (the screen MAME shows during
frame k+1, which ref.lua saves as k). The port draws at the Model 2's 57.5 Hz and skips
drawing when a vblank overruns (the boot self-test), so some of its pictures are older.
"""
import os, re, sys
import numpy as np
D, ROMS = sys.argv[1], sys.argv[2]
REC = 4096 + 16
ref = np.fromfile(D + '/ref.bin', np.uint8).reshape(-1, REC)
por = np.fromfile(D + '/port.bin', np.uint8).reshape(-1, REC)
n = min(len(ref), len(por))
ref, por = ref[:n], por[:n]
mask = np.ones(REC, bool); mask[0x800:0xc00] = False
dif = (ref != por) & mask
bad = dif.any(1)
print('memory: %d frames, byte-exact %d, differing %d' % (n, (~bad).sum(), bad.sum()))
longest, widest = 0, int(dif.sum(1).max()) if n else 0
for i in range(REC):
    run = 0
    for v in dif[:, i]:
        run = run + 1 if v else 0
        longest = max(longest, run)
print('        widest difference %d byte(s), longest %d frame(s) -> %s' % (
    widest, longest, 'boundary-instruction only' if widest <= 2 and longest <= 1 else 'REAL DIVERGENCE'))
for f in np.nonzero(bad)[0][:10]:
    idx = np.nonzero(dif[f])[0]
    print('        frame %d: %s' % (f + 1, ', '.join('%04X MAME %02X port %02X' % (
        0x4000 + i if i < 0x1000 else 0x5060 + i - 0x1000, ref[f, i], por[f, i]) for i in idx[:4])))

src = open(ROMS).read()
b = src[src.index('pac_prom_pal'):]; b = b[b.index('{') + 1:b.index('}')]
pal = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})', b))
def rgb(v):
    return ((v & 1) * 0x21 + ((v >> 1) & 1) * 0x47 + ((v >> 2) & 1) * 0x97,
            ((v >> 3) & 1) * 0x21 + ((v >> 4) & 1) * 0x47 + ((v >> 5) & 1) * 0x97,
            ((v >> 6) & 1) * 0x51 + ((v >> 7) & 1) * 0xae)
PAL = np.array([rgb(pal[i]) for i in range(16)], np.int32)
NIB = np.array([12 - 4 * x if x < 4 else 44 - 4 * x for x in range(8)])
def fb_img(p):          # the port's portrait 224x288, Model 2 char-RAM nibbles
    w = np.fromfile(p, '<u4').reshape(36, 28, 8); ys = np.arange(288); xs = np.arange(224)
    return PAL[(w[ys[:, None] >> 3, xs[None, :] >> 3, ys[:, None] & 7] >> NIB[xs & 7][None, :]) & 15]
def ref_img(p):         # MAME's native 288x224 BGRA, rotated to portrait
    a = np.frombuffer(open(p, 'rb').read()[:288 * 224 * 4], np.uint8).reshape(224, 288, 4)[:, :, [2, 1, 0]]
    return np.ascontiguousarray(np.rot90(a.astype(np.int32), 3))
res = []
for k in range(60, n + 1, 60):
    p, r = '%s/port_%05d.fb' % (D, k), '%s/ref_%05d.raw' % (D, k)
    if os.path.exists(p) and os.path.exists(r):
        res.append((k, int((np.abs(fb_img(p) - ref_img(r)).sum(2) > 0).sum())))
print('pictures: %d compared, identical %d%s' % (len(res), sum(1 for _, d in res if d == 0),
      (' (not: ' + ', '.join('frame %d %d px' % x for x in res if x[1]) + ')') if any(d for _, d in res) else ''))
