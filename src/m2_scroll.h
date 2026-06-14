/*
 * m2_scroll.h — Model 2 tile-layer "scroll" CG + pattern loader, layered on m2.h.
 *
 * Ports three STF routines so homebrew can reuse the game's built-in tile artwork
 * straight out of the data ROM (reverse-engineered from sfight via Ghidra):
 *   _ScrollCG_Initialize    @ 0x0002e5ac -> m2_scroll_cg(cg)
 *   _ScrollColor_Initialize @ 0x0002e610 -> m2_scroll_color(cg)
 *   dsp_pattern_new         @ 0x00005df4 -> m2_dsp_pattern(tex, dst)
 *
 * The artwork lives in the STF "main_data" ROM, mapped into the i960 space at
 * 0x02000000+ / 0x06000000+ (MAME model2.cpp). It is present in the sfight romset
 * we run on (we only replace the program EPROMs), so these read the dispatch
 * tables at runtime — no data needs to be embedded:
 *   cg_offsets              @ 0x06480000  (cg  < 0xB8)
 *   texture_palette_offsets @ 0x06480300  (tex < 0x3FF)
 * A "CG set" spans two adjacent slots: tile graphics at [cg], palette at [cg+1]
 * (hence m2_scroll_init calls Color with cg+1, matching _Scroll_Initialize).
 *
 * Include from the SAME .c as m2.h.
 */
#ifndef M2_SCROLL_H
#define M2_SCROLL_H

#include "m2.h"

#define M2_CG_OFFSETS   ((const u32 *)0x06480000u)  /* [cg]  -> tile / palette block list */
#define M2_TEX_OFFSETS  ((const u32 *)0x06480300u)  /* [tex] -> pattern descriptor        */

/* _ScrollCG_Initialize: cg_offsets[cg] = array of {descPtr, dest} pairs (descPtr==0
   terminates). descPtr -> { u32 ntiles; u16 gfx[ntiles*16] }; copy ntiles*16 shorts
   (one 8x8 4bpp tile = 16 shorts) into dest (char-generator RAM, baked in the table). */
static void m2_scroll_cg(u32 cg) {
    const u32 *list;
    if (cg >= 0xB8u) return;
    list = (const u32 *)M2_CG_OFFSETS[cg];
    if (!list) return;
    for (;;) {
        const u32 *desc = (const u32 *)list[0];
        volatile u16 *dst;
        const u16 *src;
        u32 n;
        if (!desc) break;
        dst = (volatile u16 *)list[1];
        list += 2;
        n   = desc[0] << 4;                 /* ntiles * 16 shorts */
        src = (const u16 *)(desc + 1);
        while (n--) *dst++ = *src++;
    }
}

/* _ScrollColor_Initialize: cg_offsets[cg] = {dest, len, word0..wordN, dest2, len2, ...}
   terminated by dest==0. The palette words are stored inline; copy len/2 u32 words to
   each dest (palette RAM). Pass cg+1 of the CG set (see m2_scroll_init). */
static void m2_scroll_color(u32 cg) {
    const u32 *list;
    if (cg >= 0xB8u) return;
    list = (const u32 *)M2_CG_OFFSETS[cg];
    if (!list) return;
    for (;;) {
        volatile u32 *dst = (volatile u32 *)list[0];
        u32 n;
        if (!dst) break;
        n = list[1] >> 1;                   /* len/2 = number of 32-bit words */
        list += 2;
        while (n--) *dst++ = *list++;
    }
}

/* _Scroll_Initialize: load CG set number `cg`. Each set occupies TWO adjacent table
   slots, so CG number N lives at slot 2N (tile graphics) + slot 2N+1 (palette) — e.g.
   CG 84 -> slots 168/169. (The STF wrapper takes the slot and does slot, slot+1; the
   game multiplies the CG number by 2 at the call site, which we fold in here.) */
static void m2_scroll_init(u32 cg) {
    m2_scroll_cg(cg * 2u);
    m2_scroll_color(cg * 2u + 1u);
}

/* dsp_pattern_new: texture_palette_offsets[tex] = { u16 base; u32 rows; u32 cols;
   u16 pat[rows*cols] }. Writes a rows x cols block of tilemap (name-table) entries
   (each = base + pat[i]) starting at dst, with a row stride of 64 entries (the Model 2
   tilemap is 64 tiles wide). dst is a name-table address, e.g. M2_TILE_BG + row*64+col. */
static void m2_dsp_pattern(u32 tex, volatile u16 *dst) {
    const u8 *p;
    const u16 *pat;
    u16 base;
    u32 rows, cols, r, c;
    if (tex >= 0x3ffu) return;
    p    = (const u8 *)M2_TEX_OFFSETS[tex];
    base = *(const u16 *)p;
    rows = *(const u32 *)(p + 4);
    cols = *(const u32 *)(p + 8);
    pat  = (const u16 *)(p + 0xc);
    for (r = 0; r < rows; r++) {
        volatile u16 *row = dst + r * 64u;
        for (c = 0; c < cols; c++) row[c] = (u16)(base + *pat++);
    }
}

/* Draw a pattern as a centered BACKGROUND on the given tile layer. Centers the
   rows x cols block within the visible tile grid (Model 2 = 62 cols x 48 rows of 8x8
   tiles) and clears the per-tile priority bit (0x8000) so the tiles render BEHIND the
   3D polygons: the Model 2 compositor draws bit15=0 tiles before the 3D framebuffer
   (which is copied with a transparent backdrop pen) and bit15=1 tiles after it. */
static void m2_dsp_pattern_bg(u32 tex, volatile u16 *layer_base) {
    const u8 *p;
    const u16 *pat;
    u16 base;
    u32 rows, cols, r, c;
    int c0, r0;
    if (tex >= 0x3ffu) return;
    p    = (const u8 *)M2_TEX_OFFSETS[tex];
    base = *(const u16 *)p;
    rows = *(const u32 *)(p + 4);
    cols = *(const u32 *)(p + 8);
    pat  = (const u16 *)(p + 0xc);
    c0 = (62 - (int)cols) / 2; if (c0 < 0) c0 = 0;
    r0 = (48 - (int)rows) / 2; if (r0 < 0) r0 = 0;
    layer_base += r0 * 64 + c0;
    for (r = 0; r < rows; r++) {
        volatile u16 *row = layer_base + r * 64u;
        for (c = 0; c < cols; c++) row[c] = (u16)((base + *pat++) & 0x7fffu);
    }
}

/* Centered FOREGROUND draw: same as m2_dsp_pattern_bg but sets the priority bit
   (0x8000) so the tiles render IN FRONT of the 3D polygons (e.g. a game-over image). */
static void m2_dsp_pattern_fg(u32 tex, volatile u16 *layer_base) {
    const u8 *p;
    const u16 *pat;
    u16 base;
    u32 rows, cols, r, c;
    int c0, r0;
    if (tex >= 0x3ffu) return;
    p    = (const u8 *)M2_TEX_OFFSETS[tex];
    base = *(const u16 *)p;
    rows = *(const u32 *)(p + 4);
    cols = *(const u32 *)(p + 8);
    pat  = (const u16 *)(p + 0xc);
    c0 = (62 - (int)cols) / 2; if (c0 < 0) c0 = 0;
    r0 = (48 - (int)rows) / 2; if (r0 < 0) r0 = 0;
    layer_base += r0 * 64 + c0;
    for (r = 0; r < rows; r++) {
        volatile u16 *row = layer_base + r * 64u;
        for (c = 0; c < cols; c++) row[c] = (u16)((base + *pat++) | 0x8000u);
    }
}

/* ---- per-scanline horizontal wave (segas24 line-scroll) -------------------- *
 * Tile layer 0's horizontal scroll can be driven per scanline: set bit15 of its
 * hscroll control register, then write a 9-bit x-offset (0..511, wraps the 512px
 * tilemap) per scanline into the line table. Animating a sine down the table makes
 * the layer (e.g. a background pattern) ripple / wave horizontally in place.
 *   M2_WAVE_CTL = tile_ram[0x5000]  (layer 0/1 hscroll ctrl; bit15 => per-line)
 *   M2_WAVE_TBL = tile_ram[0x4000]  (layer 0 per-scanline x-offset table)        */
#define M2_WAVE_CTL  (*(volatile u16 *)0x0100A000u)
#define M2_WAVE_TBL  ((volatile u16 *)0x01008000u)

static void m2_wave_enable(int on) { M2_WAVE_CTL = on ? 0x8000u : 0x0000u; }
static void m2_wave_line(int y, int xoff) { M2_WAVE_TBL[y] = (u16)((u32)xoff & 0x1ffu); }

/* ---- 2x3 tile message font (STF dsp_mes_2x3) ------------------------------- *
 * STF's large message font: each glyph is a 2-tile-wide x 3-tile-tall block. The glyph
 * tile graphics are CG 0 (load with m2_scroll_init(0)); the glyph atlas is texture cell
 * 13 -- texture_palette_offsets[13] -> { u16 base; ...; u16 atlas[] } at offset 0xc, an
 * 8-glyph-wide grid. The ASCII->atlas-slot map (asciihex_to_texindex_table, originally
 * in STF program ROM @0x0008e60c, which homebrew replaces) is embedded below. Each
 * name-table entry is base + atlas tile, forced to the above-3D priority category. */
static u8 m2_font2x3_ti(u8 c) {
    static const u8 tbl[59] = {   /* ASCII 0x20 .. 0x5A */
        0x25,0x1E,0x21,0xFF,0x24,0x33,0x26,0x20,0x35,0x36,0x32,0x27,0xFF,0x1D,0x1B,0x3F,
        0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,0x30,0x31,0x1A,0xFF,0x22,0x34,0x23,0x1F,
        0xFF,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,
        0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19
    };
    if (c < 0x20u || c > 0x5Au) return 0x25u;   /* blank glyph */
    return tbl[c - 0x20u];
}
/* Draw one 2x3 glyph at name-table dst (top-left). */
static void m2_dsp_char_2x3(volatile u16 *dst, u8 ascii) {
    const u8  *desc  = (const u8 *)M2_TEX_OFFSETS[13];   /* cell 13 font descriptor */
    u16        base  = *(const u16 *)desc;
    const u16 *atlas = (const u16 *)(desc + 0xc);
    u32        ti    = m2_font2x3_ti(ascii);
    const u16 *src   = atlas + (ti >> 3) * 48u + (ti & 7u) * 2u;
    int r, c;
    for (r = 0; r < 3; r++)
        for (c = 0; c < 2; c++)
            dst[r * 64 + c] = (u16)((base + src[r * 16 + c]) | 0x8000u);  /* above the 3D */
}
/* Draw a string in the 2x3 font, 2 tiles per glyph, left to right, from dst. */
static void m2_dsp_mes_2x3(volatile u16 *dst, const char *s) {
    for (; *s; s++) { m2_dsp_char_2x3(dst, (u8)*s); dst += 2; }
}

#endif /* M2_SCROLL_H */
