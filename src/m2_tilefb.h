/*
 * m2_tilefb.h — a linear pixel framebuffer on the Model 2b System 24 tile plane.
 *
 * This is the "cell data" path: STF renders all its 2D (HUD, menus, text) through the
 * Sega System 24 scroll/CG char cells, NOT through the 3D texture RAM. The Scroll CG
 * (_ScrollCG_Initialize) loads char patterns into char RAM and print_mes writes cell
 * entries (0x8000|char) into the tilemap. This header generalises that to a full pixel
 * framebuffer so a UI can draw arbitrary rects/lines/ellipses/text with ZERO texram use
 * — the texram stays free for real 3D model textures, and (unlike the COP flat-quad 2D
 * path, memory/direct-2d-pipeline.md) the tile plane renders clean on real silicon.
 *
 * The trick (legacy src/legacy/xfb.h): give every on-screen cell its own char block,
 * mapped linearly so cell n uses char index n; a pixel is then a 4-bit nibble inside
 * that block. Since the FB cell stride equals the tilemap stride (M2_W == 64), char
 * index == tilemap index == cell n. 64x48 cells = 512x384 px (visible ~496x384 = 62 cols).
 *
 * ⭐ PIXEL PACKING — the corrected convention (memory/model2b-platform-facts, 2026-07-01;
 * empirically proven by peek + PIL forensics on the geoserial gs_draw_bar render). The
 * old xfb.h used the WRONG order (even px = low nibble, no word swap), which renders any
 * COMPUTED pixel horizontally-scrambled (it only "worked" for solid fills and for glyphs
 * copied verbatim through the same wrong convention). The true order:
 *   - within a char-RAM 16-bit word the two bytes render SWAPPED vs i960 byte addresses,
 *     so the byte for screen pixel-pair (px>>1) lives at row offset ((px>>1) ^ 1);
 *   - within a byte the LEFT (even) pixel is the HIGH nibble, the odd pixel the LOW.
 * So: byteoff = (py&7)*4 + (((px&7)>>1) ^ 1);  even px -> high nibble, odd px -> low.
 *
 * gFont is STORED in this same hardware char-RAM byte order (m2_loadfont copies it
 * verbatim and it renders correctly), so tfb_glyph decodes a screen pixel from gFont with
 * the matching inverse: gb = g[py*4 + ((px>>1)^1)]; even px -> high nibble.
 *
 * Include AFTER m2.h (uses M2_TILE_FG / M2_CHARGFX / M2_PALETTE / M2_PRIO / m2font gFont).
 */
#ifndef M2_TILEFB_H
#define M2_TILEFB_H

#include "m2.h"                /* pulls in m2font.h (gFont[128*32], hw char-RAM order) */

#define TFB_CELLS_W   64                          /* char index == row*stride, stride = M2_W = 64 */
#define TFB_CELLS_H   48
#define TFB_W         (TFB_CELLS_W * 8)            /* 512 (visible 496 = 62 cols) */
#define TFB_H         (TFB_CELLS_H * 8)            /* 384 */
#define TFB_VIS_W     496                          /* active raster width (clip drawing to this) */
#define TFB_CELLS     (TFB_CELLS_W * TFB_CELLS_H)  /* 3072 */
#define TFB_BANKS     ((TFB_CELLS + 127) / 128)    /* palette banks the FB spans (24) */

/* Set colormap entry `pix` (0..15) to a BGR555 colour, replicated across every palette
 * bank the framebuffer spans (palbank = char>>7) so a pixel renders the same in any cell.
 * NB pixel 0 is TRANSPARENT on the FG tile layer regardless of palette (the hw backdrop,
 * m2_setpal(0,..), shows through) — use it for the see-through root/background. */
static void tfb_setcolor(u8 pix, u16 bgr555) {
    int b;
    for (b = 0; b < TFB_BANKS; b++)
        M2_PALETTE[b * 16 + (pix & 15)] = bgr555;
}

/* Point every FB cell at its own char block (entry = PRIO | n) and clear all pixels to 0
 * (transparent). Call once when entering FB mode (after m2_init). Does NOT touch palettes.
 * ⚠ this repurposes char RAM 0..TFB_CELLS as raw pixel blocks — it collides with the
 * font/solid tiles (m2_loadfont, m2_backdrop, the manager bar), so FB mode must OWN the
 * screen. Re-run m2_loadfont / the manager draw to go back to text-tile mode. */
static void tfb_init(void) {
    int n, i;
    volatile u32 *g = (volatile u32 *)M2_CHARGFX;
    for (n = 0; n < TFB_CELLS; n++)
        M2_TILE_FG[n] = (u16)(M2_PRIO | (u32)n);
    for (i = 0; i < TFB_CELLS * 8; i++) g[i] = 0u;   /* 32 bytes/cell = 8 u32/cell */
}

/* Plot one pixel (clipped to the visible FB). colour = pixel value 0..15. */
static void tfb_putpixel(int x, int y, u8 colour) {
    int cell, byteoff;
    volatile u8 *p;
    if ((unsigned)x >= (unsigned)TFB_VIS_W || (unsigned)y >= (unsigned)TFB_H) return;
    cell = (y >> 3) * TFB_CELLS_W + (x >> 3);
    byteoff = (y & 7) * 4 + ((((x & 7) >> 1)) ^ 1);      /* corrected word-swap */
    p = M2_CHARGFX + cell * 32 + byteoff;
    if (x & 1) *p = (u8)((*p & 0xF0) | (colour & 0x0F));         /* odd  px -> LOW  nibble */
    else       *p = (u8)((*p & 0x0F) | ((colour & 0x0F) << 4));  /* even px -> HIGH nibble */
}

static void tfb_hline(int x, int y, int w, u8 colour) {
    int i; for (i = 0; i < w; i++) tfb_putpixel(x + i, y, colour);
}
static void tfb_vline(int x, int y, int h, u8 colour) {
    int i; for (i = 0; i < h; i++) tfb_putpixel(x, y + i, colour);
}

/* Filled rectangle (top-left x,y, size w,h), clipped. */
static void tfb_fillrect(int x, int y, int w, int h, u8 colour) {
    int yy; for (yy = 0; yy < h; yy++) tfb_hline(x, y + yy, w, colour);
}

/* Rectangle outline (1px). */
static void tfb_rect(int x, int y, int w, int h, u8 colour) {
    tfb_hline(x, y, w, colour);
    tfb_hline(x, y + h - 1, w, colour);
    tfb_vline(x, y, h, colour);
    tfb_vline(x + w - 1, y, h, colour);
}

/* Clear the whole framebuffer to a pixel value (0 = transparent root). Fast path for 0:
 * zero the char blocks directly; otherwise scanline-fill. */
static void tfb_clear(u8 colour) {
    if ((colour & 15) == 0) {
        volatile u32 *g = (volatile u32 *)M2_CHARGFX;
        int i; for (i = 0; i < TFB_CELLS * 8; i++) g[i] = 0u;
    } else {
        tfb_fillrect(0, 0, TFB_VIS_W, TFB_H, colour);
    }
}

/* A line (Bresenham), width 1 for the thin case; thicker lines plot a (t x t) stamp. */
static void tfb_line(int x0, int y0, int x1, int y1, int t, u8 colour) {
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = (adx > ady ? adx : -ady) / 2, e2;
    if (t < 1) t = 1;
    for (;;) {
        if (t == 1) tfb_putpixel(x0, y0, colour);
        else        tfb_fillrect(x0 - t / 2, y0 - t / 2, t, t, colour);
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -adx) { err -= ady; x0 += sx; }
        if (e2 <  ady) { err += adx; y0 += sy; }
    }
}

/* Integer square root (ellipse extents). */
static u32 tfb_isqrt(u32 v) {
    u32 r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* Filled axis-aligned ellipse centred at (cx,cy), radii rx,ry (scanline fill). */
static void tfb_fillellipse(int cx, int cy, int rx, int ry, u8 colour) {
    int dy, dx;
    if (rx <= 0 || ry <= 0) return;
    for (dy = -ry; dy <= ry; dy++) {
        u32 t = (u32)(rx * rx) * (u32)(ry * ry - dy * dy);
        dx = (int)(tfb_isqrt(t) / (u32)ry);
        tfb_hline(cx - dx, cy + dy, 2 * dx + 1, colour);
    }
}

/* Ellipse outline (1px). */
static void tfb_ellipse(int cx, int cy, int rx, int ry, u8 colour) {
    int dy, dx;
    if (rx <= 0 || ry <= 0) return;
    for (dy = -ry; dy <= ry; dy++) {
        u32 t = (u32)(rx * rx) * (u32)(ry * ry - dy * dy);
        dx = (int)(tfb_isqrt(t) / (u32)ry);
        tfb_putpixel(cx - dx, cy + dy, colour);
        tfb_putpixel(cx + dx, cy + dy, colour);
    }
}

/* Draw one 8x8 glyph from the built-in font (gFont) at (x,y). Font stroke (value 1) is
 * painted in `ink`; the shadow/fill layer (value 2) and empty cells paint `bg` when
 * bg >= 0, else stay transparent. Decodes gFont in the hardware char-RAM order (see the
 * packing note above), matching m2_loadfont's verbatim-copy rendering. */
static void tfb_glyph(int x, int y, u8 ch, u8 ink, int bg) {
    const u8 *g = gFont + (u32)(ch & 0x7f) * 32;
    int py, px;
    for (py = 0; py < 8; py++) {
        for (px = 0; px < 8; px++) {
            u8 gb = g[py * 4 + ((((px >> 1) & 3)) ^ 1)];
            u8 pix = (px & 1) ? (u8)(gb & 0x0f) : (u8)(gb >> 4);
            if (pix == 1)        tfb_putpixel(x + px, y + py, ink);
            else if (bg >= 0)    tfb_putpixel(x + px, y + py, (u8)bg);
        }
    }
}

/* Draw a NUL-terminated string with the built-in 8x8 font, 8px advance per glyph. */
static void tfb_text(int x, int y, const char *s, u8 ink, int bg) {
    int i; for (i = 0; s[i]; i++) tfb_glyph(x + i * 8, y, (u8)s[i], ink, bg);
}

#endif /* M2_TILEFB_H */
