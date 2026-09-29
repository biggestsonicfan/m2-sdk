/*
 * pacman_hw.h — the Namco Pac-Man board (MAME namco/pacman.cpp), rebuilt for the i960:
 * memory map, IRQ, inputs and the tile/sprite video, on the Z80 core in m2_z80.h.
 *
 * Portable C (no Model 2 registers here): the video renders into pac_fb, a 28x36-cell
 * shadow of the PORTRAIT screen (224x288, the monitor is rotated) laid out exactly like
 * Model 2 char RAM (8x8 cells, 4bpp, the byte-swapped nibble order of m2_tilefb.h), so the
 * Model 2 side (src/pacman.c) only copies the cells listed in pac_clist[] into char RAM.
 * tools/pachost.c runs the same code on the host and writes a PPM.
 *
 * Needs, before including: u8/u16/u32 types and the ROM data (see tools/pacrom.py):
 *   pac_rom[0x4000]    6e 6f 6h 6j program       pac_tiles[0x1000]   5e char gfx
 *   pac_sprites[0x1000] 5f sprite gfx             pac_prom_pal[32]    7f palette PROM
 *   pac_prom_lut[256]  4a colour lookup PROM      pac_prom_snd[256]   1m WSG waveforms
 *
 * References are MAME namco/pacman.cpp (map, inputs) and namco/pacman_v.cpp (video):
 * the tilemap scan (pacman_scan_rows), sprite placement and the gfx layouts below.
 * Sound: the WSG registers are captured in pac_snd[] for the host to play (src/pacman.c
 * sends them to the SCSP). Not emulated: flip screen / cocktail, the watchdog.
 */
#ifndef PACMAN_HW_H
#define PACMAN_HW_H

#define PAC_W      224                 /* portrait pixels */
#define PAC_H      288
#define PAC_TW     28                  /* portrait cells */
#define PAC_TH     36
#define PAC_CELLS  (PAC_TW * PAC_TH)   /* 1008 */
#define PAC_CYCLES_PER_FRAME 50688     /* 3.072 MHz / 60.606 Hz: 384 x 264 pixels at 6.144 MHz */

/* ---- board state ----------------------------------------------------------- */
static u8  pac_ram[0x1000];            /* 0x4000-0x4FFF: video, colour, (gap), work+sprite RAM */
static u8  pac_spr_xy[16];             /* 0x5060-0x506F sprite coordinates (write-only) */
static u8  pac_irq_mask, pac_vector;
static u8  pac_irq_line;                /* INT: raised at vblank if enabled, held until the latch clears it */
static u8  pac_in0 = 0xff, pac_in1 = 0xff;   /* active-low, set by the host each frame */
static u8  pac_dsw1 = 0xc9;            /* MAME defaults: 1 coin 1 credit, 3 lives, bonus 10000 */
static u8  pac_snd[32];                /* WSG registers 0x5040-0x505F (4 bits each) */
static u8  pac_snd_on;                 /* 0x5001 latch bit: sound enable */
static u8  pac_latch;                  /* 0x5000-0x5007 74LS259 outputs, bit n = 0x500n (lockstep) */

/* ---- video state ----------------------------------------------------------- */
static u32 pac_fb[PAC_CELLS][8];       /* portrait screen, Model 2 char-RAM format */
static u8  pac_tile_dirty[PAC_CELLS];  /* tile/colour changed: redraw the cell from VRAM */
static u16 pac_tlist[PAC_CELLS];       /* ... the same cells as a list */
static int pac_tn;
static u8  pac_copy[PAC_CELLS];        /* cell changed in pac_fb: the host copies it out */
static u16 pac_clist[PAC_CELLS];       /* ... as a list, emptied by pac_copy_done() */
static int pac_cn;
static u16 pac_offs2cell[1024];        /* VRAM offset -> portrait cell (0xffff = offscreen) */
static u16 pac_cell2offs[PAC_CELLS];
static u16 pac_tilepat[256][8];        /* per char: 8 portrait rows, 2bpp, px0 in bits 15..14 */
static u8  pac_sprpat[64][16][16];     /* per sprite: native orientation pixels 0..3 */
static u16 pac_lut4[64][256];          /* colour c: 4 x 2bpp pixels -> 4 x 4bpp nibbles */
static u8  pac_colour[64][4];          /* colour c, pixel p -> palette index 0..15 */
static u16 pac_spr_cells[16 * 9];      /* cells the sprites covered in the last drawn frame */
static int pac_spr_ncells;
static u8  pac_spr_mark[PAC_CELLS];    /* == pac_spr_stamp: cell already in the current list */
static u8  pac_spr_stamp;

static inline void pac_mark_tile(int c) {
    if (!pac_tile_dirty[c]) { pac_tile_dirty[c] = 1; pac_tlist[pac_tn++] = (u16)c; }
}
static inline void pac_mark_copy(int c) {
    if (!pac_copy[c]) { pac_copy[c] = 1; pac_clist[pac_cn++] = (u16)c; }
}
/* the host has copied every cell in pac_clist out */
static inline void pac_copy_done(void) {
    while (pac_cn) pac_copy[pac_clist[--pac_cn]] = 0;
}

/* ---- bus ------------------------------------------------------------------- */
static inline __attribute__((always_inline)) u8 pac_rd(u16 a) {
    a &= 0x7fff;                                    /* A15 is not decoded */
    if (a < 0x4000) return pac_rom[a];
    if (a < 0x5000) return (a >= 0x4800 && a < 0x4c00) ? 0xbf : pac_ram[a - 0x4000];
    switch (a & 0x50c0) {
    case 0x5000: return pac_in0;
    case 0x5040: return pac_in1;
    case 0x5080: return pac_dsw1;
    default:     return 0xff;                       /* DSW2 */
    }
}

/* everything but work RAM: out of line, so the many write sites (and the statically
 * recompiled code) stay small; video/colour RAM marks dirty cells, I/O latches */
static __attribute__((noinline)) void pac_wr_slow(u16 a, u8 v) {
    a &= 0x7fff;
    if (a < 0x4000) return;
    if (a < 0x5000) {
        a -= 0x4000;
        if (a < 0x800) {                            /* video (0x000) / colour (0x400) RAM */
            u16 c;
            if (pac_ram[a] == v) return;
            c = pac_offs2cell[a & 0x3ff];
            if (c != 0xffff) pac_mark_tile(c);
        }
        pac_ram[a] = v;
        return;
    }
    if (a >= 0x5060 && a < 0x5070) { pac_spr_xy[a & 15] = v; return; }
    if ((a & 0xffe0) == 0x5040) { pac_snd[a & 31] = v & 15; return; }   /* Namco WSG */
    if ((a & 0xffc0) == 0x5000)                         /* the latch: IRQ, sound, flip, lamps, coins */
        pac_latch = (u8)((pac_latch & ~(1u << (a & 7))) | ((v & 1u) << (a & 7)));
    if ((a & 0xffc7) == 0x5000) {                       /* 0x5000 latch bit 0: IRQ enable */
        pac_irq_mask = v & 1;
        if (!pac_irq_mask) pac_irq_line = 0;            /* MAME pacman irq_mask_w: CLEAR_LINE */
    }
    if ((a & 0xffc7) == 0x5001) pac_snd_on = v & 1;     /* 0x5001 latch bit 1: sound enable */
    /* 0x5003 flip, 0x50C0 watchdog: ignored */
}

static inline __attribute__((always_inline)) void pac_wr(u16 a, u8 v) {
    if ((a & 0x7c00) == 0x4c00) { pac_ram[a & 0x0fff] = v; return; }   /* work RAM + stack */
    pac_wr_slow(a, v);
}

#define Z80_RD(a)     pac_rd(a)
#define Z80_WR(a, v)  pac_wr(a, v)
#define Z80_IN(p)     ((void)(p), 0xff)
#define Z80_OUT(p, v) ((void)(p), pac_vector = (v)) /* any port: the IM 2 vector latch */
/* INT as MAME's pacman driver drives it: asserted at vblank while the latch enables it,
 * held (not cleared by the acknowledge) until the game clears the latch; the vector is
 * whatever OUT (0) last wrote, read at acknowledge (pacman_state::interrupt_vector_r) */
#define Z80_EXT_IRQ     pac_irq_line
#define Z80_EXT_IRQ_VEC pac_vector
/* every accepted IRQ: the CPU state as it's taken (tools/lockstep compares it with MAME's) */
static u32 pac_irq_count;
static void pac_irq_snap(const void *zp);   /* after m2_z80.h: it needs z80_t */
#define Z80_IRQ_HOOK(zp) pac_irq_snap(zp)
#define Z80_FETCH(a)  (((a) & 0x7fff) < 0x4000 ? pac_rom[(a) & 0x3fff] : pac_rd(a))
/* idle skip: the game's main loop spins on its task queue (`ld hl,(nn); ld a,(hl);
 * and a; jp m,loop`, 0x238D in pacman/puckman) until the vblank IRQ queues work; once
 * it loops, nothing changes before that IRQ, so fast-forward it to the slice end. */
static int pac_idle_pc = -1;           /* found by pac_find_idle(); -1 = none */
#define PAC_IDLE_LOOP_CYCLES 37         /* ld hl,(nn) 16 + ld a,(hl) 7 + and a 4 + jp m 10 */
/* skip whole passes of the loop (it only reads) and run the last one normally, so the IRQ
 * interrupts it at the same instruction as a CPU that looped all the way (lockstep-exact
 * with MAME, return address on the stack included) */
static u16 pac_idle_ptr;                /* the loop's `ld hl,(nn)` operand: the queue pointer */
/* only while the queue really is empty: an IRQ between `ld a,(hl)` and `jp m` can queue a
 * task, and the jump is then taken on the stale value; the loop finds it on its next pass */
static inline int pac_idle_empty(void) {
    u16 slot = (u16)(pac_rd(pac_idle_ptr) | (pac_rd((u16)(pac_idle_ptr + 1)) << 8));
    return (pac_rd(slot) & 0x80) != 0;
}
#define Z80_JP_TAKEN(t) do { if ((int)(t) == pac_idle_pc && z80.cycles > PAC_IDLE_LOOP_CYCLES \
        && pac_idle_empty()) \
        { int n_ = (z80.cycles - 1) / PAC_IDLE_LOOP_CYCLES;  /* passes of 4 ops, 4 M1s each */ \
          z80.cycles -= n_ * PAC_IDLE_LOOP_CYCLES; z80.rr = (z80_u8)(z80.rr + 4 * n_); } } while (0)
#include "m2_z80.h"
/* `used`: only tools/lockstep reads it (from outside), so GCC would drop it and its stores */
static z80_t pac_irq_regs __attribute__((used));
_Static_assert(sizeof(z80_t) % 4 == 0, "pac_irq_snap copies whole words");
/* word by word through a volatile pointer: a plain copy becomes a memcpy call, and this
 * freestanding build has no memcpy (m2-pacman issue #2) */
static void pac_irq_snap(const void *zp) {
    const u32 *s = (const u32 *)zp;
    volatile u32 *d = (volatile u32 *)&pac_irq_regs;
    unsigned i;
    for (i = 0; i < sizeof pac_irq_regs / 4; i++) d[i] = s[i];
    pac_irq_count++;
}
/* PAC_RECOMP names a header from tools/z80recomp.py (e.g. "pacman_recomp.h"): the traced
 * ROM code statically recompiled into z80_run_rc, which then replaces z80_run. */
#ifdef PAC_RECOMP
#include PAC_RECOMP
#define PAC_Z80_RUN z80_run_rc
#else
#define PAC_Z80_RUN z80_run
#endif

/* ---- decode ---------------------------------------------------------------- */
/* MAME gfx_layout bit n: byte n/8, mask 0x80 >> (n%8); planes {0,4}: plane 0 is the MSB */
static int pac_gfxbit(const u8 *base, int n) { return (base[n >> 3] >> (7 - (n & 7))) & 1; }
static int pac_gfxpix(const u8 *base, int n) { return (pac_gfxbit(base, n) << 1) | pac_gfxbit(base, n + 4); }

/* nibble position of pixel x (0..7) in a char-RAM row word (see m2_tilefb.h packing) */
static inline int pac_nibshift(int x) { return x < 4 ? 12 - 4 * x : 44 - 4 * x; }

static void pac_video_init(void) {
    static const u8 txoff[8]  = { 64, 65, 66, 67, 0, 1, 2, 3 };
    static const u16 sxoff[16] = { 64, 65, 66, 67, 128, 129, 130, 131,
                                   192, 193, 194, 195, 0, 1, 2, 3 };
    int c, x, y, p, b, tx, ty;

    /* palette index per colour/pixel: the 4a lookup PROM, low nibble (palette bank 0) */
    for (c = 0; c < 64; c++)
        for (p = 0; p < 4; p++) pac_colour[c][p] = pac_prom_lut[c * 4 + p] & 0x0f;
    for (c = 0; c < 64; c++)
        for (b = 0; b < 256; b++)
            pac_lut4[c][b] = (u16)((pac_colour[c][(b >> 6) & 3] << 12) | (pac_colour[c][(b >> 4) & 3] << 8)
                                 | (pac_colour[c][(b >> 2) & 3] << 4) |  pac_colour[c][b & 3]);

    /* chars, rotated into portrait: portrait (x',y') = native (x = y', y = 7 - x') */
    for (c = 0; c < 256; c++)
        for (y = 0; y < 8; y++) {
            u16 row = 0;
            for (x = 0; x < 8; x++)
                row |= (u16)(pac_gfxpix(pac_tiles + c * 16, txoff[y] + (7 - x) * 8) << (14 - 2 * x));
            pac_tilepat[c][y] = row;
        }
    for (c = 0; c < 64; c++)
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
                pac_sprpat[c][y][x] = (u8)pac_gfxpix(pac_sprites + c * 64,
                                                     sxoff[x] + (y & 7) * 8 + (y >> 3) * 256);

    /* pacman_scan_rows: native tilemap is 36 cols x 28 rows; portrait tx = 27-row, ty = col */
    for (x = 0; x < 1024; x++) pac_offs2cell[x] = 0xffff;
    for (ty = 0; ty < PAC_TH; ty++)
        for (tx = 0; tx < PAC_TW; tx++) {
            int row = (27 - tx) + 2, col = ty - 2, offs;
            if (col & 0x20) offs = row + ((col & 0x1f) << 5);
            else            offs = col + (row << 5);
            pac_cell2offs[ty * PAC_TW + tx] = (u16)offs;
            pac_offs2cell[offs] = (u16)(ty * PAC_TW + tx);
        }
    for (c = 0; c < PAC_CELLS; c++) pac_mark_tile(c);
}

/* palette PROM entry i (0..15) -> 8-bit RGB (MAME's resistor weights 1000/470/220 ohm) */
static void pac_palette_rgb(int i, u8 *r, u8 *g, u8 *b) {
    u8 v = pac_prom_pal[i];
    *r = (u8)(((v >> 0) & 1) * 0x21 + ((v >> 1) & 1) * 0x47 + ((v >> 2) & 1) * 0x97);
    *g = (u8)(((v >> 3) & 1) * 0x21 + ((v >> 4) & 1) * 0x47 + ((v >> 5) & 1) * 0x97);
    *b = (u8)(((v >> 6) & 1) * 0x51 + ((v >> 7) & 1) * 0xae);
}

/* find the idle loop by its bytes, so any set built on this code gets the skip */
static void pac_find_idle(void) {
    int i;
    pac_idle_pc = -1;
    for (i = 0; i + 8 <= 0x4000; i++)
        if (pac_rom[i] == 0x2a && pac_rom[i + 3] == 0x7e && pac_rom[i + 4] == 0xa7
            && pac_rom[i + 5] == 0xfa && (pac_rom[i + 6] | (pac_rom[i + 7] << 8)) == i) {
            pac_idle_pc = i;
            pac_idle_ptr = (u16)(pac_rom[i + 1] | (pac_rom[i + 2] << 8));
            return;
        }
}

static void pac_reset(void) {
    pac_video_init();
    pac_find_idle();
    pac_irq_mask = 0; pac_vector = 0; pac_irq_line = 0; pac_latch = 0;
    pac_spr_ncells = 0;
    z80_reset();
    /* Lockstep with MAME's pacman driver (tools/lockstep): its vblank IRQ is taken at the
     * first instruction boundary at or after 1 cycle before each 50688-cycle frame edge;
     * one cycle of debt at reset puts every slice end, and so the IRQ, exactly there */
    z80.cycles = -1;
}

/* ---- render ---------------------------------------------------------------- */
static void pac_draw_cell(int cell) {
    u16 offs = pac_cell2offs[cell];
    const u16 *pat = pac_tilepat[pac_ram[offs]];
    const u16 *lut = pac_lut4[pac_ram[0x400 + offs] & 0x1f];
    u32 *d = pac_fb[cell];
    int y;
    for (y = 0; y < 8; y++) d[y] = (u32)lut[pat[y] >> 8] | ((u32)lut[pat[y] & 0xff] << 16);
    pac_mark_copy(cell);
}

/* sprite n with its top-left at native (sx,sy), clipped to the sprite area */
static void pac_draw_sprite(int n, int sx, int sy) {
    u8 attr = pac_ram[0xff0 + 2 * n];
    const u8 *col = pac_colour[pac_ram[0xff1 + 2 * n] & 0x1f];
    int code = attr >> 2, fx = attr & 1, fy = (attr >> 1) & 1;
    int i0 = 16 - sx, i1 = PAC_H - 16 - sx, j0 = -sy, j1 = PAC_W - sy, i, j;
    /* portrait rows py = sx+i must be in 16..271 (spriteclip rows 2..33), columns
     * px = 223-(sy+j) in 0..223 */
    if (i0 < 0) i0 = 0;
    if (i1 > 16) i1 = 16;
    if (j0 < 0) j0 = 0;
    if (j1 > 16) j1 = 16;
    if (i0 >= i1 || j0 >= j1 || !(col[1] | col[2] | col[3])) return;
    for (j = j0; j < j1; j++) {
        int px = PAC_W - 1 - (sy + j), s = pac_nibshift(px & 7);
        u32 keep = ~(0xfu << s);
        const u8 *src = pac_sprpat[code][fy ? 15 - j : j];
        for (i = i0; i < i1; i++) {
            int py = sx + i, cell;
            u8 ci = col[src[fx ? 15 - i : i]];
            if (!ci) continue;                           /* pen 0 is clear */
            cell = (py >> 3) * PAC_TW + (px >> 3);
            pac_fb[cell][py & 7] = (pac_fb[cell][py & 7] & keep) | ((u32)ci << s);
            if (pac_spr_mark[cell] != pac_spr_stamp) {   /* restore this cell next frame */
                pac_spr_mark[cell] = pac_spr_stamp;
                pac_spr_cells[pac_spr_ncells++] = (u16)cell;
                pac_mark_copy(cell);
            }
        }
    }
}

/* Build this frame: redraw changed cells and the cells last frame's sprites covered,
 * then draw the sprites on top (MAME order: 7..3, then 2..0 one pixel over). */
static void pac_render(void) {
    int i, c, n;
    /* the cells last frame's sprites covered go back to their tiles, then this frame's
     * sprites refill the list (the list is the last *drawn* frame's: frameskip-safe) */
    for (i = 0; i < pac_spr_ncells; i++) pac_mark_tile(pac_spr_cells[i]);
    pac_spr_ncells = 0;
    for (i = 0; i < pac_tn; i++) {
        c = pac_tlist[i];
        pac_draw_cell(c);
        pac_tile_dirty[c] = 0;
    }
    pac_tn = 0;
    if (++pac_spr_stamp == 0) {                          /* stamp wrapped: forget old marks */
        for (c = 0; c < PAC_CELLS; c++) pac_spr_mark[c] = 0;
        pac_spr_stamp = 1;
    }
    for (n = 7; n >= 0; n--) {
        int sx = 272 - pac_spr_xy[2 * n + 1], sy = pac_spr_xy[2 * n] - 31;
        if (n < 3) sy += 1;                              /* MAME xoffsethack */
        pac_draw_sprite(n, sx, sy);
        pac_draw_sprite(n, sx - 256, sy);                /* wraparound */
    }
}

/* One video frame: the Z80 runs a frame's worth of cycles, then vblank raises the IRQ (if
 * enabled) and the screen is rendered. Under MAME's pacman driver vblank (IRQ + screen
 * update + frame_done) likewise falls every 50688 cycles from reset, so frame k here ends
 * where MAME's frame k does. `render` 0 skips the picture (frameskip: changes keep
 * collecting in the dirty lists and show up on the next rendered frame). */
static u32 pac_frames;                 /* emulated frames so far */

static void pac_frame(int render) {
    pac_frames++;
    PAC_Z80_RUN(PAC_CYCLES_PER_FRAME);
    if (pac_irq_mask) pac_irq_line = 1;             /* vblank: MAME pacman vblank_irq */
#ifndef PAC_NORENDER
    if (render) pac_render();
#endif
}

#endif /* PACMAN_HW_H */
