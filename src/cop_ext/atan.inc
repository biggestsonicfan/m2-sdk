/* From m2-hle2 (2662dda2) src/board/sharc.h: cpres1's atan2 (sharc_fw_atan2). */
static inline float sharc_fw_atan2(float y, float x) {
    const float TAN15 = sharc_bits_to_float(0x3E8930A2u), SQRT3 = sharc_bits_to_float(0x3FDDB3D7u),
                EPS   = sharc_bits_to_float(0x39800000u),
                P0 = sharc_bits_to_float(0xBF3853ADu), P1 = sharc_bits_to_float(0xBFB854A7u),
                Q0 = sharc_bits_to_float(0x4098123Bu), Q1 = sharc_bits_to_float(0x408A3F7Du),
                HALF_PI = sharc_bits_to_float(0x3FC90FDAu), PI = sharc_bits_to_float(0x40490FDAu);
    const float OCTANT[4] = { 0.0f, sharc_bits_to_float(0x3F060A91u), HALF_PI, sharc_bits_to_float(0x3F860A91u) };
    uint32_t xb = sharc_float_to_bits(x), yb = sharc_float_to_bits(y);
    float f0 = y, f15;
    if (!(xb & 0x7F800000u)) {                               /* _L2032F: pass treats a denormal as zero */
        if (!(yb & 0x7F800000u)) return y;
        f15 = HALF_PI;
    } else {
        float f2 = (xb & 0x80000000u) ? PI : 0.0f;
        int32_t de = (int32_t)((uint32_t)sharc_logb(y) - (uint32_t)sharc_logb(x));
        if (de >= 0x7C) {
            f15 = HALF_PI;                                   /* _L2032C */
        } else {
            if (de <= -0x7C) {
                f15 = 0.0f;                                  /* _L20329 */
            } else {
                float f7;
                f0 = sharc_fw_div_ex(y, x, &f7);
                if (sharc_float_to_bits(f2) != 0) f0 = -f0;
                int k = 0;
                f15 = fabsf(f0);
                f7 = 1.0f;
                if (!(f15 <= f7)) { f15 = sharc_fw_div_ex(1.0f, f15, &f7); k = 2; }
                if (!(f15 < TAN15)) {                        /* _L202FD */
                    k++;
                    float f14 = SQRT3 * f15;
                    f7 = f14 - f7;
                    f15 = SQRT3 + f15;
                    f15 = sharc_fw_div_ex(f7, f15, &f7);
                }
                if (!(fabsf(f15) <= EPS)) {                  /* _L2030B */
                    float g = f15 * f15;
                    float num = g * P0; num = num + P1; num = num * g;
                    float den = g + Q0; den = den * g; den = den + Q1;
                    float r = sharc_fw_div(num, den);
                    r = r * f15;
                    f15 = r + f15;
                }
                if (k - 1 > 0) f15 = -f15;                   /* _L2031F */
                f15 = f15 + OCTANT[k];
            }
            if (sharc_float_to_bits(f2) != 0) f15 = f2 - f15; /* _L20324 */
        }
    }
    if (sharc_float_to_bits(f0) & 0x80000000u) f15 = -f15;   /* _L20326 */
    return f15;
}

/* An integral-valued float as the 32-bit integer `fix` produces. Out of range,
 * or NaN, is 0x80000000 on every host: a C cast leaves that undefined, x86 gives
 * 0x80000000 (as MAME's `compute_fix` does on x86) and AArch64 saturates. */
static inline uint32_t sharc_angle_word(float rad) {
    float scaled = rad * sharc_bits_to_float(0x4622F983u);
    return sharc_float_to_int32(floorf(scaled)) & 0xFFFFu;
}
static inline uint32_t sharc_fw_atan2_word(float y, float x) { return sharc_angle_word(sharc_fw_atan2(y, x)); }
