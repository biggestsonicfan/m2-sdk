/* From m2-hle2 (2662dda2) src/board/sharc_exec.h: sharc_exec, the cpres1 commands. The matrix
 * stack and the FIFO go through the VM natives (compat.h). One change: 0x32 runs its cubic for
 * any span, as cpres1 does (m2-hle2 returns v0 when span <= 0; sfight disagrees). */
/* ---- Matrix helpers shared by several commands ---------------------------- */

/* push: copy the current matrix one level down the stack (0x00800101). */
static inline void sharc_push_stack(void) { vm_sync_out(); vm_push(); }

/* _L201EA, as 0x0B, 0x37 and 0x45 use it: the current matrix composed with B
 * (12 words, column-major 3x3 then T) — each new column is rot * B's column,
 * T = rot * B's T + T. In the firmware's order, which the rounding follows:
 * the three products summed left to right, and T added last. */
static inline void sharc_compose_words(const float *B, float *out) {
    float (*r)[3] = g_sharc.rot;
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            float s = B[j*3] * r[0][i];
            s = s + B[j*3+1] * r[1][i];
            out[j*3+i] = s + B[j*3+2] * r[2][i];
        }
    for (int i = 0; i < 3; i++) {
        float s = B[9] * r[0][i];
        s = s + B[10] * r[1][i];
        s = s + B[11] * r[2][i];
        out[9+i] = s + g_sharc.pos[i];
    }
}

static inline void sharc_compose(const float *B) {
    float w[12];
    sharc_compose_words(B, w);
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) g_sharc.rot[j][i] = w[j*3+i];
    memcpy(g_sharc.pos, w + 9, sizeof g_sharc.pos);
}

/* 0x37's composition with a unit-matrix cache slot (player arg, slot*12 arg). */
/* A unit slot a fighter's table has (0..15). The firmware addresses the slot
 * from the offset with no bound, so a larger one would reach past the table
 * there; we ignore it, and say so once. STF never sends one. */
static inline bool sharc_unit_slot_ok(int slot) {
    static int warned = 0;
    if ((unsigned)slot < 16u) return true;
    if (!warned) { LOG_WARN("SHARC: unit slot %d is past the 16 a fighter has; ignored", slot); warned = 1; }
    return false;
}

/* The inner bank's slot n (PM 0x21F20 + 12n). The firmware has no bound
 * either; past 15 we wrap, and say so once. STF never sends one. */
static inline float *sharc_inner_slot(uint32_t n) {
    static int warned = 0;
    if (n >= 16u && !warned) { LOG_WARN("SHARC: inner slot %u is past the 16 we keep; wrapped", n); warned = 1; }
    return g_sharc.pm_bone[n & 0xFu];
}

static inline void sharc_compose_unit(uint32_t player_arg, uint32_t slot_arg) {
    int player = (player_arg & 0xFF) == 1 ? 1 : 0;
    int slot   = (int)slot_arg / 12;
    if (sharc_unit_slot_ok(slot)) sharc_compose(ROT_CACHE[player * 16 + slot]);
}

/* _L2021F, as 0x46 and kage_poly use it: B applied after the current matrix —
 * each current column (and T) is transformed by B, then B's T added:
 * current = B * current. (Read the other way round it only ever agreed where
 * the two commuted; the board's shadows and 0x46 disagreed everywhere else.) */
static inline void sharc_compose_rev(const float *pm) {
    float (*r)[3] = g_sharc.rot;
    float nr[3][3], np[3];
    for (int c = 0; c < 3; c++)
        for (int w = 0; w < 3; w++) {
            float v = r[c][0] * pm[w];
            v = v + r[c][1] * pm[3 + w];
            v = v + r[c][2] * pm[6 + w];
            nr[c][w] = v;
        }
    for (int w = 0; w < 3; w++) {
        float v = g_sharc.pos[0] * pm[w];
        v = v + g_sharc.pos[1] * pm[3 + w];
        v = v + g_sharc.pos[2] * pm[6 + w];
        np[w] = v + pm[9 + w];
    }
    memcpy(g_sharc.rot, nr, sizeof nr);
    memcpy(g_sharc.pos, np, sizeof np);
}

/* 12 slot words (col0, col1, col2, T) become the current matrix. */
static inline void sharc_load_words(const float *B) {
    for (int c = 0; c < 3; c++)
        for (int w = 0; w < 3; w++) g_sharc.rot[c][w] = B[c*3 + w];
    for (int w = 0; w < 3; w++) g_sharc.pos[w] = B[9 + w];
}

/* 0x44: the inner bank's matrix n becomes the current matrix. */
static inline void sharc_load_inner(int n) { sharc_load_words(sharc_inner_slot((uint32_t)n)); }

/* The shadow reorients kage_poly calls, read word for word off _L20516 and
 * _L20529 (slot words: 0..2 col0, 3..5 col1, 6..8 col2). */
static inline void sharc_kage_leave_x(void) {       /* Fn_kage_leave_x_axis */
    float (*r)[3] = g_sharc.rot;
    float x = r[0][0], z = r[0][2], m = x*x + z*z, k = sharc_fw_rsqrt(m);      /* _L2029B */
    float nx = x * k, nz = z * k;
    r[1][0] = nx;  r[1][2] = nz;
    r[2][0] = -nz; r[2][2] = nx;
}
static inline void sharc_kage_leave_z(void) {       /* Fn_kage_leave_z_axis */
    float (*r)[3] = g_sharc.rot;
    float x = r[2][0], z = r[2][2], m = x*x + z*z, k = sharc_fw_rsqrt(m);      /* _L2029B */
    float nx = x * k, nz = z * k;
    r[1][0] = nx;  r[1][2] = nz;
    r[0][2] = -nx; r[0][0] = nz;
}

/* pop: the matrix one level up the stack becomes current (0x01000202). */
static inline void sharc_pop_stack(void) { vm_pop(); vm_sync_in(); }

/* the current matrix into the inner bank's slot n (_L2053E) */
static inline void sharc_store_inner(int n) {
    float *pm = sharc_inner_slot((uint32_t)n);
    for (int c = 0; c < 3; c++)
        for (int w = 0; w < 3; w++) pm[c*3 + w] = g_sharc.rot[c][w];
    for (int w = 0; w < 3; w++) pm[9 + w] = g_sharc.pos[w];
}

static inline void sharc_ang_y(int32_t a) { float s_, c_; sharc_sincos(a, &s_, &c_); sharc_postmul_ry(c_, s_); }
static inline void sharc_ang_x(int32_t a) { float s_, c_; sharc_sincos(a, &s_, &c_); sharc_postmul_rx(c_, s_); }
static inline void sharc_ang_z(int32_t a) { float s_, c_; sharc_sincos(a, &s_, &c_); sharc_postmul_rz(c_, s_); }

/* Fn_calc_unit_2_fast (0x6B, PM 0x2126B) — the two-bone IK, register for
 * register. 17 args:
 *   [0..2]   offset: Fn_trans on the current matrix
 *   [3..8]   six turns, z y x y x z (a zero angle skipped, as _L201AA.. do)
 *   [9..11]  target, world
 *   [12]     lower bone length (forearm, shin)   [13] upper (upper arm, thigh)
 *   [14]     TGP address the LOWER bone is stored to   [15] the UPPER's
 *   [16]     bend: 0 = elbow below
 * It works on the CURRENT matrix and leaves it turned, as the firmware does —
 * the game leans on that: it pops after each limb but pushes only before the
 * first, so the next limb starts from what this one left. The chain:
 *   aim: z turn by clip(d0, -d1)/|d_xy|, y turn by clip(|d_xy|, d2)/|d|
 *   shoulder: cos = ((d^2 + a12^2) - a13^2) / ((d a12) 2), sin = sqrt(1 - cos^2)
 *     negated when [16] == 0; z turn; the slot (12 words, T the pivot) -> [14]
 *   elbow: cos = ((a12^2 + a13^2) - d^2) / ((a12 a13) 2), z turn by (-cos, sin)
 *     with sin negated when [16] != 0; the slot -> [15]
 *   out of reach ((a12 + a13) <= |d|, compared as raw bits): the aimed slot to both.
 * Bufferram gets exactly those words (the i960 reads them back: Fn_mul_mot_yrot
 * loads the rotation). Both slots keep T at the pivot; the lower bone is drawn
 * from the elbow, T + a13 * the upper bone's x axis, which grade-pose adds. */
static inline float sharc_clip1(float x) {
    uint32_t b = sharc_float_to_bits(x);
    if (!(b & 0x7F800000u)) return sharc_bits_to_float(b & 0x80000000u);
    return x < -1.0f ? -1.0f : x > 1.0f ? 1.0f : x;
}
static inline void sharc_ik_store(uint32_t tgp_addr) {
    float words[12];
    for (int k = 0; k < 9; k++) words[k] = g_sharc.rot[k / 3][k % 3];
    for (int k = 0; k < 3; k++) words[9 + k] = g_sharc.pos[k];
    if (1) {
        uint32_t bo = tgp_addr * 4u;
        if (DMEXT_SIZE >= 48u && bo <= DMEXT_SIZE - 48u)   /* bo + 48 can wrap */
            for (int k = 0; k < 12; k++) { uint32_t u = sharc_float_to_bits(words[k]); memcpy(DMEXT + bo + 4u * (uint32_t)k, &u, 4); }
    }
}
static void sharc_calc_unit_2_fast(const uint32_t *args) {
    float (*r)[3] = g_sharc.rot;
    float *T = g_sharc.pos;
    float vx = sharc_bits_to_float(args[0]), vy = sharc_bits_to_float(args[1]), vz = sharc_bits_to_float(args[2]);
    for (int w = 0; w < 3; w++) T[w] = T[w] + vx * r[0][w];               /* _L20182 */
    for (int w = 0; w < 3; w++) T[w] = T[w] + vy * r[1][w];
    for (int w = 0; w < 3; w++) T[w] = T[w] + vz * r[2][w];
    static const int axes[6] = { 2, 1, 0, 1, 0, 2 };
    for (int i = 0; i < 6; i++) {
        int32_t a = (int32_t)args[3 + i];
        if (a == 0) continue;
        if (axes[i] == 0)      sharc_ang_x(a);
        else if (axes[i] == 1) sharc_ang_y(a);
        else                   sharc_ang_z(a);
    }
    float a12 = sharc_bits_to_float(args[12]), a13 = sharc_bits_to_float(args[13]);
    float dx = sharc_bits_to_float(args[9])  - T[0];
    float dy = sharc_bits_to_float(args[10]) - T[1];
    float dz = sharc_bits_to_float(args[11]) - T[2];
    float d0 = dx * r[0][0]; d0 = d0 + dy * r[0][1]; d0 = d0 + dz * r[0][2];
    float d1 = r[1][1] * dy; d1 = d1 + r[1][0] * dx; d1 = d1 + r[1][2] * dz; d1 = -d1;
    float d2 = r[2][0] * dx; d2 = d2 + r[2][1] * dy; d2 = d2 + r[2][2] * dz;
    float dxy2 = d0 * d0 + d1 * d1;
    float dtot2 = dxy2 + d2 * d2;
    float dtot = sharc_fw_sqrt(dtot2);
    float inv_dxy = sharc_fw_rsqrt(dxy2);
    float dxy = dxy2 * inv_dxy;
    float inv_dtot = sharc_fw_div(1.0f, dtot);
    float c = sharc_clip1(inv_dxy * d0), sn = sharc_clip1(inv_dxy * d1);
    sharc_postmul_rz(c, sn);                                                /* _L201D7 */
    c = sharc_clip1(inv_dtot * dxy); sn = sharc_clip1(inv_dtot * d2);
    sharc_postmul_ry(c, sn);                                                /* _L201C2 */
    uint32_t slot_lo = args[14], slot_up = args[15];
    if ((int32_t)sharc_float_to_bits(a12 + a13) <= (int32_t)sharc_float_to_bits(dtot)) {   /* _L21342 */
        sharc_ik_store(slot_lo);
        sharc_ik_store(slot_up);
    } else {
        float a12s = a12 * a12, dts = dtot * dtot, a13s = a13 * a13;
        float num = dts + a12s; num = num - a13s;
        float den = dtot * a12; den = den * 2.0f;
        float cs = sharc_fw_div(num, den);
        float ss = sharc_fw_sqrt(1.0f - cs * cs);
        if (args[16] == 0) ss = -ss;
        sharc_postmul_rz(sharc_clip1(cs), sharc_clip1(ss));
        float lower[3][3]; memcpy(lower, g_sharc.rot, sizeof lower);
        num = a12s + a13s; num = num - dts;
        den = a12 * a13; den = den * 2.0f;
        float ce = sharc_fw_div(num, den);
        float nce = -ce;
        float se = sharc_fw_sqrt(1.0f - ce * ce);
        if (args[16] != 0) se = -se;
        sharc_postmul_rz(sharc_clip1(nce), sharc_clip1(se));
        /* The lower slot holds the frame between the two turns. */
        float upper[3][3];
        memcpy(upper, g_sharc.rot, sizeof upper);
        memcpy(g_sharc.rot, lower, sizeof lower);
        sharc_ik_store(slot_lo);
        memcpy(g_sharc.rot, upper, sizeof upper);
        sharc_ik_store(slot_up);
    }
}

/* _L2033F: asin(x) in radians -- atan2(x, sqrt(1 - x*x)), with +-1 answered
 * outright by 0x3FC90FD7 (a hair under pi/2). */
static inline float sharc_fw_asin_rad(float x) {
    float xx = x * x;
    if (1.0f == x)  return sharc_bits_to_float(0x3FC90FD7u);
    if (-1.0f == x) return sharc_bits_to_float(0xBFC90FD7u);
    return sharc_fw_atan2(x, sharc_fw_sqrt(1.0f - xx));
}

/* _L20332: asin(a) as an angle word -- atan2(a, sqrt(1 - a*a)), with +-1
 * answered outright as 0x4000 / 0xC000. No clamp: |a| > 1 goes to the sqrt. */
static inline uint32_t sharc_fw_asin_word(float a) {
    if (a == 1.0f)  return 0x4000u;
    if (a == -1.0f) return 0xC000u;
    return sharc_fw_atan2_word(a, sharc_fw_sqrt(1.0f - a * a));
}

/* Fn_get_sm_ang_f / Fn_get_sm_ang_r (0x54 / 0x55): the current matrix becomes
 * identity (T too) turned by nine angles in the op's axis order (0 x, 1 y, 2 z;
 * a zero angle is skipped), and _L2117C reads three angles back:
 *   a0 = atan2(col2.x, col2.z), a1 = asin(col2.y), a2 = atan2(col0.y, col1.y)
 * and each also turned half a revolution the other way. The firmware keeps
 * whichever set is smaller -- but by the time it sums them, the register that
 * held |a0 turned| has been loaded with -1.0, so the turned set is judged by
 * -1 + |a1'| + |a2'|. That is the board; the motion blend reads it. */
static void sharc_get_sm_ang(const uint32_t *args, const int axes[9]) {
    sharc_rot_identity();
    g_sharc.pos[0] = g_sharc.pos[1] = g_sharc.pos[2] = 0.0f;
    for (int i = 0; i < 9; i++) {
        int32_t a = (int32_t)args[i];
        if (a == 0) continue;
        if (axes[i] == 0)      sharc_ang_x(a);
        else if (axes[i] == 1) sharc_ang_y(a);
        else                   sharc_ang_z(a);
    }
    float (*r)[3] = g_sharc.rot;
    const float PI = sharc_bits_to_float(0x40490FD7u);
    float a0 = sharc_fw_atan2(r[2][0], r[2][2]);
    float a1 = sharc_fw_asin_rad(r[2][1]);
    float a2 = sharc_fw_atan2(r[0][1], r[1][1]);
    float b0 = 0.0f > a0 ? a0 + PI : a0 - PI;
    float n1 = a1 + PI; n1 = n1 * -1.0f;
    float b1 = 0.0f > a1 ? n1 : PI - a1;
    float b2 = 0.0f > a2 ? a2 + PI : a2 - PI;
    float turned = -1.0f + fabsf(b1); turned = turned + fabsf(b2);
    float plain  = fabsf(a0) + fabsf(a1); plain = plain + fabsf(a2);
    if (!(turned <= plain)) { b0 = a0; b1 = a1; b2 = a2; }
    sharc_push_u(sharc_angle_word(b0));
    sharc_push_u(sharc_angle_word(b1));
    sharc_push_u(sharc_angle_word(b2));
}
static inline void sharc_scale3(float x, float y, float z) {
    for (int w = 0; w < 3; w++) { g_sharc.rot[0][w] *= x; g_sharc.rot[1][w] *= y; g_sharc.rot[2][w] *= z; }
}

/* Fn_kage_mat (0x73, PM 0x20D1F): the three shadow projections kage_poly
 * composes a part with, into inner slots 1, 2 and 0. The light comes down at
 * elevation a0 from azimuth a1; each projection turns into the light's frame
 * (Ry(a1 + 0x8000)), shears or flattens there, and turns back:
 *   inner[1] = cur * Ry * S(1, 1, 1/sin a0) * Ry^-1
 *   inner[2] = cur * Ry * T(0, floor, -floor * cot a0) * S(1, 1, 1/sin a0) * Ry^-1
 *   inner[0] = I * Ry * S(1, 0, 1) * Rx(a0 - 0x4000) * Ry^-1
 * where floor is the stage height Fn_area_coli last took (DM 0x30800). */
static inline void sharc_kage_mat(uint32_t a0, uint32_t a1) {
    float s_, c_;
    sharc_sincos((int32_t)a0, &s_, &c_);
    float inv = 1.0f / s_;                         /* recips + Newton-Raphson */
    float cot = inv * c_;
    float floor_y = sharc_dm_getf(0x30800u);
    float back = -floor_y;
    back = back * cot;
    int32_t ry = (int32_t)(a1 ^ 0x8000u), ryi = -ry;
    sharc_push_stack();
    sharc_ang_y(ry); sharc_scale3(1.0f, 1.0f, inv); sharc_ang_y(ryi);
    sharc_store_inner(1);
    sharc_pop_stack();
    sharc_push_stack();
    sharc_ang_y(ry);
    for (int w = 0; w < 3; w++) g_sharc.pos[w] += g_sharc.rot[1][w] * floor_y + g_sharc.rot[2][w] * back;
    sharc_scale3(1.0f, 1.0f, inv); sharc_ang_y(ryi);
    sharc_store_inner(2);
    sharc_pop_stack();
    sharc_push_stack();
    sharc_rot_identity();
    g_sharc.pos[0] = g_sharc.pos[1] = g_sharc.pos[2] = 0.0f;
    sharc_ang_y(ry); sharc_scale3(1.0f, 0.0f, 1.0f); sharc_ang_x((int32_t)a0 - 0x4000); sharc_ang_y(ryi);
    sharc_store_inner(0);
    sharc_pop_stack();
}

/* Fn_kage_flag (0x75, PM 0x20CBF): which of a fighter's 16 shadow balls land
 * near (mask 1) or far (mask 2) once slid along the light onto the floor.
 * a0 player, a1 round (nonzero) or square test, a2/a3 the slide per unit of
 * height in x/z, a4 the near limit, a5 the far limit. */
static inline void sharc_kage_flag(uint32_t a0, uint32_t a1, float kx, float kz, float near_, float far_) {
    uint32_t table = a0 ? 0x1407E80u : 0x1403E80u, ptrs = a0 ? 0x307E0u : 0x306E0u;
    uint32_t m_near = 0, m_far = 0;
    for (uint32_t b = 0; b < 16u; b++) {
        uint32_t addr = table + 3u * sharc_dm_get(ptrs + b);
        float x = sharc_dm_getf(addr), y = sharc_dm_getf(addr + 1u), z = sharc_dm_getf(addr + 2u);
        float fx = kx * y; fx = fx + x;
        float fz = kz * y; fz = fz + z;
        float d;
        if (a1) {
            float r2 = fx * fx; r2 = r2 + fz * fz;
            d = 0.70710677f * sharc_fw_sqrt(r2);
        } else {
            float ax = fabsf(fx), az = fabsf(fz);
            d = ax < az ? az : ax;
        }
        if (!(sharc_float_to_bits(y) >> 31) && !(d > near_)) m_near |= 1u << b;
        else if (!(d < far_))                               m_far  |= 1u << b;
    }
    sharc_push_u(m_near);
    sharc_push_u(m_far);
}

/* ---- Fn_osage: the sway chains ------------------------------------------- */

/* "if lt" straight after a float operation: negative, and not zero or
 * underflowed (AN and not AZ). */
static inline bool sharc_flt_lt0(float f) {
    uint32_t b = sharc_float_to_bits(f);
    return (b >> 31) && (b & 0x7FFFFFFFu) >= 0x00800000u;
}

/* _L20173: a point through the 12-word matrix at DM m, T first and the
 * columns added on in turn */
static inline void sharc_osage_xform(uint32_t m, uint32_t src, float *o) {
    float x = sharc_dm_getf(src), y = sharc_dm_getf(src + 1), z = sharc_dm_getf(src + 2);
    for (uint32_t i = 0; i < 3; i++) {
        float s = sharc_dm_getf(m + 9 + i);
        s = s + x * sharc_dm_getf(m + i);
        s = s + y * sharc_dm_getf(m + 3 + i);
        o[i] = s + z * sharc_dm_getf(m + 6 + i);
    }
}

/* _L20873 / _L2088A: out of the sphere at DM c (centre, radius, radius squared) */
static inline void sharc_osage_sphere(uint32_t c, float *x, float *y, float *z) {
    float cx = sharc_dm_getf(c), cy = sharc_dm_getf(c + 1), cz = sharc_dm_getf(c + 2);
    float ex = *x - cx, ey = *y - cy, ez = *z - cz;
    float s = ez * ez + ey * ey;
    s = s + ex * ex;
    if (sharc_dm_getf(c + 4) < s) return;
    float k = sharc_dm_getf(c + 3) * sharc_fw_rsqrt(s);
    *x = cx + ex * k; *y = cy + ey * k; *z = cz + ez * k;
}

/* Type 5, one segment (PM 0x207BD). rec is the record after its type word:
 * [0..2] the point it last reached, [3..5] its carry, [7] its length and
 * [8..10] the bias os_set_osage leaves. */
static inline void sharc_osage_segment(uint32_t rec) {
    const uint32_t P = 0x30362u;                     /* the point the chain has reached */
    sharc_dm_set(0x30341u, rec);
    for (uint32_t k = 0; k < 3; k++) sharc_dm_set(0x3036Bu + k, sharc_dm_get(rec + 8 + k));
    for (uint32_t k = 0; k < 3; k++) sharc_dm_set(0x30377u + k, sharc_dm_get(P + k));
    float a[3], b[3];
    sharc_osage_xform(0x3037Au, rec, a);
    sharc_osage_xform(0x30386u, rec + 3, b);
    for (uint32_t k = 0; k < 3; k++) { sharc_dm_setf(0x30365u + k, a[k]); sharc_dm_setf(0x30368u + k, b[k]); }
    float x = a[0] + b[0], y = a[1] + b[1], z = a[2] + b[2];
    x = x + sharc_dm_getf(0x3036Bu);
    y = y + sharc_dm_getf(0x3036Cu);
    z = z + sharc_dm_getf(0x3036Du);

    /* _L2084B: the plane at 0x30342 pushes the aim back onto it; past the
     * plane, unless 0x30360 is 1, the chain's limits (_L2085D) apply. */
    float n0 = sharc_dm_getf(0x30342u), n1 = sharc_dm_getf(0x30343u), n2 = sharc_dm_getf(0x30344u);
    float t = n0 * x + n1 * y;
    t = t + n2 * z;
    t = sharc_dm_getf(0x30345u) - t;
    if (!sharc_flt_lt0(t)) {
        x = x + n0 * t; y = y + n1 * t; z = z + n2 * t;
    } else if (sharc_dm_get(0x30360u) != 1u) {
        if (y < 0.0f) {
            /* _L208A2: a 2D plane for each quadrant below */
            uint32_t q = x < 0.0f ? (!(y <= sharc_dm_getf(0x30359u)) ? 0x30355u : 0x3035Du)
                                  : (!(y <= sharc_dm_getf(0x30358u)) ? 0x30352u : 0x3035Au);
            float m0 = sharc_dm_getf(q), m1 = sharc_dm_getf(q + 1);
            float u = m0 * x + m1 * y;
            u = sharc_dm_getf(q + 2) - u;
            if (!sharc_flt_lt0(u)) { x = x + m0 * u; y = y + m1 * u; }
        } else {
            if (!(y <= sharc_dm_getf(0x30350u))) {
                sharc_osage_sphere(0x30346u, &x, &y, &z);
            } else {
                float s = x * x + y * y;         /* the cylinder, radius 0x30350 */
                if (!(sharc_dm_getf(0x30351u) < s)) {
                    float k = sharc_dm_getf(0x30350u) * sharc_fw_rsqrt(s);
                    x = x * k; y = y * k;
                }
            }
            sharc_osage_sphere(0x3034Bu, &x, &y, &z);
        }
    }

    /* _L207F6: the segment's frame. Its Y is the unit direction from P to the
     * aim, its X that turned flat, and it hangs from P as it stood. */
    float px = sharc_dm_getf(P), py = sharc_dm_getf(P + 1), pz = sharc_dm_getf(P + 2);
    float vx = x - px, vy = y - py, vz = z - pz;
    float s = vx * vx + vy * vy;
    float inv = sharc_fw_rsqrt(s + vz * vz);
    float ux = vx * inv, uy = vy * inv, uz = vz * inv;
    sharc_dm_setf(0x30371u, ux); sharc_dm_setf(0x30372u, uy); sharc_dm_setf(0x30373u, uz);
    float h2 = 1.0f - uz * uz;
    float ih = sharc_fw_rsqrt(h2);
    sharc_dm_setf(0x30376u, h2 * ih);
    float w = ih * uy;
    sharc_dm_setf(0x3036Eu, w);
    w = w * uz; w = w * -1.0f;
    sharc_dm_setf(0x30375u, w);
    w = ih * ux; w = w * -1.0f;
    sharc_dm_setf(0x3036Fu, w);
    w = w * uz;
    sharc_dm_setf(0x30374u, w);
    sharc_dm_set(0x30370u, 0);

    /* The current matrix composed with it (_L201EA into PM scratch, not back
     * into the slot) goes to the i960: os_set_osage_after -> set_obj_fifo. */
    float B[12], out[12];
    for (uint32_t k = 0; k < 12; k++) B[k] = sharc_dm_getf(0x3036Eu + k);
    sharc_compose_words(B, out);
    for (int k = 0; k < 12; k++) sharc_push_f(out[k]);

    /* One length on, written back into the record for the next frame. */
    float len = sharc_dm_getf(rec + 7);
    float nx = ux * len, ny = uy * len, nz = uz * len;
    nx = nx + px; ny = ny + py; nz = nz + pz;
    float damp = sharc_dm_getf(0x30361u);
    sharc_dm_setf(rec,     nx);
    sharc_dm_setf(rec + 1, ny);
    sharc_dm_setf(rec + 2, nz);
    sharc_dm_setf(rec + 3, (nx - a[0]) * damp);
    sharc_dm_setf(rec + 4, (ny - a[1]) * damp);
    sharc_dm_setf(rec + 5, (nz - a[2]) * damp);
    sharc_dm_setf(P, nx); sharc_dm_setf(P + 1, ny); sharc_dm_setf(P + 2, nz);
}

/* 0x25004A4A Fn_osage (cpres1 PM 0x2076E): the sway chains ("osage": Honey's
 * pigtails, Fang's tail, Bean's feathers) as one command over a stream of
 * typed records in bufferram, which the argument names by word. Each type
 * word is echoed to the i960 as it is read — osage_copro reads one a record —
 * and then:
 *   0  end
 *   1  36 words: the current matrix, then two more (DM 0x3037A, 0x30386)
 *   2  30 words: the chain's limit planes and spheres (DM 0x30342..)
 *   3   2 words: DM 0x30360 (1 = no limits) and the carry factor 0x30361
 *   4   3 words: where the chain starts (DM 0x30362)
 *   5  11 words: a segment, which answers 12 more words (its draw matrix)
 * The i960 hands a segment's matrix straight to set_obj_fifo and reads the
 * point written back into the record (os_set_osage_after). With only the
 * type words answered, every segment took its matrix off an empty FIFO. */
static inline void sharc_osage(uint32_t arg) {
    uint32_t p = 0x1400000u + arg;
    for (int guard = 0; guard < 4096; guard++) {
        uint32_t type = sharc_dm_get(p), rec = p + 1;
        sharc_push_u(type);
        switch (type) {
            case 0: return;
            case 1:
                p += 0x25;
                for (int c = 0; c < 3; c++)
                    for (int r = 0; r < 3; r++) g_sharc.rot[c][r] = sharc_dm_getf(rec + (uint32_t)(c * 3 + r));
                for (int i = 0; i < 3; i++) g_sharc.pos[i] = sharc_dm_getf(rec + 9u + (uint32_t)i);
                for (uint32_t k = 0; k < 12; k++) sharc_dm_set(0x3037Au + k, sharc_dm_get(rec + 12 + k));
                for (uint32_t k = 0; k < 12; k++) sharc_dm_set(0x30386u + k, sharc_dm_get(rec + 24 + k));
                break;
            case 2: p += 0x1F; for (uint32_t k = 0; k < 30; k++) sharc_dm_set(0x30342u + k, sharc_dm_get(rec + k)); break;
            case 3: p += 3;    for (uint32_t k = 0; k < 2;  k++) sharc_dm_set(0x30360u + k, sharc_dm_get(rec + k)); break;
            case 4: p += 4;    for (uint32_t k = 0; k < 3;  k++) sharc_dm_set(0x30362u + k, sharc_dm_get(rec + k)); break;
            case 5: p += 0xC;  sharc_osage_segment(rec); break;
            default: return;   /* the firmware's jump table ends at 5 */
        }
    }
}

/* ---- Command executor ---------------------------------------------------- */

static inline void sharc_exec(uint32_t cmd, const uint32_t *args, int n) {

    g_sharc.reply_count = 0;
    g_sharc.reply_idx   = 0;

    switch (cmd) {
        /* ---- Setters ---- */
        case 0x06000C0C: {
            /* Fn_inv_matrix (PM 0x2041F, _L2023D): the current matrix inverted in
             * place -- cofactors, T' = -(T . cofactor columns), all scaled by
             * 1/det from the RECIPS divide. Float, in the firmware's order: it
             * used to be a double-precision adjugate, a ULP off wherever the
             * osage and point-transform paths read it back. No det == 0 check,
             * as on the board. */
            float C[12], S[12];
            for (int k = 0; k < 9; k++) C[k] = g_sharc.rot[k / 3][k % 3];
            for (int k = 0; k < 3; k++) C[9 + k] = g_sharc.pos[k];
            S[0] = C[4] * C[8] - C[5] * C[7];
            S[3] = C[5] * C[6] - C[3] * C[8];
            S[6] = C[3] * C[7] - C[4] * C[6];
            S[1] = C[7] * C[2] - C[8] * C[1];
            S[4] = C[8] * C[0] - C[6] * C[2];
            S[7] = C[6] * C[1] - C[7] * C[0];
            S[2] = C[1] * C[5] - C[2] * C[4];
            S[5] = C[2] * C[3] - C[0] * C[5];
            S[8] = C[0] * C[4] - C[1] * C[3];
            for (int r = 0; r < 3; r++) {
                float t = C[9] * S[r];
                t = t + C[10] * S[3 + r];
                t = t + C[11] * S[6 + r];
                S[9 + r] = -t;
            }
            float det = C[0] * S[0];
            det = det + C[1] * S[3];
            det = det + C[2] * S[6];
            float inv = sharc_fw_div(1.0f, det);
            for (int k = 0; k < 12; k++) S[k] = S[k] * inv;
            for (int k = 0; k < 9; k++) g_sharc.rot[k / 3][k % 3] = S[k];
            for (int k = 0; k < 3; k++) g_sharc.pos[k] = S[9 + k];
            return;
        }
        case 0x06800D0D:
            /* STF PM 0x02042A / FV PM 0x0203EF: `DM(I7, 0x09)` post-modify-by-9
             * advances I7 from slot[0] to slot[9]=T[0], then zeros slot[9..11].
             * Both firmwares share hex 0x00006E7E48000000 for this instruction —
             * the "+9" is in bits[31:27] of the lower instruction word.
             * CLAUDE.md had this wrong (said "zeros slot[1..3]"); zeros T[] is correct. */
            g_sharc.pos[0] = 0.0f;
            g_sharc.pos[1] = 0.0f;
            g_sharc.pos[2] = 0.0f;
            return;

        /* ---- Readback ---- */
        case 0x07800F0F:
            /* STF PM 0x02043D / FV PM 0x020402: same DM(I7, 0x09) post-modify-by-9
             * positions I7 at slot[9]=T[0]; LCNTR=3 loop outputs T[0..2].
             * Both firmwares are identical. CLAUDE.md had "slot[1..3]=rotation" wrong;
             * the STF annotation correctly named this read_world_pos.
             * The i960 stores T[] to g7+0x1F4 for collision/IK purposes. */
            sharc_push_f(g_sharc.pos[0]);
            sharc_push_f(g_sharc.pos[1]);
            sharc_push_f(g_sharc.pos[2]);
            return;

        /* 0x05800B0B: pre-multiply current bone by incoming 3×4 matrix — PM 0x203E9.
         * Firmware reads 12 column-major floats from FIFO → PM, then calls _L201E9
         * (mat×mat multiply: incoming × current → current). Same formula as 0x1B803737
         * but the left-factor B comes from the FIFO args rather than the bone cache.
         * Called by rob_kage_disp_test (shadow rendering) to apply a shadow projection
         * matrix to the current character bone. */
        case 0x05800B0B: {
            if (n >= 12) {                 /* 12 column-major floats, then _L201E9 */
                float B[12];
                for (int k = 0; k < 12; k++) B[k] = sharc_bits_to_float(args[k]);
                sharc_compose(B);
            }
            return;
        }
        case 0x02000404: {
            /* Fn_load_matrix (cpres1 PM 0x203AA): the 12 words straight into the
             * slot, col0, col1, col2, T — what Fn_get_matrix (0x05) hands back.
             * It used to read them as a row-major, Z-negated render matrix, the
             * old 0x05's format. osage_dsp loads the camera with it, so every
             * sway chain was composed onto a sheared matrix. */
            if (n >= 12) {
                for (int c = 0; c < 3; c++)
                    for (int r = 0; r < 3; r++) g_sharc.rot[c][r] = sharc_bits_to_float(args[c*3 + r]);
                for (int r = 0; r < 3; r++) g_sharc.pos[r] = sharc_bits_to_float(args[9 + r]);
            }
            return;
        }

        /* ---- Math: 2-in / 1-out ---- */

        /* 0x19003232: Fn_fcurve_spl, the Hermite segment get_fcurve_value_f
         * hands over for every spline channel of a motion.
         * Args: (span, t, v0, v1, m0, m1) — the key interval, the time into it,
         * the two key values, the earlier key's out-tangent and the later key's
         * in-tangent (i960 0x30CD0: key record +8 and +4).
         *
         * Both tangents count. The firmware (cpres1 PM 0x210E9) reads all six
         * and sums the two tangents into the cubic, each scaled by span / 30
         * (0x3D08882F) because the curves are authored at 30 fps and store rates
         * per second. This used to drop m1 — "zero in-tangent at v1", 55 of 62
         * against MAME — which leaves any segment ending on a key with a slope
         * slightly wrong: STF's stance motion 278 came out 2-3 binary radians
         * off the board on frames 30-38, and a steep turn (motion 265) thousands
         * off. Evaluated in float, in the firmware's operation order below: a
         * double Hermite agrees with the board's capture to the binary radian
         * (tools/grade-motion.mjs) but not to the bit, and the i960 feeds the
         * value back into the next frame. */
        case 0x19003232:
            if (n >= 6) {
                float span = sharc_bits_to_float(args[0]);
                float t    = sharc_bits_to_float(args[1]);
                float v0   = sharc_bits_to_float(args[2]);
                float v1   = sharc_bits_to_float(args[3]);
                float m0   = sharc_bits_to_float(args[4]);
                float m1   = sharc_bits_to_float(args[5]);
                /* The firmware's own order: values divided by span/30 (one
                 * RECIPS divide), the cubic in Horner form, scaled back. It has no
                 * test on span: a negative one runs the same cubic (the stftest run). */
                const float k30 = sharc_bits_to_float(0x3D08882Fu);
                float s30 = span * k30;
                float inv = sharc_fw_div(1.0f, s30);
                float u   = t * k30; u = u * inv;
                float a   = v0 * inv, b = v1 * inv;
                float d   = a - b, d2 = d + d;
                float mm  = m0 + m1;
                float r   = mm + d2;
                r = r * u; r = r - d2; r = r - d; r = r - mm; r = r - m0;
                r = r * u; r = r + m0;
                r = r * u; r = r + a;
                r = r * s30;
                sharc_push_f(r);
            }
            return;



        case 0x0B001616: case 0x16802D2D: case 0x13802727:
            if (n >= 2) {
                float a = sharc_bits_to_float(args[0]);
                float b = sharc_bits_to_float(args[1]);
                if (cmd == 0x13802727) {                /* Fn_atan: _L202CA */
                    sharc_push_u(sharc_fw_atan2_word(b, a));
                    return;
                }
                float r;
                switch (cmd) {
                    case 0x09801313: r = a + b; break;
                    case 0x0A001414: r = a - b; break;
                    case 0x0A801515: r = a * b; break;
                    case 0x0B001616: r = sharc_fw_div(a, b); break;             /* _L205D0 */
                    default:         r = sharc_fw_sqrt(a*a + b*b); break;      /* 0x16802D2D: _L20352 */
                }
                sharc_push_f(r);
            }
            return;

        /* ---- Math: 1-in / 1-out ---- */

        case 0x1A003434:
            /* Fn_mov_matrix (PM 0x2049E): the current matrix, as the slot holds it
             * (col0, col1, col2, T), into the GEO display list at byte offset a0 —
             * the matrix command set_obj_tpd opens and then follows with its own
             * object command. The eyes are drawn this way (their texture points are
             * the gaze), so without it they took whatever an earlier list left there. */
            if (n >= 1 ) {
                uint32_t mask = DMEXT_SIZE / 4 - 1;
                uint32_t base = args[0] >> 2;
                for (int c = 0; c < 3; c++)
                    for (int r_ = 0; r_ < 3; r_++) {
                        uint32_t w = sharc_float_to_bits(g_sharc.rot[c][r_]);
                        memcpy(DMEXT + ((base + (uint32_t)(c*3 + r_)) & mask) * 4u, &w, 4);
                    }
                for (int r_ = 0; r_ < 3; r_++) {
                    uint32_t w = sharc_float_to_bits(g_sharc.pos[r_]);
                    memcpy(DMEXT + ((base + 9u + (uint32_t)r_) & mask) * 4u, &w, 4);
                }
            }
            sharc_push_u(0);
            return;

        /* ---- Spatial: 4-in / 1-out ---- */

        /* ---- Vector transform: vec3 × matrix ---- */
        case 0x14802929:
        case 0x35006A6A:
            if (n >= 3) {
                float ix = sharc_bits_to_float(args[0]);
                float iy = sharc_bits_to_float(args[1]);
                float iz = sharc_bits_to_float(args[2]);
                float ox, oy, oz;
                float (*r)[3] = g_sharc.rot;
                if (cmd == 0x14802929) {
                    /* Fn_point_trans (_L20173): rot * v + T, accumulated onto T
                     * one column at a time -- ((T + x c0) + y c1) + z c2. */
                    float o[3];
                    for (int w = 0; w < 3; w++) {
                        float v = g_sharc.pos[w] + ix * r[0][w];
                        v = v + iy * r[1][w];
                        o[w] = v + iz * r[2][w];
                    }
                    ox = o[0]; oy = o[1]; oz = o[2];
                } else {
                    /* 0x35006A6A world->model = R^T*(v-T), the inverse of
                     * 0x14802929: contracts by rot COLUMN, no z-negation. Used by
                     * snc_eye_thd_set (head/eye look-at): camera -> head-bone local
                     * frame -> atan2 look angles. */
                    float rx = ix - g_sharc.pos[0];
                    float ry = iy - g_sharc.pos[1];
                    float rz = iz - g_sharc.pos[2];
                    ox = r[0][0]*rx + r[0][1]*ry + r[0][2]*rz;
                    oy = r[1][0]*rx + r[1][1]*ry + r[1][2]*rz;
                    oz = r[2][0]*rx + r[2][1]*ry + r[2][2]*rz;
                }
                sharc_push_f(ox); sharc_push_f(oy); sharc_push_f(oz);
            }
            return;

        /* ---- Afterimage (zanzou) — sharc_zanzou.h ---- */

        /* 0x42808585 Fn_zanzou_get_info (PM 0x208E6): one ring slot, 5 words.
         * zanzou_disp reads all 128 slots a frame and draws the ones still
         * alive, so five zeros meant no afterimage was ever drawn. */
        case 0x42808585: {
            uint32_t info[5] = { 0, 0, 0, 0, 0 };
            if (n >= 1) sharc_zanzou_get_info(args[0], info);
            for (int k = 0; k < 5; k++) sharc_push_u(info[k]);
            return;
        }

        /* 0x42008484 Fn_zanzou_mul_matrix_inner (PM 0x20926): current matrix
         * composed with the slot's own, through _L201EA. */
        case 0x42008484:
            if (n >= 1) {
                float B[12];
                uint32_t m = ZZ_RING + (args[0] & 0x7Fu) * ZZ_SLOT_WORDS + ZZ_MAT;
                for (int k = 0; k < 12; k++) B[k] = sharc_dm_getf(m + (uint32_t)k);
                sharc_compose(B);
            }
            return;

        /* 0x41808383 Fn_zanzou_load_matrix_inner (PM 0x20937): the slot's 12
         * words straight into the current matrix, col0 col1 col2 T. */
        case 0x41808383:
            if (n >= 1) {
                uint32_t m = ZZ_RING + (args[0] & 0x7Fu) * ZZ_SLOT_WORDS + ZZ_MAT;
                for (int col = 0; col < 3; col++)
                    for (int row = 0; row < 3; row++)
                        g_sharc.rot[col][row] = sharc_dm_getf(m + (uint32_t)(col * 3 + row));
                for (int k = 0; k < 3; k++) g_sharc.pos[k] = sharc_dm_getf(m + 9u + (uint32_t)k);
            }
            return;

        /* 0x43808787 Fn_zanzou_get_matrix_inner (PM 0x20946): the mirror. */
        case 0x43808787:
            if (n >= 1) {
                uint32_t m = ZZ_RING + (args[0] & 0x7Fu) * ZZ_SLOT_WORDS + ZZ_MAT;
                for (int col = 0; col < 3; col++)
                    for (int row = 0; row < 3; row++)
                        sharc_dm_setf(m + (uint32_t)(col * 3 + row), g_sharc.rot[col][row]);
                for (int k = 0; k < 3; k++) sharc_dm_setf(m + 9u + (uint32_t)k, g_sharc.pos[k]);
            }
            return;

        /* 0x40008080 Fn_zanzou_reserve (PM 0x20961): variable length. cop.h
         * streams it word by word; this path is for a captured argument list
         * (tests/cop_replay), which holds the whole conversation at once. */
        case 0x40008080:
            sharc_zanzou_begin();
            for (int k = 0; k < n; k++)
                if (sharc_zanzou_feed(args[k])) break;
            return;

        /* ---- Bone matrix cache (SHARC DM[0x30420..0x305A0]) ---- */

        /* 0x1B003636: plain load — PM 0x0204C2.
         * Copies bone slot FROM rot_cache INTO current rot[]/pos[]. */
        case 0x1B003636: {
            if (n < 2) return;
            int player   = (args[0] & 0xFF) == 1 ? 1 : 0;
            int slot_idx = (int)args[1] / 12;
            if (!sharc_unit_slot_ok(slot_idx)) return;
            sharc_load_words(ROT_CACHE[player * 16 + slot_idx]);
            return;
        }

        /* 0x1B803737: load + C×B multiply — PM 0x0204D6.
         * Loads bone from rot_cache, computes result = current × bone (affine 3×4
         * product: rot[j][i] = Σ_k B[j*3+k]*r[k][i], pos[i] += Σ_k B[9+k]*r[k][i])
         * where B=bone (rot_cache entry), r=current rotation — current is the left factor.
         * Firmware PM 0x201E9 computes C×B; our indexing matches that convention. */
        case 0x1B803737: {                                  /* Fn_mul_unit_mat: _L201EA */
            if (n < 2) return;
            int player   = (args[0] & 0xFF) == 1 ? 1 : 0;
            int slot_idx = (int)args[1] / 12;
            if (!sharc_unit_slot_ok(slot_idx)) return;
            sharc_compose(ROT_CACHE[player * 16 + slot_idx]);
            return;
        }

        /* 0x1C803939: set_bone_vec — PM 0x020DC2.
         * Reads 3 float args (x,y,z) in local bone space, transforms to world space
         * using current rot[]/pos[] (raw, no Z-negation — mirrors SHARC subroutine
         * 0x020173 which uses slot[0..11] directly without col2 sign-flip).
         * arg4 = slot_off: DM word offset from the ball table 0x38 selected (= ball * 3).
         * The world position replaces the ball's entry, and the entry it replaces
         * moves 0x60 words on: last frame's position, for the swept tests
         * (Fn_coli_point_trans, sharc_coli.h). */
        case 0x1C803939: {
            if (n >= 4 ) {
                float ix = sharc_bits_to_float(args[0]);
                float iy = sharc_bits_to_float(args[1]);
                float iz = sharc_bits_to_float(args[2]);
                uint32_t slot_off = args[3];
                float (*r)[3] = g_sharc.rot;
                /* Raw transform: world = rot * local + pos (no Z-negation) */
                float ox = g_sharc.pos[0] + r[0][0]*ix + r[1][0]*iy + r[2][0]*iz;
                float oy = g_sharc.pos[1] + r[0][1]*ix + r[1][1]*iy + r[2][1]*iz;
                float oz = g_sharc.pos[2] + r[0][2]*ix + r[1][2]*iy + r[2][2]*iz;
                float w_[3] = { ox, oy, oz };
                sharc_coli_point_trans(w_, slot_off);
            }
            return;
        }

        /* 0x24804949: Fn_write_ram — PM 0x2058D. DM[a0] = a1: how the i960
         * uploads the collision radii, ball maps and pointer tables. */
        case 0x24804949:
            if (n >= 2) sharc_dm_set(args[0], args[1]);
            return;

        /* 0x40808181: COP internal — PM 0x0208E1. 0 args, no output. */
        case 0x40808181:
            sharc_zanzou_init();
            return;

        /* 0x43008686 Fn_zanzou_kill_timer_buffer — PM 0x020955. 1 arg, the
         * player; clears that fighter's 16 per-part age counters. */
        case 0x43008686:
            if (n >= 1) sharc_zanzou_kill_timers(args[0]);
            return;

        /* 0x22004444: Fn_load_matrix_inner (PM 0x2054C) — inner bank
         * PM[0x21F20 + 12n] -> current matrix. 0x43 is the store; this was a
         * second store, and 0x45 below the load, one op off all the way along. */
        case 0x22004444:
            if (n >= 1) sharc_load_inner((int)args[0]);
            return;

        /* 0x2A005454: smooth_int — STF PM 0x021121 / FV PM 0x020EC7.
         * Applies 9 ang ops (Z,Y,X,X,Y,Z,Y,Z,X) to identity, outputs col0 as 3 × int16 Q14
         * (1.0 = 0x4000) packed in lower 16 bits of 32-bit FIFO words.
         * i960 reads these with ldis (16-bit signed) and uses arithmetic right shifts to
         * compute bone correction deltas stored at 0x690(g7)+bone*12. */
        /* 0x2A805555: dispatch[0x55] — PM 0x02115F. 9 args (3 triples) in, 3 out.
         * The i960 sends 0x2A805555 + 3 stt triples then reads 3 results back (e.g.
         * the 12-iteration loop @0x2FB00). Treating it as 0-arg desynced the input
         * FIFO: every following data word (the triples) was misread as an opcode →
         * the 0xFFFF.../0.5f "unknown cmd" WARN spam → corrupted ALL downstream COP
         * state, including the stage/cage matrix (cage/pole drift cascade). */
        case 0x2A805555: {                                  /* Fn_get_sm_ang_r (PM 0x2115F) */
            static const int axes_r[9] = { 2, 0, 1, 0, 1, 2, 2, 1, 0 };
            if (n < 9) { sharc_push_u(0); sharc_push_u(0); sharc_push_u(0); return; }
            sharc_get_sm_ang(args, axes_r);
            return;
        }

        /* 0x08001010: Fn_base_3x3 (cpres1 PM 0x20460). 0 args, 0 outputs.
         * The current matrix's 3x3 becomes the identity and T is left alone, so
         * whatever is drawn next faces the screen from the position reached. The
         * Flying Carpet's flames, the Death Egg's Earth and kira_kira_disp's
         * sparkles open their draws with it. */
        case 0x08001010:
            sharc_rot_identity();
            g_sharc.ang[0] = g_sharc.ang[1] = g_sharc.ang[2] = 0;
            return;

        /* 0x34006868: Fn_ld_glb_mat (cpres1 PM 0x205A3), the mirror of 0x67.
         * I6 = DM[0x01400000 + arg0], I7 = DM[0x3033F] (the current slot),
         * LCNTR=12, DM(I7,1)=DM(I6,1): the twelve words go raw into the current
         * matrix, col0, col1, col2, T. name_char_kage_disp sends 0x3D00, the
         * kage matrix 0x67 stored; it used to be an empty handler, so that
         * shadow was drawn with whatever matrix happened to be current. */
        case 0x34006868:
            if (n >= 1 ) {
                uint32_t byte_off = args[0] * 4u;
                if (DMEXT_SIZE >= 12 * 4 && byte_off <= DMEXT_SIZE - 12 * 4) {
                    float w[12];
                    for (int k = 0; k < 12; k++) {
                        uint32_t u;
                        memcpy(&u, DMEXT + byte_off + (uint32_t)k * 4u, 4);
                        w[k] = sharc_bits_to_float(u);
                    }
                    for (int c = 0; c < 3; c++)
                        for (int r = 0; r < 3; r++) g_sharc.rot[c][r] = w[c*3 + r];
                    for (int r = 0; r < 3; r++) g_sharc.pos[r] = w[9 + r];
                }
            }
            return;

        /* 0x3B807777: Fn_parts_oidasi — dispatch[0x77] PM 0x020B1F (sharc_coli.h).
         * 4 args (x, y, z, radius); 9 replies. Projectiles (sub_8AE48) take their
         * hits from the unit masks; flying parts (epc_oidasi) the push-out. */
        case 0x3B807777:
            if (n >= 4) sharc_coli_parts_oidasi(sharc_bits_to_float(args[0]), sharc_bits_to_float(args[1]),
                                                sharc_bits_to_float(args[2]), args[3]);
            else { int _i; for (_i = 0; _i < 9; _i++) sharc_push_u(0); }
            return;

        /* 0x2D005A5A: dispatch[0x5A] — PM 0x0206F4. 2 args, 2 outputs.
         * IDA os_set_coli: args (col0.x, col0.y); 2 reads back into g4, g5. */
        case 0x2D005A5A: {
            /* Fn_regular_vector_2d (PM 0x206F4): (x, y) scaled by 1/|v| (0 for a zero vector). */
            float _x = n > 0 ? sharc_bits_to_float(args[0]) : 0.0f;
            float _y = n > 1 ? sharc_bits_to_float(args[1]) : 0.0f;
            float _m = _x*_x + _y*_y, _k = sharc_fw_rsqrt(_m);         /* _L2035C */
            sharc_push_f(_x * _k);
            sharc_push_f(_y * _k);
            return;
        }

        case 0x2A005454: {                                  /* Fn_get_sm_ang_f (PM 0x21121) */
            static const int axes_f[9] = { 0, 1, 2, 2, 1, 0, 1, 0, 2 };
            if (n < 9) { sharc_push_u(0); sharc_push_u(0); sharc_push_u(0); return; }
            sharc_get_sm_ang(args, axes_f);
            return;
        }

        /* The collision chain — sharc_coli.h has the firmware ports and how the
         * i960 drives them each frame. */
        case 0x1E803D3D: {                                  /* Fn_coli_trans_mat */
            float d0[3], d1[3];
            for (int c = 0; c < 3; c++) {
                d0[c] = n > c ? sharc_bits_to_float(args[c]) : 0.0f;
                d1[c] = n > 3 + c ? sharc_bits_to_float(args[3 + c]) : 0.0f;
            }
            sharc_coli_trans_mat(d0, d1);
            return;
        }
        case 0x1D003A3A:                                    /* Fn_area_table_gen */
            if (n >= 4) sharc_coli_area_table_gen(args[0], args[1], args[2], args[3]);
            return;
        case 0x1F003E3E:                                    /* Fn_coli_trans_xz */
            if (n >= 5)
                sharc_coli_trans_xz(sharc_bits_to_float(args[0]), sharc_bits_to_float(args[1]),
                                    sharc_bits_to_float(args[2]), sharc_bits_to_float(args[3]),
                                    sharc_bits_to_float(args[4]));
            return;
        case 0x1D803B3B:                                    /* Fn_calc_coli_flag: 4 replies */
            if (n >= 7) sharc_coli_calc_flag(args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
            return;
        case 0x39007272:                                    /* Fn_outside_ball: 1 reply */
            if (n >= 3) sharc_coli_outside_ball(args[0], sharc_bits_to_float(args[1]), sharc_bits_to_float(args[2]));
            return;
        case 0x38007070:                                    /* Fn_area_coli: 22 replies */
            if (n >= 5) sharc_coli_area_coli(args[0], args[1], args[2], args[3], args[4]);
            return;
        case 0x38807171:                                    /* Fn_ball_to_unit: 1 reply */
            if (n >= 2) sharc_coli_ball_to_unit(args[0], args[1]);
            return;

        case 0x24004848:                                    /* Fn_read_ram: DM[a0] */
            sharc_push_u(n >= 1 ? sharc_dm_get(args[0]) : 0);
            return;

        case 0x2B005656:
            /* Read col0 of current matrix (first 3 words in SHARC column-major layout).
             * In row-major terms: [m[0][0], m[1][0], m[2][0]]. */
            sharc_push_f(g_sharc.rot[0][0]);
            sharc_push_f(g_sharc.rot[0][1]);
            sharc_push_f(g_sharc.rot[0][2]);
            return;

        /* 0x2D805B5B: dispatch[0x5B] — PM 0x020700. 3 args (angle, x, y) -> 2 floats. */
        case 0x2D805B5B: {
            /* Fn_rot_2d (PM 0x20700): (x, y) turned by a 16-bit angle --
             * (x cos - y sin, x sin + y cos), sin/cos from the ROM table. */
            float s_ = 0.0f, c_ = 1.0f;
            if (n > 0) sharc_sincos((int32_t)args[0], &s_, &c_);
            float _x = n > 1 ? sharc_bits_to_float(args[1]) : 0.0f;
            float _y = n > 2 ? sharc_bits_to_float(args[2]) : 0.0f;
            sharc_push_f(c_*_x - s_*_y);
            sharc_push_f(s_*_x + c_*_y);
            return;
        }
        /* 0x2C805959: dot2D — PM 0x02068C.
         * 4 args (a,b,c,d) -> 1 float: a*b + c*d.
         * cpres1 PM 0x02068C: f8=f0*f4+f1*f5 where (f0,f1)=(arg0,arg2), (f4,f5)=(arg1,arg3). */

        /* 0x3F807F7F: flush_bone_sel — STF PM 0x020DB3.
         * Copies 192 words from selection buffer to output buffer. No FIFO output.
         * NOTE: FV firmware maps index 0x7F to the error/hang handler — this command
         * is STF-specific and must not be sent by FV game code. */
        case 0x3F807F7F:
            if (n >= 1) sharc_coli_copy_unit_matrix(args[0]);
            return;

        /* 0x1C003838: select_bone_buf — PM 0x020DA8.
         * Writes base pointer to DM[0x3033E]: 0x01403E80 (arg≠1) or 0x01407E80 (arg=1).
         * P1 buf → BUFF_RAM+0x00FA00, P2 buf → BUFF_RAM+0x01FA00.
         * Selects active player's bone animation data buffer. No FIFO output. */
        case 0x1C003838:
            if (n >= 1) sharc_coli_set_ball_adrs(args[0]);
            return;

        case 0x09001212:
            /* Output 9 raw slot values in column-major order, no z-negation.
             * Firmware (PM 0x02045B, fvipers): LCNTR=9; R0=DM(I7,M1); DM(M0,I1)=R0.
             * Slot layout: slot[col*3+row] = rot[col][row].
             * Output order: rot[0][0..2], rot[1][0..2], rot[2][0..2]. */
            for (int _col = 0; _col < 3; _col++)
                for (int _row = 0; _row < 3; _row++)
                    sharc_push_f(g_sharc.rot[_col][_row]);
            return;

        /* 0x35806B6B: calc_rob_angle_cont — 2-bone IK chain transform.
         *
         * SHARC-verified algorithm (553-step single-step trace):
         *   args[0..2]  = skel_offset (a0,a1,a2): local bone offset in parent space
         *   args[3..8]  = 6 joint angles (i16 fixed-pt): Rz,Ry,Rx,Ry,Rx,Rz order
         *   args[9..11] = target pos (a9,a10,a11): world-space IK target
         *   args[12]    = a12: lower bone length — forearm or shin. Named by a
         *                 MAME capture of a real fight: stf-tools/motion-pose.csv
         *                 holds args[12] against the character record's forearm
         *                 slot and args[13] against its upper arm, and the two
         *                 differ (0.3932 against 0.3464 for a thigh and shin),
         *                 so the pairing is not a coin toss.
         *   args[13]    = a13: upper bone length — upper arm or thigh. This is
         *                 the bone that hangs at the pivot; a12 carries on from
         *                 the elbow it reaches.
         *   args[14]    = TGP word address for the LOWER bone's matrix
         *                 (left arm: 0x3A30, slot 4, the forearm)
         *   args[15]    = TGP word address for the UPPER bone's matrix
         *                 (left arm: 0x3A24, slot 3, the upper arm)
         *   args[16]    = flip flag: 0 = elbow below, non-zero = elbow above (negates sin_sh/sin_el) */
        case 0x35806B6B: {
            if (n < 17) { sharc_push_u(0); return; }
            sharc_calc_unit_2_fast(args);
            sharc_push_u(0);
            return;
        }

        /* 0x31806363: get_frame_dat — 7 args [A,B,C,D,E,F,G], 3 results: the
         * relative position in a rotated frame, cpres1 PM 0x21209:
         * out = [(E-A)cosD + (G-C)sinD, F-B, (G-C)cosD - (E-A)sinD], D a 16-bit
         * angle. An earlier MAME reading of zeros here was the i960 side of the
         * FIFO, where a read the COP has not answered yet stalls and shows as 0;
         * the SHARC's own side gives the formula, e.g. args
         * (-3,-1.2,3, 0x9000, 10,1,0) -> (-10.8624, 2.2, 7.74652). */
        case 0x31806363:
            if (n >= 7) {
                float A = sharc_bits_to_float(args[0]), B = sharc_bits_to_float(args[1]);
                float C = sharc_bits_to_float(args[2]);
                float E = sharc_bits_to_float(args[4]), F = sharc_bits_to_float(args[5]);
                float G = sharc_bits_to_float(args[6]);
                float s_, c_; sharc_sincos((int32_t)args[3], &s_, &c_);
                /* PM 0x21209 expands the rotation term by term, in this order */
                float x = G * s_; x = x + E * c_; x = x - C * s_; x = x - A * c_;
                float z = A * s_; z = z + G * c_; z = z - E * s_; z = z - C * c_;
                sharc_push_f(x);
                sharc_push_f(F - B);
                sharc_push_f(z);
            } else {
                sharc_push_u(0); sharc_push_u(0); sharc_push_u(0);
            }
            return;

        /* 0x08801111: load 3×3 rotation into current slot — PM 0x020443.
         * Firmware: LCNTR=9; R0=DM(M0,I0); DM(I7,M1)=R0 (I7=DM[0x3033F]).
         * Slot layout is col-major: slot[col*3+row] = rot[col][row].
         * Sets the base rotation; subsequent ang commands post-multiply from here. */
        case 0x08801111:
            if (n >= 9) {
                for (int _col = 0; _col < 3; _col++)
                    for (int _row = 0; _row < 3; _row++)
                        g_sharc.rot[_col][_row] = sharc_bits_to_float(args[_col*3 + _row]);
            }
            return;

        /* 0x41008282 Fn_zanzou_inc — PM 0x020911. Ages every ring slot and
         * answers how many are still alive (the i960 keeps it in zanzou_num). */
        case 0x41008282:
            sharc_push_u(sharc_zanzou_inc());
            return;

        case 0x18003030:
            /* Not a rotate: dispatch 0x30 is Fn_regular_vector (PM 0x206E3), the
             * 3D normalise -- (x, y, z) * 1/|v|, via _L20356. The rotate is 0x5B. */
            if (n >= 3) {
                float _x = sharc_bits_to_float(args[0]);
                float _y = sharc_bits_to_float(args[1]);
                float _z = sharc_bits_to_float(args[2]);
                float _m = _x*_x + _y*_y + _z*_z, _k = sharc_fw_rsqrt(_m);  /* _L20356 */
                sharc_push_f(_x * _k);
                sharc_push_f(_y * _k);
                sharc_push_f(_z * _k);
            } else {
                sharc_push_u(0); sharc_push_u(0); sharc_push_u(0);
            }
            return;

        /* 0x13002626: asin(a) -> i16_angle  (dispatch index 0x26, PM 0x20636)
         * Firmware: _L20332 calls sqrt(1-f1^2) + atan2(f1,sqrt) -> i16.
         * i960 passes a float, reads back 16-bit angle (via ldis).
         * IDA-confirmed: 9 call sites, result compared against angle thresholds.
         * Fn_asin hands r1 straight to _L20332, which does not clamp: |a| > 1
         * goes to the square root as it is. */
        case 0x13002626:
            if (n >= 1)
                sharc_push_u(sharc_fw_asin_word(sharc_bits_to_float(args[0])));   /* _L20332 */
            return;

        case 0x1F803F3F:
            /* set_ang_xyz: firmware PM 0x0203DB calls ang_z(arg0), ang_y(arg1), ang_x(arg2). */
            if (n >= 3) {
                g_sharc.ang[2] = (int32_t)args[0];  /* z angle */
                g_sharc.ang[1] = (int32_t)args[1];  /* y angle */
                g_sharc.ang[0] = (int32_t)args[2];  /* x angle */
                { float s_, c_; sharc_sincos(g_sharc.ang[2], &s_, &c_); sharc_postmul_rz(c_, s_); }
                { float s_, c_; sharc_sincos(g_sharc.ang[1], &s_, &c_); sharc_postmul_ry(c_, s_); }
                { float s_, c_; sharc_sincos(g_sharc.ang[0], &s_, &c_); sharc_postmul_rx(c_, s_); }
            }
            sharc_push_u(0);
            return;

        /* ---- Matrix stack: push / pop ---- */

        /* ---- Bone slot write/select commands — no FIFO output ---- */

        case 0x07000E0E:
            /* Fn_load_point (PM 0x20433): the three args ARE the translation --
             * `dm(i7,9)` post-modifies to T. calc_unit_mat opens every part with
             * load_point(0, +0x678, 0); ignoring it left whatever T the previous
             * matrix had, and STF's unit matrices came out tens of units away. */
            if (n >= 3) {
                g_sharc.pos[0] = sharc_bits_to_float(args[0]);
                g_sharc.pos[1] = sharc_bits_to_float(args[1]);
                g_sharc.pos[2] = sharc_bits_to_float(args[2]);
            }
            return;

        /* 0x1A803535: save current matrix to bone cache — PM 0x0204AE.
         * arg0 & 0xFF selects player: 1 → P2 base (slots 16..31), else P1 (0..15).
         * arg1 = bone_idx * 12 (word offset in the player's bone table).
         * Copies current rot[]/pos[] → rot_cache[player*16 + slot_idx]. */
        case 0x1A803535: {
            if (n < 2) return;
            int player   = (args[0] & 0xFF) == 1 ? 1 : 0;
            int slot_idx = (int)args[1] / 12;
            if (!sharc_unit_slot_ok(slot_idx)) return;
            float *dst = ROT_CACHE[player * 16 + slot_idx];
            float (*r)[3] = g_sharc.rot;
            for (int col = 0; col < 3; col++)
                for (int row = 0; row < 3; row++)
                    dst[col * 3 + row] = r[col][row];
            dst[9]  = g_sharc.pos[0];
            dst[10] = g_sharc.pos[1];
            dst[11] = g_sharc.pos[2];
            return;
        }

        /* 0x34806969: load_anim_frame — PM 0x021237.
         * arg0 = word offset into BUFF_RAM (written by 0x33806767 from IK solver).
         * arg1 = character facing angle (world-space Ry, pre-multiplied onto bone).
         * Loads 9 col-major floats as bone base rotation, then applies
         * Ry(arg1) as a world-space pre-multiply: rot = Ry(arg1) × bone_rot. */
        case 0x34806969: {
            if (n >= 1 ) {
                uint32_t byte_off = args[0] * 4;
                if (DMEXT_SIZE >= 9 * 4 && byte_off <= DMEXT_SIZE - 9 * 4) {
                    for (int _col = 0; _col < 3; _col++)
                        for (int _row = 0; _row < 3; _row++) {
                            uint32_t u;
                            memcpy(&u, DMEXT + byte_off + (_col*3+_row)*4, 4);
                            g_sharc.rot[_col][_row] = sharc_bits_to_float(u);
                        }
                    if (n >= 2) {
                        float s_, c_; sharc_sincos((int32_t)args[1], &s_, &c_);
                        sharc_premul_ry(c_, s_);
                    }
                }
            }
            return;
        }

        /* 0x31006262: set_body_matrix — PM 0x0211E1.
         * Performs an INTERNAL PUSH, then sets the new bone slot's T = args[0..2]
         * (world position) and builds a 3×3 rotation matrix from args[3..5]
         * (body Euler angles from g7+0x140..0x144), then post-multiplies
         * ang_y(args[6]), ang_x(args[7]), ang_z(args[8]) (world angles from g7+0xC00..0xC04).
         *
         * Firmware derivation (PM 0x0211E1):
         *   CALL(0x020375)                   ← internal push
         *   I7 = DM[0x3033F]
         *   DM[I7+9..11] = args[0..2]        ← world T
         *   cos3,sin3 = sincos(args[3])
         *   cos4,sin4 = sincos(args[4])
         *   cos5,sin5 = sincos(args[5])
         *   PM 0x0211C0: build rot[][]:
         *     rot[0] = [c4*c3, -c4*s3, s4]
         *     rot[1] = [s3*c5+c3*s4*s5, c3*c5-s3*s4*s5, -c4*s5]
         *     rot[2] = [s3*s5-c3*s4*c5, c3*s5+s3*s4*c5,  c4*c5]
         *   ang_y(args[6]); ang_x(args[7]); ang_z(args[8])
         */
        case 0x31006262: {
            if (n < 9) return;
            sharc_push_stack();   /* internal push */
            /* set world translation */
            g_sharc.pos[0] = sharc_bits_to_float(args[0]);
            g_sharc.pos[1] = sharc_bits_to_float(args[1]);
            g_sharc.pos[2] = sharc_bits_to_float(args[2]);
            /* build rotation matrix from body angles (args[3..5]) via PM 0x0211C0 */
            { float c3, s3, c4, s4, c5, s5;
              sharc_sincos((int32_t)args[3], &s3, &c3);
              sharc_sincos((int32_t)args[4], &s4, &c4);
              sharc_sincos((int32_t)args[5], &s5, &c5);
              float (*r)[3] = g_sharc.rot;
              r[0][0] =  c4*c3;              r[0][1] = -c4*s3;              r[0][2] =  s4;
              r[1][0] =  s3*c5 + c3*s4*s5;  r[1][1] =  c3*c5 - s3*s4*s5;  r[1][2] = -c4*s5;
              r[2][0] =  s3*s5 - c3*s4*c5;  r[2][1] =  c3*s5 + s3*s4*c5;  r[2][2] =  c4*c5;
            }
            /* post-multiply world angles: ang_y(args[6]), ang_x(args[7]), ang_z(args[8]) */
            { float s_, c_; sharc_sincos((int32_t)args[6], &s_, &c_); sharc_postmul_ry(c_, s_); }
            { float s_, c_; sharc_sincos((int32_t)args[7], &s_, &c_); sharc_postmul_rx(c_, s_); }
            { float s_, c_; sharc_sincos((int32_t)args[8], &s_, &c_); sharc_postmul_rz(c_, s_); }
            g_sharc.ang[0] = g_sharc.ang[1] = g_sharc.ang[2] = 0;
            return;
        }

        /* 0x21804343: save_bone_to_PM_scratch — PM 0x02053C. Same semantics as 0x22004444.
         * 1 arg: N. Copy current 12-word bone (rot+pos, col-major) to pm_bone[N]. */
        case 0x21804343:
            if (n >= 1) sharc_store_inner((int)args[0]);
            return;

        /* 0x22804545: Fn_mul_matrix_inner (PM 0x2055C, _L201EA) — the current
         * matrix composed with inner[n], the same multiply 0x37 does with a unit
         * matrix. */
        case 0x22804545:
            if (n >= 1) sharc_compose(sharc_inner_slot(args[0]));
            return;

        /* 0x39807373: COP internal — PM 0x020D1F. 2 args, no output. */
        case 0x39807373:                                    /* Fn_kage_mat */
            if (n >= 2) sharc_kage_mat(args[0], args[1]);
            return;
        case 0x3A807575:                                    /* Fn_kage_flag: 2 replies */
            if (n >= 6)
                sharc_kage_flag(args[0], args[1], sharc_bits_to_float(args[2]), sharc_bits_to_float(args[3]),
                                sharc_bits_to_float(args[4]), sharc_bits_to_float(args[5]));
            return;
        /* 0x3A007474: Fn_kage_poly (PM 0x20D0A) — a fighter part's shadow.
         * args: player, slot (as 0x37), flags, x, y, z, inner index.
         *   push; current composed with the unit matrix (0x37);
         *   flags bit 2: T += rot * (x, y, z);
         *   flags bit 1: keep the Z axis (_L20529), else bit 0: keep X (_L20516);
         *   current = current * inner[index] (0x46).
         * The matrix is left pushed: the i960 pops after the draw. As a no-op
         * that pop took one level too many, and every shadow's matrix was wrong. */
        case 0x3A007474:
            if (n >= 7) {
                sharc_push_stack();
                sharc_compose_unit(args[0], args[1]);
                uint32_t flags = args[2];
                if (flags & 4) {
                    float v[3] = { sharc_bits_to_float(args[3]), sharc_bits_to_float(args[4]), sharc_bits_to_float(args[5]) };
                    for (int r_ = 0; r_ < 3; r_++)
                        g_sharc.pos[r_] += g_sharc.rot[0][r_]*v[0] + g_sharc.rot[1][r_]*v[1] + g_sharc.rot[2][r_]*v[2];
                }
                if (flags & 2)      sharc_kage_leave_z();
                else if (flags & 1) sharc_kage_leave_x();
                sharc_compose_rev(sharc_inner_slot(args[6]));
            }
            return;

        /* 0x23004646: Fn_mul_matrix_inner_rev (PM 0x2056E -> _L2021F): the
         * current matrix transformed by inner[n] — current = inner[n] * current. */
        case 0x23004646:
            if (n >= 1) sharc_compose_rev(sharc_inner_slot(args[0]));
            return;

        /* 0x2B805757: read_col1 — PM 0x02044E.
         * Advances I7 by 3 (past col0) then reads slot[3..5] = rot[1][0..2] → 3 FIFO words. */
        case 0x2B805757:
            sharc_push_f(g_sharc.rot[1][0]);
            sharc_push_f(g_sharc.rot[1][1]);
            sharc_push_f(g_sharc.rot[1][2]);
            return;

        /* 0x2C005858: read_col2 — PM 0x020457.
         * Advances I7 by 6 (past col0+col1) then reads slot[6..8] = rot[2][0..2] → 3 FIFO words.
         * Must use rot[] directly — matrix[][2] has z-negation applied, which callers don't expect. */
        case 0x2C005858:
            sharc_push_f(g_sharc.rot[2][0]);
            sharc_push_f(g_sharc.rot[2][1]);
            sharc_push_f(g_sharc.rot[2][2]);
            return;

        /* 0x11802323: get_tan — PM 0x02061C.
         * 1 arg = i16 fixed-point angle.  Firmware: CALL sincos, CALL 0x0205D0 (RECIPS+NR divide),
         * result = sin/cos = tan(angle).  Returns 1 float. */

        case 0x25004A4A:            /* Fn_osage: the sway chains (sharc_osage, above) */
            if (n >= 1) sharc_osage(args[0]); else sharc_push_u(0);
            return;

        /* 0x33806767: store the current matrix into the TGP slot arg0 names.
         *
         * Firmware PM 0x020597: I7 = DM[0x3033F] (the current slot pointer),
         * I6 = DM[0x01400000 + arg0], LCNTR=12, DM(I6,1)=DM(I7,1) — twelve
         * words, so arg0 is a word offset, and 0x3A00 is P1 bone 0.
         *
         * This is how the four slots the IK never reaches get filled.
         * `calc_rob_angle_cont` places the waist, the chest, the head and the
         * pelvis by stacking translate / 0x3F / angle ops on the body matrix and
         * then handing the result to this op — the explorer's toolkit rebuilds
         * exactly those four that way (stf-tools/dl-rig.mjs), because the slots
         * cannot be read back out of either ADSP's data space.
         *
         * The addresses are the ones the IK writes too: 0x0C a slot from 0x3A00
         * for P1 and 0x3B00 for P2, in the SHARC's data space (bufferram).
         *
         * shadow_rot is the kage-matrix path, which comes through here with
         * arg0 = 0x3D00, the kage matrix, just past the two fighters' slots. */
        case 0x33806767: {
            for (int _c = 0; _c < 3; _c++)
                for (int _r = 0; _r < 3; _r++)
                    g_sharc.shadow_rot[_c][_r] = g_sharc.rot[_c][_r];
            if (n >= 1) {
                uint32_t addr = args[0];
                float slot[12];
                for (int _c = 0; _c < 3; _c++)
                    for (int _r = 0; _r < 3; _r++)
                        slot[_c*3+_r] = g_sharc.rot[_c][_r];
                slot[9]  = g_sharc.pos[0];
                slot[10] = g_sharc.pos[1];
                slot[11] = g_sharc.pos[2];

                if (1) {
                    uint32_t byte_off = addr * 4;
                    if (DMEXT_SIZE >= 12 * 4 && byte_off <= DMEXT_SIZE - 12 * 4) {
                        /* 9 col-major rotation words, then 3 translation words. */
                        for (int _k = 0; _k < 12; _k++) {
                            uint32_t u = sharc_float_to_bits(slot[_k]);
                            memcpy(DMEXT + byte_off + _k*4, &u, 4);
                        }
                    }
                }
            }
            return;
        }

        case 0x17002E2E:
            /* Fn_get_3d_len (PM 0x206CB): sqrt((x*x + y*y) + z*z) through _L2034C.
             * It was missing from the table, so its three floats were read as
             * commands and the i960 took an empty FIFO as the length. */
            if (n >= 3) {
                float x = sharc_bits_to_float(args[0]), y = sharc_bits_to_float(args[1]), z = sharc_bits_to_float(args[2]);
                float s2 = x * x + y * y;
                sharc_push_f(sharc_fw_sqrt(s2 + z * z));
            } else {
                sharc_push_u(0);
            }
            return;
        case 0x20804141:  /* Fn_kage_leave_x_axis (PM 0x20516), 0 args */
            sharc_kage_leave_x();
            return;
        case 0x21004242:  /* Fn_kage_leave_z_axis (PM 0x20529), 0 args */
            sharc_kage_leave_z();
            return;
        case 0x18803131: {
            /* FV PM 0x020E80: F8=b-a, F1=F8*t, CALL fdiv(F1,span)→F0, result=F0+a.
             * arg3 = span: the full interpolation range; formula = a + (b-a)*t/span.
             * IDA 0x2B6E0: st r3,r4,r5,r6 → ld r3. */
            float a, b, t, span;
            memcpy(&a,    &args[0], 4);
            memcpy(&b,    &args[1], 4);
            memcpy(&t,    &args[2], 4);
            memcpy(&span, &args[3], 4);
            float r = (b - a) * t;                          /* Fn_fcurve_lin: _L205D0, then + a */
            r = sharc_fw_div(r, span) + a;
            sharc_push_f(r);
            return;
        }
        case 0x28805151: {
            /* Fn_get_glo_ang (cpres1 PM 0x2114F): the current matrix's angles,
             * read as _L2117C reads them for Fn_get_sm_ang but with no
             * half-turn alternative. Each is an angle word (_L202CA / _L20332).
             * IDA 0x17CD4: ldis g0,g1,g2. */
            float (*r)[3] = g_sharc.rot;
            sharc_push_u(sharc_fw_atan2_word(r[2][0], r[2][2]));  /* col2.x, col2.z */
            sharc_push_u(sharc_fw_asin_word(r[2][1]));            /* col2.y */
            sharc_push_u(sharc_fw_atan2_word(r[0][1], r[1][1]));  /* col0.y, col1.y */
            return;
        }
    }
}

