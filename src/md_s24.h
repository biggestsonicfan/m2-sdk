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
#define S24_POOL0      96                /* groups 96-119: composite (sprite) chars, handed */
#define S24_POOL       24                /* out each frame to a line or to a mixed bank */
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
static u16 s24_ntA[4096], s24_ntB[4096]; /* the name entries last written, 0xffff = redo */
static u8  s24_pw, s24_ph;               /* plane size in cells (32 or 64) as last mapped */
static u32 s24_ntA_at = 0xffffffffu, s24_ntB_at;  /* ... and the name tables' VRAM words */
static u8  s24_nt_all = 1;               /* redo every entry (not only the written ones) */
static u16 s24_cram[64];                 /* colours last written */
static u8  s24_bg = 0xff;                /* backdrop index last written */
static u8  s24_nib_rev[256];             /* a byte with its two pixels swapped (h-flip) */
static u16 s24_hsA[224], s24_hsB[224];   /* this frame's horizontal scroll per line */
static u16 s24_hsA_w[224], s24_hsB_w[224];   /* ... as last written */
static u8  s24_blank = 0xff;             /* display disabled, as last written */

/* composites: the plane A cells sprites cover this frame, as rows of eight 4-bit pixels
 * (the leftmost in bits 31-28, as a pattern row) */
#define S24_MAXCMP 1024
typedef struct {
    u16 cell;                            /* layer A cell (row * 64 + col, rows 0-63) */
    u16 gcell;                           /* the Mega Drive name entry behind it (0-2047) */
    u8  aline, apri;                     /* plane A's palette line, priority (0x80) */
    u32 a[8];                            /* plane A's pixels */
    u32 s[8], sm[8], sl[8], sp[8];       /* sprite pixels; which are set (0xf each); their
                                          * lines (0-3 each); which are high priority */
} s24_cmp_t;
static s24_cmp_t s24_cmp[S24_MAXCMP];
static int s24_ncmp;
static u16 s24_cmp_of[4096];             /* layer A cell -> composite record + 1 */
static u16 s24_prev[S24_MAXCMP];         /* the Mega Drive entries composited last frame */
static int s24_nprev;
static u32 s24_bank_set[S24_POOL][2];    /* mixed banks: the CRAM indexes in it */
static u8  s24_bank_n[S24_POOL];         /* pens used (1-15) */
static u8  s24_bank_slots[S24_POOL];     /* chars used (any pool group) */
static u8  s24_bank_pen[S24_POOL][64];   /* CRAM index -> pen */
static u8  s24_bank_cram[S24_POOL][16];  /* pen -> CRAM index */
static u16 s24_bank_w[S24_POOL][16];     /* colours last written */
static u8  s24_pool_line[S24_POOL];      /* the line a pool group holds, 0xff = mixed */
static u8  s24_bank_list[S24_POOL];      /* this frame's mixed banks */
static int s24_npool, s24_nbanks;        /* pool groups taken this frame, mixed banks */
static int s24_lgrp[4];                  /* the pool group each line fills, -1 = none */
static u8  s24_onl[224], s24_mask[224];      /* sprites on a line so far, line masked */
static u32 s24_ncomposited, s24_nmixed;      /* statistics: composite cells, mixed ones */

/* a row of eight pixels mirrored */
static inline u32 s24_rev32(u32 r) {
    return ((u32)s24_nib_rev[r & 0xff] << 24) | ((u32)s24_nib_rev[(r >> 8) & 0xff] << 16)
         | ((u32)s24_nib_rev[(r >> 16) & 0xff] << 8) | s24_nib_rev[r >> 24];
}
/* 0xf in each nibble that is not 0 */
static inline u32 s24_opaque(u32 r) {
    r |= r >> 1; r |= r >> 2;
    return (r & 0x11111111u) * 15u;
}

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
    for (i = 0; i < 2048; i++) s24_varmask[i] = 0;
    for (i = 0; i < 4096; i++) { s24_ntA[i] = 0xffff; s24_ntB[i] = 0xffff; }
    s24_nt_all = 1;
    for (i = 0; i < 4; i++) s24_line_grp[i] = 0xff;
    for (i = 0; i < S24_VAR_GROUPS; i++) s24_grp_line[i] = 0xff;
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
    for (i = 0; i < S24_POOL * 16; i++) s24_bank_w[i >> 4][i & 15] = 0xffff;
    for (i = 0; i < S24_POOL; i++) s24_pool_line[i] = 0xff;
    s24_nprev = 0;
    for (i = S24_VAR_GROUPS; i < 128; i++) s24_grp_line[i] = 0xff;
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
static __attribute__((noinline)) s24_cmp_t *s24_rec(u32 cell, u32 grow) {
    u32 gcell = (grow & (s24_ph - 1)) * s24_pw + (cell & (s24_pw - 1)), e, r;
    s24_cmp_t *rec;
    const u16 *pat;
    if (s24_ncmp >= S24_MAXCMP) return 0;
    rec = &s24_cmp[s24_ncmp++];
    s24_cmp_of[cell] = (u16)s24_ncmp;
    rec->cell = (u16)cell;
    rec->gcell = (u16)gcell;
    e = md_vram[((((u32)md_reg[2] & 0x38) << 10) >> 1) + gcell];
    rec->aline = (u8)((e >> 13) & 3); rec->apri = (u8)((e >> 8) & 0x80);
    pat = &md_vram[(e & 0x7ff) * 16];
    for (r = 0; r < 8; r++) {
        const u16 *p = pat + ((e & 0x1000) ? 7 - r : r) * 2;
        u32 row = ((u32)p[0] << 16) | p[1];
        rec->a[r] = (e & 0x0800) ? s24_rev32(row) : row;
        rec->s[r] = rec->sm[r] = rec->sl[r] = rec->sp[r] = 0;
    }
    return rec;
}

/* put a sprite row's pixels (R, set in M) into row y of a cell, where no earlier sprite is */
static inline void s24_put(u32 cell, u32 grow, u32 y, u32 R, u32 M, u32 L, u32 P) {
    u16 k = s24_cmp_of[cell];
    s24_cmp_t *rec = k ? &s24_cmp[k - 1] : s24_rec(cell, grow);
    u32 f;
    if (!rec) return;
    f = M & ~rec->sm[y];
    rec->s[y] |= R & f; rec->sm[y] |= f; rec->sl[y] |= L & f; rec->sp[y] |= P & f;
}

/* rasterize the sprite list into composite records (layer A's map space), a row of eight
 * pixels at a time */
static __attribute__((noinline)) void s24_sprites(u32 vsA) {
    u32 base = (u32)(md_reg[5] & 0x7e) << 9, link = 0, count = 0;
    int i;
    for (i = 0; i < 224; i++) { s24_onl[i] = 0; s24_mask[i] = 0; }
    do {
        u32 a = (base + link * 8) & 0xffff;
        u16 w0 = md_vram[a >> 1], w1 = md_vram[(a >> 1) + 1], w2 = md_vram[(a >> 1) + 2], w3 = md_vram[(a >> 1) + 3];
        int sy = (int)(w0 & 0x3ff) - 128, sx = (int)(w3 & 0x1ff) - 128;
        int cw = ((w1 >> 10) & 3) + 1, ch = ((w1 >> 8) & 3) + 1, y0, y1, gy;
        u32 L = 0x11111111u * ((w2 >> 13) & 3), P = (w2 & 0x8000) ? 0xffffffffu : 0;
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
                int tc = (w2 & 0x0800) ? cw - 1 - c : c, gx = sx + c * 8;
                const u16 *s = &md_vram[(((w2 & 0x7ff) + tc * ch + (r >> 3)) & 0x7ff) * 16 + (r & 7) * 2];
                u32 row = ((u32)s[0] << 16) | s[1], M, mx, sh, cell;
                if (!row || gx <= -8 || gx >= 320) continue;
                if (w2 & 0x0800) row = s24_rev32(row);
                M = s24_opaque(row);
                if (gx < 0) M &= 0xffffffffu >> (-gx * 4);                /* clip left */
                if (gx > 312) M &= 0xffffffffu << ((gx - 312) * 4);      /* clip right */
                mx = ((u32)gx - hs) & 511;
                sh = mx & 7;
                cell = (my >> 3) * 64 + (mx >> 3);
                if (!sh) { s24_put(cell, my >> 3, my & 7, row, M, L, P); continue; }
                if (M >> (sh * 4))
                    s24_put(cell, my >> 3, my & 7, row >> (sh * 4), M >> (sh * 4), L >> (sh * 4), P >> (sh * 4));
                if (M << ((8 - sh) * 4))
                    s24_put((my >> 3) * 64 + (((mx >> 3) + 1) & 63), my >> 3, my & 7,
                            row << ((8 - sh) * 4), M << ((8 - sh) * 4), L << ((8 - sh) * 4), P << ((8 - sh) * 4));
            }
        }
    } while (link && count < 80);
}

static int s24_popc(u32 v) { int n = 0; while (v) { v &= v - 1; n++; } return n; }

/* resolve the records into chars: a cell showing one palette line goes into that line's
 * composite groups as it is; a mixed one gets its colours a bank of up to 15 */
static __attribute__((noinline)) void s24_composite(void) {
    int i, k, b, j;
    s24_nbanks = 0; s24_npool = 0;
    for (k = 0; k < 4; k++) s24_lgrp[k] = -1;
    s24_nprev = 0; s24_nmixed = 0;
    for (i = 0; i < s24_ncmp; i++) {
        s24_cmp_t *rec = &s24_cmp[i];
        u32 out[8], lin[8], y, cat = rec->apri, AL = 0x11111111u * rec->aline, mixed = 0, idx;
        u32 anyl = 0xffffffffu, L0 = 0;
        for (y = 0; y < 8; y++) {
            u32 am = s24_opaque(rec->a[y]);
            /* a sprite pixel shows unless plane A there is opaque and high and it is low */
            u32 win = rec->sm[y] & (rec->apri ? (~am | rec->sp[y]) : 0xffffffffu);
            out[y] = (rec->s[y] & win) | (rec->a[y] & ~win);
            lin[y] = (rec->sl[y] & win) | (AL & ~win);
            if (win & rec->sp[y]) cat = 0x80;
            /* the lines of the opaque pixels shown */
            {
                u32 om = s24_opaque(out[y]), l = lin[y] & om;
                if (om) {
                    if (anyl == 0xffffffffu) { u32 n = 0; while (!((om >> (n * 4)) & 15)) n++; L0 = (l >> (n * 4)) & 3; anyl = 0x11111111u * L0; }
                    if ((l ^ (anyl & om)) != 0) mixed = 1;
                }
            }
        }
        if (!mixed) {
            u32 L = anyl == 0xffffffffu ? rec->aline : L0;
            int g = s24_lgrp[L];
            if (g < 0 || s24_bank_slots[g] >= 128) {        /* a pool group for this line */
                if (s24_npool >= S24_POOL) continue;
                g = s24_npool++;
                s24_bank_slots[g] = 0;
                s24_lgrp[L] = g;
                if (s24_pool_line[g] != L) {
                    s24_pool_line[g] = (u8)L;
                    s24_grp_line[S24_POOL0 + g] = (u8)L;
                    s24_bank_colours(S24_POOL0 + (u32)g, L);
                    for (k = 0; k < 16; k++) s24_bank_w[g][k] = 0xffff;
                }
            }
            idx = (S24_POOL0 + (u32)g) * 128 + s24_bank_slots[g]++;
            {
                volatile u16 *d = &S24_CHAR[idx * 16];
                for (y = 0; y < 8; y++) { d[y * 2] = (u16)(out[y] >> 16); d[y * 2 + 1] = (u16)out[y]; }
            }
        } else {
            u32 set0 = 0, set1 = 0, x;
            s24_nmixed++;
            for (y = 0; y < 8; y++)
                for (x = 0; x < 32; x += 4) {
                    u32 p = (out[y] >> x) & 15, ci;
                    if (!p) continue;
                    ci = ((lin[y] >> x) & 3) * 16 + p;
                    if (ci < 32) set0 |= 1u << ci; else set1 |= 1u << (ci - 32);
                }
            for (j = 0; j < s24_nbanks; j++) {
                b = s24_bank_list[j];
                if (s24_bank_slots[b] < 128 &&
                    s24_bank_n[b] + s24_popc(set0 & ~s24_bank_set[b][0]) + s24_popc(set1 & ~s24_bank_set[b][1]) <= 15) break;
            }
            if (j == s24_nbanks) {                          /* a pool group as a new bank */
                if (s24_npool >= S24_POOL) continue;
                b = s24_npool++;
                s24_bank_list[s24_nbanks++] = (u8)b;
                s24_bank_slots[b] = 0; s24_bank_n[b] = 0; s24_bank_set[b][0] = s24_bank_set[b][1] = 0;
                s24_pool_line[b] = 0xff; s24_grp_line[S24_POOL0 + b] = 0xff;
            }
            for (k = 0; k < 64; k++) {
                u32 ci = (u32)k, in = ci < 32 ? (set0 >> ci) & 1 : (set1 >> (ci - 32)) & 1;
                if (!in) continue;
                if (ci < 32 ? (s24_bank_set[b][0] >> ci) & 1 : (s24_bank_set[b][1] >> (ci - 32)) & 1) continue;
                if (ci < 32) s24_bank_set[b][0] |= 1u << ci; else s24_bank_set[b][1] |= 1u << (ci - 32);
                s24_bank_n[b]++;
                s24_bank_pen[b][ci] = s24_bank_n[b];
                s24_bank_cram[b][s24_bank_n[b]] = (u8)ci;
            }
            idx = (S24_POOL0 + (u32)b) * 128 + s24_bank_slots[b]++;
            {
                volatile u16 *d = &S24_CHAR[idx * 16];
                for (y = 0; y < 8; y++) {
                    u32 w = 0;
                    for (x = 0; x < 32; x += 4) {
                        u32 p = (out[y] >> (28 - x)) & 15;
                        w = (w << 4) | (p ? s24_bank_pen[b][((lin[y] >> (28 - x)) & 3) * 16 + p] : 0);
                    }
                    d[y * 2] = (u16)(w >> 16); d[y * 2 + 1] = (u16)w;
                }
            }
        }
        S24_TILE[S24_LAYER_A + rec->cell] = (u16)((cat << 8) | idx);
        s24_prev[s24_nprev++] = rec->gcell;
    }
    for (j = 0; j < s24_nbanks; j++) {
        b = s24_bank_list[j];
        for (k = 1; k <= s24_bank_n[b]; k++) {
            u16 c = s24_rgb(md_cram[s24_bank_cram[b][k]]);
            if (s24_bank_w[b][k] != c) { s24_bank_w[b][k] = c; S24_PAL[(S24_POOL0 + b) * 16 + k] = c; }
        }
    }
    for (i = 0; i < s24_ncmp; i++) s24_cmp_of[s24_cmp[i].cell] = 0;
    s24_ncomposited = (u32)s24_ncmp;
    s24_ncmp = 0;
}

/* a name entry of plane A (layer 0) or B (layer 2), onto every copy of its cell */
static void s24_nt(u32 layer, u16 *shadow, u32 base, u32 i) {
    u16 e = md_vram[(base + i) & 0x7fff], t;
    u32 x, y, pw = s24_pw, ph = s24_ph, gx = i & (pw - 1), gy = i >> (pw == 64 ? 6 : 5);
    if (e == shadow[i]) return;
    shadow[i] = e;
    t = s24_entry(e);
    for (y = gy; y < 64; y += ph) for (x = gx; x < 64; x += pw) S24_TILE[layer + y * 64 + x] = t;
}

/* ---- one frame -------------------------------------------------------------------------- */
static __attribute__((noinline)) void s24_update(void) {
    u32 ntA = ((u32)(md_reg[2] & 0x38) << 10) >> 1, ntB = ((u32)(md_reg[4] & 7) << 13) >> 1;
    u32 hsb = ((u32)(md_reg[13] & 0x3f) << 10) >> 1, vsA, vsB, i, bg = md_reg[7] & 0x3f;
    int blank = !(md_reg[1] & 0x40);
    u32 nd[64];

    if (s24_full) s24_flush();

    /* colours: the lines' banks, the backdrop (pen 0 of every variant bank) */
    for (i = 0; i < 64; i++) {
        u16 c = md_cram[i];
        if (c == s24_cram[i]) continue;
        s24_cram[i] = c;
        if (i & 15) {
            u32 g, line = i >> 4;
            u16 rgb = s24_rgb(c);
            for (g = 0; g < 128; g++)
                if (s24_grp_line[g] == line) S24_PAL[g * 16 + (i & 15)] = rgb;
        }
        if (i == bg) s24_bg = 0xff;
    }
    if (s24_bg != bg) {
        u16 rgb = s24_rgb(md_cram[bg]);
        u32 g;
        for (g = 0; g < 128; g++) if (s24_grp_line[g] != 0xff) S24_PAL[g * 16] = rgb;
        s24_bg = (u8)bg;
    }

    /* the name tables' 32-byte chunks the game wrote (VRAM marks, taken before the
     * pattern pass below clears them) */
    {
        u32 w;
        for (w = 0; w < 64; w++) nd[w] = md_tile_any ? md_tile_dirty[w] : 0;
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

    /* name tables: a 32- or 64-cell plane repeated over the 64x64 layer */
    {
        u32 pw = (md_reg[16] & 3) == 0 ? 32 : 64, ph = ((md_reg[16] >> 4) & 3) == 0 ? 32 : 64, n = pw * ph, c;
        if (pw != s24_pw || ph != s24_ph || ntA != s24_ntA_at || ntB != s24_ntB_at) {
            s24_pw = (u8)pw; s24_ph = (u8)ph; s24_ntA_at = ntA; s24_ntB_at = ntB;
            for (i = 0; i < 4096; i++) { s24_ntA[i] = 0xffff; s24_ntB[i] = 0xffff; }
            s24_nt_all = 1;
        }
        /* last frame's composite cells go back to plane A's own chars */
        for (i = 0; i < (u32)s24_nprev; i++) { s24_ntA[s24_prev[i]] = 0xffff; s24_nt(S24_LAYER_A, s24_ntA, ntA, s24_prev[i]); }
        s24_nprev = 0;
        for (c = 0; c < n / 16; c++) {                   /* 16 entries a chunk */
            u32 ka = (ntA * 2 >> 5) + c, kb = (ntB * 2 >> 5) + c, k;
            if (s24_nt_all || ((nd[(ka >> 5) & 63] >> (ka & 31)) & 1))
                for (k = c * 16; k < c * 16 + 16; k++) s24_nt(S24_LAYER_A, s24_ntA, ntA, k);
            if (s24_nt_all || ((nd[(kb >> 5) & 63] >> (kb & 31)) & 1))
                for (k = c * 16; k < c * 16 + 16; k++) s24_nt(S24_LAYER_B, s24_ntB, ntB, k);
        }
        s24_nt_all = 0;
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
