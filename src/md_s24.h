/*
 * md_s24.h — the Mega Drive picture on the Model 2's System 24 tilemaps (MAME
 * sega/segaic24.cpp, as sega/model2_v.cpp composes it), from the VDP state in md_hw.h.
 *
 * The Model 2 has two scrolling tilemap planes, each 64x64 cells of 8x8 4bpp chars with
 * per-line horizontal scroll: exactly a Mega Drive plane. So
 *   Mega Drive plane B -> tilemap layer 2 (drawn opaque: its pen 0 is the backdrop colour)
 *   Mega Drive plane A -> tilemap layer 0, with the sprites composited into its cells
 * and the hardware does the scrolling. The Mega Drive's 320x224 screen sits at (88,80) in
 * the Model 2's 496x384; the window layers 1 and 3 (selected per 8 pixels by the mask)
 * cover the rest: layer 3 black, layer 1 free for text (s24_text).
 *
 * Chars. A Mega Drive name entry picks a pattern, a flip and a palette line; a System 24
 * entry picks a char, and the char's number fixes its palette bank (bank = char >> 7). So
 * each (pattern, flip, line) the planes use gets its own char, in a 128-char group whose
 * bank holds that line's colours, allocated on first use and redrawn when the pattern
 * changes. A Mega Drive pattern row is already in char-RAM order (the 16-bit words are
 * the VRAM's), so an unflipped char is a straight copy.
 *
 * Sprites. The cells of plane A that sprites cover are replaced, each frame, by composite
 * chars: plane A's pixels with the sprite pixels on top, resolved as the VDP does (first
 * sprite in the list wins; a low-priority sprite pixel stays behind a high-priority plane A
 * pixel). A composite cell mixes palette lines, so its colours go into one of 24 banks
 * shared by cells whose colours fit in 15. The cell keeps one priority bit, so a sprite is
 * wrong only where plane B's high-priority pixels meet it (rare).
 *
 * Not done: the window plane, shadow/highlight, H32, 2-cell vertical scroll, per-line
 * palette changes (Labyrinth Zone's water line), sprite limits per line. Planes are taken
 * as 64x32 cells (Sonic's).
 *
 * Portable: the includer defines S24_TILE / S24_CHAR / S24_PAL as u16 pointers to tile RAM
 * (0x8000 words), char RAM (0x40000 words) and palette RAM (0x1000 words): the hardware
 * on the i960 (src/sonic.c), arrays on the host (tools/mds24.c renders them to check
 * this against src/md_render.h). Include after md_hw.h and m2font.h (gFont).
 */
#ifndef MD_S24_H
#define MD_S24_H

#define S24_X0 88                        /* Mega Drive screen origin on the Model 2 screen */
#define S24_Y0 80
#define S24_LAYER_A   0x0000u            /* tile RAM word offsets of the four layers */
#define S24_LAYER_AW  0x1000u
#define S24_LAYER_B   0x2000u
#define S24_LAYER_BW  0x3000u

#define S24_VAR_GROUPS 96                /* groups 0-95: (pattern, flip, line) chars */
#define S24_CMP_GROUP0 96                /* groups 96-119: composite (sprite) chars */
#define S24_CMP_GROUPS 24
#define S24_UI_GROUP   120               /* the font (m2font gFont) */
#define S24_BLK_GROUP  121               /* its char 0: solid pen 1 (black) */

/* Mega Drive colour -> BGR555 (3 bits -> 5) */
static u16 s24_rgb(u16 cram) {
    static const u8 c5[8] = { 0, 4, 9, 13, 18, 22, 27, 31 };
    return (u16)(c5[(cram >> 1) & 7] | (c5[(cram >> 5) & 7] << 5) | (c5[(cram >> 9) & 7] << 10));
}

/* ---- state --------------------------------------------------------------------------- */
static u16 s24_var[2048][16];            /* char of (pattern, flip * 4 + line) */
static u16 s24_varmask[2048];            /* which of the 16 exist */
static u8  s24_grp_line[128];            /* palette line of a variant group */
static u8  s24_grp_used[128];            /* chars allocated in it */
static u8  s24_line_grp[4];              /* the group each line allocates from, 0xff = none */
static u8  s24_ngroups;                  /* variant groups taken */
static u8  s24_full;                     /* out of chars: start over next frame */
static u16 s24_ntA[2048], s24_ntB[2048]; /* the name entries last written, 0xffff = redo */
static u16 s24_cram[64];                 /* colours last written */
static u8  s24_bg = 0xff;                /* backdrop index last written */
static u8  s24_nib_rev[256];             /* a byte with its two pixels swapped (h-flip) */
static u16 s24_hsA[224], s24_hsB[224];   /* this frame's horizontal scroll per line */
static u16 s24_hsA_w[224], s24_hsB_w[224];   /* ... as last written */
static u8  s24_blank = 0xff;             /* display disabled, as last written */

/* composites: the plane A cells sprites cover this frame */
#define S24_MAXCMP 480
typedef struct {
    u16 cell;                            /* layer A cell (row * 64 + col, rows 0-63) */
    u16 gcell;                           /* the Mega Drive name entry behind it (0-2047) */
    u8  a[64];                           /* plane A pixel: CRAM index | 0x80 priority; 0 = clear */
    u8  s[64];                           /* sprite pixel: likewise */
} s24_cmp_t;
static s24_cmp_t s24_cmp[S24_MAXCMP];
static int s24_ncmp;
static u16 s24_cmp_of[4096];             /* layer A cell -> composite record + 1 */
static u16 s24_prev[S24_MAXCMP];         /* the Mega Drive entries composited last frame */
static int s24_nprev;
static u32 s24_bank_set[S24_CMP_GROUPS][2];  /* CRAM indexes in each composite bank */
static u8  s24_bank_n[S24_CMP_GROUPS];       /* pens used (1-15) */
static u8  s24_bank_slots[S24_CMP_GROUPS];   /* chars used */
static u8  s24_bank_pen[S24_CMP_GROUPS][64]; /* CRAM index -> pen */
static u8  s24_bank_cram[S24_CMP_GROUPS][16];/* pen -> CRAM index */
static u16 s24_bank_w[S24_CMP_GROUPS][16];   /* colours last written */
static int s24_nbanks;
static u8  s24_onl[224], s24_mask[224];      /* sprites on a line so far, line masked */
static u32 s24_ncomposited;                  /* statistics: composite cells, last frame */

/* ---- chars ----------------------------------------------------------------------------- */
static void s24_draw_char(u32 tile, u32 flip, u32 idx) {
    const u16 *src = &md_vram[(tile & 0x7ff) * 16];
    volatile u16 *dst = &S24_CHAR[idx * 16];
    int r;
    for (r = 0; r < 8; r++) {
        const u16 *s = src + ((flip & 2) ? 7 - r : r) * 2;
        if (flip & 1) {
            u16 w0 = s[0], w1 = s[1];
            dst[r * 2]     = (u16)((s24_nib_rev[w1 & 0xff] << 8) | s24_nib_rev[w1 >> 8]);
            dst[r * 2 + 1] = (u16)((s24_nib_rev[w0 & 0xff] << 8) | s24_nib_rev[w0 >> 8]);
        } else {
            dst[r * 2] = s[0]; dst[r * 2 + 1] = s[1];
        }
    }
}

static void s24_bank_colours(u32 g, u32 line) {
    int i;
    S24_PAL[g * 16] = s24_rgb(md_cram[md_reg[7] & 0x3f]);
    for (i = 1; i < 16; i++) S24_PAL[g * 16 + i] = s24_rgb(md_cram[line * 16 + i]);
}

/* the char for a pattern/flip/line, drawn and allocated on first use (0 when out of room) */
static u16 s24_char_of(u32 tile, u32 flip, u32 line) {
    u32 v = flip * 4 + line, g;
    if (s24_varmask[tile] & (1u << v)) return s24_var[tile][v];
    g = s24_line_grp[line];
    if (g == 0xff || s24_grp_used[g] >= 128) {
        if (s24_ngroups >= S24_VAR_GROUPS) { s24_full = 1; return 0; }
        g = s24_ngroups++;
        s24_line_grp[line] = (u8)g;
        s24_grp_line[g] = (u8)line;
        s24_grp_used[g] = g == 0 ? 1 : 0;           /* char 0 stays blank */
        s24_bank_colours(g, line);
    }
    {
        u16 idx = (u16)(g * 128 + s24_grp_used[g]++);
        s24_var[tile][v] = idx;
        s24_varmask[tile] |= (u16)(1u << v);
        s24_draw_char(tile, flip, idx);
        return idx;
    }
}

static inline u16 s24_entry(u16 e) {
    return (u16)((e & 0x8000) | s24_char_of(e & 0x7ff, (e >> 11) & 3, (e >> 13) & 3));
}

/* forget every char (out of room, e.g. after many level loads): all is redrawn */
static void s24_flush(void) {
    int i;
    for (i = 0; i < 2048; i++) { s24_varmask[i] = 0; s24_ntA[i] = 0xffff; s24_ntB[i] = 0xffff; }
    for (i = 0; i < 4; i++) s24_line_grp[i] = 0xff;
    s24_ngroups = 0; s24_full = 0;
}

/* text on layer 1 (the window around the picture): 8x8 cells, col 0-61, row 0-47 */
static void s24_text(int col, int row, const char *s) {
    for (; *s && col < 62; s++, col++)
        S24_TILE[S24_LAYER_AW + row * 64 + col] = (u16)(0x8000 | (S24_UI_GROUP * 128 + (*s & 0x7f)));
}

static void s24_init(void) {
    int i, y;
    for (i = 0; i < 256; i++) s24_nib_rev[i] = (u8)((i >> 4) | ((i & 15) << 4));
    s24_flush();
    for (i = 0; i < 64; i++) s24_cram[i] = 0xffff;
    for (i = 0; i < 224; i++) { s24_hsA_w[i] = 0xffff; s24_hsB_w[i] = 0xffff; }
    for (i = 0; i < 4096; i++) s24_cmp_of[i] = 0;
    for (i = 0; i < S24_CMP_GROUPS * 16; i++) s24_bank_w[i >> 4][i & 15] = 0xffff;
    s24_nprev = 0;
    /* the font (group 120, bank 120: pen 1 white, 2 grey) and the black char (group 121) */
    for (i = 0; i < (int)sizeof(gFont) / 2; i++)
        S24_CHAR[S24_UI_GROUP * 128 * 16 + i] = (u16)(gFont[2 * i] | (gFont[2 * i + 1] << 8));
    for (i = 0; i < 16; i++) S24_CHAR[S24_BLK_GROUP * 128 * 16 + i] = 0x1111;
    for (i = 0; i < 16; i++) { S24_PAL[S24_UI_GROUP * 16 + i] = 0; S24_PAL[S24_BLK_GROUP * 16 + i] = 0; }
    S24_PAL[S24_UI_GROUP * 16 + 1] = 0x7fff;
    S24_PAL[S24_UI_GROUP * 16 + 2] = 0x2108;
    /* layers: A and B blank, the window layers transparent (1) and black (3) */
    for (i = 0; i < 4096; i++) {
        S24_TILE[S24_LAYER_A + i] = 0;
        S24_TILE[S24_LAYER_B + i] = 0;
        S24_TILE[S24_LAYER_AW + i] = (u16)(S24_UI_GROUP * 128 + ' ');
        S24_TILE[S24_LAYER_BW + i] = (u16)(S24_BLK_GROUP * 128);
    }
    /* the masks: the window layers everywhere but the 320x224 picture (cells 11-50) */
    for (y = 0; y < 384; y++) {
        int in = y >= S24_Y0 && y < S24_Y0 + 224;
        u16 m0 = in ? 0xffe0 : 0xffff, m1 = in ? 0 : 0xffff, m3 = in ? 0x1fff : 0xffff;
        S24_TILE[0x6000 + y * 4 + 0] = m0; S24_TILE[0x6000 + y * 4 + 1] = m1;
        S24_TILE[0x6000 + y * 4 + 2] = m1; S24_TILE[0x6000 + y * 4 + 3] = m3;
        S24_TILE[0x6800 + y * 4 + 0] = m0; S24_TILE[0x6800 + y * 4 + 1] = m1;
        S24_TILE[0x6800 + y * 4 + 2] = m1; S24_TILE[0x6800 + y * 4 + 3] = m3;
    }
    /* scroll: layers 0 and 2 per-line (bit 15); windows fixed; all enabled */
    S24_TILE[0x5000] = 0x8000; S24_TILE[0x5001] = 0; S24_TILE[0x5002] = 0x8000; S24_TILE[0x5003] = 0;
    S24_TILE[0x5004] = 0; S24_TILE[0x5005] = 0; S24_TILE[0x5006] = 0; S24_TILE[0x5007] = 0;
    for (i = 0; i < 0x200; i++) { S24_TILE[0x4000 + i] = 0; S24_TILE[0x4400 + i] = 0; }
}

/* ---- composites -------------------------------------------------------------------------- */
static s24_cmp_t *s24_rec(u32 mx, u32 my) {
    u32 cell = (my >> 3) * 64 + (mx >> 3), gcell, e, line, pri, r, c;
    s24_cmp_t *rec;
    u16 k = s24_cmp_of[cell];
    const u16 *pat;
    if (k) return &s24_cmp[k - 1];
    if (s24_ncmp >= S24_MAXCMP) return 0;
    rec = &s24_cmp[s24_ncmp++];
    s24_cmp_of[cell] = (u16)s24_ncmp;
    rec->cell = (u16)cell;
    gcell = ((my >> 3) & 31) * 64 + (mx >> 3);
    rec->gcell = (u16)gcell;
    e = md_vram[((((u32)md_reg[2] & 0x38) << 10) >> 1) + gcell];
    line = (e >> 13) & 3; pri = (e >> 8) & 0x80;
    pat = &md_vram[(e & 0x7ff) * 16];
    for (r = 0; r < 8; r++) {
        const u16 *s = pat + ((e & 0x1000) ? 7 - r : r) * 2;
        u32 row = ((u32)s[0] << 16) | s[1];
        for (c = 0; c < 8; c++) {
            u32 p = (row >> (((e & 0x0800) ? c : 7 - c) * 4)) & 15;
            rec->a[r * 8 + c] = (u8)(p ? pri | (line << 4) | p : 0);
            rec->s[r * 8 + c] = 0;
        }
    }
    return rec;
}

static int s24_popc(u32 v) { int n = 0; while (v) { v &= v - 1; n++; } return n; }

/* rasterize the sprite list into composite records (map space of layer A) */
static void s24_sprites(u32 vsA) {
    u32 base = (u32)(md_reg[5] & 0x7e) << 9, link = 0, count = 0;
    int i;
    for (i = 0; i < 224; i++) { s24_onl[i] = 0; s24_mask[i] = 0; }
    do {
        u32 a = (base + link * 8) & 0xffff;
        u16 w0 = md_vram[a >> 1], w1 = md_vram[(a >> 1) + 1], w2 = md_vram[(a >> 1) + 2], w3 = md_vram[(a >> 1) + 3];
        int sy = (int)(w0 & 0x3ff) - 128, sx = (int)(w3 & 0x1ff) - 128;
        int cw = ((w1 >> 10) & 3) + 1, ch = ((w1 >> 8) & 3) + 1, y0, y1, gy;
        u32 attr = (u32)(w2 & 0xe000) >> 9;   /* priority -> 0x40, line -> 0x30 (>> 9: 0x8000 -> 0x40) */
        link = w1 & 0x7f;
        count++;
        y0 = sy < 0 ? 0 : sy; y1 = sy + ch * 8 > 224 ? 224 : sy + ch * 8;
        for (gy = y0; gy < y1; gy++) {
            int r = gy - sy, c;
            u32 my, hs;
            if (++s24_onl[gy] > 20 || s24_mask[gy]) continue;
            if ((w3 & 0x1ff) == 0) { if (s24_onl[gy] > 1) s24_mask[gy] = 1; continue; }
            if (sx >= 320 || sx + cw * 8 <= 0) continue;
            if (w2 & 0x1000) r = ch * 8 - 1 - r;
            my = ((u32)gy + vsA) & 511;
            hs = s24_hsA[gy];
            for (c = 0; c < cw; c++) {
                int tc = (w2 & 0x0800) ? cw - 1 - c : c, px;
                const u16 *s = &md_vram[(((w2 & 0x7ff) + tc * ch + (r >> 3)) & 0x7ff) * 16 + (r & 7) * 2];
                u32 row = ((u32)s[0] << 16) | s[1];
                if (!row) continue;
                for (px = 0; px < 8; px++) {
                    u32 p = (row >> (((w2 & 0x0800) ? px : 7 - px) * 4)) & 15, mx;
                    int gx = sx + c * 8 + px;
                    s24_cmp_t *rec;
                    if (!p || gx < 0 || gx >= 320) continue;
                    mx = ((u32)gx - hs) & 511;
                    rec = s24_rec(mx, my);
                    if (!rec) continue;
                    {
                        u8 *d = &rec->s[(my & 7) * 8 + (mx & 7)];
                        if (!*d) *d = (u8)(((attr & 0x40) << 1) | (attr & 0x30) | p);
                    }
                }
            }
        }
    } while (link && count < 80);
}

/* resolve the records into chars in the composite banks */
static void s24_composite(void) {
    int i, k, b;
    s24_nbanks = 0;
    for (k = 0; k < S24_CMP_GROUPS; k++) { s24_bank_n[k] = 0; s24_bank_slots[k] = 0;
        s24_bank_set[k][0] = s24_bank_set[k][1] = 0; }
    s24_nprev = 0;
    for (i = 0; i < s24_ncmp; i++) {
        s24_cmp_t *rec = &s24_cmp[i];
        u32 set0 = 0, set1 = 0, cat = 0, p;
        u8 pix[64];
        for (p = 0; p < 64; p++) {
            u8 a = rec->a[p], s = rec->s[p], v;
            v = (s && (!a || !(a & 0x80) || (s & 0x80))) ? s : a;
            pix[p] = v;
            if (v) {
                u32 ci = v & 0x3f;
                if (ci < 32) set0 |= 1u << ci; else set1 |= 1u << (ci - 32);
                cat |= v & 0x80;
            }
        }
        cat |= (u32)(s24_ntA[rec->gcell] & 0x8000) >> 8;
        /* a bank these colours fit in */
        for (b = 0; b < s24_nbanks; b++)
            if (s24_bank_slots[b] < 128 &&
                s24_bank_n[b] + s24_popc(set0 & ~s24_bank_set[b][0]) + s24_popc(set1 & ~s24_bank_set[b][1]) <= 15) break;
        if (b == s24_nbanks) {
            if (s24_nbanks >= S24_CMP_GROUPS) continue;
            s24_nbanks++;
        }
        /* add the new colours as pens */
        for (p = 0; p < 64; p++) {
            u32 ci = pix[p] & 0x3f;
            if (!pix[p]) continue;
            if (ci < 32 ? (s24_bank_set[b][0] >> ci) & 1 : (s24_bank_set[b][1] >> (ci - 32)) & 1) continue;
            if (ci < 32) s24_bank_set[b][0] |= 1u << ci; else s24_bank_set[b][1] |= 1u << (ci - 32);
            s24_bank_n[b]++;
            s24_bank_pen[b][ci] = s24_bank_n[b];
            s24_bank_cram[b][s24_bank_n[b]] = (u8)ci;
        }
        {
            u32 idx = (S24_CMP_GROUP0 + (u32)b) * 128 + s24_bank_slots[b]++, r;
            volatile u16 *d = &S24_CHAR[idx * 16];
            for (r = 0; r < 8; r++) {
                u32 w = 0, c;
                for (c = 0; c < 8; c++) {
                    u8 v = pix[r * 8 + c];
                    w = (w << 4) | (v ? s24_bank_pen[b][v & 0x3f] : 0);
                }
                d[r * 2] = (u16)(w >> 16); d[r * 2 + 1] = (u16)w;
            }
            S24_TILE[S24_LAYER_A + rec->cell] = (u16)((cat << 8) | idx);
            s24_prev[s24_nprev++] = rec->gcell;
        }
    }
    for (b = 0; b < s24_nbanks; b++)
        for (k = 1; k <= s24_bank_n[b]; k++) {
            u16 c = s24_rgb(md_cram[s24_bank_cram[b][k]]);
            if (s24_bank_w[b][k] != c) { s24_bank_w[b][k] = c; S24_PAL[(S24_CMP_GROUP0 + b) * 16 + k] = c; }
        }
    for (i = 0; i < s24_ncmp; i++) s24_cmp_of[s24_cmp[i].cell] = 0;
    s24_ncomposited = (u32)s24_ncmp;
    s24_ncmp = 0;
}

/* ---- one frame -------------------------------------------------------------------------- */
static void s24_update(void) {
    u32 ntA = ((u32)(md_reg[2] & 0x38) << 10) >> 1, ntB = ((u32)(md_reg[4] & 7) << 13) >> 1;
    u32 hsb = ((u32)(md_reg[13] & 0x3f) << 10) >> 1, vsA, vsB, i, bg = md_reg[7] & 0x3f;
    int blank = !(md_reg[1] & 0x40);

    if (s24_full) s24_flush();

    /* colours: the lines' banks, the backdrop (pen 0 of every variant bank) */
    for (i = 0; i < 64; i++) {
        u16 c = md_cram[i];
        if (c == s24_cram[i]) continue;
        s24_cram[i] = c;
        if (i & 15) {
            u32 g, line = i >> 4;
            u16 rgb = s24_rgb(c);
            for (g = 0; g < s24_ngroups; g++)
                if (s24_grp_line[g] == line) S24_PAL[g * 16 + (i & 15)] = rgb;
        }
        if (i == bg) s24_bg = 0xff;
    }
    if (s24_bg != bg) {
        u16 rgb = s24_rgb(md_cram[bg]);
        u32 g;
        for (g = 0; g < s24_ngroups; g++) S24_PAL[g * 16] = rgb;
        s24_bg = (u8)bg;
    }

    /* patterns that changed: redraw their chars */
    if (md_tile_any) {
        u32 w;
        for (w = 0; w < 64; w++) {
            u32 bits = md_tile_dirty[w];
            while (bits) {
                u32 b = 0, t, m;
                while (!((bits >> b) & 1)) b++;
                bits &= ~(1u << b);
                t = w * 32 + b;
                for (m = s24_varmask[t]; m; ) {
                    u32 v = 0;
                    while (!((m >> v) & 1)) v++;
                    m &= ~(1u << v);
                    s24_draw_char(t, v >> 2, s24_var[t][v]);
                }
            }
            md_tile_dirty[w] = 0;
        }
        md_tile_any = 0;
    }

    /* last frame's composite cells go back to plane A's own chars */
    for (i = 0; i < (u32)s24_nprev; i++) s24_ntA[s24_prev[i]] = 0xffff;
    s24_nprev = 0;

    /* name tables: rows 0-31 at rows r and r + 32 of the 64-row layer */
    for (i = 0; i < 2048; i++) {
        u16 e = md_vram[(ntA + i) & 0x7fff];
        if (e != s24_ntA[i]) {
            u16 t = s24_entry(e);
            s24_ntA[i] = e;
            S24_TILE[S24_LAYER_A + i] = t; S24_TILE[S24_LAYER_A + 2048 + i] = t;
        }
        e = md_vram[(ntB + i) & 0x7fff];
        if (e != s24_ntB[i]) {
            u16 t = s24_entry(e);
            s24_ntB[i] = e;
            S24_TILE[S24_LAYER_B + i] = t; S24_TILE[S24_LAYER_B + 2048 + i] = t;
        }
    }

    /* scroll: per line (VDP register 11 modes), onto the per-line tables of layers 0 and 2 */
    for (i = 0; i < 224; i++) {
        u32 o;
        switch (md_reg[11] & 3) {
        case 0:  o = hsb; break;
        case 2:  o = hsb + (i & ~7u) * 2; break;
        case 3:  o = hsb + i * 2; break;
        default: o = hsb + (i & 7) * 2; break;
        }
        s24_hsA[i] = md_vram[o & 0x7fff] & 0x3ff;
        s24_hsB[i] = md_vram[(o + 1) & 0x7fff] & 0x3ff;
        if (s24_hsA[i] != s24_hsA_w[i]) { s24_hsA_w[i] = s24_hsA[i]; S24_TILE[0x4000 + S24_Y0 + i] = (u16)((s24_hsA[i] + S24_X0) & 0x1ff); }
        if (s24_hsB[i] != s24_hsB_w[i]) { s24_hsB_w[i] = s24_hsB[i]; S24_TILE[0x4400 + S24_Y0 + i] = (u16)((s24_hsB[i] + S24_X0) & 0x1ff); }
    }
    vsA = md_vsram[0] & 0x3ff; vsB = md_vsram[1] & 0x3ff;
    if (blank != s24_blank) {
        s24_blank = (u8)blank;
        if (blank) { S24_TILE[0x5004] = 0x8000; S24_TILE[0x5006] = 0x8000; }
    }
    if (!blank) {
        S24_TILE[0x5004] = (u16)((vsA - S24_Y0) & 0x1ff);
        S24_TILE[0x5006] = (u16)((vsB - S24_Y0) & 0x1ff);
        s24_sprites(vsA);
    }
    s24_composite();
}

#endif /* MD_S24_H */
