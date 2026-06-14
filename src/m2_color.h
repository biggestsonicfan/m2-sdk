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

#define M2_PALRAM    0x01800000u
#define M2_COLORXLAT 0x01810000u   /* R @+0x0000, G @+0x4000, B @+0x8000 (u16)   */
#define M2_LUMARAM   0x11400000u
#define M2_ROM_LUMA_BLOCKS 0x020d0008u   /* STF data ROM: luma block count        */
#define M2_ROM_LUMA_SRC    0x020d000cu   /* STF data ROM: luma byte ramp           */

/* chg_pol_color_req formula (STF @0x32a8): per component c, per luma L (1..63):
 *   step=(c*MUL*28)/18; accum+=step; out=(GAIN*min(ADD+(accum>>8),255))>>7.
 * Monitor params solved from STF's table: MUL=54, ADD=22, GAIN=128. Luma 64+ is
 * left to m2_init's 2D identity (the tile/text colour lookup). */
static void m2__fill_colorxlat(volatile u16 *chan) {
    u32 c, L;
    for (c = 0; c <= 0x1fu; c++) {
        int step = (int)((c * 54u * 28u) / 18u), accum = 0, v;
        volatile u16 *row = chan + c * 256u;
        row[0] = 0;
        for (L = 1u; L <= 0x3fu; L++) {
            accum += step;
            v = 22 + (accum >> 8);
            if (v > 255) v = 255;
            row[L] = (u16)((128 * v) >> 7);
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
    m2__fill_colorxlat((volatile u16 *)(M2_COLORXLAT + 0x0000u));
    m2__fill_colorxlat((volatile u16 *)(M2_COLORXLAT + 0x4000u));
    m2__fill_colorxlat((volatile u16 *)(M2_COLORXLAT + 0x8000u));
    m2__build_lumaram();
    ((volatile u16 *)M2_PALRAM)[0] = 0u;     /* pen 0 = black backdrop */
}

/* Assign a polygon colorbase slot a BGR555 hue (read at palram[colorbase+0x1000]). */
static void m2_setcolor(u32 colorbase, u16 bgr555) {
    ((volatile u16 *)M2_PALRAM)[(colorbase + 0x1000u) & 0xffffu] = bgr555;
}

#endif /* M2_COLOR_H */
