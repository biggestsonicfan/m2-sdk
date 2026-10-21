/*
 * vaporbench.c — m2-sdk benchmark: every coprocessor command, timed on 2A, 2B and 2C, under a
 * vaporwave scene that the coprocessor draws.
 *
 * The scene (sky bands, a striped sun, a scrolling neon grid, a spinning cube with three cubes
 * orbiting it and their shadows) is all one quad, STF's model 456 (12x12 units in XZ), submitted
 * through the COP (m2_obj.h's path) with the math done by COP commands: FADD..F2INT and SQRT for
 * the grid scroll and the sun's stripes, SIN/COS/TAN/SINSCALE/COSSCALE for the motion, ADD3/SUB3/
 * SCALE3/MAG2D/DIST3D/ATAN2 for the cubes and shadows (DOT2D for the sun), PUSH/POP for the cube
 * hierarchy, WMATRIX for the sky and RMATRIX to read back where the orbiting cubes are.
 *
 * Each frame, before it draws, it times one native command (VB_K calls, board timer 1, 25 MHz)
 * after checking its answer against coptest.c's known answers, and one of
 * the other cpres1 commands (vaporbench_cases.h, tools/vaporbench_gen.py), which 2A and 2C run
 * in their VM (M2_COP_EXT). The HUD (text the i960 writes into texture RAM, three textured quads)
 * shows microseconds per call for the native commands, the scene's COP time, SUBMIT's, and the
 * other commands' total and slowest. Everything also stays in the g_vb_* globals for a MAME Lua
 * script (tools/vaporbench_dump.lua) to compare the boards.
 *
 * On 2B the quad is model 456 from sfight's model table. dynamcop and dynamcopc have no such
 * model, so on 2A and 2C a copy of its 26 words goes into polygon RAM (MAME's GEO HLE draws
 * that; the real cpres2 in MAME, M2_HLE_GEO_OFF, reads meshes from the polygon ROM only).
 *
 *   cmake ... -DM2_GAME=vaporbench -DM2_BOARD=2A     run on dynamcop
 *   cmake ... -DM2_GAME=vaporbench -DM2_BOARD=2C     run on dynamcopc
 *   cmake ... -DM2_GAME=vaporbench                   run on sfight (2B, needs cpres1.h)
 *   cmake ... -DM2_GAME=vaporbench -DM2_GSX=ON       also roms/vaporbench.gsx (below)
 *
 * GSX (M2_GSX): the 2B build as a geoserial app, uploaded over RS-422 to m2-x11's kernel and
 * run from its manager instead of burned (tools/gsx_upload.py). The kernel booted the SHARCs
 * with STF's cpres1/cpres2 already, so the app doesn't upload them again (which also keeps it
 * under the kernel's 64 KB slot). It reads the link while it waits for vsync and returns to the
 * manager on PROG_EXIT; once the ext sweep has gone round, it sends its numbers back as text.
 */
#define M2_NO_FASTMATH   /* keep m2_cop_* on the COP: the point is to time it */
#define M2_COP_EXT       /* 2A/2C: every other cpres1 command in the kernels' VM */
#include "m2.h"
#include "m2_obj.h"
#include "m2_polyattr.h"
#include "vaporbench_cases.h"
#ifdef M2_GSX
int  vb_gs_poll_exit(void);           /* vaporbench_gsx.c: the kernel's syscalls */
void vb_gs_puts(const char *s);
#endif
#if defined(M2_GSX) && defined(VB_TRACE)
/* -DVB_TRACE: a breadcrumb over the link before each step of the start and of the first ext
 * sweep, so a board that hangs shows where (the last line that came through) */
static u32 g_trace_on = 1;
static void vb_step(const char *s, u32 v) {
    char b[24], *p = b; int k;
    if (!g_trace_on) return;
    while (*s) *p++ = *s++;
    for (k = 28; k >= 0; k -= 4) *p++ = "0123456789abcdef"[v >> k & 15u];
    *p++ = '\r'; *p++ = '\n'; *p = 0;
    vb_gs_puts(b);
}
#define VB_STEP(s, v) vb_step(s, (u32)(v))
#else
#define VB_STEP(s, v) ((void)0)
#endif

/* ---- board timers (0xF00000 + 4n): count down at 25 MHz from the 32-bit value written (171 s
 * from 0xFFFFFFFF); an expired timer reads 0xfffff (MAME model2.cpp timers_r). T0 times the
 * frame, T1 the benchmark, T2 the scene, T3 each SUBMIT. */
#define VB_TIMER(n) (*(volatile u32 *)(0x00F00000u + 4u * (n)))
#define VB_TSTART   0xFFFFFFFFu
#define VB_TSAT     0xFFFFFFFFu          /* expired */
static u32 vb_tstart(int n) { VB_TIMER(n) = VB_TSTART; return VB_TIMER(n); }
static u32 vb_tstop(int n, u32 t0) {
    u32 t = VB_TIMER(n);
    return t == 0xfffffu ? VB_TSAT : t0 - t;
}
#define VB_NS(ticks, k) ((ticks) == VB_TSAT ? VB_TSAT : (ticks) / 25u * 1000u / (k) + (ticks) % 25u * 40u / (k))

/* ---- results, for tools/vaporbench_dump.lua ---- */
#define VB_NNAT 30
#define VB_K    16                       /* calls per native timing */
volatile u32 g_vb_nat_ns[VB_NNAT];       /* ns per call */
volatile u32 g_vb_nat_bad;               /* bit i: native command i gave a wrong answer */
volatile u32 g_vb_next = VB_NEXT;        /* ext cases */
volatile u32 g_vb_ext_cmd[VB_NEXT];      /* ext case i's command */
volatile u32 g_vb_ext_ns[VB_NEXT];       /* ns for one call of ext case i */
#define VB_EXTW 48
volatile u32 g_vb_ext_out[VB_NEXT][VB_EXTW];   /* its word count, then its results, anything
                                                 * more it sent, and the matrix after it */
volatile u32 g_vb_ext_done;              /* ext cases run (it cycles through them) */
volatile u32 g_vb_scene_us;              /* COP time of the last frame's scene */
volatile u32 g_vb_nobj;                  /* submits so far in this frame */
volatile u32 g_vb_objs;                  /* submits in the last frame, the HUD's included */
volatile u32 g_vb_frame_us;              /* last frame, vsync to vsync */
volatile u32 g_vb_frames;

static u32 fb(float f) { union { float f; u32 u; } x; x.f = f; return x.u; }
static float bf(u32 u) { union { float f; u32 u; } x; x.u = u; return x.f; }
static volatile u32 *const cf = (volatile u32 *)M2_COPFIFO_ADDR;

/* ======================================================================================= quad */
/* Model 456 as sfight's polygon ROM has it: 2 leading vertices (-6,0,-6) (6,0,-6), a quad unit
 * (attr, normal -Y, (-6,0,6) (6,0,6)) and a closing unit. obc 0x00010002. */
static const u32 vb_quad_mesh[26] = {
    0xc0c00000u, 0x00000000u, 0xc0c00000u, 0x40c00000u, 0x00000000u, 0xc0c00000u,
    0x88fc1901u, 0x00000000u, 0xbf800000u, 0x00000000u,
    0xc0c00000u, 0x00000000u, 0x40c00000u, 0x40c00000u, 0x00000000u, 0x40c00000u,
    0x00000000u, 0x00040401u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x38d1b717u, 0x428e0000u, 0xc3960000u, 0x42e59c36u, 0x428e0000u,
};
#define VB_QUAD_OBA 0x0100u
static u32 g_q_oba, g_q_obc;
static void quad_init(void) {
#if M2_BOARD_SHARC
    volatile const u32 *mdl = (volatile const u32 *)(M2_MODEL_TABLE + M2_FLAT_QUAD_MODEL * 16u);
    g_q_oba = mdl[2]; g_q_obc = mdl[3];
#else
    g_q_oba = VB_QUAD_OBA; g_q_obc = 0x00010002u;
#endif
}
static void quad_upload(void) {   /* GEO FIFO, inside a frame */
#if !M2_BOARD_SHARC
    volatile u32 *fifo = (volatile u32 *)M2_GEOFIFO_ADDR;
    int i;
    *fifo = GEO_OP_POLYDATA; *fifo = VB_QUAD_OBA; *fifo = 26u;
    for (i = 0; i < 26; i++) *fifo = vb_quad_mesh[i];
#endif
}

/* SUBMIT the quad under the current matrix (m2__obj_commit: fence, SUBMIT, counts, END), timed */
static u32 g_sub_ticks;
static void commit(u32 tpa, u32 tha) {
    u32 t = vb_tstart(3);
    m2__obj_commit(cf, tpa, tha, g_q_oba, g_q_obc);
    t = vb_tstop(3, t);
    if (t != VB_TSAT) g_sub_ticks += t;
    g_vb_nobj++;
}
static void commit_cb(u32 cb) { commit(0u, GEO_TEXRAM_BIT | (cb * 4u)); }

static void setpos(float x, float y, float z) { *cf = COP_SET_POS; *cf = fb(x); *cf = fb(y); *cf = fb(z); }
static void scale(float x, float y, float z)  { *cf = COP_SCALE; *cf = fb(x); *cf = fb(y); *cf = fb(z); }

/* A camera-facing quad: centre (x,y,z) in view space (x right, y up, z ahead), half sizes hx,hy */
static void face_quad(float x, float y, float z, float hx, float hy) {
    *cf = COP_IDENTITY;
    setpos(x, y, z);
    *cf = COP_ANG_X; *cf = 0x4000u;                     /* XZ quad -> faces the camera */
    scale(m2_cop_fmul(hx, 1.0f / 6.0f), 1.0f, m2_cop_fmul(hy, 1.0f / 6.0f));
}
/* A floor quad (XZ) */
static void floor_quad(float x, float y, float z, float hx, float hz) {
    *cf = COP_IDENTITY;
    setpos(x, y, z);
    scale(m2_cop_fmul(hx, 1.0f / 6.0f), 1.0f, m2_cop_fmul(hz, 1.0f / 6.0f));
}

/* ===================================================================================== colours */
#define CB_SKY    1    /* 8 bands, top to horizon */
#define CB_GND    9    /* 4 bands below the horizon */
#define CB_SUN    13   /* 12, top to bottom */
#define CB_GRIDL  25
#define CB_GRIDT  26
#define CB_CUBE   27   /* 6 faces */
#define CB_ORB    33   /* 3 */
#define CB_SHADOW 36
#define CB_HUD    40   /* title, board line, body */
#define CB_N      43

static u16 lerp_rgb(int r0, int g0, int b0, int r1, int g1, int b1, int i, int n) {
    return M2_RGB(r0 + (r1 - r0) * i / n, g0 + (g1 - g0) * i / n, b0 + (b1 - b0) * i / n);
}
static void colours(void) {
    int i;
    static const u16 cube[6] = { M2_RGB(31, 16, 28), M2_RGB(16, 28, 31), M2_RGB(28, 31, 18),
                                 M2_RGB(31, 22, 14), M2_RGB(22, 16, 31), M2_RGB(14, 31, 24) };
    for (i = 0; i < 8; i++)  m2_3d_solid(CB_SKY + i, lerp_rgb(5, 2, 14, 31, 9, 20, i, 7));
    for (i = 0; i < 4; i++)  m2_3d_solid(CB_GND + i, lerp_rgb(9, 1, 14, 2, 0, 5, i, 3));
    for (i = 0; i < 12; i++) m2_3d_solid(CB_SUN + i, lerp_rgb(31, 29, 8, 31, 6, 19, i, 11));
    m2_3d_solid(CB_GRIDL, M2_RGB(4, 30, 31));
    m2_3d_solid(CB_GRIDT, M2_RGB(31, 6, 29));
    for (i = 0; i < 6; i++)  m2_3d_solid(CB_CUBE + i, cube[i]);
    m2_3d_solid(CB_ORB + 0, M2_RGB(31, 31, 31));
    m2_3d_solid(CB_ORB + 1, M2_RGB(6, 31, 31));
    m2_3d_solid(CB_ORB + 2, M2_RGB(31, 10, 26));
    m2_3d_solid(CB_SHADOW, M2_RGB(13, 2, 19));
    m2_3d_solid(CB_HUD + 0, M2_RGB(31, 12, 26));
    m2_3d_solid(CB_HUD + 1, M2_RGB(6, 31, 31));
    m2_3d_solid(CB_HUD + 2, M2_RGB(31, 31, 31));
}

/* ========================================================================================= HUD */
/* 32x16 characters of 8x8, written by the i960 into a 256x128 texture at texram (512,0), shown
 * as three translucent textured quads: the title (row 0, twice the size), the board (row 1) and
 * the table (rows 2-15). */
#define HUD_TX    512
#define HUD_TY    0
#define HUD_COLS  32
#define HUD_ROWS  16
#define HUD_HDR   0x300u                 /* texture_ram header slots, 4 words each */
#define HUD_UV    0x320u                 /* UV slots, 8 words each */
static char g_hud[HUD_ROWS][HUD_COLS], g_hud_shown[HUD_ROWS][HUD_COLS];

static void hud_glyph(int col, int row, int c) {
    int x0 = HUD_TX + col * 8, y0 = HUD_TY + row * 8, xx, yy, dx, dy;
    for (yy = 0; yy < 8; yy += 2)
        for (xx = 0; xx < 8; xx += 4) {
            u32 w = 0, widx = (u32)((y0 + yy) / 2) * 256u + (u32)((x0 + xx) / 4);
            for (dy = 0; dy < 2; dy++)
                for (dx = 0; dx < 4; dx++) {
                    int x = x0 + xx + dx, y = y0 + yy + dy;
                    w |= (u32)m2__font_texel(c & 127, xx + dx, yy + dy)
                         << (((x & 1) ? 0 : 4) + ((y & 1) ? 0 : 8) + ((x & 2) ? 16 : 0));
                }
            m2_tex_wr32(M2_BOARD_TEXRAM0, widx, w);
            m2_tex_wr32(M2_BOARD_TEXRAM1, widx, w);
        }
}
/* write the characters that changed */
static void hud_flush(void) {
    int r, c;
    for (r = 0; r < HUD_ROWS; r++)
        for (c = 0; c < HUD_COLS; c++)
            if (g_hud[r][c] != g_hud_shown[r][c]) {
                hud_glyph(c, r, g_hud[r][c]);
                g_hud_shown[r][c] = g_hud[r][c];
            }
}
static void hud_puts(int col, int row, const char *s) {
    while (*s && col < HUD_COLS) g_hud[row][col++] = *s++;
}
/* v right-aligned in w characters */
static void hud_uint(int col, int row, u32 v, int w) {
    int i;
    for (i = w - 1; i >= 0; i--) {
        g_hud[row][col + i] = (char)(v || i == w - 1 ? '0' + v % 10u : ' ');
        v /= 10u;
    }
}
/* ns in 5 characters: microseconds "  1.2", "123.4", then " 1234" (whole us), "123MS", "12.3S" */
static void hud_us(int col, int row, u32 ns) {
    u32 t = (ns + 50u) / 100u, ms = (ns + 500000u) / 1000000u;
    if (ns == VB_TSAT) { hud_puts(col, row, "SLOW!"); return; }
    if (t < 10000u) {
        hud_uint(col, row, t / 10u, 3);
        g_hud[row][col + 3] = '.';
        g_hud[row][col + 4] = (char)('0' + t % 10u);
    } else if (ns < 99999500u) hud_uint(col, row, (ns + 500u) / 1000u, 5);
    else if (ms < 1000u) { hud_uint(col, row, ms, 3); hud_puts(col + 3, row, "MS"); }
    else {
        hud_uint(col, row, ms / 1000u, 2);
        g_hud[row][col + 2] = '.';
        g_hud[row][col + 3] = (char)('0' + ms / 100u % 10u);
        g_hud[row][col + 4] = 'S';
    }
}
static void hud_hex2(int col, int row, u32 v) {
    g_hud[row][col] = "0123456789ABCDEF"[(v >> 4) & 15u];
    g_hud[row][col + 1] = "0123456789ABCDEF"[v & 15u];
}

/* The three quads: texture rect (u0,v0)-(u1,v1) in texels, on screen at (x,y), `zoom` pixels
 * per texel. Screen to view space as M2_SPR_COP: z = 280 * 2^-9, so one pixel is 1/512 unit and
 * the distance mip stays at level 0 (log2(z) <= 0). */
#define HUD_Z (280.0f / 512.0f)
static const struct { u16 u0, v0, u1, v1, x, y, zoom; } g_hudq[3] = {
    { 0,  0, 80,  8,  16, 12, 2 },       /* "VAPORBENCH" */
    { 0,  8, 256, 16, 16, 32, 1 },       /* board */
    { 0, 16, 256, 128, 16, 44, 1 },      /* table */
};
static void hud_tex_setup(void) {   /* GEO FIFO, inside a frame: headers + UVs, written once */
    volatile u32 *fifo = (volatile u32 *)M2_GEOFIFO_ADDR;
    u32 q;
    for (q = 0; q < 3; q++) {
        u32 u0 = g_hudq[q].u0 * 8u, v0 = g_hudq[q].v0 * 8u, u1 = g_hudq[q].u1 * 8u, v1 = g_hudq[q].v1 * 8u;
        *fifo = GEO_OP_TEXDATA; *fifo = GEO_TEXRAM_BIT | (HUD_HDR + q * 4u); *fifo = 4u;
        *fifo = M2_TH0_TEX | M2_TH0_XLUC | M2_TH0_MAPX(3u) | M2_TH0_MAPY(2u);   /* 256 x 128 */
        *fifo = 0u; *fifo = M2_TH2_TILE(HUD_TX, HUD_TY); *fifo = M2_TH3_COLORBASE(CB_HUD + q);
        /* UVs in model 456's vertex order: BR, BL, TL, TR (m2_sprite.h) */
        *fifo = GEO_OP_TEXDATA; *fifo = GEO_TEXRAM_BIT | (HUD_UV + q * 8u); *fifo = 8u;
        *fifo = v1; *fifo = u1; *fifo = v1; *fifo = u0;
        *fifo = v0; *fifo = u0; *fifo = v0; *fifo = u1;
    }
}
static void hud_draw(void) {
    int q;
    for (q = 0; q < 3; q++) {
        float w = (float)((g_hudq[q].u1 - g_hudq[q].u0) * g_hudq[q].zoom);
        float h = (float)((g_hudq[q].v1 - g_hudq[q].v0) * g_hudq[q].zoom);
        float x = (float)g_hudq[q].x, y = (float)g_hudq[q].y;
        face_quad((x + x + w - 496.0f) * (1.0f / 1024.0f), (384.0f - y - y - h) * (1.0f / 1024.0f), HUD_Z,
                  w * (1.0f / 1024.0f), h * (1.0f / 1024.0f));
        commit(GEO_TEXRAM_BIT | (HUD_UV + (u32)q * 8u), GEO_TEXRAM_BIT | (HUD_HDR + (u32)q * 4u));
    }
}

/* ======================================================================================= scene */
#define FOCAL   280.0f
#define FLOOR_Y (-30.0f)
#define SKY_Z   620.0f
#define SUN_Z   580.0f
#define GRID_Z0 18.0f
#define GRID_Z1 560.0f
#define GRID_DZ 40.0f
#define SUN_X   372.0f   /* screen */
#define SUN_Y   116.0f
#define SUN_R   70.0f
#define SUN_N   12
#define HORIZON (192.0f - FOCAL * FLOOR_Y / SKY_Z)   /* screen y of the floor at SKY_Z */

/* screen (px,py) at depth z -> view x, y */
static float sx(float px, float z) { return m2_cop_fmul(m2_cop_fsub(px, 248.0f), m2_cop_fdiv(z, FOCAL)); }
static float sy(float py, float z) { return m2_cop_fmul(m2_cop_fsub(192.0f, py), m2_cop_fdiv(z, FOCAL)); }

/* WMATRIX: the camera-facing quad's matrix written whole (columns 0-2, then T) */
static void sky_band(float y0, float y1, u32 cb) {
    float k = SKY_Z / FOCAL, hy = m2_cop_fmul(m2_cop_fsub(y1, y0), 0.5f * k);
    u32 m[12];
    int i;
    m[0] = fb(m2_cop_fmul(248.0f, k * (1.0f / 6.0f))); m[1] = 0; m[2] = 0;
    m[3] = 0; m[4] = 0; m[5] = fb(-1.0f);
    m[6] = 0; m[7] = fb(m2_cop_fmul(hy, 1.0f / 6.0f)); m[8] = 0;
    m[9] = 0; m[10] = fb(sy(m2_cop_fmul(m2_cop_fadd(y0, y1), 0.5f), SKY_Z)); m[11] = fb(SKY_Z);
    *cf = COP_WMATRIX;
    for (i = 0; i < 12; i++) *cf = m[i];
    commit_cb(cb);
}

static void sky(void) {
    int i;
    float h = HORIZON;
    for (i = 0; i < 8; i++) sky_band(h * (float)i * 0.125f, h * (float)(i + 1) * 0.125f, CB_SKY + i);
    for (i = 0; i < 4; i++) sky_band(h + (384.0f - h) * (float)i * 0.25f, h + (384.0f - h) * (float)(i + 1) * 0.25f, CB_GND + i);
}

/* The sun: SUN_N stripes; the lower half's stripes thin out, scrolling down. Half width from
 * SQRT(DOT2D(r, r, -dy, dy)) = sqrt(r^2 - dy^2). */
static void sun(u32 t) {
    float h = 2.0f * SUN_R / (float)SUN_N, k = SUN_Z / FOCAL;
    /* phase = (t * 0.25) mod h: INT2F, FMUL, FDIV, F2INT, FSUB */
    float f = m2_cop_fmul(m2_cop_int2f((int)t), 0.25f);
    float ph = m2_cop_fsub(f, m2_cop_fmul(m2_cop_int2f(m2_cop_f2int(m2_cop_fdiv(f, h))), h));
    int j;
    for (j = -1; j < SUN_N; j++) {
        float o = m2_cop_fadd((float)j * h, ph), th = h, gap, cy, dy, hw;
        if (j < SUN_N / 2) { if (j < 0) continue; o = (float)j * h; }
        else { gap = m2_cop_fmul(m2_cop_fsub(o, SUN_R), 0.42f); th = m2_cop_fsub(h, gap); }
        if (o < 0.0f) { th = m2_cop_fadd(th, o); o = 0.0f; }
        if (th < 1.0f || o >= 2.0f * SUN_R) continue;
        if (o + th > 2.0f * SUN_R) th = 2.0f * SUN_R - o;
        cy = m2_cop_fadd(SUN_Y - SUN_R, m2_cop_fadd(o, m2_cop_fmul(th, 0.5f)));
        dy = m2_cop_fsub(cy, SUN_Y);
        hw = m2_cop_sqrt(m2_cop_dot2d(SUN_R, SUN_R, -dy, dy));
        if (hw < 1.0f) continue;
        face_quad(sx(SUN_X, SUN_Z), sy(cy, SUN_Z), SUN_Z, m2_cop_fmul(hw, k), m2_cop_fmul(th, 0.5f * k));
        commit_cb(CB_SUN + (u32)m2_cop_f2int(m2_cop_fmul(o, (float)SUN_N / (2.0f * SUN_R))));
    }
}

/* The grid: lines along z swaying (SINSCALE), lines across z scrolling toward the camera, each
 * as thick as about 1.5 pixels at its depth: dz = z^2 * 1.5 / (FOCAL * -FLOOR_Y). */
static void grid(u32 t) {
    float sway = m2_cop_sin_scale(t * 0x0090u, 14.0f), zc = 0.5f * (GRID_Z0 + GRID_Z1);
    float f = m2_cop_fmul(m2_cop_int2f((int)t), 1.6f);
    float ph = m2_cop_fsub(f, m2_cop_fmul(m2_cop_int2f(m2_cop_f2int(m2_cop_fdiv(f, GRID_DZ))), GRID_DZ));
    int i;
    for (i = -8; i <= 8; i++) {
        floor_quad(m2_cop_fadd((float)i * 36.0f, sway), FLOOR_Y, zc, 0.35f, 0.5f * (GRID_Z1 - GRID_Z0));
        commit_cb(CB_GRIDL);
    }
    for (i = 0; i < 14; i++) {
        float z = m2_cop_fsub(GRID_Z0 + GRID_DZ * (float)(i + 1), ph);
        float dz = m2_cop_fmul(m2_cop_fmul(z, z), 1.5f / (FOCAL * -FLOOR_Y));
        if (z > GRID_Z1) break;
        floor_quad(sway, FLOOR_Y + 0.05f, z, 8.0f * 36.0f + 1.0f, dz);
        commit_cb(CB_GRIDT);
    }
}

/* the six faces of a cube of half size 6 under the current matrix: PUSH, turn the floor quad
 * to the face, push it out 6, SUBMIT, POP */
static const u16 g_face_rot[6][2] = {   /* ANG_X, ANG_Z */
    { 0, 0 }, { 0x8000u, 0 }, { 0x4000u, 0 }, { 0xC000u, 0 }, { 0, 0x4000u }, { 0, 0xC000u },
};
static void cube_faces(u32 cb0, int ncb) {
    int k;
    for (k = 0; k < 6; k++) {
        *cf = COP_PUSH;
        *cf = COP_ANG_X; *cf = g_face_rot[k][0];
        *cf = COP_ANG_Z; *cf = g_face_rot[k][1];
        setpos(0.0f, -6.0f, 0.0f);
        commit_cb(cb0 + (u32)(k % ncb));
        *cf = COP_POP;
    }
}

static void shadow(const float p[3], float half) {
    /* turned toward the main cube (ATAN2), smaller the higher it floats (FDIV) */
    u32 a = m2_cop_atan2(p[0], 10.0f, p[2], 150.0f);
    float s = m2_cop_fmul(half, m2_cop_fdiv(40.0f, m2_cop_fsub(p[1], FLOOR_Y - 20.0f)));
    *cf = COP_IDENTITY;
    setpos(p[0], FLOOR_Y + 0.3f, p[2]);
    *cf = COP_ANG_Y; *cf = (0x10000u - a) & 0xFFFFu;
    scale(m2_cop_fmul(s, 1.0f / 6.0f), 1.0f, m2_cop_fmul(s, 1.0f / 6.0f));
    commit_cb(CB_SHADOW);
}

static void cubes(u32 t) {
    static const float base[3] = { 10.0f, -2.0f, 150.0f }, up[3] = { 0.0f, 1.0f, 0.0f };
    float bob[3], c[3], orb[3][3], m[12];
    int i;
    /* centre = base + up * 6 cos(t): COSSCALE, SCALE3, ADD3 */
    m2_cop_scale3(m2_cop_cos_scale(t * 0x0180u, 6.0f), up, bob);
    m2_cop_add3(base, bob, c);
    *cf = COP_IDENTITY;
    setpos(c[0], c[1], c[2]);
    *cf = COP_ANG_Y; *cf = (t * 0x00C0u) & 0xFFFFu;
    *cf = COP_ANG_X; *cf = (t * 0x0110u) & 0xFFFFu;
    *cf = COP_ANG_Z; *cf = (t * 0x0050u) & 0xFFFFu;
    scale(2.6f, 2.6f, 2.6f);
    cube_faces(CB_CUBE, 6);
    /* three cubes orbiting it: PUSH/POP, each read back with RMATRIX for its shadow */
    *cf = COP_IDENTITY;
    setpos(c[0], c[1], c[2]);
    *cf = COP_ANG_Y; *cf = (t * 0x0200u) & 0xFFFFu;
    for (i = 0; i < 3; i++) {
        float r = m2_cop_fadd(44.0f, m2_cop_sin_scale(t * 0x0100u + (u32)i * 0x5555u, 6.0f));
        *cf = COP_PUSH;
        *cf = COP_ANG_Y; *cf = (u32)i * 0x5555u;
        setpos(r, m2_cop_fmul(m2_cop_tan((t * 0x0080u + (u32)i * 0x2000u) & 0x3FFFu), 3.0f), 0.0f);
        *cf = COP_ANG_X; *cf = (t * 0x0400u) & 0xFFFFu;
        *cf = COP_ANG_Z; *cf = (t * 0x0300u) & 0xFFFFu;
        scale(0.75f, 0.75f, 0.75f);
        m2_cop_rmatrix(m);
        orb[i][0] = m[9]; orb[i][1] = m[10]; orb[i][2] = m[11];
        cube_faces(CB_ORB + (u32)i, 1);
        *cf = COP_POP;
    }
    shadow(c, 20.0f);
    for (i = 0; i < 3; i++) {
        /* SUB3 + MAG2D: how far out it is; DIST3D: how far from a light above the main cube.
         * The shadow shrinks with both. */
        float d[3], h, l;
        m2_cop_sub3(orb[i], c, d);
        h = m2_cop_mag2d(d[0], d[2]);
        l = m2_cop_dist3d(orb[i][0], c[0], orb[i][1], m2_cop_fadd(c[1], 40.0f), orb[i][2], c[2]);
        shadow(orb[i], m2_cop_fdiv(600.0f, m2_cop_fadd(h, l)));
    }
}

/* ===================================================================================== bench */
/* The native commands, timed VB_K calls at a time; the answers from coptest.c. A command that
 * answers nothing is timed with one FADD after the calls, less a FADD timed alone. */
static const char g_nat_name[VB_NNAT][5] = {
    "FADD", "FSUB", "FMUL", "FDIV", "I2F", "F2I", "SQRT", "SIN", "COS", "TAN",
    "SINS", "COSS", "ATN2", "DST2", "DST3", "MAG2", "DOT2", "ADD3", "SUB3", "SCL3",
    "IDNT", "SPOS", "SCAL", "ANGX", "ANGY", "ANGZ", "RMAT", "WMAT", "PSPP", "SUBM",
};
static const float g_va[3] = { 1.0f, 2.0f, 3.0f }, g_vb[3] = { 0.5f, -4.0f, 10.0f };
static const u32 g_wm[12] = { 0x3f800000u, 0x40000000u, 0x40400000u, 0x40800000u, 0x40a00000u, 0x40c00000u,
                              0x40e00000u, 0x41000000u, 0x41100000u, 0x41200000u, 0x41300000u, 0x41400000u };
static volatile u32 g_sink;

static float dist2d(float x1, float x2, float z1, float z2) {
    m2_cop(COP_DIST2D); m2_cop_pf(x1); m2_cop_pf(x2); m2_cop_pf(z1); m2_cop_pf(z2);
    return m2_cop_gf();
}
static float sin1(u32 a) { m2_cop(COP_SIN); m2_cop_pi(a); return m2_cop_gf(); }
static float cos1(u32 a) { m2_cop(COP_COS); m2_cop_pi(a); return m2_cop_gf(); }
static void rmat(u32 m[12]) { int i; *cf = COP_RMATRIX; for (i = 0; i < 12; i++) m[i] = *cf; }

/* one call of native i; returns 1 if it leaves no answer to wait for */
static int nat_call(int i) {
    float o[3];
    u32 m[12];
    int j;
    switch (i) {
    case 0:  g_sink = fb(m2_cop_fadd(1.5f, 2.25f)); break;
    case 1:  g_sink = fb(m2_cop_fsub(5.0f, 1.25f)); break;
    case 2:  g_sink = fb(m2_cop_fmul(3.0f, -2.5f)); break;
    case 3:  g_sink = fb(m2_cop_fdiv(1.0f, 3.0f)); break;
    case 4:  g_sink = fb(m2_cop_int2f(-7)); break;
    case 5:  g_sink = (u32)m2_cop_f2int(-3.7f); break;
    case 6:  g_sink = fb(m2_cop_sqrt(2.0f)); break;
    case 7:  g_sink = fb(sin1(0x4000u)); break;
    case 8:  g_sink = fb(cos1(0x8000u)); break;
    case 9:  g_sink = fb(m2_cop_tan(0x2000u)); break;
    case 10: g_sink = fb(m2_cop_sin_scale(0x4000u, 3.0f)); break;
    case 11: g_sink = fb(m2_cop_cos_scale(0x2000u, 2.0f)); break;
    case 12: g_sink = m2_cop_atan2(1.0f, 5.0f, -1.0f, 2.0f); break;
    case 13: g_sink = fb(dist2d(1.0f, 4.0f, 2.0f, 6.0f)); break;
    case 14: g_sink = fb(m2_cop_dist3d(0.0f, 2.0f, 0.0f, 3.0f, 0.0f, 6.0f)); break;
    case 15: g_sink = fb(m2_cop_mag2d(3.0f, 4.0f)); break;
    case 16: g_sink = fb(m2_cop_dot2d(2.0f, 3.0f, 4.0f, 5.0f)); break;
    case 17: m2_cop_add3(g_va, g_vb, o); g_sink = fb(o[2]); break;
    case 18: m2_cop_sub3(g_va, g_vb, o); g_sink = fb(o[2]); break;
    case 19: m2_cop_scale3(-2.0f, g_va, o); g_sink = fb(o[2]); break;
    case 20: *cf = COP_IDENTITY; return 1;
    case 21: setpos(1.0f, 2.0f, 3.0f); return 1;
    case 22: scale(1.0f, 1.0f, 1.0f); return 1;
    case 23: *cf = COP_ANG_X; *cf = 0x1000u; return 1;
    case 24: *cf = COP_ANG_Y; *cf = 0x1000u; return 1;
    case 25: *cf = COP_ANG_Z; *cf = 0x1000u; return 1;
    case 26: rmat(m); g_sink = m[0]; break;
    case 27: *cf = COP_WMATRIX; for (j = 0; j < 12; j++) *cf = g_wm[j]; return 1;
    case 28: *cf = COP_PUSH; *cf = COP_POP; return 1;
    }
    return 0;
}
static int near(float got, float want) {
    float err = got - want, mag = want < 0 ? -want : want;
    if (err < 0) err = -err;
    return err <= (mag > 1.0f ? mag : 1.0f) * 1e-5f;
}
static int nat_check(int i) {
    float o[3];
    u32 m[12];
    int j;
    switch (i) {
    case 0:  return near(m2_cop_fadd(1.5f, 2.25f), 3.75f);
    case 1:  return near(m2_cop_fsub(5.0f, 1.25f), 3.75f);
    case 2:  return near(m2_cop_fmul(3.0f, -2.5f), -7.5f);
    case 3:  return near(m2_cop_fdiv(1.0f, 3.0f), 1.0f / 3.0f);
    case 4:  return near(m2_cop_int2f(-7), -7.0f);
    case 5:  return m2_cop_f2int(-3.7f) == -4;
    case 6:  return near(m2_cop_sqrt(2.0f), 1.41421356f);
    case 7:  return near(sin1(0x4000u), 1.0f);
    case 8:  return near(cos1(0x8000u), -1.0f);
    case 9:  return near(m2_cop_tan(0x2000u), 1.0f);
    case 10: return near(m2_cop_sin_scale(0x4000u, 3.0f), 3.0f);
    case 11: return near(m2_cop_cos_scale(0x2000u, 2.0f), 1.41421356f);
    case 12: return m2_cop_atan2(1.0f, 5.0f, -1.0f, 2.0f) == 0x1a37u;
    case 13: return near(dist2d(1.0f, 4.0f, 2.0f, 6.0f), 5.0f);
    case 14: return near(m2_cop_dist3d(0.0f, 2.0f, 0.0f, 3.0f, 0.0f, 6.0f), 7.0f);
    case 15: return near(m2_cop_mag2d(3.0f, 4.0f), 5.0f);
    case 16: return near(m2_cop_dot2d(2.0f, 3.0f, 4.0f, 5.0f), 26.0f);
    case 17: m2_cop_add3(g_va, g_vb, o); return near(o[0], 1.5f) && near(o[1], -2.0f) && near(o[2], 13.0f);
    case 18: m2_cop_sub3(g_va, g_vb, o); return near(o[0], 0.5f) && near(o[1], 6.0f) && near(o[2], -7.0f);
    case 19: m2_cop_scale3(-2.0f, g_va, o); return near(o[0], -2.0f) && near(o[1], -4.0f) && near(o[2], -6.0f);
    }
    *cf = COP_IDENTITY;
    switch (i) {
    case 21: setpos(1.0f, 2.0f, 3.0f); break;
    case 22: scale(2.0f, 3.0f, 4.0f); break;
    case 23: *cf = COP_ANG_X; *cf = 0x4000u; break;
    case 24: *cf = COP_ANG_Y; *cf = 0x4000u; break;
    case 25: *cf = COP_ANG_Z; *cf = 0x4000u; break;
    case 27: *cf = COP_WMATRIX; for (j = 0; j < 12; j++) *cf = g_wm[j]; break;
    case 28: *cf = COP_PUSH; setpos(5.0f, 0.0f, 0.0f); *cf = COP_POP; break;
    }
    rmat(m);
    switch (i) {
    case 21: return near(bf(m[9]), 1.0f) && near(bf(m[10]), 2.0f) && near(bf(m[11]), 3.0f);
    case 22: return near(bf(m[0]), 2.0f) && near(bf(m[4]), 3.0f) && near(bf(m[8]), 4.0f);
    case 23: return near(bf(m[7]), 1.0f) && near(bf(m[5]), -1.0f);   /* col2 = col1, col1 = -col2 */
    case 24: return near(bf(m[2]), 1.0f) && near(bf(m[6]), -1.0f);   /* col0 = col2, col2 = -col0 */
    case 25: return near(bf(m[3]), 1.0f) && near(bf(m[1]), -1.0f);   /* col1 = col0, col0 = -col1 */
    case 27: for (j = 0; j < 12; j++) if (m[j] != g_wm[j]) return 0; return 1;
    case 28: return near(bf(m[9]), 0.0f);
    }
    /* 20, 26: the identity */
    for (j = 0; j < 12; j++) if (m[j] != (j == 0 || j == 4 || j == 8 ? 0x3f800000u : 0u)) return 0;
    return 1;
}
static u32 fadd_ticks(void) {
    u32 t = vb_tstart(1);
    g_sink = fb(m2_cop_fadd(1.0f, 1.0f));
    return vb_tstop(1, t);
}
static void bench_native(int i) {
    u32 t, f, k;
    int quiet = 0;
    if (i == VB_NNAT - 1) {   /* SUBMIT: the scene's, per call */
        if (g_vb_nobj) g_vb_nat_ns[i] = g_sub_ticks * 40u / g_vb_nobj;
        return;
    }
    if (!nat_check(i)) g_vb_nat_bad |= 1u << i; else g_vb_nat_bad &= ~(1u << i);
    f = fadd_ticks();
    t = vb_tstart(1);
    for (k = 0; k < VB_K; k++) quiet = nat_call(i);
    if (quiet) g_sink = fb(m2_cop_fadd(1.0f, 1.0f));
    t = vb_tstop(1, t);
    if (quiet && t != VB_TSAT) t = t > f ? t - f : 0;
    g_vb_nat_ns[i] = VB_NS(t, VB_K);
}

/* ext case i: the buffer RAM words the cases read, the matrix (case 0, a WMATRIX), then the
 * command, timed to its last answer (or a FADD after it, less a FADD); the answers, anything
 * more it sends, and the matrix go into g_vb_ext_out[i]. */
static const u32 *g_ext_at[VB_NEXT];
static void ext_index(void) {
    const u32 *c = vb_cases;
    int i;
    for (i = 0; i < VB_NEXT; i++) { g_ext_at[i] = c; g_vb_ext_cmd[i] = c[0]; c += 3 + c[1]; }
}
static u32 g_on;
static volatile u32 *g_o;
static void out(u32 w) { if (g_on < VB_EXTW - 1u) g_o[++g_on] = w; }
static void bench_ext(int i) {
    const u32 *c = g_ext_at[i], *p = vb_bufs, *a = c + 3;
    u32 n = c[0], na = c[1], nr = c[2], j, k, t, f, idle;
    for (; p[1]; p += 2 + p[1])
        for (j = 0; j < p[1]; j++) ((volatile u32 *)GEO_BUFFERRAM)[p[0] + j] = p[2 + j];
    *cf = COP_WMATRIX;
    for (j = 0; j < 12; j++) *cf = g_ext_at[0][3 + j];
    f = fadd_ticks();
    g_o = g_vb_ext_out[i]; g_on = 0;
    t = vb_tstart(1);
    *cf = COP_CMD(n);
    if (n == 0x80) {   /* streamed, as stftest.c */
        for (j = 0; j < 4; j++) *cf = a[j];
        for (; a[j] != 0xffffffffu; j += 4) {
            *cf = a[j]; *cf = a[j + 1]; *cf = a[j + 2]; *cf = a[j + 3];
            out(*cf);
        }
        *cf = a[j]; *cf = a[j + 1];
        out(*cf);
    } else {
        for (j = 0; j < na; j++) *cf = a[j];
        for (j = 0; j < nr; j++) out(*cf);
        if (!nr) g_sink = fb(m2_cop_fadd(1.0f, 1.0f));
    }
    t = vb_tstop(1, t);
    if (n != 0x80 && !nr && t != VB_TSAT) t = t > f ? t - f : 0;
    for (idle = 0, k = 0; idle < 2000u && k < 64u; ) {
        if (*(volatile u32 *)COP_STATUS_REG & 1u) { idle++; continue; }
        out(*cf); k++; idle = 0;
    }
    *cf = COP_RMATRIX;
    for (j = 0; j < 12; j++) out(*cf);
    g_vb_ext_ns[i] = VB_NS(t, 1u);
    g_o[0] = g_on;
    g_vb_ext_done++;
}

/* ======================================================================================== text */
#if defined(M2_BOARD_2O)
#define VB_BOARD "MODEL 2  TGP + VM"
#elif defined(M2_BOARD_2A)
#define VB_BOARD "MODEL 2A  TGP + VM"
#elif defined(M2_BOARD_2C)
#define VB_BOARD "MODEL 2C  TGPX4 + VM"
#else
#define VB_BOARD "MODEL 2B  CPRES1 SHARC"
#endif
static void hud_text(void) {
    u32 i, tot = 0, done = g_vb_ext_done < VB_NEXT ? g_vb_ext_done : VB_NEXT, bad = 0;
    u32 slow[3] = { 0, 0, 0 }, sl[3] = { 0, 0, 0 };
    int r, c;
    for (r = 0; r < HUD_ROWS; r++) for (c = 0; c < HUD_COLS; c++) g_hud[r][c] = ' ';
    hud_puts(0, 0, "VAPORBENCH");
    hud_puts(0, 1, VB_BOARD);
    hud_uint(28, 1, g_vb_frame_us ? (1000000u + g_vb_frame_us / 2u) / g_vb_frame_us : 0u, 2);
    hud_puts(30, 1, "HZ");
    hud_puts(0, 2, "COP CALL (US)");
    for (i = 0; i < VB_NNAT; i++) {
        int col = (int)(i % 3u) * 11, row = 3 + (int)(i / 3u);
        hud_puts(col, row, g_nat_name[i]);
        if (g_vb_nat_bad & (1u << i)) { g_hud[row][col + 4] = '!'; bad++; }
        hud_us(col + 5, row, g_vb_nat_ns[i]);
    }
    hud_puts(19, 2, bad ? "BAD" : "ALL OK");
    if (bad) hud_uint(23, 2, bad, 2);
    hud_puts(0, 13, "SCENE");
    hud_uint(6, 13, g_vb_scene_us, 5);
    hud_puts(11, 13, "US");
    hud_uint(14, 13, g_vb_objs, 3);
    hud_puts(18, 13, "OBJECTS");
    for (i = 0; i < done; i++) {
        u32 ns = g_vb_ext_ns[i], j;
        tot += ns == VB_TSAT ? 4294967u : (ns + 500u) / 1000u;   /* us */
        for (j = 0; j < 3; j++)
            if (ns > slow[j]) {
                u32 k;
                for (k = 2; k > j; k--) { slow[k] = slow[k - 1]; sl[k] = sl[k - 1]; }
                slow[j] = ns; sl[j] = i;
                break;
            }
    }
    hud_puts(0, 14, "EXT");
    hud_uint(4, 14, done, 3);
    g_hud[14][7] = '/';
    hud_uint(8, 14, VB_NEXT, 3);
    hud_uint(13, 14, tot, 8);
    hud_puts(22, 14, "US ALL");
    hud_puts(0, 15, "SLOW");
    for (i = 0; i < 3 && slow[i]; i++) {
        hud_hex2(5 + (int)i * 9, 15, g_ext_at[sl[i]][0]);
        hud_us(8 + (int)i * 9, 15, slow[i]);
    }
}

#ifdef M2_GSX
/* ===================================================================== report over the link */
static char *rp_str(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *rp_uint(char *p, u32 v) {
    char d[10]; int n = 0;
    do { d[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = d[--n];
    return p;
}
static char *rp_ns(char *p, u32 ns) {   /* microseconds, 2 decimals */
    if (ns == VB_TSAT) return rp_str(p, "sat");
    p = rp_uint(p, ns / 1000u); *p++ = '.';
    *p++ = (char)('0' + ns % 1000u / 100u); *p++ = (char)('0' + ns % 100u / 10u);
    return p;
}
static const char vb_hex[] = "0123456789abcdef";
static void report(void) {
    char line[96], *p;
    u32 i, tot = 0;
    vb_gs_puts("\r\nvaporbench: " VB_BOARD " (geoserial app), us per call\r\n");
    for (i = 0; i < VB_NNAT; i++) {
        p = line;
        p = rp_str(p, g_nat_name[i]); *p++ = ' ';
        p = rp_ns(p, g_vb_nat_ns[i]);
        if (g_vb_nat_bad & (1u << i)) p = rp_str(p, " WRONG");
        p = rp_str(p, (i % 3u) == 2u ? "\r\n" : "   ");
        *p = 0; vb_gs_puts(line);
    }
    p = rp_str(line, "scene "); p = rp_uint(p, g_vb_scene_us); p = rp_str(p, " us, ");
    p = rp_uint(p, g_vb_objs); p = rp_str(p, " objects, frame ");
    p = rp_uint(p, g_vb_frame_us); p = rp_str(p, " us\r\next (cmd us):\r\n");
    *p = 0; vb_gs_puts(line);
    for (i = 0; i < VB_NEXT; i++) {
        u32 n = g_vb_ext_cmd[i], ns = g_vb_ext_ns[i];
        p = line;
        *p++ = vb_hex[n >> 4 & 15u]; *p++ = vb_hex[n & 15u]; *p++ = ' ';
        p = rp_ns(p, ns);
        p = rp_str(p, (i % 6u) == 5u || i + 1u == VB_NEXT ? "\r\n" : "  ");
        *p = 0; vb_gs_puts(line);
        tot += ns == VB_TSAT ? 4294967u : (ns + 500u) / 1000u;
    }
    p = rp_str(line, "ext total "); p = rp_uint(p, tot);
    p = rp_str(p, g_vb_nat_bad ? " us\r\nvaporbench: done, NATIVE ANSWERS WRONG\r\n"
                               : " us\r\nvaporbench: done, natives ALL OK\r\n");
    *p = 0; vb_gs_puts(line);
}

/* the m2_silicon_boot steps after the firmware upload, which the kernel has done: they set up
 * this program's own copies of the SDK's colour and display-list state */
static void vb_gsx_boot(void) {
    m2_color_init();
    m2_load_poly_palette();
    geo_func();
    geo_initialize();
    geometry_stuff();
}

/* On silicon a host byte waits in RXD1 only until the next one lands (the 315-5649 has no FIFO;
 * MAME queues them, so a poll once a frame passed there and lost PROG_EXIT on a real 2B). So the
 * frame reads the link the whole time it waits for vsync, about half of it. */
static int g_vb_exit;
static void vb_vsync(void) {
    u32 start = frameVBL;
    while (frameVBL == start)
        if (!g_vb_exit && vb_gs_poll_exit()) g_vb_exit = 1;
}

int app_entry(void) {
#else
int main(void) {
#endif
    u32 t = 0, tf, ts;
    int i;
    VB_STEP("init ", 0);
    m2_init();
#ifdef M2_GSX
    VB_STEP("boot ", 0);
    vb_gsx_boot();
    vb_gs_puts("\r\nvaporbench: running; the report follows the first ext sweep (~2 s)\r\n");
#else
    m2_silicon_boot();
#endif
    VB_STEP("patch ", 0);
    m2_obj_fill_patch();
    VB_STEP("quad ", 0);
    quad_init();
    ext_index();
    colours();
    VB_STEP("tex ", 0);
    m2_texram_fill(M2_BOARD_TEXRAM0, HUD_TX, HUD_TY, HUD_COLS * 8, HUD_ROWS * 8, 0xFu);
    m2_texram_fill(M2_BOARD_TEXRAM1, HUD_TX, HUD_TY, HUD_COLS * 8, HUD_ROWS * 8, 0xFu);
    { int r, c; for (r = 0; r < HUD_ROWS; r++) for (c = 0; c < HUD_COLS; c++) g_hud_shown[r][c] = ' '; }
    /* the mesh, the colour headers and the HUD's headers and UVs stay in polygon / texture RAM */
    for (i = 0; i < 8; i++) {
        u32 cb;
        VB_STEP("prime ", i);
        m2_frame_begin();
        quad_upload();
        for (cb = 1; cb < CB_HUD; cb++) m2_obj_color_header(cb);
        hud_tex_setup();
        m2_frame_end();
    }
    tf = vb_tstart(0);
    for (;;) {
        VB_STEP("nat ", t % VB_NNAT);
        bench_native((int)(t % VB_NNAT));
        VB_STEP("ext ", g_vb_ext_cmd[t % VB_NEXT]);
        bench_ext((int)(t % VB_NEXT));
        VB_STEP("scene ", t);
        m2_frame_begin();
        m2_obj_frame_setup();
        g_vb_nobj = 0; g_sub_ticks = 0;
        ts = vb_tstart(2);
        sky();
        sun(t);
        grid(t);
        cubes(t);
        ts = vb_tstop(2, ts);
        g_vb_scene_us = ts == VB_TSAT ? 999999u : ts / 25u;
        VB_STEP("hud ", t);
        hud_draw();
        g_vb_objs = g_vb_nobj;
        VB_STEP("commit ", t);
        m2_frame_commit();
        hud_text();
        hud_flush();
        VB_STEP("vsync ", t);
#ifdef M2_GSX
        vb_vsync();
#else
        m2_vsync();
#endif
        { u32 e = vb_tstop(0, tf); tf = vb_tstart(0); if (e != VB_TSAT) g_vb_frame_us = e / 25u; }
        g_vb_frames = ++t;
#ifdef M2_GSX
        if (t == (u32)VB_NEXT + 1u) {
#ifdef VB_TRACE
            g_trace_on = 0;
#endif
            report();
        }   /* every ext case has run once */
        if (g_vb_exit) return 0;                /* PROG_EXIT: back to the manager */
#endif
    }
    return 0;
}
