/*
 * hwdemo.c — the SILICON / m2emulator demo (object_data, COP-bridge path).
 *
 * demo.c / fontdemo.c use g2d (m2_gfx2d.h): hard-float + the GEO direct_data path,
 * which is MAME-HLE-only. This demo instead drives flat-colour quads through the COP
 * (m2_obj.h: m2_silicon_boot + m2_solid_quad) — the object_data path proven on real
 * Model 2B hardware by m2-snake's ice-cube sandbox. Built soft-float (the default) it
 * runs on m2emulator and on hardware (NOT just MAME's HLE geo_parse).
 *
 *   build:  cmd /c ".\build_clang64.bat hwdemo"          (soft-float is the default)
 *      or:  cmake ... -DM2_GAME=hwdemo && ninja -C build_cmake
 *   run  :  copy roms\epr-19001.15 + epr-19002.16 over m2emulator's sfight romset.
 *           (MAME proxy: launch sfight with M2_HLE_GEO_OFF set for the real GEO. NB MAME
 *            still emulates the i960 FPU even then, so it does NOT catch FP-opcode bugs —
 *            only m2emulator/hardware do; keep the build soft-float.)
 *
 * The scalar math here is plain C: under the soft-float build it is already FP-opcode-free
 * (libgcc), so it needs no i960 FPU and no SHARC round-trip. The SHARC does the real
 * per-frame work — transforming each quad's geometry inside the object submit.
 */
#include "m2.h"
#include "m2_obj.h"     /* m2_silicon_boot, m2_solid_quad, m2_obj_frame_setup, m2_obj_fb */

#define HW_FOCUS  280.0f
#define HW_CX     248.0f
#define HW_CY     192.0f
#define HW_MODELW 12.0f          /* model 456 is 12 world units wide (scale = worldsize/12) */

enum { CB_WHITE = 1, CB_RED, CB_GREEN, CB_BLUE, CB_YELLOW, CB_CYAN };

/* set a colorbase's ink colour (BGR555) — palram[cb+0x1000]. */
static void hw_color(u32 cb, u16 bgr555) {
    *(volatile u16 *)(0x01800000u + (cb + 0x1000u) * 2u) = bgr555;
}

/* Submit one flat-colour quad covering screen rect (x,y,w,h) [top-left x,y] at depth z,
 * colorbase cb. ax=0x4000 (90deg) faces the quad to the camera; smaller z = nearer. */
static void hw_quad(u32 cb, float x, float y, float w, float h, float z) {
    float wpp = z / HW_FOCUS;                          /* world units per screen pixel */
    float wx  = ((x + w * 0.5f) - HW_CX) * wpp;        /* rect centre -> world X */
    float wy  = (HW_CY - (y + h * 0.5f)) * wpp;        /* (screen Y is down) */
    float sx  = (w * wpp) / HW_MODELW;
    float sz  = (h * wpp) / HW_MODELW;
    m2_solid_quad(cb, m2_obj_fb(wx), m2_obj_fb(wy), m2_obj_fb(z),
                  0x4000u, 0u, 0u,
                  m2_obj_fb(sx), m2_obj_fb(1.0f), m2_obj_fb(sz));
}

/* outlined panel: border quad one z-step behind a fill quad. */
static void hw_panel(float x, float y, float w, float h, u32 fill_cb, u32 edge_cb, float t) {
    hw_quad(edge_cb, x - t, y - t, w + 2.0f * t, h + 2.0f * t, 8.10f);
    hw_quad(fill_cb, x, y, w, h, 8.00f);
}

int main(void) {
    float bx = 248.0f, by = 240.0f, vx = 2.6f, vy = 1.9f;
    static const u32 bar[5] = { CB_RED, CB_YELLOW, CB_GREEN, CB_CYAN, CB_BLUE };
    u32 i;

    m2_init();
    m2_silicon_boot();                 /* COP + real GEO boot + colour pipeline + GEO seed */
    m2_cam_set_focus(HW_FOCUS);        /* poly-test camera (eye at origin, look +Z)        */
    hw_color(CB_WHITE,  M2_RGB(31, 31, 31));
    hw_color(CB_RED,    M2_RGB(31,  0,  0));
    hw_color(CB_GREEN,  M2_RGB( 0, 31,  0));
    hw_color(CB_BLUE,   M2_RGB( 0,  0, 31));
    hw_color(CB_YELLOW, M2_RGB(31, 31,  0));
    hw_color(CB_CYAN,   M2_RGB( 0, 31, 31));
    for (i = 0; i < 8u; i++) { m2_frame_begin(); m2_frame_end(); }   /* prime the GEO */

    for (;;) {
        bx += vx; by += vy;
        if (bx <  28.0f) { bx =  28.0f; vx = -vx; }  if (bx > 452.0f) { bx = 452.0f; vx = -vx; }
        if (by < 100.0f) { by = 100.0f; vy = -vy; }  if (by > 344.0f) { by = 344.0f; vy = -vy; }

        m2_frame_begin();
        m2_obj_frame_setup();                        /* GEO state + COP projection, ONCE/frame */

        /* white border (z=8.2, furthest of the UI) */
        hw_quad(CB_WHITE,   8.0f,   8.0f, 480.0f,   6.0f, 8.20f);   /* top    */
        hw_quad(CB_WHITE,   8.0f, 370.0f, 480.0f,   6.0f, 8.20f);   /* bottom */
        hw_quad(CB_WHITE,   8.0f,   8.0f,   6.0f, 368.0f, 8.20f);   /* left   */
        hw_quad(CB_WHITE, 482.0f,   8.0f,   6.0f, 368.0f, 8.20f);   /* right  */

        /* five colour bands — a DIFFERENT colorbase per quad in one frame */
        for (i = 0; i < 5u; i++)
            hw_quad(bar[i], 40.0f + (float)i * 84.0f, 36.0f, 76.0f, 20.0f, 8.10f);

        /* outlined panel */
        hw_panel(184.0f, 150.0f, 128.0f, 40.0f, CB_BLUE, CB_WHITE, 4.0f);

        /* the bouncing block (z=7.8 = nearest, drawn on top) */
        hw_quad(CB_YELLOW, bx - 12.0f, by - 12.0f, 24.0f, 24.0f, 7.80f);

        *(volatile u32 *)ZCLIP_REG = 0xFFFF00FFu;    /* un-clip (full-word ZCLIP), as the cube does */
        m2_frame_commit();
        m2_vsync();                                  /* ~60fps */
    }
    return 0;
}
