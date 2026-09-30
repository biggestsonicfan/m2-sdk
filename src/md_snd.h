/*
 * md_snd.h — the Mega Drive's sound chips (YM2612 FM, SN76489 PSG) played on the Model 2's
 * SCSP, through the sound board's 68000 running the relay program snd/scsp_passthru.s
 * (m2_scsp.h: the i960 writes SCSP registers and sound RAM over the serial line).
 *
 * The line carries ~50 bytes a frame, far too little to stream sound, so the chips are
 * re-voiced, not emulated:
 *   - each FM channel plays on one SCSP slot a looped 128-sample wavetable: two periods of
 *     the channel's instrument (algorithm, feedback, operator multipliers and modulator
 *     levels) rendered here, cached in sound RAM and uploaded the first time it is used.
 *     The carrier's attack/decay/sustain/release rates map onto the SCSP's envelope (the
 *     same 5-bit rates), its total level onto TL (0.75 dB steps -> 0.375). No detune, LFO,
 *     SSG-EG or channel 3 special mode.
 *   - the three PSG tones loop a square wave, the noise channel uses the SCSP's noise.
 *   - the drums: Sonic's Z80 driver plays 4-bit DPCM samples through the YM2612's DAC. The
 *     Z80 is not run; the samples are decoded from the driver the 68000 loads into Z80 RAM
 *     (table at Z80 0x00D6: pointer, length, rate; deltas at 0x0022), uploaded while the
 *     line is idle, and each command the 68000 leaves at Z80 0x1FFF plays one on a slot at
 *     the driver's rate: 3579545 / (137.5 + 13 x rate) samples/s (its loop's Z80 cycles).
 * Only SCSP registers that change are sent. When the line falls behind, pitch and level
 * updates wait (the last value wins) and new instruments play as a sine until uploaded.
 *
 * YM2612 frequencies: f = fnum * 2^(block-1) * (7670453 / 144) / 2^20 (the YM2612 manual).
 * SCSP registers and envelope: MAME sound/scsp.cpp. Include after md_hw.h and m2_scsp.h.
 */
#ifndef MD_SND_H
#define MD_SND_H

typedef signed char snd_s8;
typedef signed short snd_s16;
typedef signed int snd_s32;

#define SND_SLOT_FM    0                 /* SCSP slots 0-5: FM channels 1-6 */
#define SND_SLOT_PSG   6                 /* 6-8: PSG tones, 9: PSG noise */
#define SND_SLOT_DAC   10                /* 10: the drums */
#define SND_DAC_RAM    0x20000u          /* sound RAM: the decoded drum samples, 8-bit */
#define SND_DAC_N      3
#define SND_WAVES      0x10000u          /* sound RAM: the wavetable cache, 128 bytes each */
#define SND_NWAVES     256
#define SND_SQUARE     0x0f000u          /* a 32-sample square wave (PSG) */
#define SND_SINE       0x0f100u          /* a 128-sample sine: an instrument not uploaded yet */
#define SND_QMAX       3000u             /* leave the relay queue this much room */
/* direct send levels (DISDL: 7 = 0 dB, 6 dB a step): six FM voices and four PSG ones
 * at full level would clip */
#define SND_FM_SDL     0xa000u           /* -12 dB */
#define SND_PSG_SDL    0x8000u           /* -18 dB */
#define SND_NOISE_SDL  0x6000u           /* -24 dB */

static u8  snd_ok;                       /* the relay answered */
static u8  snd_ym[2][256];               /* YM2612 registers, per port */
static u8  snd_key[6];                   /* channel keyed on (any operator) */
static u16 snd_psg_tone[3], snd_psg_noise;
static u8  snd_psg_att[4] = { 15, 15, 15, 15 }, snd_psg_latch;
static u16 snd_sent[16][16];             /* SCSP slot registers last sent (0x00-0x1E / 2) */
static u8  snd_sent_ok[16][16];
static snd_s16 snd_sin[256];                 /* one period, amplitude 16384 */
static u32 snd_wkey[SND_NWAVES];         /* what each cached wavetable was rendered from */
static u8  snd_wready[SND_NWAVES];
static u32 snd_wnext;
static u32 snd_dropped;                  /* statistics: updates that had to wait */
static u32 snd_dac_at[SND_DAC_N], snd_dac_len[SND_DAC_N];   /* where each drum went, samples */
static u32 snd_dac_up;                   /* samples of all drums uploaded so far */
static u32 snd_dac_total;                /* 0: the driver's table not read yet */
static u8  snd_dac_ready[SND_DAC_N];

/* ---- the relay: SCSP slot registers, only what changed --------------------------------- */
static void snd_slot_w(u32 slot, u32 reg, u16 v) {
    u32 r = reg >> 1;
    if (snd_sent_ok[slot][r] && snd_sent[slot][r] == v && !(reg == 0 && (v & 0x1000))) return;
    snd_sent[slot][r] = v; snd_sent_ok[slot][r] = 1;
    m2_scsp_w(M2_SCSP_SLOT(slot) + reg, v);
}
static inline int snd_room(u32 bytes) { return m2_scsp_pending() + bytes < SND_QMAX; }

/* ---- tables ---------------------------------------------------------------------------- */
static void snd_tables(void) {
    /* sin by the rotation recurrence s[n+1] = 2 cos(d) s[n] - s[n-1], d = 2 pi / 256,
     * 2 cos(d) = 1.99939766 in 2.30 fixed point */
    snd_s32 a = 0, b = 402, c, i;                       /* 16384 sin(d) = 402.1 */
    const snd_s32 k = 2146836866;                       /* 2 cos(d) << 30 */
    for (i = 0; i < 256; i++) {
        snd_sin[i] = (snd_s16)a;
        c = (snd_s32)(((long long)k * b) >> 30) - a;
        a = b; b = c;
    }
}
/* sin of a phase in 1/256 turns, 8.8 fixed: amplitude 16384, interpolated */
static inline snd_s32 snd_sinp(snd_s32 ph) {
    snd_s32 i = (ph >> 8) & 255, f = ph & 255;
    return snd_sin[i] + (((snd_sin[(i + 1) & 255] - snd_sin[i]) * f) >> 8);
}

/* ---- FM: instruments as wavetables ---------------------------------------------------- */
/* operator k (0-3 = op1-op4) of channel ch: its register offset (op1 +0, op3 +4, op2 +8, op4 +12) */
static const u8 snd_opoff[4] = { 0, 8, 4, 12 };
static inline u8 snd_opreg(int ch, int k, int base) { return snd_ym[ch / 3][base + (ch % 3) + snd_opoff[k]]; }
/* which operators are carriers, per algorithm (bit k = op k+1) */
static const u8 snd_carriers[8] = { 8, 8, 8, 8, 10, 14, 14, 15 };

/* the loudest carrier: its total level decides the slot's, its rates the envelope */
static int snd_main_carrier(int ch) {
    int alg = snd_ym[ch / 3][0xb0 + ch % 3] & 7, k, best = 3, tl = 999;
    for (k = 0; k < 4; k++)
        if ((snd_carriers[alg] >> k) & 1) {
            int t = snd_opreg(ch, k, 0x40) & 0x7f;
            if (t < tl) { tl = t; best = k; }
        }
    return best;
}

/* what the timbre depends on: algorithm, feedback, multipliers, modulator levels, the
 * other carriers' levels against the loudest */
static u32 snd_patch_key(int ch) {
    u32 h = 2166136261u, alg = snd_ym[ch / 3][0xb0 + ch % 3] & 0x3f, k, mc = (u32)snd_main_carrier(ch);
    snd_s32 mtl = snd_opreg(ch, (int)mc, 0x40) & 0x7f;
    h = (h ^ alg) * 16777619u;
    for (k = 0; k < 4; k++) {
        snd_s32 tl = snd_opreg(ch, (int)k, 0x40) & 0x7f;
        if ((snd_carriers[alg & 7] >> k) & 1) tl -= mtl;
        h = (h ^ (u32)(snd_opreg(ch, (int)k, 0x30) & 0x0f)) * 16777619u;
        h = (h ^ (u32)tl) * 16777619u;
        h = (h ^ (u32)(snd_opreg(ch, (int)k, 0x80) >> 4)) * 16777619u;
    }
    return h | 1u;
}

static const u16 snd_db8[8] = { 256, 234, 215, 197, 181, 166, 152, 139 };   /* 256 * 10^(-0.0375 k) */

/* render two periods of the channel's instrument (128 samples, signed 8-bit) */
static void snd_render(int ch, snd_s8 *out) {
    int alg = snd_ym[ch / 3][0xb0 + ch % 3] & 7, fb = (snd_ym[ch / 3][0xb0 + ch % 3] >> 3) & 7, k, i;
    int mc = snd_main_carrier(ch);
    snd_s32 mul2[4], lev[4], fbprev = 0, fbprev2 = 0, peak = 1, acc[128];
    snd_s32 mtl = snd_opreg(ch, mc, 0x40) & 0x7f;
    for (k = 0; k < 4; k++) {
        snd_s32 m = snd_opreg(ch, k, 0x30) & 15, tl = snd_opreg(ch, k, 0x40) & 0x7f, db;
        mul2[k] = m ? m * 2 : 1;                         /* in half-periods: MUL 0 = x0.5 */
        if ((snd_carriers[alg] >> k) & 1) tl -= mtl;     /* carriers relative to the loudest */
        else tl += (snd_opreg(ch, k, 0x80) >> 4) * 4;     /* modulators at their sustain level
                                                            * (3 dB a step): the held note's timbre */
        /* level 0-4096 for 0.75 dB steps: 4096 * 10^(-0.0375 tl), halving every 8 steps */
        db = tl < 0 ? 0 : tl;
        lev[k] = db >= 96 ? 0 : ((4096 >> (db >> 3)) * snd_db8[db & 7]) >> 8;
    }
    for (i = 0; i < 128; i++) acc[i] = 0;
    /* 16 steps a sample, averaged down: the multipliers' harmonics and the feedback loop
     * need finer steps than the 64 a period kept */
    for (i = 0; i < 128 * 16; i++) {
        /* phase of op k at step i: i/1024 periods of the fundamental, times MUL */
        snd_s32 ph[4], o1, o2, o3, o4, mod1, out = 0;
        for (k = 0; k < 4; k++) ph[k] = (i * mul2[k] * 32) & 0xffff;               /* 8.8 turns */
        /* a modulator adds up to 4 periods of phase at full level (ymfm: op output >> 1) */
#define SND_OP(k, pm) ((snd_sinp(ph[k] + (pm)) * lev[k]) >> 12)                  /* +-16384 */
#define SND_PM(o) ((o) << 4)                                                         /* 16384 -> 4 turns */
        /* ymfm: (the last two outputs) >> (10 - FB), in its 1024-a-turn phase */
        mod1 = fb ? ((fbprev + fbprev2) << 5) >> (10 - fb) : 0;
        o1 = SND_OP(0, mod1);                            /* mod1 is phase already */
        fbprev2 = fbprev; fbprev = o1;
        switch (alg) {
        case 0: o2 = SND_OP(1, SND_PM(o1)); o3 = SND_OP(2, SND_PM(o2)); out = SND_OP(3, SND_PM(o3)); break;
        case 1: o3 = SND_OP(2, SND_PM(o1 + SND_OP(1, 0))); out = SND_OP(3, SND_PM(o3)); break;
        case 2: o3 = SND_OP(2, SND_PM(SND_OP(1, 0))); out = SND_OP(3, SND_PM(o1 + o3)); break;
        case 3: o2 = SND_OP(1, SND_PM(o1)); out = SND_OP(3, SND_PM(o2 + SND_OP(2, 0))); break;
        case 4: out = SND_OP(1, SND_PM(o1)) + SND_OP(3, SND_PM(SND_OP(2, 0))); break;
        case 5: out = SND_OP(1, SND_PM(o1)) + SND_OP(2, SND_PM(o1)) + SND_OP(3, SND_PM(o1)); break;
        case 6: out = SND_OP(1, SND_PM(o1)) + SND_OP(2, 0) + SND_OP(3, 0); break;
        default: out = o1 + SND_OP(1, 0) + SND_OP(2, 0) + SND_OP(3, 0); break;
        }
#undef SND_OP
#undef SND_PM
        (void)o4;
        acc[i >> 4] += out;
    }
    for (i = 0; i < 128; i++) {
        if (acc[i] > peak) peak = acc[i];
        if (-acc[i] > peak) peak = -acc[i];
    }
    for (i = 0; i < 128; i++) out[i] = (snd_s8)(acc[i] * 120 / peak);
}

/* the cached wavetable for the channel's instrument, rendered and queued if new;
 * -1 = not uploaded yet (the line is busy) */
static int snd_wave(int ch) {
    u32 key = snd_patch_key(ch), i;
    snd_s8 w[128];
    for (i = 0; i < SND_NWAVES; i++) if (snd_wkey[i] == key) return snd_wready[i] ? (int)i : -1;
    if (!snd_room(64 * 7 + 64)) return -1;
    i = snd_wnext; snd_wnext = (snd_wnext + 1) % SND_NWAVES;
    snd_wkey[i] = key;
    snd_render(ch, w);
    { u32 j; for (j = 0; j < 128; j += 2)
        m2_scsp_ram_w(SND_WAVES + i * 128 + j, (u16)(((u8)w[j] << 8) | (u8)w[j + 1])); }
    snd_wready[i] = 1;      /* queued ahead of any key-on that uses it: in order on the line */
    return (int)i;
}

/* YM2612 fnum/block -> SCSP pitch for a 64-samples-a-period wave:
 * t = f * 64 / 44100 * 2^20 = fnum * 2^(block-1) * 77.30 */
static u16 snd_fm_pitch(int ch) {
    u32 lo = snd_ym[ch / 3][0xa0 + ch % 3], hi = snd_ym[ch / 3][0xa4 + ch % 3];
    u32 fnum = ((hi & 7) << 8) | lo, block = (hi >> 3) & 7;
    u32 t = fnum * 773u / 10u;
    t = block ? t << (block - 1) : t >> 1;
    return m2_scsp_pitch_fx(t);
}

/* the SCSP decay level (linear, 0-31 against the attack's peak) for a YM sustain level
 * (3 dB steps, 15 = -93 dB) */
static const u8 snd_dl[16] = { 0, 9, 15, 20, 23, 25, 27, 28, 29, 30, 30, 31, 31, 31, 31, 31 };

static void snd_fm_update(int ch, int keyon_edge) {
    u32 slot = SND_SLOT_FM + (u32)ch;
    int mc = snd_main_carrier(ch);
    u32 ar = snd_opreg(ch, mc, 0x50) & 31, d1r = snd_opreg(ch, mc, 0x60) & 31, d2r = snd_opreg(ch, mc, 0x70) & 31;
    u32 slrr = snd_opreg(ch, mc, 0x80), tl = (snd_opreg(ch, mc, 0x40) & 0x7f) * 2u;
    u32 pan = snd_ym[ch / 3][0xb4 + ch % 3] >> 6, dipan = pan == 2 ? 0x1f : pan == 1 ? 0x0f : 0;
    if (tl > 255) tl = 255;
    if (ch == 5 && (snd_ym[0][0x2b] & 0x80)) return;             /* DAC mode */
    if (keyon_edge) {
        int w;
        if (!snd_room(12 * 6)) { snd_dropped++; return; }
        w = snd_wave(ch);
        {
            u32 sa = w < 0 ? SND_SINE : SND_WAVES + (u32)w * 128u;
            snd_slot_w(slot, 0x02, (u16)sa);
            snd_slot_w(slot, 0x04, 0);                             /* LSA */
            snd_slot_w(slot, 0x06, 128);                           /* LEA */
            snd_slot_w(slot, 0x08, (u16)((d2r << 11) | (d1r << 6) | ar));
            snd_slot_w(slot, 0x0A, (u16)((0xf << 10) | (snd_dl[slrr >> 4] << 5) | (((slrr & 15) * 2 + 1) & 31)));
            snd_slot_w(slot, 0x0C, (u16)tl);
            snd_slot_w(slot, 0x10, snd_fm_pitch(ch));
            snd_slot_w(slot, 0x16, (u16)(SND_FM_SDL | (dipan << 8)));
            snd_slot_w(slot, 0x00, (u16)(0x1800 | 0x0020 | 0x0010 | ((sa >> 16) & 15)));   /* KYONEX|KYONB, loop, 8-bit */
        }
        return;
    }
    if (!snd_room(3 * 6)) { snd_dropped++; return; }
    snd_slot_w(slot, 0x10, snd_fm_pitch(ch));
    snd_slot_w(slot, 0x0C, (u16)tl);
}

static void snd_fm_keyoff(int ch) {
    u32 slot = SND_SLOT_FM + (u32)ch;
    if (!snd_sent_ok[slot][0]) return;
    snd_slot_w(slot, 0x00, (u16)((snd_sent[slot][0] & ~0x0800u) | 0x1000));   /* KYONEX, KYONB off */
}

/* ---- PSG ------------------------------------------------------------------------------ */
/* tone: 3579545 / (32 N) Hz on a 32-sample square: t = 3579545 * 2^20 / (44100 N) */
static void snd_psg_update(int c) {
    u32 slot = SND_SLOT_PSG + (u32)c, att = snd_psg_att[c];
    u16 tl = (u16)(att >= 15 ? 255 : att * 16 / 3);
    if (!snd_room(3 * 6)) { snd_dropped++; return; }
    if (c < 3) {
        u32 n = snd_psg_tone[c] ? snd_psg_tone[c] : 1024;
        snd_slot_w(slot, 0x10, m2_scsp_pitch_fx(85110910u / n));
    }
    snd_slot_w(slot, 0x0C, tl);
}

/* ---- the Mega Drive's writes ----------------------------------------------------------- */
static void snd_ym_w(u32 port, u32 reg, u32 v) {
    snd_ym[port][reg] = (u8)v;
    if (port == 0 && reg == 0x28) {                                 /* key on/off */
        u32 c = v & 7;
        int ch = c < 3 ? (int)c : c >= 4 && c < 7 ? (int)c - 1 : -1;
        if (ch < 0) return;
        if (v & 0xf0) { snd_key[ch] = 1; snd_fm_update(ch, 1); }
        else if (snd_key[ch]) { snd_key[ch] = 0; snd_fm_keyoff(ch); }
        return;
    }
    if ((reg & 0xf0) == 0xa0 && (reg & 3) != 3) {                   /* frequency: on the fnum write */
        int ch = (int)port * 3 + (int)(reg & 3);
        if (!(reg & 4) && snd_key[ch]) snd_fm_update(ch, 0);
        return;
    }
    if ((reg & 0xf0) == 0x40 && (reg & 3) != 3) {                   /* level: volume changes */
        int ch = (int)port * 3 + (int)(reg & 3);
        if (snd_key[ch]) snd_fm_update(ch, 0);
    }
}

static void snd_psg_w(u32 v) {
    u32 c;
    if (v & 0x80) {
        snd_psg_latch = (u8)v;
        c = (v >> 5) & 3;
        if (v & 0x10) { snd_psg_att[c] = (u8)(v & 15); snd_psg_update((int)c); }
        else if (c < 3) { snd_psg_tone[c] = (u16)((snd_psg_tone[c] & 0x3f0) | (v & 15)); snd_psg_update((int)c); }
        else snd_psg_noise = (u16)(v & 7);
        return;
    }
    c = (snd_psg_latch >> 5) & 3;
    if (!(snd_psg_latch & 0x10) && c < 3) {
        snd_psg_tone[c] = (u16)((snd_psg_tone[c] & 15) | ((v & 0x3f) << 4));
        snd_psg_update((int)c);
    }
}

/* ---- drums ---------------------------------------------------------------------------- */
/* the n-th decoded sample of drum d (the driver's DPCM: acc starts at 0x80, a byte's high
 * nibble first), decoded sequentially: call for i = 0, 1, 2... */
static u8 snd_dac_acc;
static snd_s8 snd_dac_sample(u32 d, u32 i) {
    u32 p = md_zram[0xd6 + d * 8] | (md_zram[0xd7 + d * 8] << 8);
    u8 b = md_zram[(p + (i >> 1)) & 0x1fff], n = (i & 1) ? (u8)(b & 15) : (u8)(b >> 4);
    if (i == 0) snd_dac_acc = 0x80;
    snd_dac_acc = (u8)(snd_dac_acc + md_zram[0x22 + n]);
    return (snd_s8)(snd_dac_acc - 0x80);
}

/* read the table once the 68000 has loaded the driver; then feed the samples to sound RAM
 * a few words a frame, while the line has nothing more urgent */
static void snd_dac_feed(void) {
    u32 d, words = 0;
    if (!snd_dac_total) {
        u32 at = SND_DAC_RAM;
        if (md_zram[0x32] != 0x21 || !md_zram[0xd6]) return;     /* not loaded yet */
        for (d = 0; d < SND_DAC_N; d++) {
            snd_dac_at[d] = at;
            snd_dac_len[d] = ((u32)md_zram[0xd8 + d * 8] | ((u32)md_zram[0xd9 + d * 8] << 8)) * 2u;
            if (snd_dac_len[d] > 0x4000) snd_dac_len[d] = 0x4000;
            at += (snd_dac_len[d] + 1) & ~1u;
            snd_dac_total += snd_dac_len[d];
        }
    }
    while (snd_dac_up < snd_dac_total && m2_scsp_pending() < 64 && words < 8) {
        u32 base = 0, i;
        snd_s8 a, b;
        for (d = 0; d < SND_DAC_N && snd_dac_up >= base + snd_dac_len[d]; d++) base += snd_dac_len[d];
        i = snd_dac_up - base;
        a = snd_dac_sample(d, i);
        b = i + 1 < snd_dac_len[d] ? snd_dac_sample(d, i + 1) : 0;
        m2_scsp_ram_w(snd_dac_at[d] + i, (u16)(((u8)a << 8) | (u8)b));
        snd_dac_up += i + 2 <= snd_dac_len[d] ? 2 : 1;
        if (snd_dac_up >= base + snd_dac_len[d]) snd_dac_ready[d] = 1;
        words++;
    }
}

/* a drum command (0x81 kick, 0x82 snare, 0x83 timpani; Sonic's higher timpani notes
 * rewrite its rate) */
static void snd_dac_play(u32 cmd) {
    u32 d = cmd - 0x81, rate, t, slot = SND_SLOT_DAC;
    if (d >= SND_DAC_N || !snd_dac_ready[d] || !snd_room(8 * 6)) return;
    rate = md_zram[0xda + d * 8];
    /* samples/s = 3579545 / (137.5 + 13 rate); t = that / 44100 * 2^20 */
    t = 170223355u / (275u + 26u * rate);             /* 2 x 3579545 x 2^20 / 44100 */
    snd_slot_w(slot, 0x00, 0x1000);                               /* key off first: restart */
    snd_slot_w(slot, 0x02, (u16)snd_dac_at[d]);
    snd_slot_w(slot, 0x04, 0);
    snd_slot_w(slot, 0x06, (u16)(snd_dac_len[d] - 1));
    snd_slot_w(slot, 0x08, 0x001f);
    snd_slot_w(slot, 0x0A, 0x3c1f);
    snd_slot_w(slot, 0x0C, 0);
    snd_slot_w(slot, 0x10, m2_scsp_pitch_fx(t));
    snd_slot_w(slot, 0x16, SND_FM_SDL);
    snd_slot_w(slot, 0x00, (u16)(0x1800 | 0x0010 | ((snd_dac_at[d] >> 16) & 15)));   /* no loop, 8-bit */
}

/* ---- setup, per frame ------------------------------------------------------------------ */
static void snd_init(void) {
    int i, s;
    snd_tables();
    m2_scsp_init();
    snd_ok = (u8)m2_scsp_probe(90);                  /* ~1.5 s: the sound board boots alongside */
    if (!snd_ok) return;
    m2_scsp_irq_start();                              /* the UART's interrupt sends from now on */
    /* a square (PSG) and a sine (an instrument not uploaded yet), 8-bit */
    for (i = 0; i < 32; i += 2) m2_scsp_ram_w(SND_SQUARE + (u32)i, i < 16 ? 0x6060 : 0xa0a0);
    for (i = 0; i < 128; i += 2)
        m2_scsp_ram_w(SND_SINE + (u32)i, (u16)(((u8)(snd_sin[i * 2] >> 7) << 8) | (u8)(snd_sin[(i * 2 + 2) & 255] >> 7)));
    for (s = 0; s < 6; s++) {
        snd_slot_w((u32)s, 0x02, (u16)SND_SINE);
        snd_slot_w((u32)s, 0x06, 128);
        snd_slot_w((u32)s, 0x0C, 255);
        snd_slot_w((u32)s, 0x16, SND_FM_SDL);
    }
    for (s = 0; s < 3; s++) {                         /* PSG tones: looped square, always keyed */
        u32 sl = SND_SLOT_PSG + (u32)s;
        snd_slot_w(sl, 0x02, (u16)SND_SQUARE);
        snd_slot_w(sl, 0x04, 0);
        snd_slot_w(sl, 0x06, 32);
        snd_slot_w(sl, 0x08, 0x001f);                 /* AR max, no decay */
        snd_slot_w(sl, 0x0A, 0x3c1f);
        snd_slot_w(sl, 0x0C, 255);
        snd_slot_w(sl, 0x16, SND_PSG_SDL);
        snd_slot_w(sl, 0x00, 0x0830);                 /* KYONB, loop, 8-bit */
    }
    {                                                 /* PSG noise: the SCSP's noise source */
        u32 sl = SND_SLOT_PSG + 3;
        snd_slot_w(sl, 0x08, 0x001f);
        snd_slot_w(sl, 0x0A, 0x3c1f);
        snd_slot_w(sl, 0x0C, 255);
        snd_slot_w(sl, 0x16, SND_NOISE_SDL);
        snd_slot_w(sl, 0x00, 0x1880);                 /* KYONEX|KYONB, SSCTL 1 (noise) */
    }
}

/* after each Mega Drive frame: play what the game wrote */
static void snd_update(void) {
    if (snd_ok && md_z80_cmd_new) { md_z80_cmd_new = 0; if (md_z80_cmd >= 0x81) snd_dac_play(md_z80_cmd); }
    while (md_snd_tail != md_snd_head) {
        u32 e = md_snd[md_snd_tail++ & (MD_SND_Q - 1)];
        if (!snd_ok) continue;
        if ((e >> 24) == 1) snd_ym_w((e >> 16) & 1, (e >> 8) & 0xff, e & 0xff);
        else snd_psg_w(e & 0xff);
    }
    if (snd_ok) snd_dac_feed();
}

#endif /* MD_SND_H */
