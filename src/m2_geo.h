/*
 * m2_geo.h — descriptive builder for the Model 2 GEO display list.
 *
 * The GEO chip (cpres2 on hardware; MAME's geo_parse) rasterizes a display list
 * the i960 places in bufferram, which is i960-mapped at 0x00900000. Each command
 * is an opcode word — cmd = (word >> 23) & 0x1f — optionally followed by 32-bit
 * args. This header builds that list with *named commands* instead of raw hex,
 * then flushes it to bufferram and points geo_read_start at it.
 *
 * geo_parse runs every vblank, so you build once and hold. See the reverse-
 * engineered format notes in memory/mame-geo-display-list.
 *
 *   geo_begin();
 *   geo_zsort(0x40800000);
 *   geo_mode(GEO_MODE_NN_S);
 *   geo_texparam_flat(0xFF, 0x60);     // diffuse, ambient (needed for lighting)
 *   geo_focal(280.0f, 280.0f);
 *   geo_light(0.706f, -0.693f, 0.145f);
 *   geo_window_fullscreen();
 *   geo_matrix_identity_z(7.19f);      // identity rotation, depth +7.19
 *   geo_object_from_table(4203);       // STF model-table entry -> tpa/tha/oba
 *   geo_end();
 *   geo_flush(0x10000);                // write to bufferram + set read_start
 */
#ifndef M2_GEO_H
#define M2_GEO_H

#include "m2.h"
#include "m2_3d.h"   /* COP/DSP float helpers used by the object_data primitives */

#define GEO_BUFFERRAM   0x00900000u   /* m_bufferram, i960-mapped               */
#define GEO_START       0x00800000u   /* g10 in STF — GEO command region        */
#define GEO_READ_REG    0x00803008u   /* write -> geo_read_start                */

/* Opcode words (cmd in bits[28:23]) — STF's encodings, verified vs geo_parse. */
#define GEO_OP_OBJECT     0x00800101u  /* 01 object_data: tpa, tha, oba, obc    */
#define GEO_OP_WINDOW     0x01800303u  /* 03 window/clip: 6 coords              */
#define GEO_OP_TEXPARAM   0x03000606u  /* 06 texture params: index, count, ...  */
#define GEO_OP_MODE       0x03800707u  /* 07 geo mode (&3 selects vtx parser)   */
#define GEO_OP_ZSORT      0x04000808u  /* 08 zsort mode                         */
#define GEO_OP_FOCAL      0x04800909u  /* 09 focal distance: fx, fy             */
#define GEO_OP_LIGHT      0x05000A0Au  /* 0a light vector: x, y, z              */
#define GEO_OP_MATRIX     0x05800B0Bu  /* 0b transform matrix: 12 floats        */
#define GEO_OP_LOD        0x0B001616u  /* 16 LOD                                */
#define GEO_OP_END        0x07800F0Fu  /* 0f end of list                        */

/* geo mode low 2 bits select the polygon vertex parser. */
#define GEO_MODE_NP_NS  0u   /* normals present, no specular */
#define GEO_MODE_NP_S   1u   /* normals present, specular    */
#define GEO_MODE_NN_NS  2u   /* no normals,      no specular */
#define GEO_MODE_NN_S   3u   /* no normals,      specular    */

/* STF model table: entry N = MODEL_TABLE + N*16 = {uv, material, mesh, w3},
 * which map directly to object_data {tpa, tha, oba, (obc bound)}. */
#define GEO_MODEL_TABLE  0x020E0004u

/* Display-list build buffer. Sized to hold many objects in one list (e.g. the
   ice-cube physics demo): bufferram has 0x4000 u32 free past read_start=0x10000,
   and each object_data is ~18 words, so 0x1000 holds ~220 objects. */
static u32 g_geo[0x2000];
static u32 g_geo_n;

static u32  geo__f(float f) { union { float f; u32 u; } x; x.f = f; return x.u; }
static void geo__w(u32 v)   { if (g_geo_n < 0x2000) g_geo[g_geo_n++] = v; }

static void geo_begin(void) { g_geo_n = 0; }

static void geo_zsort(u32 v)            { geo__w(GEO_OP_ZSORT); geo__w(v); }
static void geo_mode(u32 m)             { geo__w(GEO_OP_MODE);  geo__w(m); }
static void geo_lod(float v)            { geo__w(GEO_OP_LOD);   geo__w(geo__f(v)); }
static void geo_focal(float x, float y) { geo__w(GEO_OP_FOCAL); geo__w(geo__f(x)); geo__w(geo__f(y)); }
static void geo_light(float x, float y, float z) {
    geo__w(GEO_OP_LIGHT); geo__w(geo__f(x)); geo__w(geo__f(y)); geo__w(geo__f(z));
}

/* STF's standard full-screen clip window (start, end, 4 eye-mode vanishing pts). */
static void geo_window_fullscreen(void) {
    geo__w(GEO_OP_WINDOW);
    geo__w(0x0000007Fu); geo__w(0x01F001FFu);
    geo__w(0x00F8013Fu); geo__w(0x00F8013Fu); geo__w(0x00F8013Fu); geo__w(0x00F8013Fu);
}

/* Fill all 0x20 texture-param slots with one diffuse/ambient (flat lit). A
 * standalone list lacks the persistent texparam state STF carries between frames;
 * without it luma = luminance*diffuse + ambient = 0 and the object renders black. */
static void geo_texparam_flat(u8 diffuse, u8 ambient) {
    u32 param = (u32)diffuse | ((u32)ambient << 8);
    int i;
    geo__w(GEO_OP_TEXPARAM); geo__w(0u); geo__w(0x20u);
    for (i = 0; i < 0x20; i++) { geo__w(param); geo__w(geo__f(1.0f)); }
}

/* 12-float transform: 3x3 rotation (column-major triples) + translation [9..11]. */
static void geo_matrix(const float m[12]) {
    int i; geo__w(GEO_OP_MATRIX); for (i = 0; i < 12; i++) geo__w(geo__f(m[i]));
}
/* Identity rotation, translation (0,0,z): centers a unit-scale model at depth z. */
static void geo_matrix_identity_z(float z) {
    float m[12] = { 1,0,0, 0,1,0, 0,0,1, 0,0,0 };
    m[11] = z;
    geo_matrix(m);
}

/* object_data: geometry from polygon ROM (oba bit23). obc bounds the strip; the
 * mesh self-terminates (attr&3==0), so a generous cap is safe. */
static void geo_object(u32 tpa, u32 tha, u32 oba, u32 obc) {
    geo__w(GEO_OP_OBJECT); geo__w(tpa); geo__w(tha); geo__w(oba); geo__w(obc);
}
/* Draw STF model-table entry N: {uv, material, mesh} -> {tpa, tha, oba}. */
static void geo_object_from_table(u32 n) {
    volatile u32 *e = (volatile u32 *)(GEO_MODEL_TABLE + n * 16u);
    geo_object(e[0], e[1], e[2], 0x200u);
}

/* ---- custom vector geometry via direct_data (cmd 0x02) -------------------- *
 * Emit flat-colored polygons/lines. Coords are IEEE-754 floats (process_polygon
 * reads u2f(word<<8)). NOTE: direct_data is projected as screen = center + x/z
 * (NO focal), unlike object_data which bakes geo_focal into its matrix — so to
 * align a line with an object at world (X,Y,Z) supply (focal*X, focal*Y, Z).
 * Color is flat: a texture-header slot in texture-header RAM carries the colorbase
 * (th0=0 untextured); object.luma = (lumaword>>23)&0xff. */
#define GEO_OP_DIRECT   0x01000202u
#define GEO_OP_TEXDATA  0x02000404u
#define GEO_TEXRAM_BIT  0x00800000u

/* Pure-C Newton sqrt. NOTE: do NOT route this through m2_cop_sqrt — COP_SQRT is
 * always "defined" (it's the opcode macro, not a feature flag), and depending on
 * the COP here hangs the render loop if the COP math FIFO doesn't answer. The COP
 * is only an accelerator; under soft-float the C path is correct and safe. */
static float geo__sqrt(float x) {
    float g; int i;
    if (x <= 0.0f) return 0.0f;
    g = x; for (i = 0; i < 8; i++) g = 0.5f * (g + x / g);
    return g;
}

/* Write an untextured colour header into texture-header RAM slot cb (th3=cb<<6,
 * colorbase==cb). Reference via tha = GEO_TEXRAM_BIT | (cb*4). Emit once per frame. */
static void geo_color_header(u32 cb) {
    geo__w(GEO_OP_TEXDATA);
    geo__w(GEO_TEXRAM_BIT | (cb * 4u));
    geo__w(4u);
    geo__w(0u); geo__w(0u); geo__w(0u);     /* th0=0 (opaque solid), th1=0, th2=0 */
    geo__w((cb & 0x3ffu) << 6);             /* th3: colorbase = (th3>>6)&0x3ff */
}

/* Flat-colored quad (v0,v1,v2,v3 cyclic). */
static void geo_quad(const float v0[3], const float v1[3],
                     const float v2[3], const float v3[3], u32 cb) {
    geo__w(GEO_OP_DIRECT);
    geo__w(0u);
    geo__w(GEO_TEXRAM_BIT | (cb * 4u));
    geo__w(geo__f(v1[0])); geo__w(geo__f(v1[1])); geo__w(geo__f(v1[2]));
    geo__w(geo__f(v0[0])); geo__w(geo__f(v0[1])); geo__w(geo__f(v0[2]));
    geo__w(1u | (1u << 8) | (1u << 17));    /* quad, linktype 1, doubleside */
    geo__w(0xFFu << 23);                    /* object.luma = 0xFF */
    geo__w(0u);
    geo__w(geo__f(v2[0])); geo__w(geo__f(v2[1])); geo__w(geo__f(v2[2]));
    geo__w(geo__f(v3[0])); geo__w(geo__f(v3[1])); geo__w(geo__f(v3[2]));
    geo__w(0u); geo__w(0u);
}

/* Thin-quad line from a to b, half-width w, widened perpendicular in the XY plane. */
static void geo_line(const float a[3], const float b[3], float w, u32 cb) {
    float dx = b[0] - a[0], dy = b[1] - a[1];
    float n = geo__sqrt(dx * dx + dy * dy);
    float px, py, v0[3], v1[3], v2[3], v3[3];
    if (n < 0.0001f) { px = w; py = 0.0f; } else { px = -dy * w / n; py = dx * w / n; }
    v0[0]=a[0]+px; v0[1]=a[1]+py; v0[2]=a[2];
    v1[0]=a[0]-px; v1[1]=a[1]-py; v1[2]=a[2];
    v2[0]=b[0]-px; v2[1]=b[1]-py; v2[2]=b[2];
    v3[0]=b[0]+px; v3[1]=b[1]+py; v3[2]=b[2];
    geo_quad(v0, v1, v2, v3, cb);
}

/* ---- PORTABLE custom geometry: instanced flat ROM quad via object_data --------
 * direct_data (above) renders only on MAME's HLE'd GEO; m2emulator and real
 * hardware draw nothing for it. Instead instance model-table entry 456 (a clean
 * 1-face 12x12 quad, local +-6 in XZ at y=0) through object_data + a per-shape
 * matrix. Flat color via a tha override to the colorbase header (single face takes
 * tha directly). col1 -> world -Z so the camera-facing quad isn't back-face culled.
 * Coords are whatever world space the current geo_focal projects (e.g. focal(1,1)
 * for screen-space (sx,sy,z) layout). Matrix math runs on the DSP. */
#define FLAT_QUAD_MODEL 456u
static u32 g_flatquad;   /* model-456 mesh pointer (oba) */
static void geo_flatquad_init(void) {
    g_flatquad = *(volatile u32 *)(GEO_MODEL_TABLE + FLAT_QUAD_MODEL * 16u + 8u);
}

/* DSP-backed vec3 ops: the SHARC does every add/sub/mul (the SDK convention). */
static void v3sub(float o[3], const float a[3], const float b[3]) {
    o[0] = m2_cop_fsub(a[0], b[0]); o[1] = m2_cop_fsub(a[1], b[1]); o[2] = m2_cop_fsub(a[2], b[2]);
}
static void v3cross(float o[3], const float a[3], const float b[3]) {
    o[0] = m2_cop_fsub(m2_cop_fmul(a[1], b[2]), m2_cop_fmul(a[2], b[1]));
    o[1] = m2_cop_fsub(m2_cop_fmul(a[2], b[0]), m2_cop_fmul(a[0], b[2]));
    o[2] = m2_cop_fsub(m2_cop_fmul(a[0], b[1]), m2_cop_fmul(a[1], b[0]));
}
static float v3dot(const float a[3], const float b[3]) {
    return m2_cop_fadd(m2_cop_fadd(m2_cop_fmul(a[0], b[0]), m2_cop_fmul(a[1], b[1])),
                       m2_cop_fmul(a[2], b[2]));
}

/* Flat-colored line a->b (full width w) as an instanced flat quad. Works for any
 * 3D line orientation (including lines running into the screen along z): col0 is the
 * full 3D direction, and the width direction is dir x view-ray (camera at origin ->
 * midpoint), which always lands in the screen plane perpendicular to the line. The
 * DSP (m2_cop_sqrt) does the two normalisations; the cross products are scalar. */
static void geo_obj_line(const float a[3], const float b[3], float w, u32 cb) {
    float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    float mx = (a[0] + b[0]) * 0.5f, my = (a[1] + b[1]) * 0.5f, mz = (a[2] + b[2]) * 0.5f;
    /* width perp = dir x midpoint (screen-plane perpendicular to the line) */
    float px = dy * mz - dz * my, py = dz * mx - dx * mz, pz = dx * my - dy * mx;
    float pn = geo__sqrt(px * px + py * py + pz * pz);
    float nx, ny, nz, nn, hw, m[12];
    if (pn < 1e-6f) { px = 1.0f; py = 0.0f; pz = 0.0f; pn = 1.0f; }  /* line on the view axis */
    hw = (w * 0.5f) / pn;
    px *= hw; py *= hw; pz *= hw;                       /* half-width vector */
    nx = dy * pz - dz * py; ny = dz * px - dx * pz; nz = dx * py - dy * px;  /* dir x perp */
    nn = geo__sqrt(nx * nx + ny * ny + nz * nz);
    if (nn < 1e-6f) nn = 1.0f;
    if (nx * mx + ny * my + nz * mz > 0.0f) nn = -nn;  /* orient the normal toward the camera */
    m[0] = dx / 12.0f; m[1] = dy / 12.0f; m[2] = dz / 12.0f;   /* col0: model +x -> half-line  */
    m[3] = nx / nn;    m[4] = ny / nn;    m[5] = nz / nn;       /* col1: normal toward camera   */
    m[6] = px / 6.0f;  m[7] = py / 6.0f;  m[8] = pz / 6.0f;     /* col2: model +z -> half-width */
    m[9] = mx; m[10] = my; m[11] = mz;
    geo_matrix(m);
    geo_object(0u, GEO_TEXRAM_BIT | (cb * 4u), g_flatquad, 0x200u);
}

/* Fill a planar quad (cyclic v0,v1,v2,v3) with one flat color. Affine-maps model
   456 onto the parallelogram (exact for parallelograms; near-exact for small cells).
   col1 carries the quad's REAL geometric normal (u x v) so the GEO lights each quad
   per its own angle (luma = |normal.light|*diffuse + ambient): panels facing the
   light come out bright, those facing away dim to ambient -> the tube is shaded.
   Model 456's verts are y=0, so col1 affects only the normal, not the quad shape. */
static void geo_obj_quad(const float v0[3], const float v1[3],
                         const float v2[3], const float v3[3], u32 cb) {
    float m[12], nx, ny, nz, nn;
    m[0] = ((v1[0]-v0[0]) + (v2[0]-v3[0])) / 24.0f;   /* col0: u edge */
    m[1] = ((v1[1]-v0[1]) + (v2[1]-v3[1])) / 24.0f;
    m[2] = ((v1[2]-v0[2]) + (v2[2]-v3[2])) / 24.0f;
    m[6] = ((v0[0]-v3[0]) + (v1[0]-v2[0])) / 24.0f;   /* col2: v edge */
    m[7] = ((v0[1]-v3[1]) + (v1[1]-v2[1])) / 24.0f;
    m[8] = ((v0[2]-v3[2]) + (v1[2]-v2[2])) / 24.0f;
    m[9]  = (v0[0]+v1[0]+v2[0]+v3[0]) * 0.25f;        /* T: center */
    m[10] = (v0[1]+v1[1]+v2[1]+v3[1]) * 0.25f;
    m[11] = (v0[2]+v1[2]+v2[2]+v3[2]) * 0.25f;
    nx = m[1]*m[8] - m[2]*m[7];                        /* col1: u x v (real normal) */
    ny = m[2]*m[6] - m[0]*m[8];
    nz = m[0]*m[7] - m[1]*m[6];
    if (nx*m[9] + ny*m[10] + nz*m[11] > 0.0f) { nx = -nx; ny = -ny; nz = -nz; }  /* inward (we see the rear/inner wall) */
    nn = geo__sqrt(nx*nx + ny*ny + nz*nz);
    if (nn < 1e-9f) nn = 1.0f;
    m[3] = nx/nn; m[4] = ny/nn; m[5] = nz/nn;
    geo_matrix(m);
    geo_object(0u, GEO_TEXRAM_BIT | (cb * 4u), g_flatquad, 0x200u);
}

static void geo_end(void) { geo__w(GEO_OP_END); }

/* Write the built list into bufferram at read_start and point the GEO at it.
 * The builder emits no jumps, so any read_start offset works. */
static void geo_flush(u32 read_start) {
    volatile u32 *buf = (volatile u32 *)(GEO_BUFFERRAM + (read_start & 0x1ffffu));
    u32 i;
    for (i = 0; i < g_geo_n; i++) buf[i] = g_geo[i];
    *(volatile u32 *)GEO_READ_REG = read_start;
}

/* ---- STF-faithful 4-buffer rotating display list -------------------------- *
 * STF (Ghidra: geo_initialize @0x169c, set_end_mark @0x354c; g10=GEO_START
 * 0x800000) builds each frame into one of FOUR bufferram buffers and commits it
 * to geo_read_start (0x803008), then rotates to the next. The 4 buffers are
 * BUFF_RAM 0x900000/0x908000/0x910000/0x918000 = bufferram offsets 0/0x8000/
 * 0x10000/0x18000 (read/write_start mask to the 0xfffff window). The GEO renders
 * the committed buffer while the i960 fills the next, so it never parses a
 * half-built list (which a single fixed buffer risks). STF's 0xf0f write to the
 * END register is just the END opcode (0x07800f0f) — already our geo_end() word. */
#define GEO_CTL_REG     0x0098000Cu   /* STF clears this in geo_initialize        */
#define GEO_WRITE_REG   0x00801008u   /* geo_write_start  (g10+0x1008)            */
#define GEO_ZCLIP_REG   0x0181C000u   /* _3D_ZCLIP_START                          */
#define GEO_NBUF        4u
static const u32 geo_buf_off[GEO_NBUF] = { 0x00000u, 0x08000u, 0x10000u, 0x18000u };
static u32 g_geo_buf;                  /* current build buffer (0..3)             */

/* GEO_RELATED config table — STF geo_func @0x1550 uploads these 64 halfwords into
 * the GEO command region (GEO_START + i*0x10) BEFORE geo_initialize. Four 16-entry
 * blocks (one per display-list buffer); block 0 is the rich header, 1..3 are the
 * sparse per-buffer headers. */
static const u16 geo_related[64] = {
    0x0000,0x0004,0x8808,0x0006,0x0101,0x0101,0x0201,0x0001,0x0001,0x0002,0x0003,0x000C,0x0003,0x0003,0x0320,0x0000,
    0x0001,0x0004,0x8808,0x0000,0x0101,0x0000,0x0001,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0x0001,0x0004,0x8808,0x0000,0x0101,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0x0000,0x0004,0x8808,0x0000,0x0101,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000
};

/* STF geo_func @0x1550: prime the GEO command region with the GEO_RELATED table.
 * For each of 64 entries: halfword H -> GEO_START+i*0x10+4, (H>>8) -> +8 (the
 * System-24 GEO entry split). STF runs this BEFORE geo_initialize; without these
 * per-buffer command headers the GEO has no valid list framing and faults when it
 * parses ours. Call after the GEO is booted, before geo_initialize. */
static void geo_func(void) {
    volatile u32 *geo = (volatile u32 *)GEO_START;
    int i;
    for (i = 0; i < 64; i++) {
        u32 h = geo_related[i];
        geo[i * 4u + 1u] = h;          /* slot+4 = halfword     */
        geo[i * 4u + 2u] = h >> 8;     /* slot+8 = high byte     */
    }
}

/* Zero the whole 4-buffer region (0x8000 words = 0x20000 bytes), then write the END
 * opcode to each of the 4 buffer heads (STF init_0 @0x1FC). Kills the GEO striping
 * (uninitialised BUFF_RAM parsed as bogus polygons). Called from geo_initialize after
 * the GEO_CTL clear: it can't run before the COP boot (iop-1st poison) nor before
 * m2_init / geo_func / GEO_CTL clear (m2emu won't map BUFF_RAM yet → black screen), so
 * this late spot is the only one that boots on hardware + MAME + m2emu. */
static void geo_buffram_clear(void) {
    volatile u32 *buf = (volatile u32 *)GEO_BUFFERRAM;
    u32 i, b;
    for (i = 0; i < 0x8000u; i++) buf[i] = 0u;
    for (b = 0; b < GEO_NBUF; b++)
        *(volatile u32 *)(GEO_BUFFERRAM + geo_buf_off[b]) = GEO_OP_END;
}

/* One-time GEO display-list setup (STF geo_initialize @0x169C): clear GEO ctl, prime
 * the four buffers empty, point the GEO at buffer 0, set ZCLIP. Call after the GEO is
 * booted and after geo_buffram_clear(), before the first frame. */
static void geo_initialize(void) {
    *(volatile u32 *)GEO_CTL_REG = 0u;                    /* clear GEO ctl (0x98000c) */
    geo_buffram_clear();                                  /* zero BUFF_RAM + END heads */
    g_geo_buf = 0u;
    *(volatile u32 *)GEO_WRITE_REG = geo_buf_off[1];      /* next write target        */
    *(volatile u32 *)GEO_READ_REG  = geo_buf_off[0];      /* GEO reads buffer 0       */
    *(volatile u8  *)GEO_ZCLIP_REG = 0xFFu;               /* ZCLIP                    */
}

/* Commit the built list into the current buffer, point the GEO at it, then rotate
 * to the next buffer (STF set_end_mark). Use in place of geo_flush() for the
 * double-buffered draw loop. Each buffer is 0x8000 bytes (0x2000 u32) — exactly
 * the g_geo[] cap — and the list self-terminates with GEO_OP_END. */
static void geo_flush_flip(void) {
    u32 off = geo_buf_off[g_geo_buf];
    volatile u32 *buf = (volatile u32 *)(GEO_BUFFERRAM + off);
    u32 i;
    for (i = 0; i < g_geo_n; i++) buf[i] = g_geo[i];
    *(volatile u32 *)GEO_READ_REG = off;                 /* commit -> display        */
    g_geo_buf = (g_geo_buf + 1u) & 3u;                   /* flip                     */
    *(volatile u32 *)GEO_WRITE_REG = geo_buf_off[g_geo_buf];
}

/* Flush an empty list so the GEO renders nothing (clears the 3D plane, e.g. when
   a 3D game returns to the 2D launcher menu). */
static void geo_clear(void) { geo_begin(); geo_end(); geo_flush(0x10000u); }

#endif /* M2_GEO_H */
