#!/usr/bin/env python3
"""compare.py <dir> <pacman_roms.h>: lockstep MAME pacman (ref.*) against the i960 port
(port.*), frame by frame: memory, the Z80's registers, the board's latch/IRQ/vector, the
WSG, what the SCSP plays, the audio, and the pictures.

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


# ---- the Z80's registers, at every accepted IRQ (ref_irq.bin / port_irq.bin) ------------
# Both are taken as the CPU acknowledges: PC pushed, the IM 2 vector about to be read, an
# instruction boundary on both machines (unlike a frame edge, where MAME's cycle-stepped
# Z80 can be mid-instruction).
import struct, wave
REGS = "A F B C D E H L A' F' B' C' D' E' H' L' IXl IXh IYl IYh SPl SPh PCl PCh I R IM IFF1 IFF2 HALT".split()
dt = np.dtype([('f', '<u4'), ('r', 'u1', 30)])
RI, PI = np.fromfile(D + '/ref_irq.bin', dt), np.fromfile(D + '/port_irq.bin', dt)
rf = {int(f): r for f, r in zip(RI['f'], RI['r']) if f <= n}
pf = {int(f): r for f, r in zip(PI['f'], PI['r']) if f <= n}
com = sorted(set(rf) & set(pf))
A, B = np.array([rf[f] for f in com]), np.array([pf[f] for f in com])
d = A != B
print('registers: %d IRQs (MAME %d, port %d; only one side: %s), all %d registers equal in %d' % (
    len(com), len(rf), len(pf), sorted(set(rf) ^ set(pf)) or 'none', len(REGS), (~d.any(1)).sum()))
for j, nm in enumerate(REGS):
    if d[:, j].any():
        k = np.argmax(d[:, j])
        print('        %s differs at %d IRQs, first frame %d: MAME %02X port %02X' % (nm, d[:, j].sum(), com[k], A[k, j], B[k, j]))

# ---- the board and the WSG, every frame (ref_state.bin / port_state.bin) ---------------
# record: time f64, regs (30), IRQ mask, IM 2 vector, 74LS259 latch, sound enable, the 32 WSG
# registers; then MAME's decoded voices (freq u32, volume u32, waveform u16) x3 or the port's
# SCSP slots 0-2 (+0x00, +0x02 SA, +0x0C TL, +0x10 pitch; u16 each)
RS = np.fromfile(D + '/ref_state.bin', np.uint8).reshape(-1, 104)[:n]
PS = np.fromfile(D + '/port_state.bin', np.uint8).reshape(-1, 98)[:n]
BOARD = ['IRQ mask', 'vector', 'latch', 'sound enable'] + ['WSG %04X' % (0x5040 + i) for i in range(32)]
d = RS[:, 38:74] != PS[:, 38:74]
print('board + WSG registers: %d frames, identical %d' % (len(d), (~d.any(1)).sum()))
for j, nm in enumerate(BOARD):
    if d[:, j].any():
        k = np.argmax(d[:, j])
        print('        %s differs in %d frames, first %d: MAME %02X port %02X' % (nm, d[:, j].sum(), k + 1, RS[k, 38 + j], PS[k, 38 + j]))

wsg = PS[:, 42:74].astype(np.int64)
def voice(v):                    # the WSG's decode (MAME sound/namco.cpp pacman_sound_w)
    f = sum(wsg[:, v * 5 + 0x11 + i] << (4 * (i + 1)) for i in range(4))
    if v == 0: f = f | wsg[:, 0x10]
    return f, wsg[:, v * 5 + 0x15], wsg[:, v * 5 + 0x05] & 7
ch = [np.frombuffer(RS[:, 74 + 10 * v:84 + 10 * v].tobytes(), np.dtype([('f', '<u4'), ('v', '<u4'), ('w', '<u2')])) for v in range(3)]
bad = 0
for v in range(3):
    f, vol, w = voice(v)
    bad += ((ch[v]['f'] != f) | (ch[v]['v'] != vol) | (ch[v]['w'] != w)).sum()
print('WSG voices: MAME\'s decoded frequency/volume/waveform = the port\'s registers decoded, '
      '%s' % ('all frames' if not bad else '%d voice-frames differ' % bad))

# ---- what the SCSP plays: every write to slots 0-2 against what the WSG asks for -------
# pacman.c pac_sound_update, replayed from MAME's WSG registers (equal to the port's, above),
# gives the exact list of SCSP register writes each frame should send; the port queues them
# for the sound UART (~50 bytes a frame) and the 68000 makes them (port_scsp.bin: every
# write, emulated time). The list must match write for write, in order.
TLT = [255, 63, 47, 37, 30, 25, 21, 18, 14, 12, 9, 7, 5, 3, 1, 0]
def pitch_fx(t):
    if not t: return 0
    o = 0
    while t >= (2048 << 10) and o < 7: t >>= 1; o += 1
    while t < (1024 << 10) and o > -8: t <<= 1; o -= 1
    t = min(max(t >> 10, 1024), 2047)
    return ((o & 15) << 11) | (t - 1024)
en, rwsg = RS[:, 41], RS[:, 42:74].astype(np.int64)
want = []                                   # (frame index, register offset, value)
sent = [[0, 255, 0x1000] for _ in range(3)]
for k in range(n):
    for v in range(3):
        f = sum(int(rwsg[k, v * 5 + 0x11 + i]) << (4 * (i + 1)) for i in range(4)) | (int(rwsg[k, 0x10]) if v == 0 else 0)
        vol = int(rwsg[k, v * 5 + 0x15]) if en[k] else 0
        if not f: vol = 0
        pt, tl, sa = pitch_fx(f * 1393 // 20), TLT[vol & 15], 0x1000 + (int(rwsg[k, v * 5 + 5]) & 7) * 64
        if vol and pt != sent[v][0]: want.append((k, v * 0x20 + 0x10, pt)); sent[v][0] = pt
        if sa != sent[v][2]: want.append((k, v * 0x20 + 0x02, sa)); sent[v][2] = sa
        if tl != sent[v][1]: want.append((k, v * 0x20 + 0x0C, tl)); sent[v][1] = tl
wr = np.fromfile(D + '/port_scsp.bin', np.dtype([('t', '<f8'), ('o', '<u2'), ('v', '<u2')]))
key = np.nonzero((wr['o'] == 0) & (wr['v'] == 0x1820))[0]          # KYONEX: set-up done
got = wr[int(key[0]) + 1:] if len(key) else wr[:0]
# record k (frame k+1) is taken as frame k+2 starts, so frame k+1 started at record k-1's time
pt_ = np.frombuffer(PS[:, :8].tobytes(), '<f8')
pstart = np.concatenate([[0.0], pt_[:-1]])
ok = len(got) >= len(want) and all(int(g['o']) == o and int(g['v']) == v for g, (_, o, v) in zip(got, want))
first_bad = next((i for i, (g, (_, o, v)) in enumerate(zip(got, want)) if int(g['o']) != o or int(g['v']) != v), None)
dly = np.array([g['t'] - pstart[k] for g, (k, _, _) in zip(got, want)]) * 1000 if want else np.zeros(1)
print('SCSP: %d register writes asked for (pitch/level/waveform), %d made; %s; made %.1f-%.1f ms '
      '(mean %.1f) after their frame starts (a frame is 16.5 ms)' % (len(want), len(got), 'the same writes in the same order'
      if ok and len(got) == len(want) else 'MISMATCH at write %s' % first_bad, dly.min(), dly.max(), dly.mean()))
if first_bad is not None:
    for i in range(max(0, first_bad - 2), min(first_bad + 3, len(want), len(got))):
        print('        #%d frame %d want %03X=%04X got %03X=%04X' % (i, want[i][0] + 1, want[i][1], want[i][2], got[i]['o'], got[i]['v']))

# ---- the audio: MAME's WSG out against the port's SCSP out, per Pac-Man frame -----------
# Both WAVs run on emulated time (48 kHz). Each machine's audio is cut at its own frame
# starts (the port runs Pac-Man frames at the Model 2's vblanks), 512 samples a frame, the
# port's shifted by the delay that lines the two up best (its writes cross the sound UART,
# above), and compared as loudness and spectrum (cosine of magnitude spectra, 0-6 kHz)
# wherever MAME plays anything.
def wav(p):
    w = wave.open(p); a = np.frombuffer(w.readframes(w.getnframes()), '<i2').reshape(-1, w.getnchannels())
    return a.mean(1), w.getframerate()
rw, rate = wav(D + '/ref.wav'); pw, _ = wav(D + '/port.wav')
rt = np.frombuffer(RS[:, :8].tobytes(), '<f8')
rstart = np.concatenate([[0.0], rt[:-1]])          # MAME: record k is frame k+1's end
k0 = want[0][0] if want else n
W = 512
win = np.hanning(W); fb = int(6000 * W / rate)
def audio(off, frames):
    loud, spec = [], []
    for k in frames:
        a, b = int(rstart[k] * rate), int((pstart[k] + off) * rate)
        if a + W > len(rw) or b + W > len(pw): break
        x, y = rw[a:a + W], pw[b:b + W]
        if np.abs(x - x.mean()).max() < 200: continue          # MAME silent here
        x, y = x - x.mean(), y - y.mean()
        loud.append((np.sqrt((x * x).mean()), np.sqrt((y * y).mean())))
        X, Y = np.abs(np.fft.rfft(x * win))[1:fb], np.abs(np.fft.rfft(y * win))[1:fb]
        spec.append(float(X @ Y / (np.linalg.norm(X) * np.linalg.norm(Y) + 1e-9)))
    return np.array(loud), np.array(spec)
frames = range(max(k0, 1), n - 1)
best = max(range(0, 34), key=lambda ms: np.median(audio(ms / 1000, frames[::7])[1] if len(frames) else [0]))
L, S = audio(best / 1000, frames)
if len(S):
    print('audio: %d frames with sound; the port\'s trails MAME\'s by %d ms; loudness correlation %.3f; '
          'spectrum match median %.3f, >=0.9 in %d%%, >=0.8 in %d%%' % (len(S), best, np.corrcoef(L[:, 0], L[:, 1])[0, 1],
          np.median(S), 100 * (S >= 0.9).mean(), 100 * (S >= 0.8).mean()))
else:
    print('audio: MAME plays nothing in these frames')
