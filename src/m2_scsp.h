/*
 * m2_scsp.h — drive the sound board's SCSP straight from the i960.
 *
 * A game's own 68000 sound program decides what the SCSP plays; the i960 can only send it
 * bytes over the sound UART (i8251 at 0x9C0000, TxD wired to the SCSP's MIDI input: MAME
 * sega/model2.cpp model2_scsp). Replace that program with snd/scsp_passthru.s (load it
 * over :audiocpu with tools/m2_load.lua) and those bytes become raw SCSP register and
 * sound RAM writes — the i960 is then the sound CPU:
 *
 *     m2_scsp_init();                      // UART up (after m2_init)
 *     m2_scsp_ram_w(0x1000, sample);       // upload waveforms to sound RAM
 *     m2_scsp_w(M2_SCSP_SLOT(0) + 0x10, m2_scsp_pitch_fx(step));
 *     m2_scsp_flush();                     // or m2_scsp_pump() every so often
 *
 * Writes are queued and sent without blocking: call m2_scsp_pump() whenever there is time
 * (a vblank wait is ideal) — the UART takes a byte about every 320 us, ~50 per frame.
 * A register write costs 6 bytes, a RAM write 7.
 *
 * Include AFTER m2.h (uses M2_SND_DATA / M2_SND_CTL, M2_API).
 */
#ifndef M2_SCSP_H
#define M2_SCSP_H

#define M2_SCSP_SLOT(n)   ((u32)(n) * 0x20u)   /* slot n's registers (SCSP offset) */
#define M2_SCSP_QLEN      4096u                  /* power of two */

static u8  m2_scsp_q[M2_SCSP_QLEN];
static u32 m2_scsp_qh, m2_scsp_qt;              /* head (next to send), tail (next free) */

/* Same i8251 bring-up as m2_sound_init, without the STF sound-program handshake. */
M2_API void m2_scsp_init(void) {
    M2_SND_CTL = 0x00u; m2__snd_delay();
    M2_SND_CTL = 0x00u; m2__snd_delay();
    M2_SND_CTL = 0x00u; m2__snd_delay();
    M2_SND_CTL = 0x40u; m2__snd_delay();        /* internal reset           */
    M2_SND_CTL = 0x4Eu; m2__snd_delay();        /* mode: async 8-N-1, 16x   */
    M2_SND_CTL = 0x37u; m2__snd_delay();        /* cmd: TxEN|RxEN|DTR|RTS   */
    m2_scsp_qh = m2_scsp_qt = 0;
}

/* Is snd/scsp_passthru.s running on the sound board? Pings it (0xF0: a game's own sound
 * program ignores that byte) and waits up to `vblanks` frames for its 0x5A. Call before
 * sending anything else: to a game's program the packets would be MIDI notes. */
M2_API int m2_scsp_probe(u32 vblanks) {
    u32 t0 = frameVBL, sent = frameVBL - 8u;
    while (M2_SND_CTL & 0x02u) (void)M2_SND_DATA;            /* drop stale RX bytes */
    while (frameVBL - t0 < vblanks) {
        if (frameVBL - sent >= 8u && (M2_SND_CTL & 0x01u)) {  /* re-ping every 8 frames */
            M2_SND_DATA = 0xF0u;
            sent = frameVBL;
        }
        if ((M2_SND_CTL & 0x02u) && (u8)M2_SND_DATA == 0x5Au) return 1;
    }
    return 0;
}

/* Send queued bytes while the UART has room (TxRDY); never waits. */
M2_API void m2_scsp_pump(void) {
    while (m2_scsp_qh != m2_scsp_qt && (M2_SND_CTL & 0x01u)) {
        M2_SND_DATA = m2_scsp_q[m2_scsp_qh];
        m2_scsp_qh = (m2_scsp_qh + 1u) & (M2_SCSP_QLEN - 1u);
    }
}

/* Send everything queued (blocks until the UART has taken it). */
M2_API void m2_scsp_flush(void) {
    while (m2_scsp_qh != m2_scsp_qt) m2_scsp_pump();
}

M2_API u32 m2_scsp_pending(void) { return (m2_scsp_qt - m2_scsp_qh) & (M2_SCSP_QLEN - 1u); }

static void m2__scsp_put(u8 b) {
    while (m2_scsp_pending() == M2_SCSP_QLEN - 1u) m2_scsp_pump();   /* full: drain */
    m2_scsp_q[m2_scsp_qt] = b;
    m2_scsp_qt = (m2_scsp_qt + 1u) & (M2_SCSP_QLEN - 1u);
}

/* SCSP register write: word at SCSP offset `reg` (0x000-0xFFE) = v. */
M2_API void m2_scsp_w(u32 reg, u16 v) {
    m2__scsp_put(0x90);
    m2__scsp_put((u8)((reg >> 7) & 0x7Fu)); m2__scsp_put((u8)(reg & 0x7Fu));
    m2__scsp_put((u8)((v >> 14) & 0x03u)); m2__scsp_put((u8)((v >> 7) & 0x7Fu)); m2__scsp_put((u8)(v & 0x7Fu));
}

/* Sound RAM write: word at `addr` (0x00000-0x7FFFE, big-endian 16-bit) = v. The SCSP plays
 * samples from here; keep clear of the passthrough program's stack at the very top. */
M2_API void m2_scsp_ram_w(u32 addr, u16 v) {
    m2__scsp_put(0xA0);
    m2__scsp_put((u8)((addr >> 14) & 0x7Fu)); m2__scsp_put((u8)((addr >> 7) & 0x7Fu)); m2__scsp_put((u8)(addr & 0x7Fu));
    m2__scsp_put((u8)((v >> 14) & 0x03u)); m2__scsp_put((u8)((v >> 7) & 0x7Fu)); m2__scsp_put((u8)(v & 0x7Fu));
}

/* Slot pitch register (slot offset 0x10: OCT bits 14-11, FNS bits 9-0) for a playback rate
 * given as `t` = (samples/s / 44100) << 20, the step per 44.1 kHz output sample in 20-bit
 * fixed point. The SCSP steps (1024 + FNS) << OCT / 1024 samples per output sample (MAME
 * sound/scsp.cpp Step), OCT -8..7. Returns 0 for t = 0. */
M2_API u16 m2_scsp_pitch_fx(u32 t) {
    int oct = 0;
    if (!t) return 0;
    while (t >= (2048u << 10) && oct < 7)  { t >>= 1; oct++; }
    while (t <  (1024u << 10) && oct > -8) { t <<= 1; oct--; }
    t >>= 10;
    if (t < 1024u) t = 1024u;
    if (t > 2047u) t = 2047u;
    return (u16)((((u32)oct & 0xFu) << 11) | (t - 1024u));
}

#endif /* M2_SCSP_H */
