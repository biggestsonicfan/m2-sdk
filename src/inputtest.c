/*
 * inputtest.c — shows the three input bytes (m2_io_in) and what m2_input/m2_start decode.
 *
 * On 2A/2B/2C the bytes come from the 315-5649; on the original Model 2 (-DM2_BOARD=2O)
 * from the Model 1 IO board through its dual-port RAM (m2_io.h). Tile text only, so it
 * runs the same on every board. The values are also kept in g_in_raw / g_in_dec for a
 * script to read (tools/inputtest_check.lua).
 */
#include "m2.h"

u32 g_in_raw;     /* in0 | in1 << 8 | in2 << 16 (raw, active-low)  */
u32 g_in_dec;     /* m2_input(0) | m2_input(1) << 8 | m2_start() << 16 */
u32 g_in_frames;

static void hex2(char *d, u32 v) {
    static const char h[] = "0123456789ABCDEF";
    d[0] = h[(v >> 4) & 15u];
    d[1] = h[v & 15u];
}

static void flags(char *d, u32 v) {
    static const char n[] = "UDLR123S";
    u32 i;
    for (i = 0; i < 8u; i++) d[i] = (v >> i) & 1u ? n[i] : '.';
}

int main(void) {
    char raw[] = "IN0 .. IN1 .. IN2 ..";
    char p1[]  = "P1 ........";
    char p2[]  = "P2 ........";
    char st[]  = "START ..";

    m2_init();
    m2_textpal(1, M2_RGB(31, 31, 31));
    m2_textpal(2, M2_RGB(31, 27, 0));
    m2_print(2, 2, M2_BOARD_NAME " INPUT TEST", 2);
    for (;;) {
        u32 i, in1 = m2_input(0), in2 = m2_input(1), s = m2_start();
        for (i = 0; i < 3u; i++) hex2(raw + 4 + 7 * i, m2_io_in((int)i));
        g_in_raw = m2_io_in(0) | (u32)m2_io_in(1) << 8 | (u32)m2_io_in(2) << 16;
        g_in_dec = in1 | in2 << 8 | s << 16;
        flags(p1 + 3, in1);
        flags(p2 + 3, in2);
        st[6] = s & 1u ? '1' : '.';
        st[7] = s & 2u ? '2' : '.';
        m2_print(2, 5, raw, 1);
        m2_print(2, 7, p1, 1);
        m2_print(2, 8, p2, 1);
        m2_print(2, 10, st, 1);
        g_in_frames++;
        m2_vsync();
    }
    return 0;
}
