/*
 * coptest.c — m2-sdk check: the COP math commands give the same answers on every board.
 *
 * Boots the board's coprocessor (m2_cop_boot: STF's cpres1 SHARC on 2B, the SDK's TGP
 * program src/m2_tgp_kernel.s on 2A), sends each scalar / trig / vector command through the
 * COP FIFO and compares the result with the i960's own float math. One line per command on
 * screen (OK or BAD, result bits, expected bits) and a total at the bottom. The last twelve
 * lines are the current matrix after the transform commands m2_obj_submit sends (RMATRIX).
 *
 *   cmake ... -DM2_GAME=coptest -DM2_BOARD=2A     run on dynamcop
 *   cmake ... -DM2_GAME=coptest                   run on sfight (2B, needs cpres1.h)
 *
 * The results also stay in g_cop_result[] / g_cop_bad / g_cop_done for a debugger or a
 * MAME Lua script. Model 2C has no COP program yet, so the test only reports that there.
 */
#define M2_NO_FASTMATH   /* keep m2_cop_* on the COP: the point is to test it */
#include "m2.h"
#include "m2_gfx2d.h"
#include "m2_3d.h"

#define NTEST 48
volatile u32 g_cop_result[NTEST];
volatile u32 g_cop_bad;
volatile u32 g_cop_done;

static u32 fbits(float f) { union { float f; u32 u; } x; x.f = f; return x.u; }

static float dist2d(float x1, float x2, float z1, float z2) {   /* COP_DIST2D, (x1,x2,z1,z2) */
    m2_cop(COP_DIST2D); m2_cop_pf(x1); m2_cop_pf(x2); m2_cop_pf(z1); m2_cop_pf(z2);
    return m2_cop_gf();
}

/* The matrix commands m2_obj_submit sends, then RMATRIX. The expected matrix is worked out here
 * the way cpres1 does it (m2-hle sharc_exec.h): one slot of three rotation columns + T, each angle
 * post-multiplies, SET_POS adds col0*x, then col1*y, then col2*z. sin/cos come from the COP itself
 * (checked above), so this checks the matrix arithmetic. */
static float g_m[12];   /* col0, col1, col2, T */
static void m_rot(int u, int v, float s, float c) {   /* col u' = c*u + s*v, col v' = c*v - s*u */
    int r;
    for (r = 0; r < 3; r++) {
        float a = g_m[u * 3 + r], b = g_m[v * 3 + r];
        g_m[u * 3 + r] = c * a + s * b;
        g_m[v * 3 + r] = c * b - s * a;
    }
}
static void m_setpos(float x, float y, float z) {
    int w;
    for (w = 0; w < 3; w++) g_m[9 + w] = g_m[9 + w] + x * g_m[w];
    for (w = 0; w < 3; w++) g_m[9 + w] = g_m[9 + w] + y * g_m[3 + w];
    for (w = 0; w < 3; w++) g_m[9 + w] = g_m[9 + w] + z * g_m[6 + w];
}

static int n_line;
static void hex8(char *p, u32 v) {
    int i;
    for (i = 0; i < 8; i++) p[i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 0xF];
}

/* got/want as floats, within 1e-5 relative (the TGP's divide and square root start from a
 * table seed with one correction step, so they are not bit-exact). */
static void check(const char *name, float got, float want) {
    float err = got - want, mag = want < 0 ? -want : want;
    char line[40];
    int i, n, ok;
    if (err < 0) err = -err;
    ok = err <= (mag > 1.0f ? mag : 1.0f) * 1e-5f;
    for (i = 0; i < 39; i++) line[i] = ' ';
    line[39] = 0;
    for (n = 0; name[n] && n < 9; n++) line[n] = name[n];
    line[10] = ok ? 'O' : 'B'; line[11] = ok ? 'K' : 'A'; line[12] = 'D';
    hex8(line + 14, fbits(got));
    hex8(line + 23, fbits(want));
    if (n_line < NTEST) g_cop_result[n_line] = fbits(got);
    if (!ok) g_cop_bad++;
    /* on screen: the name in green, or "name BAD result-bits" in red (a frame's display list holds ~390
     * glyphs, spaces included, so an OK line stops at the name); the UART gets every line in full,
     * expected bits too. */
    if (ok) line[n] = 0; else line[22] = 0;
    g2d_ttext(16 + (n_line / 16) * 160, 24 + (n_line % 16) * 10, line, ok ? 2 : 3);
    n_line++;
    if (!g_cop_done) {
        line[n] = ' '; line[12] = ok ? ' ' : 'D'; line[22] = ' ';
        m2_uart_puts(line); m2_uart_puts("\r\n");
    }
}
static void checki(const char *name, int got, int want) {
    check(name, (float)got, (float)want);
}

static void run_tests(void) {
    float o[3];
    n_line = 0;
    g_cop_bad = 0;
    check("fadd",   m2_cop_fadd(1.5f, 2.25f), 3.75f);
    check("fsub",   m2_cop_fsub(5.0f, 1.25f), 3.75f);
    check("fmul",   m2_cop_fmul(3.0f, -2.5f), -7.5f);
    check("fdiv",   m2_cop_fdiv(1.0f, 3.0f), 1.0f / 3.0f);
    check("fdiv-",  m2_cop_fdiv(-10.0f, 4.0f), -2.5f);
    check("fdiv7",  m2_cop_fdiv(1234.5f, 0.0078125f), 158016.0f);
    check("int2f",  m2_cop_int2f(-7), -7.0f);
    checki("f2int", m2_cop_f2int(-3.7f), -4);   /* cpres1 rounds down */
    checki("f2int+", m2_cop_f2int(5.9f), 5);
    check("sqrt",   m2_cop_sqrt(2.0f), 1.41421356f);
    check("sqrt1e4", m2_cop_sqrt(10000.0f), 100.0f);
    check("sqrt.25", m2_cop_sqrt(0.25f), 0.5f);
    check("sin90",  (cop_sincos(0x4000, &o[0], &o[1]), o[0]), 1.0f);
    check("cos90",  o[1], 0.0f);
    check("sin45",  (cop_sincos(0x2000, &o[0], &o[1]), o[0]), 0.70710678f);
    check("cos180", (cop_sincos(0x8000, &o[0], &o[1]), o[1]), -1.0f);
    check("sin270", (cop_sincos(0xC000, &o[0], &o[1]), o[0]), -1.0f);
    check("tan45",  m2_cop_tan(0x2000), 1.0f);
    check("sinsc",  m2_cop_sin_scale(0x4000, 3.0f), 3.0f);
    check("cossc",  m2_cop_cos_scale(0x2000, 2.0f), 1.41421356f);
    check("dist2d", dist2d(1.0f, 4.0f, 2.0f, 6.0f), 5.0f);
    check("dist3d", m2_cop_dist3d(0.0f, 2.0f, 0.0f, 3.0f, 0.0f, 6.0f), 7.0f);
    check("mag2d",  m2_cop_mag2d(3.0f, 4.0f), 5.0f);
    check("dot2d",  m2_cop_dot2d(2.0f, 3.0f, 4.0f, 5.0f), 26.0f);
    {
        static const float a[3] = { 1.0f, 2.0f, 3.0f }, b[3] = { 0.5f, -4.0f, 10.0f };
        m2_cop_add3(a, b, o);
        check("add3.x", o[0], 1.5f); check("add3.y", o[1], -2.0f); check("add3.z", o[2], 13.0f);
        m2_cop_sub3(a, b, o);
        check("sub3.z", o[2], -7.0f);
        m2_cop_scale3(-2.0f, a, o);
        check("scale3.x", o[0], -2.0f); check("scale3.z", o[2], -6.0f);
    }
    {
        static const char *nm[12] = { "m.c0x", "m.c0y", "m.c0z", "m.c1x", "m.c1y", "m.c1z",
                                      "m.c2x", "m.c2y", "m.c2z", "m.tx", "m.ty", "m.tz" };
        float sy, cy, sx, cx, sz, cz, got[12];
        int i;
        cop_sincos(0x2000, &sy, &cy);   /* 45 */
        cop_sincos(0x1555, &sx, &cx);   /* 30 */
        cop_sincos(0xC000, &sz, &cz);   /* 270 */
        for (i = 0; i < 12; i++) g_m[i] = (i == 0 || i == 4 || i == 8) ? 1.0f : 0.0f;
        m_setpos(1.5f, -2.0f, 3.0f);
        m_rot(0, 2, sy, cy);            /* ANG_Y */
        m_rot(2, 1, sx, cx);            /* ANG_X */
        m_rot(1, 0, sz, cz);            /* ANG_Z */
        for (i = 0; i < 9; i++) g_m[i] *= (i < 3 ? 2.0f : i < 6 ? 0.5f : -3.0f);   /* SCALE */
        m_setpos(10.0f, 20.0f, 30.0f);
        m2_cop(COP_IDENTITY);
        m2_cop(COP_SET_POS); m2_cop_pf(1.5f); m2_cop_pf(-2.0f); m2_cop_pf(3.0f);
        m2_cop(COP_ANG_Y); m2_cop_pi(0x2000);
        m2_cop(COP_ANG_X); m2_cop_pi(0x1555);
        m2_cop(COP_ANG_Z); m2_cop_pi(0xC000);
        m2_cop(COP_SCALE); m2_cop_pf(2.0f); m2_cop_pf(0.5f); m2_cop_pf(-3.0f);
        m2_cop(COP_SET_POS); m2_cop_pf(10.0f); m2_cop_pf(20.0f); m2_cop_pf(30.0f);
        m2_cop_rmatrix(got);
        for (i = 0; i < 12; i++) check(nm[i], got[i], g_m[i]);
    }
}

int main(void) {
    m2_init();
    g2d_init();
    g2d_background(M2_RGB(0, 2, 10));
    g2d_color(1, M2_RGB(31, 31, 31));
    g2d_color(2, M2_RGB(10, 31, 10));
    g2d_color(3, M2_RGB(31, 8, 6));
    m2_font_atlas();

#if defined(M2_BOARD_2C)
    for (;;) {
        g2d_begin();
        g2d_ttext(16, 16, "Model 2C: no COP program yet", 1);
        g2d_end();
        m2_vsync();
    }
#else
    m2_cop_boot();
    m2_cop_initialize();   /* cpres1 command 0: matrix stack empty, current slot at its base */
    for (;;) {
        g2d_begin();
        g2d_ttext(16, 8, M2_BOARD_NAME " COP test", 1);
        run_tests();
        g2d_ttext(16, 24 + 16 * 10 + 6, g_cop_bad ? "SOME COMMANDS FAILED" : "ALL COMMANDS OK", g_cop_bad ? 3 : 2);
        g_cop_done++;
        g2d_end();
        m2_vsync();
    }
#endif
    return 0;
}
