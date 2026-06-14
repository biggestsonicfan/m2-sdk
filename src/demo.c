/*
 * demo.c — minimal m2-sdk example: hardware-2D (g2d) animation.
 *
 * Shows the core SDK in ~40 lines: bring up the board (m2_init), set up the
 * hardware-2D pipeline (g2d_init), then each frame build a display list of flat
 * polygons (g2d_begin .. g2d_end) and pace on vblank. No coprocessor/firmware is
 * needed — g2d renders through the GEO's direct_data path (MAME HLE). For 3D
 * vector rendering with the same module (g2d_vquad/g2d_vline/g2d_zsort_fine), see
 * the Tempest game in the m2-snake project.
 *
 * Build:  cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-i960-elf.cmake
 *         ninja -C build            (default M2_GAME=demo)
 */
#include "m2.h"
#include "m2_gfx2d.h"

#define FIELD_W 496
#define FIELD_H 384

int main(void) {
    float bx = 120.0f, by = 110.0f, vx = 3.1f, vy = 2.3f;
    u32 t = 0;

    m2_init();                              /* board + font ready                    */
    g2d_init();                             /* colour pipeline + blank the tile layers */
    g2d_background(M2_RGB(1, 2, 6));        /* deep-blue backdrop                    */
    g2d_color(1, M2_RGB(31, 6, 6));         /* slot 1 = red   (ball)                 */
    g2d_color(2, M2_RGB(4, 26, 28));        /* slot 2 = cyan  (frame)                */
    g2d_color(3, M2_RGB(31, 31, 4));        /* slot 3 = yellow(slider)               */

    for (;;) {
        u32 in = m2_input(0);
        /* bounce the ball inside the visible field */
        bx += vx; by += vy;
        if (bx < 28.0f || bx > FIELD_W - 28.0f) vx = -vx;
        if (by < 28.0f || by > FIELD_H - 28.0f) vy = -vy;
        t++;

        g2d_begin();
        /* cyan frame (four bars) */
        g2d_rect(8, 8, FIELD_W - 16, 8, 2);
        g2d_rect(8, FIELD_H - 16, FIELD_W - 16, 8, 2);
        g2d_rect(8, 8, 8, FIELD_H - 16, 2);
        g2d_rect(FIELD_W - 16, 8, 8, FIELD_H - 16, 2);
        /* a yellow slider tracking the frame counter, and a diagonal */
        g2d_rect(40.0f + (float)(t % 360u), FIELD_H - 48.0f, 64.0f, 18.0f, 3);
        g2d_line(40.0f, 40.0f, bx, by, 4.0f, 3);
        /* the bouncing red ball (hardware-filled circle) */
        g2d_fill_circle(bx, by, 26.0f, 1);
        g2d_end();

        m2_vsync();
        (void)in;
    }
    return 0;
}
