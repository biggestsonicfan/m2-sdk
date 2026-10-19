/* ---- cpres1 commands m2-hle2 has no case for (stf-sharc cpres1.asm) ----------
 * Returns the number of argument words, or -2 if cmd is not one of these. */
static int ext_args(uint32_t cmd) {
    switch ((cmd >> 23) & 0xFFu) {
        case 0x19: case 0x1B: case 0x1D: case 0x1E: case 0x1F: case 0x20: return 1;
        case 0x1C: case 0x76: return 0;
        case 0x28: return 3;
        case 0x2A: return 6;
        case 0x47: return 12;
        case 0x79: case 0x7B: return 5;
        case 0x7A: return 9;
        case 0x7C: case 0x7D: case 0x7E: return 2;
    }
    return -2;
}

#define EXT_C 0x30400u                  /* Fn_put_c .. Fn_div_c: the accumulator */

/* 0x7C/0x7D/0x7E: the 3x3 tables, DM 0x31000 (arg1 == 0) or 0x31800, + arg0 */
static uint32_t ext_tab(const uint32_t *args) { return (args[1] ? 0x31800u : 0x31000u) + args[0]; }

/* _L20208 (0x7A, 0x7E): each new column is rot * M's column; T stays */
static void ext_mul3(const float *M) {
    float B[12];
    for (int k = 0; k < 9; k++) B[k] = M[k];
    B[9] = B[10] = B[11] = 0.0f;
    float p[3];
    memcpy(p, g_sharc.pos, sizeof p);
    sharc_compose(B);
    memcpy(g_sharc.pos, p, sizeof p);
}

static void ext_exec(uint32_t cmd, const uint32_t *args) {
    float (*r)[3] = g_sharc.rot;
    switch ((cmd >> 23) & 0xFFu) {
        case 0x19:                      /* Fn_sqr_r: 1/sqrt(a) (_L2029B) */
            sharc_push_f(sharc_fw_rsqrt(sharc_bits_to_float(args[0])));
            return;
        case 0x1B: sharc_dm_set(EXT_C, args[0]); return;               /* Fn_put_c */
        case 0x1C: sharc_push_u(sharc_dm_get(EXT_C)); return;          /* Fn_get_c */
        case 0x1D: sharc_dm_setf(EXT_C, sharc_dm_getf(EXT_C) + sharc_bits_to_float(args[0])); return;
        case 0x1E: sharc_dm_setf(EXT_C, sharc_dm_getf(EXT_C) - sharc_bits_to_float(args[0])); return;
        case 0x1F: sharc_dm_setf(EXT_C, sharc_dm_getf(EXT_C) * sharc_bits_to_float(args[0])); return;
        case 0x20: sharc_dm_setf(EXT_C, sharc_fw_div(sharc_dm_getf(EXT_C), sharc_bits_to_float(args[0]))); return;
        case 0x28: {                    /* Fn_tri_shin: sides a, b, c (PM 0x20644) */
            float a = sharc_bits_to_float(args[0]), b = sharc_bits_to_float(args[1]);
            float c = sharc_bits_to_float(args[2]);
            float a2 = a * a, b2 = b * b, c2 = c * c;
            float cb = sharc_fw_div(c2 + (a2 - b2), (2.0f * a) * c);
            sharc_push_u(0x4000u - sharc_fw_asin_word(cb));            /* acos, an angle word */
            float d = sharc_fw_div((c2 + b2) - a2, 2.0f * b);
            sharc_push_f(d);
            sharc_push_f(sharc_fw_sqrt(c2 - d * d));
            return;
        }
        case 0x2A: {                    /* Fn_get_inner: a.x, b.x, a.y, b.y, a.z, b.z */
            float s = sharc_bits_to_float(args[0]) * sharc_bits_to_float(args[1]);
            s = s + sharc_bits_to_float(args[2]) * sharc_bits_to_float(args[3]);
            s = s + sharc_bits_to_float(args[4]) * sharc_bits_to_float(args[5]);
            sharc_push_f(s);
            return;
        }
        case 0x47: {                    /* Fn_mul_matrix_rev: current = M * current (_L2021E) */
            float M[12];
            for (int k = 0; k < 12; k++) M[k] = sharc_bits_to_float(args[k]);
            sharc_compose_rev(M);
            return;
        }
        case 0x76:                      /* Fn_get_glo_ang_zyx (PM 0x2113F) */
            sharc_push_u(sharc_fw_atan2_word(r[1][2], r[2][2]));       /* slot 5, slot 8 */
            sharc_push_u(sharc_fw_asin_word(r[0][2]));                 /* slot 2 */
            sharc_push_u(sharc_fw_atan2_word(r[0][1], r[0][0]));       /* slot 1, slot 0 */
            return;
        case 0x79: {                    /* Fn_ziku_rot: off, angle, axis -> table 0x31000 + off */
            uint32_t t = 0x31000u + args[0];
            float s, c;
            sharc_sincos((int32_t)args[1], &s, &c);
            float x = sharc_bits_to_float(args[2]), y = sharc_bits_to_float(args[3]);
            float z = sharc_bits_to_float(args[4]);
            float k = 1.0f - c, m;
            sharc_dm_setf(t + 0, x * x * k + c);
            sharc_dm_setf(t + 4, y * y * k + c);
            sharc_dm_setf(t + 8, z * z * k + c);
            m = k * (y * x);
            sharc_dm_setf(t + 1, m + s * z);
            sharc_dm_setf(t + 3, m - s * z);
            m = (z * x) * k;
            sharc_dm_setf(t + 2, m - s * y);
            sharc_dm_setf(t + 6, m + s * y);
            m = (z * y) * k;
            sharc_dm_setf(t + 5, m + s * x);
            sharc_dm_setf(t + 7, m - s * x);
            return;
        }
        case 0x7A: {                    /* Fn_mul_matrix3: 9 floats */
            float M[9];
            for (int k = 0; k < 9; k++) M[k] = sharc_bits_to_float(args[k]);
            ext_mul3(M);
            return;
        }
        case 0x7B: {                    /* Fn_scrn_clip: x, y, z, sx, sy (PM 0x20A8F) */
            float ix = sharc_bits_to_float(args[0]), iy = sharc_bits_to_float(args[1]);
            float iz = sharc_bits_to_float(args[2]), o[3];
            for (int w = 0; w < 3; w++) {                              /* _L20173 */
                float v = g_sharc.pos[w] + ix * r[0][w];
                v = v + iy * r[1][w];
                o[w] = v + iz * r[2][w];
            }
            float inv = sharc_fw_div(1.0f, o[2]);
            float sx = (o[0] * sharc_bits_to_float(args[3])) * inv;
            float sy = (o[1] * sharc_bits_to_float(args[4])) * inv;
            uint32_t f = 0;
            if (!(o[2] > sharc_bits_to_float(0x3DCCCCCCu))) f = 0x1F;   /* 0.1, as the firmware has it */
            else {
                if (!(sx > -248.0f)) f |= 1;
                if (!(sx < 248.0f)) f |= 2;
                if (!(sy > -192.0f)) f |= 4;
                if (!(sy < 192.0f)) f |= 8;
            }
            sharc_push_u(f);
            return;
        }
        case 0x7C:                      /* Fn_load_inner_3x3: table -> rot */
            for (int k = 0; k < 9; k++) (&r[0][0])[k] = sharc_dm_getf(ext_tab(args) + k);
            return;
        case 0x7D:                      /* Fn_store_inner_3x3: rot -> table */
            for (int k = 0; k < 9; k++) sharc_dm_setf(ext_tab(args) + k, (&r[0][0])[k]);
            return;
        case 0x7E: {                    /* Fn_mul_matrix_inner3: table 3x3 */
            float M[9];
            for (int k = 0; k < 9; k++) M[k] = sharc_dm_getf(ext_tab(args) + k);
            ext_mul3(M);
            return;
        }
    }
}
