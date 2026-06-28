/*
 * fontdemo.c — m2-sdk example: hardware 2D + the textured bitmap font.
 *
 * Build (msys2 CLANG64 i960-elf toolchain on PATH):
 *     cmd /c ".\build_clang64.bat fontdemo"      -> roms/epr-19001.15 + .16
 *   or  cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-i960-elf.cmake \
 *             -DM2_GAME=fontdemo && ninja -C build
 *
 * Run it by overriding the sfight program EPROM pair in MAME (see README).
 *
 * Shows the whole pipeline: init -> per-frame g2d_begin()/draw/g2d_end() ->
 * vsync. The textured font (m2_font_atlas + g2d_ttext) composites over arbitrary
 * 2D content, takes a colorbase tint, and scales.
 */
#include "m2.h"          /* core board: video, palette, input, vblank. include first */
#include "m2_gfx2d.h"    /* hardware-2D + textured font (pulls in m2_color.h) */

int main(void) {
    int t = 0;

    m2_init();           /* bring up video/board (boot asm has zeroed .bss) */
    g2d_init();          /* hardware-2D + colour pipeline (lumaram from STF ROM) */

    g2d_background(M2_RGB(0, 2, 10));     /* backdrop colour */
    g2d_color(1, M2_RGB(31, 31, 31));     /* colorbase 1 = white  */
    g2d_color(2, M2_RGB(10, 31, 10));     /* colorbase 2 = green  */
    g2d_color(3, M2_RGB(31, 26, 4));      /* colorbase 3 = amber  */
    g2d_color(4, M2_RGB(18, 6, 28));      /* colorbase 4 = violet */

    m2_font_atlas();    /* upload the 8x8 font into texram0 ONCE (after g2d_init) */

    for (;;) {
        int bx = 40 + ((t >> 1) % 360);   /* a little motion */

        g2d_begin();                      /* start the frame's display list */

        /* flat-colour 2D primitives (auto painter's-order depth: later = on top) */
        g2d_rect(20, 70, 440, 3, 2);              /* a rule */
        g2d_fill_circle((float)bx, 150, 14, 3);   /* a moving dot */

        /* textured font: transparent over the panel, tinted, scalable */
        g2d_rect(20, 18, 360, 40, 4);                       /* panel */
        g2d_ttext(28, 24, "m2-sdk textured font demo", 1);  /* 1:1 */
        g2d_ttext(28, 40, "ABCDEFG abcdefg 0123456789", 2); /* 1:1 */
        g2d_ttext_scaled(28, 96,  "Hello, World!", 3, 2.0f);/* 2x  */
        g2d_ttext_scaled(28, 190, "Model 2B", 1, 4.0f);     /* 4x  */

        g2d_end();                        /* submit; GEO rasterizes next vblank */
        m2_vsync();
        t++;
    }
    return 0;
}
