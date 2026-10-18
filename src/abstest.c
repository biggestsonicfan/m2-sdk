/*
 * abstest.c — m2-sdk check (Model 2C only): the TGPx4's absolute address mode.
 *
 * Uploads src/abstest_tgpx4.s instead of the SDK kernel (m2_cop_load) and runs its tests once:
 * the absolute mode (EA type 0x0c, A(nnnH) / B(nnnH)) in MOV1, MOV2 and MOV3, on the A and B
 * buses and on the external bus (buffer RAM). Each test writes a word with one address mode
 * and reads it back with the other, so a core that adds an AR register to an absolute address,
 * or does not have the mode at all (MAME before fork branch tgpx4-abs-ea stops with "illegal
 * decode_ea type 0c"), fails here. One line per test, a total at the bottom.
 *
 *   cmake ... -DM2_GAME=abstest -DM2_BOARD=2C     run on dynamcopc
 *
 * The results also stay in g_abs_result[] / g_abs_bad / g_abs_done for a debugger or a MAME
 * Lua script.
 */
#include "m2.h"
#include "m2_gfx2d.h"
#include "m2_3d.h"
#include "abstest_tgpx4.h"

#if !defined(M2_BOARD_2C)
#error "abstest is a Model 2C (TGPx4) test: build it with -DM2_BOARD=2C"
#endif

#define NTEST 16
volatile u32 g_abs_result[NTEST];
volatile u32 g_abs_bad;
volatile u32 g_abs_done;

/* Buffer RAM words the program reaches over the external bus (EB = 0x400000). */
#define BUF(w) (*(volatile u32 *)(GEO_BUFFERRAM + (w) * 4u))

static const char *g_name[NTEST];
static u32 g_got[NTEST], g_want[NTEST];
static int g_n;

static void hex8(char *p, u32 v) {
    int i;
    for (i = 0; i < 8; i++) p[i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 0xF];
}

static void check(const char *name, u32 got, u32 want) {
    if (g_n >= NTEST) return;
    g_name[g_n] = name; g_got[g_n] = got; g_want[g_n] = want;
    g_abs_result[g_n] = got;
    if (got != want) g_abs_bad++;
    g_n++;
}

static u32 cop_get(void) { return M2_COPFIFO; }
static void cop_put(u32 v) { M2_COPFIFO = v; }

/* The same order as abstest_tgpx4.s. */
static void run_tests(void) {
    BUF(0x0FF0) = 0; BUF(0x3F00) = 0; BUF(0x3F01) = 0x5EED3F01u;
    check("ready", cop_get(), 0x0AB5u);            /* sent after its CLRF (which drops earlier words) */
    cop_put(0);                                    /* start word */
    cop_put(0x11110123u); check("mov2 wr A", cop_get(), 0x11110123u);
    cop_put(0x222202A5u); check("mov2 rd A", cop_get(), 0x222202A5u);
    cop_put(0x33330155u); check("mov2 wr B", cop_get(), 0x33330155u);
    cop_put(0x444403FFu); check("mov2 rd B", cop_get(), 0x444403FFu);
    cop_put(0x5555001Bu); check("mov1 wr A", cop_get(), 0x5555001Bu);
                          check("mov3 wr A", cop_get(), 0x12345678u);
    cop_put(0x88883F00u); check("mov2 rd E", cop_get(), 0x5EED3F01u);
    cop_put(0xAAAA0015u); check("mov1 rd A", cop_get(), 0xAAAA0015u);
                          check("AR0 kept", cop_get(), 0x40u);
                          check("AR7 kept", cop_get(), 0u);
    check("mov3 wr E", BUF(0x0FF0), 0xCAFEF00Du);  /* the program has written both by now */
    check("mov2 wr E", BUF(0x3F00), 0x88883F00u);
}

int main(void) {
    int i;
    m2_init();
    g2d_init();
    g2d_background(M2_RGB(0, 2, 10));
    g2d_color(1, M2_RGB(31, 31, 31));
    g2d_color(2, M2_RGB(10, 31, 10));
    g2d_color(3, M2_RGB(31, 8, 6));
    m2_font_atlas();

    m2_cop_load(abstest_tgpx4, (int)(sizeof(abstest_tgpx4) / sizeof(abstest_tgpx4[0])));
    run_tests();   /* once, before any frame uses buffer RAM */
    g_abs_done = 1;
    for (;;) {
        g2d_begin();
        g2d_ttext(16, 8, M2_BOARD_NAME " TGPx4 absolute address test", 1);
        for (i = 0; i < g_n; i++) {
            char line[40];
            int k, n;
            for (k = 0; k < 39; k++) line[k] = ' ';
            for (n = 0; g_name[i][n]; n++) line[n] = g_name[i][n];
            line[11] = g_got[i] == g_want[i] ? 'O' : 'B';
            line[12] = g_got[i] == g_want[i] ? 'K' : 'A';
            line[13] = g_got[i] == g_want[i] ? ' ' : 'D';
            hex8(line + 15, g_got[i]);
            hex8(line + 24, g_want[i]);
            line[g_got[i] == g_want[i] ? 13 : 32] = 0;   /* an OK line stops at "OK": ~390 glyphs a frame */
            g2d_ttext(16, 24 + i * 10, line, g_got[i] == g_want[i] ? 2 : 3);
        }
        g2d_ttext(16, 24 + g_n * 10 + 6, g_abs_bad ? "SOME TESTS FAILED" : "ALL ABSOLUTE MODES OK",
                  g_abs_bad ? 3 : 2);
        g2d_end();
        m2_vsync();
    }
    return 0;
}
