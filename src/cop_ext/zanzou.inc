/* From m2-hle2 (2662dda2) src/board/sharc_zanzou.h: the afterimage (zanzou) ring, 0x7c. */
#define ZZ_RING          0x32300u   /* slot 0 */
#define ZZ_RING_SLOTS    0x80
#define ZZ_SLOT_WORDS    0x20u
#define ZZ_MAT           0x14u      /* matrix offset inside a slot */
#define ZZ_WRITE_IDX     0x32180u
#define ZZ_SPACING       0x32181u
#define ZZ_LIFE0         0x32182u
#define ZZ_DELTA         0x32184u

static inline uint32_t zz_cur_base  (int p1) { return p1 ? 0x304E0u : 0x30420u; }
static inline uint32_t zz_prev_base (int p1) { return p1 ? 0x320C0u : 0x32000u; }
static inline uint32_t zz_attr_base (int p1) { return p1 ? 0x32200u : 0x321A0u; }
static inline uint32_t zz_timer_base(int p1) { return p1 ? 0x321F0u : 0x32190u; }
static inline uint32_t zz_mask_addr (int p1) { return p1 ? 0x32240u : 0x321E0u; }

/* ---- Fn_zanzou_init (0x81, PM 0x208E1) ----------------------------------- */
static inline void sharc_zanzou_init(void) { for (uint32_t a = 0x32180u; a < 0x33300u; a++) sharc_dm_set(a, 0); }

/* ---- Fn_zanzou_kill_timer_buffer (0x86, PM 0x20955) ---------------------- */
static inline void sharc_zanzou_kill_timers(uint32_t player) {
    uint32_t base = zz_timer_base(player != 0);
    for (uint32_t k = 0; k < 0x10u; k++) sharc_dm_set(base + k, 0);
}

/* ---- Fn_zanzou_inc (0x82, PM 0x20911) ------------------------------------
 * Age every live slot by its own step and answer how many are still alive.
 * A slot that crosses into negative is cleared and not counted. */
static inline uint32_t sharc_zanzou_inc(void) {
    uint32_t live = 0;
    for (int n = 0; n < ZZ_RING_SLOTS; n++) {
        uint32_t base = ZZ_RING + (uint32_t)n * ZZ_SLOT_WORDS;
        int32_t life = (int32_t)sharc_dm_get(base + 2);
        if (life == 0) continue;
        life += (int32_t)sharc_dm_get(base + 3);
        live++;
        if (life < 0) { live--; life = 0; }
        sharc_dm_set(base + 2, (uint32_t)life);
    }
    return live;
}

/* ---- Fn_zanzou_get_info (0x85, PM 0x208E6) -------------------------------
 * Five words for one ring slot: part, player|flag, life, step, object number.
 * The object number is the age-picked one of the three the reserve stored —
 * two integer thresholds built off the (negative) step, exactly as the
 * firmware builds them, so the trail swaps model as it fades. */
static inline void sharc_zanzou_get_info(uint32_t slot, uint32_t out[5]) {
    uint32_t base = ZZ_RING + (slot & 0x7Fu) * ZZ_SLOT_WORDS;
    out[0] = sharc_dm_get(base + 0);
    out[1] = sharc_dm_get(base + 1);
    int32_t life = (int32_t)sharc_dm_get(base + 2);
    out[2] = (uint32_t)life;
    int32_t step = (int32_t)sharc_dm_get(base + 3);
    out[3] = (uint32_t)step;

    uint32_t mag = (uint32_t)step;
    if ((int32_t)mag < 0) mag = 0u - mag;                  /* btst by 31, negate */
    uint32_t t1 = (mag >> 1) + mag;                        /* 1.5 × |step| */
    uint32_t q  = (mag >> 2) - 1u;
    uint32_t t0 = ((q << 3) - q) + 0x28u;                  /* 7 × (|step|/4 − 1) + 40 */

    uint32_t obj = sharc_dm_get(base + 4);
    if (!((int32_t)t0 < life)) {
        obj = sharc_dm_get(base + 5);
        if (!((int32_t)t1 < life)) obj = sharc_dm_get(base + 6);
    }
    out[4] = obj;
}

/* ---- Fn_zanzou_reserve (0x80, PM 0x20961) -------------------------------- */

/* PM 0x20A6D, the tail of reserve: every slot that still owes a turn gets one
 * ang_x by the command's angle, on its own stored matrix.  The flag bit is
 * cleared as it goes, so a slot turns once and not once per fighter — both
 * fighters' reserves walk the same ring. */
static inline void sharc_zanzou_tick_all(int32_t angle, float *cos_out) {
    float s, c;
    sharc_sincos(angle, &s, &c);
    *cos_out = c;
    for (int n = 0; n < ZZ_RING_SLOTS; n++) {
        uint32_t base = ZZ_RING + (uint32_t)n * ZZ_SLOT_WORDS;
        uint32_t w = sharc_dm_get(base + 1);
        if (!(w & 0x80000000u)) continue;
        sharc_dm_set(base + 1, w & 1u);
        uint32_t m = base + ZZ_MAT;
        for (uint32_t k = 0; k < 3u; k++) {                /* col1, col2 */
            float a = sharc_dm_getf(m + 3u + k), b = sharc_dm_getf(m + 6u + k);
            sharc_dm_setf(m + 3u + k, c * a - s * b);
            sharc_dm_setf(m + 6u + k, s * a + c * b);
        }
    }
}

/* The streaming state machine.  One of these is live between the command word
 * and its terminator; cop.h feeds it, and sharc_exec replays a whole captured
 * argument list through it. */
typedef struct {
    int      phase;      /* 0 header, 1 record index, 2 record body, 3 tail, 4 done */
    int      k;
    uint32_t player, mask, step, bone;
    uint32_t idx, rec[3];
    int      words;
} zanzou_stream_t;

static zanzou_stream_t g_zz = { 4, 0, 0, 0, 0, 0, 0, { 0, 0, 0 }, 0 };

static inline void sharc_zanzou_begin(void) {
    memset(&g_zz, 0, sizeof g_zz);
}

/* One part's run of copies (PM 0x20A17).  t walks 0 → 1 in steps of stepf, and
 * each stop lays the part's matrix, interpolated that far from last frame's
 * towards this frame's, into the next ring slot. */
static inline void zz_spawn(int part, float stepf, int p1, uint32_t angle) {
    uint32_t attr = zz_attr_base(p1);
    uint32_t a0 = sharc_dm_get(attr + (uint32_t)part);
    uint32_t a1 = sharc_dm_get(attr + (uint32_t)part + 0x10u);
    uint32_t a2 = sharc_dm_get(attr + (uint32_t)part + 0x20u);

    uint32_t cur = zz_cur_base(p1)  + (uint32_t)part * 12u;
    uint32_t prv = zz_prev_base(p1) + (uint32_t)part * 12u;
    for (uint32_t k = 0; k < 12u; k++)
        sharc_dm_setf(ZZ_DELTA + k, sharc_dm_getf(cur + k) - sharc_dm_getf(prv + k));

    uint32_t ring = sharc_dm_get(ZZ_WRITE_IDX) & 0x7Fu;
    uint32_t tp   = zz_timer_base(p1) + (uint32_t)part;
    int32_t  life = (int32_t)sharc_dm_get(tp);
    life = life != 0 ? life - 1 : (int32_t)sharc_dm_get(ZZ_LIFE0);
    sharc_dm_set(tp, (uint32_t)life);

    uint32_t flag = angle != 0 ? 0x80000000u : 0u;
    float    t    = 0.0f;
    float    one  = sharc_dm_getf(0x30301u);               /* the firmware's own 1.0 */
    for (;;) {
        uint32_t base = ZZ_RING + ring * ZZ_SLOT_WORDS;
        sharc_dm_set(base + 0, (uint32_t)part);
        sharc_dm_set(base + 1, g_zz.player | flag);
        sharc_dm_set(base + 2, (uint32_t)life);
        sharc_dm_set(base + 3, g_zz.step);
        sharc_dm_set(base + 4, a0);
        sharc_dm_set(base + 5, a1);
        sharc_dm_set(base + 6, a2);
        life++;
        for (uint32_t k = 0; k < 12u; k++)
            sharc_dm_setf(base + ZZ_MAT + k,
                          sharc_dm_getf(ZZ_DELTA + k) * t + sharc_dm_getf(prv + k));
        ring = (ring + 1u) & 0x7Fu;
        if (!(0.0f < stepf)) break;
        t += stepf;
        if (!(t < one)) break;
    }
    sharc_dm_set(ZZ_WRITE_IDX, ring);
    sharc_dm_set(tp, (uint32_t)life);
}

/* The terminator's work: measure, choose the spacing, spawn, then tick. */
static inline void zz_finish(uint32_t angle) {
    int p1 = g_zz.player == 1u;
    uint32_t cur_b = zz_cur_base(p1), prv_b = zz_prev_base(p1);
    float s = sharc_bits_to_float(g_zz.bone);

    /* PM 0x209AE: the farthest any active part moved since last frame, taking
     * both the joint and a point one bone-length out along its own +X. */
    float far2 = 0.0f;
    for (int b = 0; b < 16; b++) {
        if (!((g_zz.mask >> b) & 1u)) continue;
        uint32_t c = cur_b + (uint32_t)b * 12u, p = prv_b + (uint32_t)b * 12u;
        float d0 = sharc_dm_getf(c + 9u)  - sharc_dm_getf(p + 9u);
        float d1 = sharc_dm_getf(c + 10u) - sharc_dm_getf(p + 10u);
        float d2 = sharc_dm_getf(c + 11u) - sharc_dm_getf(p + 11u);
        float d  = d0 * d0;  d = d + d1 * d1;  d = d + d2 * d2;
        if (!(d < far2)) far2 = d;

        float cx = sharc_dm_getf(c + 9u)  + s * sharc_dm_getf(c + 0u);
        float cy = sharc_dm_getf(c + 10u) + s * sharc_dm_getf(c + 1u);
        float cz = sharc_dm_getf(c + 11u) + s * sharc_dm_getf(c + 2u);
        float px = sharc_dm_getf(p + 9u)  + s * sharc_dm_getf(p + 0u);
        float py = sharc_dm_getf(p + 10u) + s * sharc_dm_getf(p + 1u);
        float pz = sharc_dm_getf(p + 11u) + s * sharc_dm_getf(p + 2u);
        d0 = cx - px; d1 = cy - py; d2 = cz - pz;
        d = d0 * d0;  d = d + d1 * d1;  d = d + d2 * d2;
        if (!(d < far2)) far2 = d;
    }

    /* A move of more than 13 units squared is a teleport, not a swing: no
     * trail, and the answer is that threshold itself (the i960 throws it
     * away — every answer this command gives is read into a dead register). */
    if (13.0f <= far2) { sharc_push_u(0x41500000u); return; }

    float k = sharc_fw_rsqrt(far2);                        /* _L2029B */
    float stepf = sharc_dm_getf(ZZ_SPACING) * k;           /* one copy per 0.1 units */
    if (stepf == 0.0f) stepf = 1.0f;
    if (!(0.01f < stepf)) stepf = 0.01f;                   /* at most 100 copies */

    for (int b = 0; b < 16; b++) {
        if ((g_zz.mask >> b) & 1u) zz_spawn(b, stepf, p1, angle);
        else sharc_dm_set(zz_timer_base(p1) + (uint32_t)b, 0);
    }

    float c;
    sharc_zanzou_tick_all((int32_t)angle, &c);
    sharc_push_u(sharc_float_to_bits(c));                  /* r0, out of _L202C1 */
}

/* Feed one word. Returns true once the command is complete. */
static inline bool sharc_zanzou_feed(uint32_t w) {
    if (g_zz.phase == 4) return true;
    if (++g_zz.words > 96) { g_zz.phase = 4; return true; }   /* desync guard */

    switch (g_zz.phase) {
        case 0:
            switch (g_zz.k++) {
                case 0: g_zz.player = w; break;
                case 1: g_zz.mask   = w; break;
                case 2: g_zz.step   = w; break;
                default: {
                    g_zz.bone = w;
                    /* PM 0x20987: a part that changed state starts its age over. */
                    int p1 = g_zz.player == 1u;
                    uint32_t ma = zz_mask_addr(p1), tb = zz_timer_base(p1);
                    uint32_t chg = g_zz.mask ^ sharc_dm_get(ma);
                    for (uint32_t b = 0; b < 16u; b++)
                        if ((chg >> b) & 1u) sharc_dm_set(tb + b, 0);
                    sharc_dm_set(ma, g_zz.mask);
                    g_zz.phase = 1; g_zz.k = 0;
                } break;
            }
            return false;

        case 1:
            if (w == 0xFFFFFFFFu) { g_zz.phase = 3; return false; }
            g_zz.idx = w & 0xFu; g_zz.phase = 2; g_zz.k = 0;
            return false;

        case 2: {
            g_zz.rec[g_zz.k++] = w;
            if (g_zz.k < 3) return false;
            uint32_t attr = zz_attr_base(g_zz.player == 1u) + g_zz.idx;
            sharc_dm_set(attr,         g_zz.rec[0]);
            sharc_dm_set(attr + 0x10u, g_zz.rec[1]);
            sharc_dm_set(attr + 0x20u, g_zz.rec[2]);
            sharc_push_u(g_zz.idx + 0x20u);                /* what the loop reads back */
            g_zz.phase = 1;
            return false;
        }

        default:
            zz_finish(w);
            g_zz.phase = 4;
            return true;
    }
}

