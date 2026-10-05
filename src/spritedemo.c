/*
 * spritedemo.c — m2_sprite.h: sprites as textured GEO polygons over a tile-plane background.
 *
 * The background (grid, labels, the clip box) is the tile-plane framebuffer (m2_tilefb.h),
 * set BELOW the polygons (TFB_PRIO 0). On top, as GEO quads: a three-colour ghost and an
 * asymmetric "F" in each flip, at 1x/2x/4x, a ghost bouncing inside a clip box, and two
 * overlap tests (same layer: the later draw is in front; a higher layer wins whatever the
 * order). Every sprite costs the i960 the same ~30 display-list words per colour, whatever its
 * size.
 *
 *   cmake -B build -DM2_GAME=spritedemo && make -C build     (needs src/cpres1.h + cpres2.h)
 *   MAME: sfight with the two program EPROMs replaced; M2_HLE_GEO_OFF=1 for the real GEO.
 */
#include "m2.h"
#include "m2_obj.h"            /* m2_silicon_boot, m2_frame_begin/commit */
#define TFB_PRIO 0             /* tile plane below the polygons */
#include "m2_tilefb.h"
#include "m2_sprite.h"

enum { CB_RED = 1, CB_WHITE, CB_BLUE, CB_YELLOW, CB_GREEN, CB_PINK };

/* 16x16, pens: 1 body, 2 eye white, 3 pupil */
static const char *ghost_art[16] = {
    "......1111......",
    "....11111111....",
    "...1111111111...",
    "..111111111111..",
    ".11222211222211.",
    ".12222221222222.",
    ".12233221222332.",
    "1112233211223321",
    "1111222111122221",
    "1111111111111111",
    "1111111111111111",
    "1111111111111111",
    "1111111111111111",
    "1111111111111111",
    "11.111.11.111.11",
    "1...11..1..11..1",
};
/* 8x8 "F" with a dot in its bottom-right corner (pen 2): shows every flip */
static const char *f_art[8] = {
    "11111111",
    "11111111",
    "11......",
    "111111..",
    "111111..",
    "11......",
    "11....22",
    "11....22",
};
/* 16x16 solid square, one pen */
static u8 sq_pix[16 * 16];

static u8 pix[16 * 16];
static int art_load(const char **art, int w, int h) {
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            char c = art[y][x];
            pix[y * w + x] = (u8)(c == '.' ? 0 : c - '0');
        }
    return m2_spr_load(pix, w, h, w);
}

int main(void) {
    static const u16 ghost_pens[16] = { 0, CB_RED, CB_WHITE, CB_BLUE };
    static const u16 pink_pens[16]  = { 0, CB_PINK, CB_WHITE, CB_BLUE };
    static const u16 f_pens[16]     = { 0, CB_YELLOW, CB_GREEN };
    static const u16 red_pens[16]   = { 0, CB_RED };
    static const u16 blue_pens[16]  = { 0, CB_BLUE };
    int ghost, f, sq, i, x, y, bx = 300, by = 250, vx = 2, vy = 1;
    u32 frame = 0;

    m2_init();
    m2_silicon_boot();
    m2_spr_flat_colors();
    m2_spr_color(CB_RED,    M2_RGB(31,  0,  0));
    m2_spr_color(CB_WHITE,  M2_RGB(31, 31, 31));
    m2_spr_color(CB_BLUE,   M2_RGB( 4,  4, 31));
    m2_spr_color(CB_YELLOW, M2_RGB(31, 31,  0));
    m2_spr_color(CB_GREEN,  M2_RGB( 0, 31,  0));
    m2_spr_color(CB_PINK,   M2_RGB(31, 22, 27));
    for (i = 0; i < 8; i++) { m2_frame_begin(); m2_frame_end(); }   /* prime the GEO */

    /* background: dark grid every 16 px, labels, the clip box */
    tfb_init();
    tfb_setcolor(1, M2_RGB(4, 4, 8));
    tfb_setcolor(2, M2_RGB(31, 31, 31));
    tfb_setcolor(3, M2_RGB(12, 12, 12));
    for (x = 0; x < TFB_VIS_W; x += 16) tfb_vline(x, 0, TFB_H, 1);
    for (y = 0; y < TFB_H; y += 16) tfb_hline(0, y, TFB_VIS_W, 1);
    tfb_text(16, 4, "M2_SPRITE: SPRITES AS GEO POLYGONS", 2, -1);
    tfb_text(16, 24, "1X", 2, -1);
    tfb_text(16, 56, "2X", 2, -1);
    tfb_text(16, 112, "4X", 2, -1);
    tfb_text(16, 200, "LAYERS", 2, -1);
    tfb_rect(240, 200, 160, 120, 2);          /* the clip box (sprites stay inside) */
    tfb_fillrect(16, 280, 64, 16, 3);         /* a tile-plane block: sprites go over it */

    ghost = art_load(ghost_art, 16, 16);
    f = art_load(f_art, 8, 8);
    for (i = 0; i < 16 * 16; i++) sq_pix[i] = 1;
    sq = m2_spr_load(sq_pix, 16, 16, 16);

    for (;;) {
        bx += vx; by += vy;
        if (bx < 216 || bx > 392) vx = -vx;   /* overshoots the clip box on purpose */
        if (by < 176 || by > 312) vy = -vy;

        m2_frame_begin();
        m2_spr_frame_setup();

        /* row 1x: ghost and F in each flip */
        for (i = 0; i < 4; i++) {
            m2_spr_draw(ghost, 48 + i * 24, 20, (u32)i, ghost_pens, 0);
            m2_spr_draw(f, 160 + i * 16, 24, (u32)i, f_pens, 0);
        }
        /* 2x and 4x */
        for (i = 0; i < 4; i++) {
            m2_spr_draw_scaled(ghost, 48 + i * 40, 48, 32, 32, (u32)i, i & 1 ? pink_pens : ghost_pens, 0);
            m2_spr_draw_scaled(f, 220 + i * 24, 56, 16, 16, (u32)i, f_pens, 0);
            m2_spr_draw_scaled(ghost, 48 + i * 72, 104, 64, 64, (u32)i, ghost_pens, 0);
            m2_spr_draw_scaled(f, 340 + (i & 1) * 72, 104 + (i >> 1) * 40, 32, 32, (u32)i, f_pens, 0);
        }
        /* layers: same layer -> the later (blue) in front; layer 1 red drawn first stays in front */
        m2_spr_draw(sq, 80, 200, 0, red_pens, 0);
        m2_spr_draw(sq, 88, 208, 0, blue_pens, 0);
        m2_spr_draw(sq, 140, 200, 0, red_pens, 1);
        m2_spr_draw(sq, 148, 208, 0, blue_pens, 0);
        /* over the tile-plane block */
        m2_spr_draw(ghost, 40, 272, 0, pink_pens, 0);
        /* bouncing ghost, clipped to the box */
        m2_spr_clip(241, 201, 399, 319);
        m2_spr_draw_scaled(ghost, bx, by, 32, 32, (frame >> 4) & 1 ? M2_SPR_FLIPX : 0, ghost_pens, 2);
        m2_spr_clip(0, 0, 496, 384);

        m2_frame_commit();
        m2_vsync();
        frame++;
    }
}
