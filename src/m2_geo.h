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

/* ---- direct-FIFO render-state helpers (shared by the m2_draw / m2_obj frame setups) ----
 * These push the GEO command FIFO at 0x804000 IMMEDIATELY — unlike geo_window_fullscreen /
 * geo_texparam_flat above, which BUFFER into the g_geo[] display list. Both per-frame setups
 * emit the same window clip + a 0x20-entry TEXPARAM table + a 3-vector LIGHT; only the TEXPARAM
 * material word and the light vector differ per path, so those are arguments. */
static void m2_geo_fifo_window_full(void) {
    volatile u32 *fifo = (volatile u32 *)0x00804000u;
    m2_geo_cmd(0x030u);             /* set_window slot 0x30 */
    *fifo = 0x0000007Fu; *fifo = 0x01F001FFu;               /* start / end clip rect */
    *fifo = 0x00F8013Fu; *fifo = 0x00F8013Fu; *fifo = 0x00F8013Fu; *fifo = 0x00F8013Fu;
}
static void m2_geo_fifo_texparam(u32 param) {               /* m2_draw: 0x10FF, m2_obj: 0x60FF */
    volatile u32 *fifo = (volatile u32 *)0x00804000u;
    int q; *fifo = 0x03000606u; *fifo = 0u; *fifo = 0x20u;
    for (q = 0; q < 0x20; q++) { *fifo = param; *fifo = 0x3F800000u; }
}
static void m2_geo_fifo_light(u32 x, u32 y, u32 z) {        /* slot 0xA0 + 3 light-vector words */
    volatile u32 *fifo = (volatile u32 *)0x00804000u;
    m2_geo_cmd(0x0A0u);
    *fifo = x; *fifo = y; *fifo = z;
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
/* direct_data polygon attribute word: quad | linktype 1 | doubleside (== 0x00020101). */
#define GEO_POLY_QUAD   (1u | (1u << 8) | (1u << 17))

/* sqrt is shared: m2_sqrtf (m2.h). Kept off the COP deliberately — m2_cop_sqrt can
 * hang the render loop if the COP math FIFO doesn't answer; the C path is correct
 * under soft-float and needs no libm. */

/* Write a colour header into texture-header RAM slot cb (th3=cb<<6, colorbase==cb).
 * Reference via tha = GEO_TEXRAM_BIT | (cb*4). Emit once per frame.
 * th0 = TEXTURED 128x128 (bit14=0x4000 | wbits2 | hbits2<<3): the STF models we instance
 * (e.g. 456) carry textured faces, so on the real GEO a textured header paired with a
 * TEXRAM pre-filled to a uniform opaque luma yields a clean flat colorbase fill. An
 * untextured th0=0 made the textured model sample garbage TEXRAM = stripe noise on silicon
 * (it only looked flat on MAME's lenient HLE). See memory/geo-texram-rendering. */
static void geo_color_header(u32 cb) {
    geo__w(GEO_OP_TEXDATA);
    geo__w(GEO_TEXRAM_BIT | (cb * 4u));
    geo__w(4u);
    geo__w(0x4012u); geo__w(0u); geo__w(0u);  /* th0=textured 128x128, th1=lumabase0, th2=(0,0) */
    geo__w((cb & 0x3ffu) << 6);               /* th3: colorbase = (th3>>6)&0x3ff */
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
    float n = m2_sqrtf(dx * dx + dy * dy);
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
    float pn = m2_sqrtf(px * px + py * py + pz * pz);
    float nx, ny, nz, nn, hw, m[12];
    if (pn < 1e-6f) { px = 1.0f; py = 0.0f; pz = 0.0f; pn = 1.0f; }  /* line on the view axis */
    hw = (w * 0.5f) / pn;
    px *= hw; py *= hw; pz *= hw;                       /* half-width vector */
    nx = dy * pz - dz * py; ny = dz * px - dx * pz; nz = dx * py - dy * px;  /* dir x perp */
    nn = m2_sqrtf(nx * nx + ny * ny + nz * nz);
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
/* Compute the per-quad transform matrix (the float-heavy part). Split out so STATIC
 * quads can compute it ONCE and cache it, keeping all soft-float out of the per-frame
 * draw loop — a vblank IRQ firing inside this deep soft-float chain crashes m2emu
 * (register-cache spill + interrupt). Compute it with interrupts masked to be safe. */
static void geo_quad_matrix(const float v0[3], const float v1[3],
                            const float v2[3], const float v3[3], float m[12]) {
    float nx, ny, nz, nn;
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
    nn = m2_sqrtf(nx*nx + ny*ny + nz*nz);
    if (nn < 1e-9f) nn = 1.0f;
    m[3] = nx/nn; m[4] = ny/nn; m[5] = nz/nn;
}

/* Emit a quad object from a precomputed matrix — NO float math (geo__f is a bitcast),
 * so this is safe to call every frame with interrupts armed. */
static void geo_quad_emit(const float m[12], u32 cb) {
    geo_matrix(m);
    geo_object(0u, GEO_TEXRAM_BIT | (cb * 4u), g_flatquad, 0x200u);
}

/* Combined compute+emit (for dynamic quads). Static quads should precompute the matrix
 * once via geo_quad_matrix and reuse geo_quad_emit. */
static void geo_obj_quad(const float v0[3], const float v1[3],
                         const float v2[3], const float v3[3], u32 cb) {
    float m[12];
    geo_quad_matrix(v0, v1, v2, v3, m);
    geo_quad_emit(m, cb);
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

static void geo_set_end_mark(void);   /* fwd decl: STF set_end_mark, used by geo_initialize */

/* One-time GEO display-list setup — FAITHFUL port of STF geo_initialize @0x169C.
 * The OLD version only zeroed BUFF_RAM from the i960 and poked the read/write regs once,
 * leaving the GEO's OWN internal write pointers / bank state uninitialised -> on silicon
 * the geometrizer rasterised a structured repeating pattern from boot (the vertical-bar
 * stripe noise). STF instead drives an empty list THROUGH the GEO for each of buffers
 * 0..2 (point GEO_WRITE at the buffer, push slot 0x0 x3, set the slot-0xF0 end mark),
 * then M2_MEM.buff_add=buf3 + set_end_mark — so the GEO initialises each buffer itself.
 * NOTE: this is the absolute-address path the m2emu/MAME HLE mishandles (it fakes the
 * GEO); we accept that divergence here — silicon is the target. */
static void geo_initialize(void) {
    volatile u32 *g10 = (volatile u32 *)GEO_START;        /* GEO command region (0x800000) */
    u32 r9, buf;
    *(volatile u32 *)GEO_CTL_REG = 0u;                    /* clear GEO ctl (0x98000c)       */
    geo_buffram_clear();                                  /* init_0: bulk-zero + END heads  */
    M2_MEM.buff_max = 0u;                    /* BUFF_MAX = 0                   */
    for (r9 = 0u; r9 < 3u; r9++) {                        /* STF loop r9=0..2 (buffers 0-2) */
        buf = GEO_BUFFERRAM + geo_buf_off[r9];
        *(volatile u32 *)GEO_WRITE_REG = buf;             /* GEO_WRITE = this buffer        */
        g10[0] = 0u; g10[0] = 0u; g10[0] = 0u;            /* slot 0x0 x3 (empty list head)  */
        m2_geo_cmd(0x0F0u);       /* slot 0xF0 = end-mark command   */
    }
    buf = GEO_BUFFERRAM + geo_buf_off[3];                 /* buffer 3                       */
    M2_MEM.buff_add = buf;                                /* current DL buffer = buffer 3   */
    *(volatile u32 *)GEO_WRITE_REG = buf;                 /* GEO_WRITE = buffer 3           */
    M2_MEM.buff_index = 3u;                               /* buffer index = 3               */
    geo_set_end_mark();                                   /* commit buf3 (empty), flip -> 0 */
    *(volatile u8  *)GEO_ZCLIP_REG = 0xFFu;               /* ZCLIP                          */
    g_geo_buf = 0u;
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
    /* GEO_READ_REG/WRITE_REG take the buffer OFFSET. The GEO masks to 0x7FFFC so abs vs
     * offset is identical on silicon — the absolute form + a per-frame 0xF0F end-mark were
     * tried but are HLE-incompatible (MAME's faked GEO corrupts over a few frames). */
    *(volatile u32 *)GEO_READ_REG = off;                 /* commit -> display        */
    g_geo_buf = (g_geo_buf + 1u) & 3u;                   /* flip                     */
    *(volatile u32 *)GEO_WRITE_REG = geo_buf_off[g_geo_buf];
}

/* ==== STF buffer-bank model + geometry_stuff (faithful port) ================
 * STF's GEO region init runs through a command FIFO and is bracketed by its real
 * buffer-bank + vsync-IRQ machinery. Requires the armed vblank IRQ (M2_MEM.vsync
 * increments each vsync). Software globals live at their exact STF work-RAM
 * addresses (linker symbols). */
/* The STF work-RAM globals (poly_bank / buff_add / buff_max / buff_index, and the vsync
 * counter) are M2_MEM.<field> — see m2_memory.h (pulled in via m2.h). */

static const u32 geo_buff_ram_adds[GEO_NBUF] = {
    0x900000u, 0x908000u, 0x910000u, 0x918000u    /* STF BUFF_RAM_ADDS */
};

/* STF change_poly_bank @0x3534: cache the COP poly bank (low byte of 0x98000C). */
static void change_poly_bank(void) {
    M2_MEM.poly_bank = (u8)(*(volatile u32 *)0x0098000Cu);
}

/* STF interrupt_wait @0x1768: spin until M2_MEM.vsync >= 2 with bit0 clear (two
 * vsync IRQs), clear it, then change_poly_bank. */
static void geo_interrupt_wait(void) {
    while ((M2_MEM.vsync < 2u) || (M2_MEM.vsync & 1u)) { }
    M2_MEM.vsync = 0u;
    change_poly_bank();
}

/* STF set_end_mark @0x354C: write the END mark (0xF0F -> g10+0xF0), record the
 * current buffer (g10+0x3008), advance the 0..3 buffer index, point the GEO at the
 * next buffer (g10+0x1008). (BUFF_MAX usage tracking omitted - pure diagnostic.) */
static void geo_set_end_mark(void) {
    u32 prev = M2_MEM.buff_add;
    m2_geo_cmd(0x0F0u);
    *(volatile u32 *)0x00803008u = prev;
    M2_MEM.buff_index = (u8)((M2_MEM.buff_index + 1u) & 3u);
    M2_MEM.buff_add  = geo_buff_ram_adds[M2_MEM.buff_index];
    *(volatile u32 *)0x00801008u = M2_MEM.buff_add;
}

/* ---- FRAME ORCHESTRATION (the begin/commit the draw primitives need) -----------------------------
 * The 2D/3D draw primitives (m2_draw.h shapes/text, the object_data submits) only emit geometry; they
 * do NOT manage the GEO display-list lifecycle. A drawing FRAME is:
 *
 *     m2_frame_begin();         // per-frame GEO render-state + reset the COP poly counters
 *     ... m2_fill_rect / m2_draw_text_px / cop_submit_object / geo_obj_* ...
 *     m2_frame_commit();        // close the display list (no IRQ wait)  -- or:
 *     m2_frame_end();           // close the list AND wait for the GEO frame-done IRQ (blocks ~vsync)
 *
 * Use m2_frame_end() for a steady ~60fps loop; m2_frame_commit() when the caller must keep doing other
 * work (e.g. servicing a serial link) and can't block on the IRQ. Redraw EVERY frame — the GEO holds
 * only the last committed list.
 *
 * CONSUMER / BOOT (host responsibility): the GEO + COP must already be BOOTED (m2_cop_boot / geo_init
 * sequence — see the host kernel) and PRIMED as a continuous consumer before the first real frame. Prime
 * it by committing a few empty frames once at startup:  for (i=0;i<8;i++){ m2_frame_begin(); m2_frame_end(); }
 * This SDK supplies the per-frame orchestration; the one-time GEO/COP boot stays with the host project
 * (it differs by transport — e.g. the RS-422 geoserial kernel vs a -DM2_HW standalone build). */

/* STF set_mmode (event_loop @0x113B0 pushes set_mmode(3) then set_mmode(1) at the top of every frame):
 * slot 0x70 = 0x707, then push the mode word to the GEO FIFO. */
static void m2_geo_set_mmode(u32 v) {
    m2_geo_cmd(0x070u);
    *(volatile u32 *)0x00804000u = v;
}
/* STF set_window_data @0x35E0 (main_loop calls it every frame): slot 0x80 = 0x808, then push the
 * Z-clip word (0x40800000) to the GEO FIFO — the per-frame slot-0x80 render-state. */
static void m2_geo_set_window(void) {
    m2_geo_cmd(0x080u);
    *(volatile u32 *)0x00804000u = 0x40800000u;
}

/* Begin a frame: reset the COP poly counters ONCE per frame (so multiple object submits thread their
 * BUFF_RAM append + per-poly texture headers — resetting per-submit = one colour/frame), then push the
 * per-frame GEO render-state (window + mode). */
static void m2_frame_begin(void) {
    g_cop_p2 = 0u; g_cop_p = 0u;
    m2_geo_set_window();
    m2_geo_set_mmode(3u);
    m2_geo_set_mmode(1u);
}
/* Commit the display list (END mark) WITHOUT waiting for the frame-done IRQ. */
static void m2_frame_commit(void) { geo_set_end_mark(); }
/* Commit the display list AND wait for the GEO frame-done IRQ (~vsync). */
static void m2_frame_end(void) { geo_set_end_mark(); geo_interrupt_wait(); }

/* STF sub_11FE4 @0x11FE4: fill one GEO region through the command FIFO at 0x804000
 * ((g10)[g12], g10=0x800000 g12=0x4000), then end-mark + interrupt_wait. The 4-short
 * pattern is pushed 1024x (count 0x1000). */
static void geo_region_fill(const u16 pat[4], u32 region) {
    volatile u32 *fifo = (volatile u32 *)0x00804000u;
    int i;
    m2_geo_cmd(0x040u);        /* g10+0x40   */
    *fifo = region;                                /* region addr */
    *fifo = 0x1000u;                               /* count       */
    for (i = 0; i < 1024; i++) {
        *fifo = pat[0]; *fifo = pat[1]; *fifo = pat[2]; *fifo = pat[3];
    }
    m2_geo_cmd(0x100u);        /* g10+0x100 = 0x1010 (lda 0x1010) */
    *fifo = pat[0];                                /* final short */
    geo_set_end_mark();
    geo_interrupt_wait();
}

/* STF geometry_stuff @0x11F7C: seed the 5 GEO regions with their default-state
 * patterns ({0x4000,0,0x1A00,X}). Call once after geo_initialize. */
static void geometry_stuff(void) {
    static const u16 p0[4] = {0x4000u, 0x0000u, 0x1A00u, 0x0400u};  /* 0x800000 */
    static const u16 p1[4] = {0x4000u, 0x0000u, 0x1A00u, 0x2940u};  /* 0x801000 */
    static const u16 p2[4] = {0x4000u, 0x0000u, 0x1A00u, 0x3E00u};  /* 0x802000 */
    static const u16 p3[4] = {0x4000u, 0x0000u, 0x1A00u, 0x26C0u};  /* 0x803000 */
    static const u16 p4[4] = {0x4000u, 0x0000u, 0x1A00u, 0xB140u};  /* 0x804000 */
    geo_region_fill(p0, 0x800000u);
    geo_region_fill(p1, 0x801000u);
    geo_region_fill(p2, 0x802000u);
    geo_region_fill(p3, 0x803000u);
    geo_region_fill(p4, 0x804000u);
}

/* STF sub_290FC(0) + camera_init->sub_29148 (@0x29148): the per-material shading
 * coefficient table, uploaded to GEO slot 0x60 (opcode 0x606). geotest never wrote
 * slot 0x60 at all, so the shaded object path (MODE=1) read garbage coefficients.
 * sub_290FC(0) copies flt_90A20[0:0x20] into material_num_floats; sub_29148 then pushes
 * 0x20 records {material_num_floats[i]=flt_90A20[i], flt_90BA0[i]} through the FIFO
 * (base 0, count 0x20). flt_90A20 = luma/state headers (0x03FFA040 family, same as the
 * make_luma_ram ramp); flt_90BA0 = IEEE intensity ramp 1.0 -> ~0. Tables verbatim from
 * STF program ROM @0x90A20 / @0x90BA0. Call once at boot after the config tables. */
static const u32 geo_mat_hdr[0x20] = {  /* flt_90A20 */
    0x03FFA040u,0x03FFA040u,0x03FFA040u,0x0390A050u,0x03FF8040u,0x03FFA040u,0x03FFA040u,0x07FF50FFu,
    0x07FF1C57u,0x03FFA040u,0x04FF9050u,0x0000A0FFu,0x00EEAA80u,0x04607030u,0x0000FFFFu,0x04800000u,
    0x07FFB080u,0x0000B080u,0x0000B080u,0x0000B080u,0x0000B080u,0x0000B0A0u,0x0000B080u,0x07FFB080u,
    0x0000B080u,0x000060FFu,0x0000B080u,0x0000B080u,0x0000B080u,0x0000FFFFu,0x0000FFFFu,0x0000FF00u
};
static const u32 geo_mat_int[0x20] = {  /* flt_90BA0 */
    0x3F800000u,0x3F4B4396u,0x3F2147AEu,0x3F004189u,0x3ECBC6A8u,0x3EA1CAC1u,0x3E808312u,0x3E4BC6A8u,
    0x3E21CAC1u,0x3E000000u,0x3DCCCCCDu,0x3D8F5C29u,0x3D810625u,0x3D4CCCCDu,0x3D1FBE77u,0x3CFDF3B6u,
    0x3CCCCCCDu,0x3C9BA5E3u,0x3C75C28Fu,0x3C449BA6u,0x3C23D70Au,0x3C016F00u,0x3BCE703Bu,0x3BA3D70Au,
    0x3B7F9724u,0x3B4B295Fu,0x3B23D70Au,0x3AF9096Cu,0x3AC49BA6u,0x3A9D4952u,0x3A83126Fu,0x3A378034u
};
static void geo_material_init(void) {
    volatile u32 *fifo = (volatile u32 *)0x00804000u;
    u32 last = 0u; int i;
    m2_geo_cmd(0x060u);     /* slot 0x60 = material/luma opcode (0x606) */
    *fifo = 0u;                                 /* base  = 0    */
    *fifo = 0x20u;                              /* count = 0x20 records */
    for (i = 0; i < 0x20; i++) {
        *fifo = geo_mat_hdr[i];                 /* material_num_floats[i] (= flt_90A20[i]) */
        *fifo = geo_mat_int[i]; last = geo_mat_int[i];   /* flt_90BA0[i] */
    }
    m2_geo_cmd(0x100u);     /* config-commit (sub_29148 tail)          */
    *fifo = last;
    /* Commit + sync this material block. STF's set_material(0x29148)/sub_290FC(0x290FC) do NOT flush
     * immediately (verified: callers @0x6A94 + camera_init @0x1F554 fall straight through, no
     * set_end_mark) — STF keeps material in one continuous list flushed later. geoserial instead
     * commits each init config block standalone (same as geo_region_fill, whose flush IS STF-faithful
     * via sub_11FE4), so we flush here intentionally. */
    geo_set_end_mark();
    geo_interrupt_wait();
}

/* Flush an empty list so the GEO renders nothing (clears the 3D plane, e.g. when
   a 3D game returns to the 2D launcher menu). */
static void geo_clear(void) { geo_begin(); geo_end(); geo_flush(0x10000u); }

#endif /* M2_GEO_H */
