/*
 * md_hw.h — the Sega Mega Drive / Genesis (MAME sega/megadriv.cpp + devices/video/315_5313.cpp),
 * rebuilt for the i960: 68000 memory map, the VDP's ports, DMA and interrupts, the joypads,
 * and the Z80/sound side as far as the 68000 sees it — on the 68000 core in m2_m68k.h.
 *
 * Portable C (no Model 2 registers here). The VDP keeps its memories (VRAM, CRAM, VSRAM,
 * registers) as the chip does; drawing them is the host's job: src/sonic.c maps the planes
 * onto the Model 2's tilemaps, tools/mdhost.c renders them in software to check against MAME.
 * What the host needs to redraw is flagged: md_tile_dirty (patterns written), md_cram_dirty.
 *
 * Needs, before including: u8/u16/u32 types and the cartridge:
 *   md_rom[]      the ROM as 16-bit words (MSB = the even byte), MD_ROM_WORDS of them
 * (tools/mdrom.py writes src/sonic_rom.h from the cartridge image).
 *
 * Timing is MAME's: 262 lines of 3420 master clocks, the 68000 at MCLK/7 (488.57 cycles a
 * line); VINT (level 6) at line 224, HINT (level 4) from the line counter in register 10;
 * DMA is instant and does not stall the 68000 (MAME's insta_68k_to_vram_dma).
 * The Z80 is not run: the 68000 gets its bus at once, sees the YM2612 never busy, and every
 * YM2612 / PSG write is queued in md_snd[] for the host to play (src/sonic.c: the SCSP).
 */
#ifndef MD_HW_H
#define MD_HW_H

#define MD_LINES        262
#define MD_VIS_LINES    224
#define MD_MCLK_LINE    3420            /* master clocks per line; the 68000 runs at MCLK/7 */

/* ---- board state ------------------------------------------------------------------- */
static u16 md_ram[0x8000];              /* 0xFF0000-0xFFFFFF (mirrored from 0xE00000) */
static u8  md_zram[0x2000];             /* Z80 RAM, 0xA00000 (the 68000 loads the Z80 driver) */
static u8  md_zbusreq, md_zreset = 1;   /* 0xA11100 bus request, 0xA11200 reset (held at power on) */
static u8  md_pad[2] = { 0xff, 0xff };  /* active-low: bit 0 U 1 D 2 L 3 R 4 B 5 C 6 A 7 START */
static u8  md_io_data[3], md_io_ctrl[3];

/* ---- VDP ------------------------------------------------------------------------------- */
static u16 md_vram[0x8000];             /* 64 KB as big-endian words */
static u16 md_cram[64];                 /* 0000BBB0GGG0RRR0 */
static u16 md_vsram[40];
static u8  md_reg[32];
static u16 md_vaddr, md_cmd1;
static u8  md_vcode, md_cmd_pending, md_fill_pending;
static u16 md_fill_len;
static u8  md_irq6_pending, md_irq4_pending, md_vblank;
static int md_irq4counter = -1;
static int md_line;                     /* current scanline, 0-261 */
static int md_seg_line, md_seg_start;   /* the 68000's slice: first line, m68k.cycles as it began */

/* what changed, for the host's renderer: patterns (2048 x 32 bytes), colours */
static u32 md_tile_dirty[2048 / 32];
static u8  md_tile_any;
static u32 md_cram_dirty;               /* bit n: CRAM word n>>... (one bit per 2 entries) */
static u8  md_cram_any;

/* ---- sound writes, for the host (the YM2612 and PSG are not emulated here) ------------ */
#define MD_SND_Q 1024
/* entries: 0x01PPRRVV a YM2612 write (port PP, register RR, value VV), 0x020000VV a PSG byte */
static u32 md_snd[MD_SND_Q];
static u32 md_snd_head, md_snd_tail;
static u8  md_ym_addr[2];               /* the YM2612's latched register per port */
static u8  md_z80_cmd;                  /* last byte the 68000 wrote to Z80 RAM 0x1FFF (DAC sample) */
static u8  md_z80_cmd_new;

static inline void md_snd_put(u32 v) {
    if (md_snd_head - md_snd_tail < MD_SND_Q) md_snd[md_snd_head++ & (MD_SND_Q - 1)] = v;
}

static inline void md_mark_tile(u32 byteaddr) {
    u32 t = (byteaddr & 0xffff) >> 5;
    md_tile_dirty[t >> 5] |= 1u << (t & 31);
    md_tile_any = 1;
}

/* ---- the 68000 ----------------------------------------------------------------------- */
/* idle skip: Sonic's WaitForVBla spins on `tst.b (v_vbla_routine).w; bne.s *-4` until the
 * VINT handler clears the byte; nothing changes before the next interrupt, so the frame
 * loop fast-forwards to it (md_idle). Found by its bytes, like pacman's. */
static u32 md_idle_pc = 0xffffffffu;
static u8  md_idle;
#define M68K_BCC_TAKEN(target) do { if ((target) == md_idle_pc) { md_idle = 1; m68k.cycles = 0; } } while (0)

static inline u32 md_rd8(u32 a);
static inline u32 md_rd16(u32 a);
static inline void md_wr8(u32 a, u32 v);
static inline void md_wr16(u32 a, u32 v);
static int md_irq_ack(int level);
/* a test harness (tools/mdlock.c) may define the bus hooks itself, to watch the bus */
#ifndef M68K_RD8
#define M68K_RD8(a)      md_rd8(a)
#define M68K_RD16(a)     md_rd16(a)
#define M68K_WR8(a, v)   md_wr8(a, v)
#define M68K_WR16(a, v)  md_wr16(a, v)
#define M68K_FETCH16(a)  md_rd16(a)
#endif
#define M68K_IACK(l)     md_irq_ack(l)
#include "m2_m68k.h"
/* MD_INTERRUPT() / MD_STEP(): take the pending interrupt / run one instruction (hookable) */
#ifndef MD_STEP
#define MD_INTERRUPT() m68k_interrupt()
#define MD_STEP()      m68k_step()
#endif

/* ---- interrupts ------------------------------------------------------------------------ */
/* MAME: the level-6 line is up while a VINT is pending and register 1 enables it; the
 * level-4 line likewise for HINT and register 0. The acknowledge clears VINT first. */
static int md_cyc_owed;                 /* the slice's cycles, put aside for an interrupt */
static u8  md_irq_new;                  /* an interrupt became takeable (md_update_irq) */
static inline void md_update_irq(void) {
    if (md_irq6_pending && (md_reg[1] & 0x20)) m68k.irq = 6;
    else if (md_irq4_pending && (md_reg[0] & 0x10)) m68k.irq = 4;
    else m68k.irq = 0;
    /* an interrupt the 68000 will take (a VDP register write enabled it): the recompiled
     * code, which only checks its cycles, sets them aside and stops (md_cyc_owed) */
    if (m68k_irq_pending()) md_irq_new = 1;
}
static int md_irq_ack(int level) {
    if (md_irq6_pending && (md_reg[1] & 0x20)) md_irq6_pending = 0;
    else if (md_irq4_pending && (md_reg[0] & 0x10)) md_irq4_pending = 0;
    md_update_irq();
    return 24 + level;
}

/* ---- VDP ports --------------------------------------------------------------------------- */
static void md_vram_w(u16 data) {
    if (md_vaddr & 1) data = (u16)((data >> 8) | (data << 8));
    md_vram[md_vaddr >> 1] = data;
    md_mark_tile(md_vaddr);
    md_vaddr = (u16)(md_vaddr + md_reg[15]);
}
static void md_cram_w(u16 data) {
    int i = (md_vaddr & 0x7e) >> 1;
    md_cram[i] = data;
    md_cram_dirty |= 1u << (i >> 1);
    md_cram_any = 1;
    md_vaddr = (u16)(md_vaddr + md_reg[15]);
}
static void md_vsram_w(u16 data) {
    if ((md_vaddr & 0x7e) >> 1 < 40) md_vsram[(md_vaddr & 0x7e) >> 1] = data;
    md_vaddr = (u16)(md_vaddr + md_reg[15]);
}

static u16 md_dma_src_word(u32 a) {
    a &= 0xffffff;
    if (a < 0x400000) return (a >> 1) < MD_ROM_WORDS ? md_rom[a >> 1] : 0xffff;
    if (a >= 0xe00000) return md_ram[(a & 0xffff) >> 1];
    return 0;
}

static __attribute__((noinline)) void md_dma(void) {
    u32 type = md_reg[23] >> 6;
    if (!(md_reg[1] & 0x10)) return;                    /* DMA disabled */
    if (type < 2) {                                     /* 68000 -> VRAM / CRAM / VSRAM */
        u32 src = ((u32)md_reg[21] | ((u32)md_reg[22] << 8) | ((u32)(md_reg[23] & 0x7f) << 16)) << 1;
        u32 len = ((u32)md_reg[19] | ((u32)md_reg[20] << 8)) << 1, n;
        u32 code = md_vcode & 0xf;
        if (len == 0) len = 0xffff;
        n = 0;
        /* the usual case, a block to VRAM with increment 2: a plain copy, tiles marked once */
        if (code == 1 && md_reg[15] == 2 && !(md_vaddr & 1)) {
            u32 cnt = len >> 1, a0 = md_vaddr, k;
            const u16 *sp = 0;
            if (src + len <= (MD_ROM_WORDS << 1)) sp = &md_rom[src >> 1];
            else if (src >= 0xe00000 && (src & 0xffff) + len <= 0x10000) sp = &md_ram[(src & 0xffff) >> 1];
            if (sp && a0 + len <= 0x10000) {
                u16 *dp = &md_vram[a0 >> 1];
                for (k = 0; k < cnt; k++) dp[k] = sp[k];
                for (k = a0 >> 5; k <= (a0 + len - 1) >> 5; k++) md_tile_dirty[k >> 5] |= 1u << (k & 31);
                md_tile_any = 1;
                md_vaddr = (u16)(a0 + cnt * 2);
                src += cnt * 2;
                n = cnt;
            }
        }
        for (; n < (len >> 1); n++) {
            u16 w = md_dma_src_word(src);
            if (code == 1) md_vram_w(w);
            else if (code == 3) md_cram_w(w);
            else if (code == 5) { if (md_vaddr >= 0x80) break; md_vsram_w(w); }
            src += 2;
            if (src > 0xffffff) src = code == 1 ? 0xe00000 : 0xfe0000;
        }
        md_reg[19] = md_reg[20] = 0;
        md_reg[21] = (u8)(src >> 1); md_reg[22] = (u8)(src >> 9); md_reg[23] = (u8)((md_reg[23] & 0x80) | ((src >> 17) & 0x7f));
    } else if (type == 2) {                             /* VRAM fill: on the next data write */
        u32 code = md_vcode & 0xf;
        if (code == 1 || code == 3 || code == 5) {
            md_fill_pending = 1;
            md_fill_len = (u16)(md_reg[19] | (md_reg[20] << 8));
        }
    } else {                                            /* VRAM copy */
        u32 src = (u32)md_reg[21] | ((u32)md_reg[22] << 8), len = (u32)md_reg[19] | ((u32)md_reg[20] << 8), n;
        for (n = 0; n < len; n++) {
            u16 w = md_vram[(src & 0xffff) >> 1];
            u8 b = (u8)((src & 1) ? w : w >> 8);
            u16 *d = &md_vram[md_vaddr >> 1];
            if (md_vaddr & 1) *d = (u16)((*d & 0xff00) | b); else *d = (u16)((*d & 0x00ff) | (b << 8));
            md_mark_tile(md_vaddr);
            src++;
            md_vaddr = (u16)(md_vaddr + md_reg[15]);
        }
    }
}

static void md_data_w(u16 data) {
    md_cmd_pending = 0;
    if (md_fill_pending) {                              /* MAME data_port_w, the fill */
        u32 n;
        u16 *d = &md_vram[md_vaddr >> 1];
        if (md_vaddr & 1) *d = (u16)((*d & 0xff00) | (data & 0xff));
        else *d = (u16)((*d & 0x00ff) | ((data & 0xff) << 8));
        for (n = 0; n <= md_fill_len; n++) {
            d = &md_vram[md_vaddr >> 1];
            if (md_vaddr & 1) *d = (u16)((*d & 0xff00) | (data >> 8));
            else *d = (u16)((*d & 0x00ff) | (data & 0xff00));
            md_mark_tile(md_vaddr);
            md_vaddr = (u16)(md_vaddr + md_reg[15]);
        }
        md_reg[19] = md_reg[20] = 0;
        return;
    }
    switch (md_vcode & 0xf) {
    case 1: md_vram_w(data); break;
    case 3: md_cram_w(data); break;
    case 5: md_vsram_w(data); break;
    default: break;
    }
}

static void md_set_reg(int r, u8 v) {
    if (!(md_reg[1] & 0x04) && r > 10) return;         /* mode 4 ignores registers above 10 */
    md_reg[r] = v;
    if (r == 0 || r == 1) md_update_irq();
}

static void md_ctrl_w(u16 data) {
    md_fill_pending = 0;
    if (md_cmd_pending) {
        md_cmd_pending = 0;
        md_vcode = (u8)(((md_cmd1 & 0xc000) >> 14) | ((data & 0x00f0) >> 2));
        md_vaddr = (u16)((md_cmd1 & 0x3fff) | ((data & 3) << 14));
        if (md_vcode & 0x20) md_dma();
    } else if ((data & 0xc000) == 0x8000) {             /* register write */
        md_set_reg((data >> 8) & 0x1f, (u8)data);
        md_vcode = 0; md_vaddr = 0;
    } else {
        md_cmd_pending = 1;
        md_cmd1 = data;
        md_vcode = (u8)((md_vcode & 0x3c) | ((data & 0xc000) >> 14));
        md_vaddr = (u16)((md_vaddr & 0xc000) | (data & 0x3fff));
    }
}

static u16 md_data_r(void) {
    u16 v = 0;
    md_cmd_pending = 0;
    switch (md_vcode & 0xf) {
    case 0: v = md_vram[md_vaddr >> 1]; md_vaddr = (u16)(md_vaddr + md_reg[15]); break;
    case 4: v = (md_vaddr & 0x7e) >> 1 < 40 ? md_vsram[(md_vaddr & 0x7e) >> 1] : 0; md_vaddr = (u16)(md_vaddr + md_reg[15]); break;
    case 8: v = md_cram[(md_vaddr & 0x7e) >> 1]; md_vaddr = (u16)(md_vaddr + md_reg[15]); break;
    default: break;
    }
    return v;
}

/* the 68000's position in the line, 0-487 cycles -> MAME's H position (0-479) */
/* where the beam is, from the 68000's cycles into its slice: the line, and MAME's H
 * position (0-479). Divides, but only for the few status / HV counter reads. */
static u32 md_hpos_line(int *line) {
    int c = md_seg_start - m68k.cycles;
    u32 mc;
    if (c < 0) c = 0;
    mc = (u32)c * 7u;                                /* master clocks */
    *line = md_seg_line + (int)(mc / MD_MCLK_LINE);
    return (mc % MD_MCLK_LINE) * 480u / MD_MCLK_LINE;
}
static inline u32 md_hpos(void) { int l; return md_hpos_line(&l); }

static u16 md_status_r(void) {
    u32 h = md_hpos();
    u16 v = 0x3400 | 0x0200;                            /* always-1 bits, FIFO empty */
    if (md_irq6_pending) v |= 0x80;
    if (md_vblank || !(md_reg[1] & 0x40)) v |= 0x08;
    if (h > 400 && h <= 460) v |= 0x04;
    return v;                                           /* (MAME keeps a pending command) */
}

static u16 md_hv_r(void) {
    int v;
    u32 h = md_hpos_line(&v);
    if (h > 460) v++;
    v %= MD_LINES;
    if (v > 0xea) v -= 6;                               /* vc_ntsc_224: 0x00-0xEA, then 0xE5.. */
    if (h > 0xf7) h -= 0x49;
    return (u16)(((v & 0xff) << 8) | (h & 0xff));
}

static u16 md_vdp_r(u32 a) {
    switch (a & 0x1c) {
    case 0x00: return md_data_r();
    case 0x04: return md_status_r();
    case 0x08: case 0x0c: return md_hv_r();
    default: return 0;
    }
}
static void md_vdp_w16(u32 a, u16 v) {
    switch (a & 0x1c) {
    case 0x00: md_data_w(v); break;
    case 0x04: md_ctrl_w(v); break;
    case 0x10: case 0x14: md_snd_put(0x02000000u | (v & 0xff)); break;   /* PSG */
    default: break;
    }
}

/* ---- the joypads (3 buttons): TH (bit 6 of the data port) selects the half ---------- */
static u8 md_pad_r(int n) {
    u8 pad = md_pad[n], th = md_io_data[n] & 0x40, in;
    in = th ? (u8)(0x40 | (pad & 0x3f)) : (u8)(((pad >> 2) & 0x30) | (pad & 0x03));
    return (u8)((md_io_data[n] & md_io_ctrl[n] & 0x80) | (in & ~md_io_ctrl[n] & 0x7f) | (md_io_data[n] & md_io_ctrl[n] & 0x7f));
}

/* ---- the 68000's bus ------------------------------------------------------------------- */
static __attribute__((noinline)) u32 md_rd_slow(u32 a, int word) {
    a &= 0xffffff;
    if (a < 0xa10000) {                                 /* Z80 space */
        if (a >= 0xa04000 && a < 0xa06000) return 0;    /* YM2612 status: never busy */
        if (a < 0xa02000 || (a >= 0xa02000 && a < 0xa04000)) {
            u32 o = a & 0x1fff;
            if (word) return ((u32)md_zram[o & ~1u] << 8) | md_zram[o | 1];
            return md_zram[o];
        }
        return 0;
    }
    if (a < 0xa10020) {                                 /* I/O */
        u32 r = (a >> 1) & 0xf, v;
        switch (r) {
        case 0: v = 0xa0; break;                        /* version: export, NTSC, no Mega-CD */
        case 1: case 2: case 3: v = r < 3 ? md_pad_r((int)r - 1) : 0x7f; break;
        case 4: case 5: case 6: v = md_io_ctrl[r - 4]; break;
        default: v = 0; break;
        }
        return word ? (v << 8) | v : v;
    }
    if ((a & 0xffff00) == 0xa11100) {                   /* Z80 bus: bit 0 of the high byte 0 = ours */
        u32 busy = (!md_zbusreq || md_zreset) ? 1 : 0;
        if (word) return busy << 8;
        return (a & 1) ? 0 : busy;
    }
    if ((a & 0xe00000) == 0xc00000 && (a & 0xe700e0) == 0xc00000) {   /* VDP */
        u16 v = md_vdp_r(a);
        if (word) return v;
        return (a & 1) ? (v & 0xff) : (v >> 8);
    }
    return word ? 0xffff : 0xff;
}

static __attribute__((noinline)) void md_wr_slow(u32 a, u32 v, int word) {
    a &= 0xffffff;
    if (a < 0xa10000) {                                 /* Z80 space */
        if (md_zbusreq && !md_zreset) {
            if (a < 0xa04000) {
                u32 o = a & 0x1fff;
                if (word) md_zram[o & ~1u] = (u8)(v >> 8);   /* MAME: a word write keeps only the MSB */
                else md_zram[o] = (u8)v;
                if (o == 0x1fff && !word) { md_z80_cmd = (u8)v; md_z80_cmd_new = 1; }
            } else if (a < 0xa06000) {                  /* YM2612 */
                /* even address: register latch, odd: data; a word write is its MSB at the
                 * even address (MAME's 8-bit YM2612 on the 16-bit bus) */
                u32 port = (a >> 1) & 1;
                u8 b = (u8)(word ? v >> 8 : v);
                if (word || !(a & 1)) md_ym_addr[port] = b;
                else md_snd_put(0x01000000u | (port << 16) | ((u32)md_ym_addr[port] << 8) | b);
            }
        }
        return;
    }
    if (a < 0xa10020) {                                 /* I/O */
        u32 r = (a >> 1) & 0xf;
        if (r >= 1 && r <= 3) md_io_data[r - 1] = (u8)v;
        else if (r >= 4 && r <= 6) md_io_ctrl[r - 4] = (u8)v;
        return;
    }
    if ((a & 0xffff00) == 0xa11100) {                   /* Z80 bus request */
        md_zbusreq = (u8)((word || !(a & 1)) ? (v >> (word ? 8 : 0)) & 1 : v & 1);
        return;
    }
    if ((a & 0xffff00) == 0xa11200) {                   /* Z80 reset (0 = held in reset) */
        md_zreset = !((word || !(a & 1)) ? (v >> (word ? 8 : 0)) & 1 : v & 1);
        return;
    }
    if ((a & 0xe00000) == 0xc00000 && (a & 0xe700e0) == 0xc00000) {   /* VDP */
        if (word) md_vdp_w16(a, (u16)v);
        else if ((a & 0x1c) >= 0x10) { if (a & 1) md_vdp_w16(a, (u16)(v & 0xff)); }
        else md_vdp_w16(a, (u16)((v & 0xff) | ((v & 0xff) << 8)));     /* byte to data/ctrl: both halves */
        return;
    }
}

static inline __attribute__((always_inline)) u32 md_rd16(u32 a) {
    if ((a & 0xe00000) == 0xe00000) return md_ram[(a & 0xffff) >> 1];
    if ((a & 0xffffff) < (MD_ROM_WORDS << 1)) return md_rom[(a & 0xffffff) >> 1];
    return md_rd_slow(a, 1);
}
static inline __attribute__((always_inline)) u32 md_rd8(u32 a) {
    if ((a & 0xe00000) == 0xe00000) return (md_ram[(a & 0xffff) >> 1] >> ((~a & 1) << 3)) & 0xff;
    if ((a & 0xffffff) < (MD_ROM_WORDS << 1)) return (md_rom[(a & 0xffffff) >> 1] >> ((~a & 1) << 3)) & 0xff;
    return md_rd_slow(a, 0);
}
static inline __attribute__((always_inline)) void md_wr16(u32 a, u32 v) {
    if ((a & 0xe00000) == 0xe00000) { md_ram[(a & 0xffff) >> 1] = (u16)v; return; }
    md_wr_slow(a, v, 1);
}
static inline __attribute__((always_inline)) void md_wr8(u32 a, u32 v) {
    if ((a & 0xe00000) == 0xe00000) {
        u16 *p = &md_ram[(a & 0xffff) >> 1];
        if (a & 1) *p = (u16)((*p & 0xff00) | (v & 0xff)); else *p = (u16)((*p & 0x00ff) | ((v & 0xff) << 8));
        return;
    }
    md_wr_slow(a, v, 0);
}

/* MD_RECOMP names a header from tools/m68krecomp.py (e.g. "sonic_recomp.h"): the hot code
 * statically recompiled into md_rc_run, which runs ahead of the interpreter */
#ifdef MD_RECOMP
#include MD_RECOMP
#endif

static void md_find_idle(void) {
    u32 i;
    md_idle_pc = 0xffffffffu;
    for (i = 0; i + 3 < MD_ROM_WORDS; i++)
        if (md_rom[i] == 0x4a38 && md_rom[i + 2] == 0x66fa) { md_idle_pc = i * 2; return; }
}

static void md_reset(void) {
    int i;
    for (i = 0; i < 0x8000; i++) { md_ram[i] = 0; md_vram[i] = 0; }
    for (i = 0; i < 64; i++) md_cram[i] = 0;
    for (i = 0; i < 40; i++) md_vsram[i] = 0;
    for (i = 0; i < 32; i++) md_reg[i] = 0;
    for (i = 0; i < 0x2000; i++) md_zram[i] = 0;
    md_vaddr = 0; md_vcode = 0; md_cmd_pending = 0; md_fill_pending = 0;
    md_irq6_pending = md_irq4_pending = 0; md_irq4counter = -1; md_vblank = 0;
    md_zbusreq = 0; md_zreset = 1;
    md_io_data[0] = md_io_data[1] = md_io_data[2] = 0x7f;
    md_io_ctrl[0] = md_io_ctrl[1] = md_io_ctrl[2] = 0;
    md_line = 0;
    md_find_idle();
    m68k_reset();
}

/* One frame: 262 lines. At each line start the VDP's line events run as in MAME's
 * vdp_handle_scanline_callback (VINT at 224, the HINT counter on lines 0-224), then the
 * 68000 runs the line's cycles. */
static u32 md_frames;
static u32 md_cyc_frac;                 /* master clocks not yet given to the 68000 */

static __attribute__((noinline)) void md_line_events(int line) {
    if (line == MD_VIS_LINES) { md_irq6_pending = 1; md_vblank = 1; }
    if (line <= MD_VIS_LINES) {
        if (--md_irq4counter == -1) {
            md_irq4counter = md_reg[10];
            md_irq4_pending = 1;
        }
    } else md_irq4counter = md_reg[10];
    md_update_irq();
}

/* run the 68000 for `cyc` cycles from the start of line `line0` */
static void md_run(int cyc, int line0) {
    if (md_idle && !m68k_irq_pending()) return;   /* spinning in WaitForVBla: skip ahead */
    md_idle = 0;
    m68k.cycles += cyc;
    md_seg_line = line0; md_seg_start = m68k.cycles;
    for (;;) {
        if (m68k.cycles <= 0) {
            if (!md_cyc_owed) break;
            m68k.cycles += md_cyc_owed; md_cyc_owed = 0;
        }
        if (m68k_irq_pending()) { MD_INTERRUPT(); md_idle = 0; }
        if (m68k.stopped) { m68k.cycles = 0; md_cyc_owed = 0; break; }
#ifdef MD_RECOMP
        if (!md_rc_run()) MD_STEP();
#else
        MD_STEP();
#endif
    }
}

/* One frame: the VDP's line events for each line (VINT at 224, the HINT counter), the
 * 68000 in between. Only the events that can raise an interrupt need the 68000 to have
 * got there, so it runs in two slices, lines 0-223 and 224-261, or a line at a time
 * while HINT is enabled (Labyrinth Zone's water line). */
static __attribute__((noinline)) void md_frame(void) {
    int line, pend = 0, seg = 0;
    md_vblank = 0;
    md_frames++;
    for (line = 0; line < MD_LINES; line++) {
        int cyc = 488;                                /* 3420 / 7 = 488 4/7 */
        if (pend && (line == MD_VIS_LINES || (md_reg[0] & 0x10))) { md_run(pend, seg); pend = 0; seg = line; }
        md_line = line;
        md_line_events(line);
        md_cyc_frac += MD_MCLK_LINE - 488 * 7;
        if (md_cyc_frac >= 7) { md_cyc_frac -= 7; cyc++; }
        pend += cyc;
    }
    md_run(pend, seg);
}

#endif /* MD_HW_H */
