/*
 * m2_tex_codec.h — synchronous port of STF's texture codec (unpack_lod_data Huffman +
 * send_lod_data RLE expand), validated BYTE-EXACT in Python (tools/huff_validate.py +
 * tools/rle_validate.py) against a MAME capture. See memory [[stf-texture-codec]].
 *
 * Decodes a compressed texture PAGE from the texture ROM into a sheet (16-bit texels,
 * 0x400-byte row stride). Drops STF's frame-budget cooperative scheduler — runs to
 * completion in one call. Reads the classify LUT + source straight from the tex ROM
 * (present on the board and in MAME).
 *
 * Stage 1 (unpack_lod_data): Huffman-decode src -> an RLE-intermediate word stream.
 * Stage 2 (send_lod_data):   expand the RLE stream -> the sheet.
 */
#ifndef M2_TEX_CODEC_H
#define M2_TEX_CODEC_H

#define TEX_CLASSIFY_ROM 0x02300010u          /* 256 u32 symbol-classify LUT (ROM) */

/* ---- scratch buffers (kernel .bss) ---- */
static u32 g_texA[2048];      /* off_55C344 node array     */
static u32 g_texB[512];       /* dword_55CD50 value LUT    */
static u32 g_texFast0[256];   /* make_huf fast table word0 */
static u8  g_texFastH[256];   /* fast table handler tag    */
static u16 g_texRLE[16384];   /* RLE intermediate (dword_5502F0) */
static u8  g_texRLEsh[16384]; /* per-RLE-entry 4-bit shade index (parallel to g_texRLE) */
static u32 g_tex_w = 128, g_tex_h = 128;   /* decoded tile dims (cols,rows) — set by tex_decode_into */
static u8  g_tex_shade[16384];   /* per-texel 4-bit shade of the last decoded tile (row-major, cols wide) */
static u8  g_tex_shade2[16384];  /* ping-pong shade buffer for the mip box-filter */

/* ---- LSB-first 16-bit-refill bit reader (primed exactly as @0x4B420) ---- */
typedef struct { const u8 *p; u32 r13; int r14; u32 g11; } texbr_t;

static u32 texbr_u16(texbr_t *b) { u32 v = b->p[0] | ((u32)b->p[1] << 8); b->p += 2; return v; }

static void texbr_init(texbr_t *b, const u8 *src) {
    b->p = src; b->r13 = 0; b->r14 = 0; b->g11 = texbr_u16(b);
    b->r13 |= (b->g11 << 0);    b->g11 = texbr_u16(b); b->r14 = 0x10;   /* prime refill 1 */
    b->r13 |= (b->g11 << 0x10); b->g11 = texbr_u16(b); b->r14 = 0x20;   /* prime refill 2 */
}

static u32 texbr_get(texbr_t *b, int n) {
    u32 val = (n >= 32) ? b->r13 : (b->r13 & ((1u << n) - 1u));
    b->r13 >>= n; b->r14 -= n;
    if (b->r14 <= 0x10) { b->r13 |= (b->g11 << b->r14); b->g11 = texbr_u16(b); b->r14 += 0x10; }
    return val;
}

/* make_huf_8bit (recursive, max depth 8). node = absolute byte addr into g_texA.
 * BASE must be nonzero+positive so table-A pointers (BASE+off) stay >0 and never collide with the
 * <=0 leaf test (a base-0 pointer for v==0x142 would be 0 and misread as a leaf). */
#define TEXBASE 0x0055C344u
#define TEXA_W(addr)  (g_texA[((u32)(addr) - TEXBASE) >> 2])   /* off_55C344[(addr-BASE)/4] */

static void tex_make_huf(int depth, u32 node_addr, u32 prefix) {
    int d1 = depth + 1;
    u32 a0, a1, prefix1;
    int c0, c1;
    if (depth == 8) { g_texFast0[prefix] = node_addr; g_texFastH[prefix] = 0xFF; return; }
    /* bit0 child = node+4 (adjacent word) */
    a0 = node_addr + 4;
    c0 = (int)TEXA_W(a0);
    if (c0 <= 0) {                                   /* leaf (cmpible 0,r3) */
        u32 leaf = ((u32)c0 | ((u32)d1 << 24));
        u32 tag = leaf >> 28, step = 1u << d1, idx = prefix;
        for (; idx < 256; idx += step) { g_texFast0[idx] = leaf; g_texFastH[idx] = (u8)tag; }
    } else { tex_make_huf(d1, a0, prefix); }
    /* bit1 child = *(node) (the stored pointer) */
    a1 = TEXA_W(node_addr);
    prefix1 = prefix | (1u << depth);
    c1 = (int)TEXA_W(a1);
    if (c1 <= 0) {
        u32 leaf = ((u32)c1 | ((u32)d1 << 24));
        u32 tag = leaf >> 28, step = 1u << d1, idx = prefix1;
        for (; idx < 256; idx += step) { g_texFast0[idx] = leaf; g_texFastH[idx] = (u8)tag; }
    } else { tex_make_huf(d1, a1, prefix1); }
}

/* Decode ONE texture (128x128 etc.) into sheet (16-bit texels, 0x400-byte row stride). The bit reader
 * is a STATIC local so the bitstream can be CONTINUED across tiles of one page:
 *   src_addr != 0 starts a new bitstream; src_addr == 0 continues the current one. */
static int tex_decode_into(u32 src_addr, volatile u16 *sheet) {
    static texbr_t br;
    const volatile u32 *classify = (const volatile u32 *)TEX_CLASSIFY_ROM;
    u32 rows, cols, nbitsA, nA, nB, r6, term, nodes, i;
    u32 r12, r8 = 0, r7 = 0, nout = 0;
    int rows_left, cols_left;

    if (src_addr) texbr_init(&br, (const u8 *)src_addr);

    /* ---- header ---- */
    rows   = (texbr_get(&br, 8) + 1) >> 1;
    cols   = (texbr_get(&br, 8) + 1) >> 1;
    (void)(rows * cols);                       /* dword_55C324 (unused here) */
    nbitsA = texbr_get(&br, 8);
    nA     = texbr_get(&br, 16);
    (void)texbr_get(&br, 16);                  /* dword_55C334 */
    nB     = texbr_get(&br, 16);
    r6     = texbr_get(&br, 4);
    term   = texbr_get(&br, 16);
    nodes  = (nA << 1) - 1;
    g_tex_w = cols; g_tex_h = rows;            /* publish tile dims for the mip builder */
    if (nodes > 2048 || nB > 512) return 1;    /* guard scratch buffers */

    /* ---- table-A (classify LUT from ROM) ---- */
    for (i = 0; i < nodes; i++) {
        u32 v = texbr_get(&br, nbitsA), node;
        if (v >= 0x142) {
            node = TEXBASE + ((v - 0x142) << 2);               /* abs ptr (nonzero base) */
        } else if (v < 0x100) {
            u32 e = classify[v];
            node = (e & 0xF) ? (0x80000000u | e) : (0xC0000000u | (e >> 8));
        } else if (v == 0x100) { node = 0x90000000u;
        } else if (v == 0x101) { node = 0xA0000000u;
        } else if (v < 0x122) {
            u32 d = (v - 0x102) + 1, df = (d < 0x11) ? d : ((d - 0x10) << 4);
            node = 0xB0000000u | df;
        } else {
            u32 d = (v - 0x122) + 1, df = (d < 0x11) ? d : ((d - 0x10) << 4);
            node = 0xD0000000u | df;
        }
        g_texA[i] = node;
    }
    /* ---- table-B ---- */
    for (i = 0; i < nB; i++) { u32 hi = texbr_get(&br, 16), lo = texbr_get(&br, 4); g_texB[i] = (hi << 8) | lo; }

    /* ---- make_huf ---- */
    tex_make_huf(0, TEXBASE, 0);

    /* ---- decode loop -> RLE stream g_texRLE[] ---- */
    r12 = (1u << r6) - 1u;
    rows_left = (int)rows; cols_left = (int)cols;
    for (;;) {
        u32 g0 = br.r13 & 0xFF, fw0 = g_texFast0[g0];
        u8 handler = g_texFastH[g0];
        int g4 = (int)fw0;
        if (g4 >= 0) {
            /* long walk: g4 is an internal node ptr (code > 8 bits) */
            u32 g3 = (u32)g4; int r4 = 8;
            for (;;) {
                int nw = (int)TEXA_W(g3);
                u32 bit = (br.r13 >> r4) & 1u; r4++;
                /* gcc960 -O2 miscompiles this tree-walk (hoists/fuses the per-iter
                 * br.r13 read + nw sign test), making the walk resolve to the wrong
                 * leaf -> every code >8 bits decodes as a literal. This barrier forces
                 * nw/bit/g3 to materialize each iteration. Do NOT remove. */
                /* gcc960 (gcc 2.x) has no "+r" read-write modifier; express it as matched
                 * "=r" outputs with "0"/"1"/"2" tied inputs (same materialize-each-iter barrier). */
                __asm__ __volatile__("" : "=r"(nw), "=r"(bit), "=r"(g3)
                                       : "0"(nw), "1"(bit), "2"(g3) : "memory");
                if (bit) { if (nw >= 0) { g3 = (u32)nw; continue; } else { g4 = nw; break; } }
                else     { if (nw >= 0) { g3 = g3 + 4;  continue; } else { g4 = nw; break; } }
            }
            handler = (u8)(((u32)g4) >> 28);
            texbr_get(&br, r4 - 1);
        } else {
            u32 length = (((u32)g4) >> 24) & 0xF;
            texbr_get(&br, length);
            /* handler already = g_texFastH[g0] (= tag) */
        }

        /* dispatch (tags: 8=H8,9=H9,0xA=HA,0xB=HB,0xC/0xD=HC). NUM[0..7] never occur. */
        if (handler == 9) {                              /* loc_4BA2C */
            u32 idx = br.r13 & r12; texbr_get(&br, r6);
            g4 = (int)g_texB[idx]; handler = 8;          /* fallthrough */
        }
        if (handler == 8) {                              /* loc_4BA5C */
            r8 = (r8 + (u32)g4) & 0xF; r7 = (r8 * 0x1111u) & 0xFFFF;
            g4 = (int)(((u32)g4) >> 8); handler = 0xC;   /* fallthrough */
        }
        if (handler == 0xC || handler == 0xD) {          /* loc_4BA70 literal texel */
            g_texRLEsh[nout] = (u8)r8;
            g_texRLE[nout++] = (u16)(((u32)g4 + r7) & 0xFFFF);
            if (--cols_left == 0) goto row_end;
        } else if (handler == 0xB) {                     /* sub_4BAE8 run */
            u32 g1 = ((u32)g4) & 0xFF;                    /* shlo 0x18,g4 / shro 0x18 = LOW byte (run len) */
            g_texRLEsh[nout] = (u8)r8;
            g_texRLE[nout++] = (u16)(term & 0xFFFF);
            cols_left -= (int)g1;
            g_texRLEsh[nout] = (u8)r8;                    /* run shade carried on the run word */
            g_texRLE[nout++] = (u16)(((r7 << 8) | ((u32)g4 & 0xFF)) & 0xFFFF);
            if (cols_left == 0) goto row_end;
        } else if (handler == 0xA) {                     /* sub_4BB30 literal2 */
            u32 g2 = texbr_get(&br, 16), g1 = texbr_get(&br, 4);
            r8 = (r8 + g1) & 0xF; r7 = (r8 * 0x1111u) & 0xFFFF;
            g_texRLEsh[nout] = (u8)r8;
            g_texRLE[nout++] = (u16)((g2 + r7) & 0xFFFF);
            if (--cols_left == 0) goto row_end;
        }
        if (nout >= 16384 - 2) break;
        continue;
    row_end:
        if (--rows_left <= 0) break;
        cols_left = (int)cols;
        if (nout >= 16384 - 2) break;
    }

    /* ---- stage 2: expand the RLE stream to the sheet (send_lod_data) ---- */
    {
        u32 si = 0, row; u16 g2; u8 shc;
        if (nout == 0) return 0;
        shc = g_texRLEsh[si]; g2 = g_texRLE[si]; si++;
        for (row = 0; row < rows; row++) {
            volatile u16 *dst = sheet + (row * (0x400u / 2u));   /* 0x400-byte stride */
            u8 *shd = g_tex_shade + row * cols;                  /* parallel shade row (cols wide) */
            int n = (int)cols;
            for (;;) {
                n--;
                if (g2 == (u16)term) {                  /* run */
                    u16 g1 = g_texRLE[si]; u8 rsh = g_texRLEsh[si];
                    u16 val; int rl, k;                  /* gcc960 C89: all decls before stmts */
                    si++;
                    val = (g1 >> 8) & 0xFF; val |= (val << 8);
                    rl = g1 & 0xFF;
                    n += 1; n -= rl;
                    for (k = 0; k < (rl >= 1 ? rl : 1); k++) { *dst++ = val; *shd++ = rsh; }
                    shc = g_texRLEsh[si]; g2 = g_texRLE[si]; si++;
                    if (n > 0) continue; else break;
                } else {                                /* literal */
                    *dst++ = g2; *shd++ = shc;
                    shc = g_texRLEsh[si]; g2 = g_texRLE[si]; si++;
                    if (n > 0) continue; else break;
                }
                if (si >= nout) break;
            }
            if (si >= nout) break;
        }
    }
    return 0;
}

/* Back-compat: decode a single texture starting at src_addr. */
static int tex_decode_page(u32 src_addr, volatile u16 *sheet) {
    return tex_decode_into(src_addr, sheet);
}

/* ===== send_tex_* descriptor walk (RE'd: unp_send_tex_para_sub @0x4AF34 + sub_4C444 @0x4C444) =====
 * A texture PAGE is a list of tiles. Each tile has an HDR word g2 (-> grid coords via word_4B394 LUT)
 * and its own compressed DATA block pointer. Per tile: decode LOD0 to its sheet dest, then build a
 * mip pyramid into the +0xC0000 mip region (the model-1 faces sample MIP levels, not LOD0). */

#define TEXRAM_0_1 0x11100000u
#define TEXRAM_1   0x11300000u

/* word_4B394 @0x4B394: 24 (texx,texy) tile-origin pairs (validated). */
static const u16 g_tex_lutx[24] = {0,256,512,768, 0,256,512,768, 0,256,512,768,
                                   0,256,512,768, 0,256, 0,256, 0,256, 0,256};
static const u16 g_tex_luty[24] = {0,0,0,0, 256,256,256,256, 512,512,512,512,
                                   768,768,768,768, 1024,1024, 1280,1280, 1536,1536, 1792,1792};

/* sub_4C444: fill mip[0..9] with absolute byte dests. parity picks the LOD0 bank (and banks
 * ping-pong each level); +0xC0000 = mip region; coords halve+floor-even per level. */
static void tex_mip_dests(u32 texx, u32 texy, u32 parity, u32 mip[10]) {
    u32 r9, r10, r3, g6 = texx, g7 = texy, r6 = 0, r7 = 0, t, tmp;
    int i, r8;
    if (parity & 1u) { r9 = TEXRAM_0_1; r10 = TEXRAM_1; }
    else             { r9 = TEXRAM_1;   r10 = TEXRAM_0_1; }
    if (!((texy >> 10) & 1u)) {                       /* common case */
        r3 = (texx << 9) + texy; mip[0] = r9 + r3; tmp = r9; r9 = r10; r10 = tmp;
    } else if (!((texx >> 9) & 1u)) {                 /* texy page-carry */
        r3 = ((0x400u + texx) << 9) + (texy & ~0x400u); mip[0] = r9 + r3; tmp = r9; r9 = r10; r10 = tmp;
    } else {                                          /* both-high: skip LOD0 store */
        g7 = (texy & ~0x400u) << 1; g6 = (texx & ~0x200u) << 1; mip[0] = 0;
    }
    r9 += 0xC0000u; r10 += 0xC0000u;
    for (i = 1, r8 = 9; i < 10; i++, r8--) {
        g6 >>= 1; g7 >>= 1; g6 &= ~1u; g7 &= ~1u;
        r3 = ((r6 + g6) << 9) + (r7 + g7);
        mip[i] = r9 + r3; tmp = r9; r9 = r10; r10 = tmp;
        t = 1u << r8; r7 += t; r6 += t >> 1;
    }
}

/* Build mip levels 1..9 (send_lod_data_q + sub_norm, RE'd byte-exact). Each level 2x2 box-filters
 * the previous level's 4-bit shade buffer: the mip TEXEL packs the four raw shade nibbles
 * (TL<<12|TR<<8|BL<<4|BR) — a shade-derived format distinct from the base sheet (value+shade*0x1111);
 * the per-output averaged shade ((TL+TR+BL+BR)>>2) feeds the next level. dims from the last decode. */
static void tex_build_mips(const u32 mip[10]) {
    u32 w = g_tex_w, h = g_tex_h, lvl, oy, ox;
    u8 *src = g_tex_shade, *dsh = g_tex_shade2, *t;
    for (lvl = 1; lvl < 10; lvl++) {
        u32 ow = w >> 1, oh = h >> 1;
        if (ow == 0 || oh == 0 || mip[lvl] == 0) break;
        { volatile u16 *d = (volatile u16 *)mip[lvl];
          for (oy = 0; oy < oh; oy++) {
              for (ox = 0; ox < ow; ox++) {
                  u32 a = src[(2u*oy)    * w + 2u*ox];        /* TL */
                  u32 b = src[(2u*oy)    * w + 2u*ox + 1u];   /* TR */
                  u32 c = src[(2u*oy+1u) * w + 2u*ox];        /* BL */
                  u32 e = src[(2u*oy+1u) * w + 2u*ox + 1u];   /* BR */
                  d[oy * 0x200u + ox] = (u16)((a<<12) | (b<<8) | (c<<4) | e);
                  dsh[oy * ow + ox]   = (u8)((a + b + c + e) >> 2);
              }
          } }
        t = src; src = dsh; dsh = t;                          /* ping-pong shade buffers */
        w = ow; h = oh;
    }
}

/* Walk a page's descriptor and load every tile (LOD0 + mips) to its placed dest.
 * mode = the request-slot mode word r7 (parity = (g2 ^ mode) & 1). */
static void tex_load_atlas(u32 page, u32 mode) {
    u32 HDR_TABLE = *(volatile u32 *)0x0230000Cu;
    u32 DATA_TABLE = *(volatile u32 *)0x02300008u;
    u32 P = *(volatile u32 *)(HDR_TABLE + page * 4u);     /* page HDR block */
    u32 s = *(volatile u32 *)P;                           /* sub-index into DATA_TABLE */
    const volatile u32 *dptr = (const volatile u32 *)(P + 4u);   /* per-tile DATA block ptrs */
    u32 D = *(volatile u32 *)(DATA_TABLE + s * 4u);
    u32 count = *(volatile u32 *)D;
    const volatile u32 *hdrw = (const volatile u32 *)(D + 4u);   /* per-tile HDR words (g2)  */
    u32 i, mip[10];
    if (count > 24u) count = 24u;
    for (i = 0; i < count; i++) {
        u32 g2 = hdrw[i], blk = dptr[i], idx, texx, texy, parity;
        if (blk == 0u) continue;
        idx = (g2 >> 1) & 0x7FFFu; if (idx > 23u) continue;
        texx = (u32)g_tex_lutx[idx] + (g2 >> 24);
        texy = (u32)g_tex_luty[idx] + ((g2 >> 16) & 0xFFu);
        parity = (g2 ^ mode) & 1u;
        tex_mip_dests(texx, texy, parity, mip);
        if (mip[0] == 0u) continue;                       /* both-high tile: skip for now */
        tex_decode_page(blk + 4u, (volatile u16 *)mip[0]);/* LOD0 (data starts after the flag word) */
        tex_build_mips(mip);
    }
}

#endif /* M2_TEX_CODEC_H */
