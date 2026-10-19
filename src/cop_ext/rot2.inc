/* From m2-hle2 (2662dda2) src/board/sharc.h: the cpres1 matrix rotations and helpers. */

/* ang_y (0x04800909 / 0x201BF): post-multiply rot by Ry — modifies col0 and col2. */
static inline void sharc_postmul_ry(float c, float s) {
    float (*r)[3] = g_sharc.rot;
    int row;
    for (row = 0; row < 3; row++) {
        float c0 = r[0][row], c2 = r[2][row];
        r[0][row] =  c*c0 + s*c2;
        r[2][row] = -s*c0 + c*c2;
    }
}

/* ang_x (0x04000808 / 0x201AA): post-multiply rot by Rx — modifies col1 and col2. */
static inline void sharc_postmul_rx(float c, float s) {
    float (*r)[3] = g_sharc.rot;
    int row;
    for (row = 0; row < 3; row++) {
        float c1 = r[1][row], c2 = r[2][row];
        r[1][row] =  c*c1 - s*c2;
        r[2][row] =  s*c1 + c*c2;
    }
}

/* ang_z (0x05000A0A / 0x201D4): post-multiply rot by Rz — modifies col0 and col1. */
static inline void sharc_postmul_rz(float c, float s) {
    float (*r)[3] = g_sharc.rot;
    int row;
    for (row = 0; row < 3; row++) {
        float c0 = r[0][row], c1 = r[1][row];
        r[0][row] =  c*c0 - s*c1;
        r[1][row] =  s*c0 + c*c1;
    }
}

/* pre-multiply rot by Ry: rot = Ry × rot — modifies each column's x and z rows.
 * Used by 0x34806969 to apply a world-space facing angle to a bone matrix. */
static inline void sharc_premul_ry(float c, float s) {
    float (*r)[3] = g_sharc.rot;
    int col;
    for (col = 0; col < 3; col++) {
        float r0 = r[col][0], r2 = r[col][2];
        r[col][0] =  c*r0 - s*r2;
        r[col][2] =  s*r0 + c*r2;
    }
}

/* Reset rot[] to identity. */
static inline void sharc_rot_identity(void) {
    memset(g_sharc.rot, 0, sizeof(g_sharc.rot));
    g_sharc.rot[0][0] = g_sharc.rot[1][1] = g_sharc.rot[2][2] = 1.0f;
}
