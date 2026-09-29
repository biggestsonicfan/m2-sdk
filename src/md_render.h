/*
 * md_render.h — a line-by-line software renderer of the Mega Drive VDP state in md_hw.h:
 * the reference picture (tools/mdhost.c) that the Model 2 renderer (src/sonic.c) and MAME's
 * are checked against. Far too slow for the i960; host only.
 *
 * Covers what MAME's sega315_5313 render_videoline_to_videobuffer does for a normal game:
 * planes A and B (full / cell / line horizontal scroll, full / 2-cell vertical scroll,
 * 32/64/128-cell planes, tile flips), sprites (link list, 20 a line, masking by x = 0),
 * the layer priorities, the backdrop colour. Not: the window plane, shadow/highlight,
 * interlace, H32 (Sonic runs H40 throughout).
 */
#ifndef MD_RENDER_H
#define MD_RENDER_H

#define MDR_W 320
#define MDR_H 224

static inline u32 mdr_pix(u32 tile, u32 row, u32 col) {      /* a pattern pixel, 0-15 */
    u32 b = (tile & 0x7ff) * 32 + row * 4 + (col >> 1);
    u32 byte = (md_vram[(b & 0xffff) >> 1] >> ((b & 1) ? 0 : 8)) & 0xff;
    return (col & 1) ? byte & 15 : byte >> 4;
}

/* one plane's line into out[] as (pri << 7) | (pal << 4) | pixel; 0 pixel = transparent */
static void mdr_plane(int y, int planeb, u8 *out) {
    static const int sz[4] = { 32, 64, 32, 128 };
    u32 w = (u32)sz[md_reg[16] & 3], h = (u32)sz[(md_reg[16] >> 4) & 3];
    u32 nt = planeb ? (u32)(md_reg[4] & 7) << 13 : (u32)(md_reg[2] & 0x38) << 10;
    u32 hsbase = (u32)(md_reg[13] & 0x3f) << 10, hsa;
    int hs, x;
    switch (md_reg[11] & 3) {
    case 0:  hsa = hsbase; break;
    case 2:  hsa = hsbase + (u32)(y & ~7) * 4; break;
    case 3:  hsa = hsbase + (u32)y * 4; break;
    default: hsa = hsbase + (u32)(y & 7) * 4; break;
    }
    hs = (int)(md_vram[((hsa + (planeb ? 2 : 0)) & 0xffff) >> 1] & 0x3ff);
    for (x = 0; x < MDR_W; x++) {
        u32 vs = (md_reg[11] & 4) ? md_vsram[((x >> 4) * 2 + planeb) % 40] : md_vsram[planeb];
        u32 px = (u32)(x - hs) & (w * 8 - 1), py = ((u32)y + vs) & (h * 8 - 1);
        u16 e = md_vram[((nt + ((py >> 3) * w + (px >> 3)) * 2) & 0xffff) >> 1];
        u32 row = py & 7, col = px & 7, p;
        if (e & 0x1000) row = 7 - row;
        if (e & 0x0800) col = 7 - col;
        p = mdr_pix(e, row, col);
        out[x] = (u8)(p ? ((e >> 15) << 7) | (((e >> 13) & 3) << 4) | p : 0);
    }
}

/* the sprites on line y, first in the list on top */
static void mdr_sprites(int y, u8 *out) {
    u32 base = (u32)(md_reg[5] & 0x7e) << 9, n = 0, count = 0, link = 0, masked = 0, onl = 0;
    int x;
    for (x = 0; x < MDR_W; x++) out[x] = 0;
    do {
        u32 a = base + link * 8;
        u16 w0 = md_vram[(a & 0xffff) >> 1], w1 = md_vram[((a + 2) & 0xffff) >> 1];
        u16 w2 = md_vram[((a + 4) & 0xffff) >> 1], w3 = md_vram[((a + 6) & 0xffff) >> 1];
        int sy = (int)(w0 & 0x3ff) - 128, sx = (int)(w3 & 0x1ff) - 128;
        u32 cw = ((w1 >> 10) & 3) + 1, ch = ((w1 >> 8) & 3) + 1;
        link = w1 & 0x7f;
        if (y >= sy && y < sy + (int)ch * 8) {
            u32 r = (u32)(y - sy), c;
            if (++onl > 20) break;
            if ((w3 & 0x1ff) == 0) { if (onl > 1) masked = 1; }
            else if (!masked) {
                if (w2 & 0x1000) r = ch * 8 - 1 - r;
                for (c = 0; c < cw * 8; c++) {
                    u32 cc = (w2 & 0x0800) ? cw * 8 - 1 - c : c, p;
                    int xx = sx + (int)c;
                    if (xx < 0 || xx >= MDR_W || out[xx]) continue;
                    p = mdr_pix((u32)(w2 & 0x7ff) + (cc >> 3) * ch + (r >> 3), r & 7, cc & 7);
                    if (p) out[xx] = (u8)(((w2 >> 15) << 7) | (((w2 >> 13) & 3) << 4) | p);
                }
            }
        }
        count++;
    } while (link && count < 80 && ++n);
}

/* the whole screen as CRAM indexes (0-63) */
static void mdr_frame(u8 *pic) {
    static u8 a[MDR_W], b[MDR_W], s[MDR_W];
    int y, x;
    u8 bg = md_reg[7] & 0x3f;
    for (y = 0; y < MDR_H; y++) {
        u8 *o = pic + y * MDR_W;
        if (!(md_reg[1] & 0x40)) { for (x = 0; x < MDR_W; x++) o[x] = bg; continue; }
        mdr_plane(y, 1, b); mdr_plane(y, 0, a); mdr_sprites(y, s);
        for (x = 0; x < MDR_W; x++) {
            u8 c = bg;
            /* back to front: B lo, A lo, S lo, B hi, A hi, S hi */
            if (b[x] && !(b[x] & 0x80)) c = b[x] & 0x3f;
            if (a[x] && !(a[x] & 0x80)) c = a[x] & 0x3f;
            if (s[x] && !(s[x] & 0x80)) c = s[x] & 0x3f;
            if (b[x] & 0x80) c = b[x] & 0x3f;
            if (a[x] & 0x80) c = a[x] & 0x3f;
            if (s[x] & 0x80) c = s[x] & 0x3f;
            o[x] = c;
        }
    }
}

/* CRAM entry -> 8-bit RGB (MAME pal3bit) */
static void mdr_rgb(u16 c, u8 *r, u8 *g, u8 *b) {
    u32 R = (c >> 1) & 7, G = (c >> 5) & 7, B = (c >> 9) & 7;
    *r = (u8)((R << 5) | (R << 2) | (R >> 1));
    *g = (u8)((G << 5) | (G << 2) | (G >> 1));
    *b = (u8)((B << 5) | (B << 2) | (B >> 1));
}

#endif /* MD_RENDER_H */
