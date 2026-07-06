/*
 * m2_color.h — Model 2B GPU colour-table setup for the m2 SDK.
 *
 * Regenerates the colour-translate (colorxlat) and luminance (lumaram) tables the
 * board's 3D rasterizer needs, ported from Sonic the Fighters' i960 builders
 * (reverse-engineered in Ghidra). The final pixel colour is:
 *     colorbase -> palram[colorbase+0x1000] -> colorxlat[component][luma] -> gamma
 * so a game picks a per-polygon `colorbase` and sets palram[colorbase+0x1000] to the
 * BGR555 hue it wants. Call m2_color_init() once after m2_init(), then set your
 * palette entries with m2_setcolor().  Needs m2.h first (u8/u16/u32).
 */
#ifndef M2_COLOR_H
#define M2_COLOR_H

#include "m2_constants.h"   /* M2_PALRAM / M2_COLORXLAT / M2_LUMARAM (the hardware map) */

#define M2_ROM_LUMA_BLOCKS 0x020d0008u   /* STF data ROM: luma block count        */
#define M2_ROM_LUMA_SRC    0x020d000cu   /* STF data ROM: luma byte ramp           */

/* colorxlat builder — FAITHFUL port of STF chg_pol_color_req (@0x32a8) + the
 * scatter half of chg_pol_color_send (@0x3454), folded into a direct plane fill.
 *
 * Verified byte-for-byte against the i960 disasm (not the old approximation):
 *   - 27 "passes": pass = 1..0x1B  (build outer: cmpinco 0x1B / bg).
 *   - 48 entries/pass: 1 leading BLACK entry + 47 ramp (inner: addo 0x1F,0x10=0x2F).
 *   - per pass: stepC = (MUL_C * pass * 0x1C) / 0x12 ; accum += stepC each entry.
 *   - out = (accum>>8) + ADD_C ; if that is >= 0x100 the cell SATURATES to 0xFFFF
 *     (subo 1,0 -> 0xFFFFFFFF, *GAIN(128) >>7, stos -> 0xFFFF), else GAIN=128 makes
 *     (v*128)>>7 an identity so out is the byte value directly.
 *   - send scatters staging pass p -> plane row p*256 (shlo 9,1 = 0x200 bytes), for
 *     p = 0..27; p==0 is the leading zero block -> row 0 is BLACK.
 * Monitor SRAM defaults are equal across R/G/B (TST_*_MUL=54, TST_*_ADD=22,
 * RED/GREEN/BLUE gain=128 -> check_sram_all/main), so the three planes are identical.
 * Cells STF never writes (row 0 entries past 47, rows 28..255, entries 48..255) are
 * left to m2_init's 2D identity fill (the tile/text colour lookup at luma 64+). */
#define M2_CX_MUL  54u
#define M2_CX_ADD  22u
static u16 m2__cx_cell(u32 pass, u32 entry) {
    u32 step  = (M2_CX_MUL * pass * 0x1Cu) / 0x12u;
    u32 accum = step * entry;               /* == repeated accum += step (step const) */
    u32 v     = (accum >> 8) + M2_CX_ADD;
    if (v >= 0x100u) return 0xFFFFu;        /* STF saturation value (not 255) */
    return (u16)v;                          /* GAIN=128 -> (v*128)>>7 = v */
}
static void m2__fill_colorxlat(void) {
    volatile u16 *R = (volatile u16 *)(M2_COLORXLAT + 0x0000u);
    volatile u16 *G = (volatile u16 *)(M2_COLORXLAT + 0x4000u);
    volatile u16 *B = (volatile u16 *)(M2_COLORXLAT + 0x8000u);
    u32 pass, e, row;
    for (e = 0u; e < 48u; e++) { R[e] = 0u; G[e] = 0u; B[e] = 0u; }   /* row 0 = black */
    for (pass = 1u; pass <= 0x1Bu; pass++) {
        row = pass * 256u;
        R[row] = 0u; G[row] = 0u; B[row] = 0u;                        /* entry 0 = black */
        for (e = 1u; e <= 0x2Fu; e++) {                              /* entries 1..47 */
            u16 v = m2__cx_cell(pass, e);
            R[row + e] = v; G[row + e] = v; B[row + e] = v;
        }
    }
}

/* chg_scr_color_req builder — FAITHFUL port of STF chg_scr_color_req (@0x31BC, "from Virtua
 * Fighter 2 source"), verified byte-for-byte against the i960 disasm. This is the SECOND
 * COLORXLAT fill STF runs at init (right before chg_pol_color_req, init caller @0x6A50): the
 * stage/screen BRIGHTNESS ramp. It writes entries 64..127 of rows 0..31 in all three planes
 * (R 0x1810000 / G 0x1814000 / B 0x1818000) — cells m2__fill_colorxlat (entries 0..47) never
 * touches, so this is purely ADDITIVE and cannot regress the existing render.
 *
 * Per STF: r4=STAGE_PALETTE_DATA; for row r5=0..0x1F it advances r4 by 0x80 (pre) + 0x80 (64
 * shorts) + 0x100 (post) = 0x200/row, so row r writes u16 indices [r*256+64 .. r*256+127].
 * The value is per-ROW only (same across all 64 entries of a row):
 *     idx8 = r * TST_B_BRIGHT
 *     per channel: if idx8==0 -> 0; else t=((MUL*idx8)>>8)+ADD; if t<0x100 keep else 0xFFFF
 * (threshold is 1<<8=0x100; saturate value 0xFFFF = STF's `subo 1,0` stored as a short). The
 * MUL/ADD constants are the SAME TST_*_MUL/ADD (54/22) chg_pol_color_req uses, identical R/G/B.
 *
 * TST_B_BRIGHT (STF work RAM 0x50023A) is the test-menu SCREEN-BRIGHTNESS setting, loaded from
 * backup SRAM @0x59C35F at boot and CLAMPED to 0..0x1F (STF @0x5D018). We have no STF SRAM, so
 * it is a compile-time constant here. To confirm the exact factory value, dump byte 0x50023A
 * from STF in MAME after init; 0x1F (max) yields a full 22..224 ramp with no saturation. */
#ifndef M2_SCR_BRIGHT
#define M2_SCR_BRIGHT 0x1Fu     /* TST_B_BRIGHT, clamp 0..0x1F (see note above) */
#endif
static u16 m2__scr_cell(u32 row) {
    u32 idx8 = row * (u32)M2_SCR_BRIGHT;        /* r8 = r5 * TST_B_BRIGHT */
    u32 t;
    if (idx8 == 0u) return 0u;                  /* cmpobe 0,r10 -> leaves 0 */
    t = ((M2_CX_MUL * idx8) >> 8) + M2_CX_ADD;
    if (t >= 0x100u) return 0xFFFFu;            /* cmpobl r10,0x100 fails -> subo 1,0 -> short 0xFFFF */
    return (u16)t;
}
static void m2__fill_scr_colorxlat(void) {
    volatile u16 *R = (volatile u16 *)(M2_COLORXLAT + 0x0000u);
    volatile u16 *G = (volatile u16 *)(M2_COLORXLAT + 0x4000u);
    volatile u16 *B = (volatile u16 *)(M2_COLORXLAT + 0x8000u);
    u32 row, e, base;
    for (row = 0u; row <= 0x1Fu; row++) {       /* 32 rows (r5 = 0..0x1F) */
        u16 v = m2__scr_cell(row);              /* identical R/G/B (TST_*_MUL/ADD equal) */
        base = row * 256u + 64u;                /* entries 64..127 (STF +0x80 byte = +0x40 u16) */
        for (e = 0u; e < 64u; e++) {            /* inner loop = 0x40 entries */
            R[base + e] = v; G[base + e] = v; B[base + e] = v;
        }
    }
}

/* essential_color_handling (STF @0x11dc8): luma_ram = data-ROM byte ramp -> u16. */
static void m2__build_lumaram(void) {
    volatile u8  *src = (volatile u8 *)M2_ROM_LUMA_SRC;
    volatile u16 *dst = (volatile u16 *)M2_LUMARAM;
    u32 n = (*(volatile u32 *)M2_ROM_LUMA_BLOCKS) * 0x80u, i;
    for (i = 0; i < n; i++) dst[i] = (u16)src[i];
}

/* Set up the 3D colour pipeline. Call once after m2_init(). */
static void m2_color_init(void) {
    m2__fill_colorxlat();      /* chg_pol_color_req: entries 0..47, rows pass*256 (R/G/B)   */
    m2__fill_scr_colorxlat();  /* chg_scr_color_req: entries 64..127, rows 0..31 brightness  */
    m2__build_lumaram();
    ((volatile u16 *)M2_PALRAM)[0] = 0u;        /* pen 0 = black backdrop                          */
    ((volatile u16 *)M2_PALRAM)[1] = 0xFFFFu;   /* pen 1 = white: STF's stable STAGE high word     */
}

/* Assign a polygon colorbase slot a BGR555 hue (read at palram[colorbase+0x1000]).
 * colorbase 0 maps to "Palette 0", which Polygon Color RAM (Fig 4-4) reserves as a space that
 * "cannot be used" — writing it never displays, so reject it rather than silently no-op on silicon. */
static void m2_setcolor(u32 colorbase, u16 bgr555) {
    if ((colorbase & 0x3ffu) == 0u) return;     /* Palette 0 reserved (Fig 4-4) */
    ((volatile u16 *)M2_PALRAM)[(colorbase + 0x1000u) & 0xffffu] = (u16)(bgr555 | M2_PAL_SET);
}

/* init_pol_color (STF @0x11c24): load STF's full 1024-colour polygon colorbase
 * palette from the data ROM into palram[0x1000..0x13ff]. Use this (instead of
 * per-slot m2_setcolor) when drawing STF model-table objects that reference the
 * game's own colorbase indices — e.g. textured models. Needs the STF data ROMs. */
#define M2_ROM_POLY_PALETTE 0x02100000u   /* STF data ROM: 1024 BGR555 colorbase colours */
static void m2_load_poly_palette(void) {
    volatile u16 *src = (volatile u16 *)M2_ROM_POLY_PALETTE;
    volatile u16 *dst = ((volatile u16 *)M2_PALRAM) + 0x1000u;
    u32 i;
    for (i = 0; i < 1024u; i++) dst[i] = src[i];
}

#endif /* M2_COLOR_H */
