/*
 * objtest.c — m2-sdk check: the m2_obj.h object path gives the same display-list words on every board.
 *
 * Runs the 3D path a game uses (m2_silicon_boot, then per frame m2_frame_begin, m2_obj_frame_setup,
 * m2_obj_submit_hdr, m2_frame_commit) with two objects of our own: a mesh of two quads put in
 * polygon RAM with GEO_OP_POLYDATA, so nothing comes from Sonic the Fighters' model table. After
 * each submit it reads back what the COP wrote to buffer RAM (the matrix command, the 12 matrix
 * words, the object command, tpa, tha, oba, n, then the END mark) and the polygon counts it
 * returned, and checks them against the matrix the same commands leave (RMATRIX) and the header.
 * Then it shows one line per check on screen.
 *
 *   cmake ... -DM2_GAME=objtest -DM2_BOARD=2A     run on dynamcop (the SDK's TGP kernel)
 *   cmake ... -DM2_GAME=objtest -DM2_BOARD=2C     run on dynamcopc (the SDK's TGPx4 kernel)
 *   cmake ... -DM2_GAME=objtest                   run on sfight (2B, cpres1; needs cpres1.h)
 *
 * The words stay in g_obj_words[] / g_obj_counts[] (g_obj_bad, g_obj_done) for a MAME Lua script, so
 * the two boards' results can be compared word for word. The test doesn't need the objects to be
 * visible.
 */
#include "m2.h"
#include "m2_obj.h"
#include "m2_polyattr.h"
#include "m2_gfx2d.h"

#define NWORDS 19                 /* 18 words SUBMIT writes + the END mark after them */
volatile u32 g_obj_words[2][NWORDS];
volatile u32 g_obj_counts[2][2];  /* p2, p after each submit */
volatile u32 g_obj_bad;
volatile u32 g_obj_done;

#define MESH_OBA 0x0100u          /* polygon RAM word index of the mesh */
#define MESH_CB  5u               /* its flat colorbase */

/* Two quads in a strip, mode NP_NS (normals present): the 2 leading vertices, then per quad
 * attr, normal, and its 2 new vertices (10 words). Float bits. */
static const u32 g_mesh[26] = {
    0xC1200000u, 0x00000000u, 0xC1200000u,      /* (-10, 0, -10) */
    0x41200000u, 0x00000000u, 0xC1200000u,      /* ( 10, 0, -10) */
    M2_POLY_QUAD | M2_POLY_LINK2 | M2_POLY_DOUBLE,
    0x00000000u, 0x3F800000u, 0x00000000u,      /* normal +Y */
    0x41200000u, 0x00000000u, 0x00000000u,      /* ( 10, 0, 0) */
    0xC1200000u, 0x00000000u, 0x00000000u,      /* (-10, 0, 0) */
    M2_POLY_QUAD | M2_POLY_LINK2 | M2_POLY_DOUBLE,
    0x00000000u, 0x3F800000u, 0x00000000u,
    0x41200000u, 0x00000000u, 0x41200000u,      /* ( 10, 0, 10) */
    0xC1200000u, 0x00000000u, 0x41200000u,      /* (-10, 0, 10) */
};
/* {tpa, tha, oba, obc}: obc's low 16 bits = the polygon count (2), bits 14+ are left 0 */
static const u32 g_hdr[2][4] = {
    { 0u, GEO_TEXRAM_BIT | (MESH_CB * 4u), MESH_OBA, 2u },
    { 0u, GEO_TEXRAM_BIT | (MESH_CB * 4u), MESH_OBA, 1u },   /* the first quad only */
};
/* position, angles (x, y, z), scale per object */
static const float g_pos[2][3]   = { { 0.0f, -20.0f, 200.0f }, { 30.0f, 10.0f, 150.0f } };
static const u32   g_ang[2][3]   = { { 0x4000u, 0x2000u, 0u }, { 0x1555u, 0xC000u, 0x0800u } };
static const float g_scale[2][3] = { { 1.0f, 1.0f, 1.0f }, { 2.0f, 0.5f, -1.5f } };
static u32 g_want[2][NWORDS], g_want_counts[2][2];

static void upload_mesh(void) {
    volatile u32 *fifo = (volatile u32 *)M2_GEOFIFO_ADDR;
    u32 i;
    *fifo = GEO_OP_POLYDATA; *fifo = MESH_OBA; *fifo = 26u;
    for (i = 0; i < 26u; i++) *fifo = g_mesh[i];
}

/* The matrix m2_obj_submit_hdr builds for object k, read back with RMATRIX. */
static void want_matrix(int k, u32 *m) {
    volatile u32 *cf = (volatile u32 *)M2_COPFIFO_ADDR;
    int i;
    *cf = COP_IDENTITY;
    *cf = COP_SET_POS; *cf = m2_obj_fb(g_pos[k][0]); *cf = m2_obj_fb(g_pos[k][1]); *cf = m2_obj_fb(g_pos[k][2]);
    *cf = COP_ANG_Y; *cf = g_ang[k][1];
    *cf = COP_ANG_X; *cf = g_ang[k][0];
    *cf = COP_ANG_Z; *cf = g_ang[k][2];
    *cf = COP_SCALE; *cf = m2_obj_fb(g_scale[k][0]); *cf = m2_obj_fb(g_scale[k][1]); *cf = m2_obj_fb(g_scale[k][2]);
    *cf = COP_RMATRIX;
    for (i = 0; i < 12; i++) m[i] = *cf;
}

static void draw_objects(int capture) {
    int k, i;
    m2_frame_begin();
    m2_obj_frame_setup();
    m2_obj_color_header(MESH_CB);
    upload_mesh();
    for (k = 0; k < 2; k++) {
        u32 wr, *w = g_want[k], n = g_hdr[k][3] & 0xFFFFu;
        if (capture) {
            want_matrix(k, w + 1);
            w[0] = 0x05800b0bu; w[13] = 0x00800101u;
            w[14] = g_hdr[k][0]; w[15] = g_hdr[k][1]; w[16] = g_hdr[k][2]; w[17] = n;
            w[18] = GEO_OP_END;
            g_want_counts[k][0] = g_cop_p2;          /* flag 0: p2 stays, p advances by n */
            g_want_counts[k][1] = g_cop_p + n;
        }
        wr = *(volatile u32 *)COP_WPOS_REG & 0x1FFFCu;
        m2_obj_submit_hdr(g_hdr[k], 0u, m2_obj_fb(g_pos[k][0]), m2_obj_fb(g_pos[k][1]), m2_obj_fb(g_pos[k][2]),
                          g_ang[k][0], g_ang[k][1], g_ang[k][2],
                          m2_obj_fb(g_scale[k][0]), m2_obj_fb(g_scale[k][1]), m2_obj_fb(g_scale[k][2]));
        if (capture) {
            for (i = 0; i < NWORDS; i++) g_obj_words[k][i] = *(volatile u32 *)(GEO_BUFFERRAM + wr + 4u * i);
            g_obj_counts[k][0] = g_cop_p2; g_obj_counts[k][1] = g_cop_p;
        }
    }
    m2_frame_commit();
}

/* ---- the report screen (g2d, as coptest) ---- */
static int n_line;
static void hex8(char *p, u32 v) {
    int i;
    for (i = 0; i < 8; i++) p[i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 0xF];
}
static void report(const char *name, int ok, u32 got, u32 want) {
    char line[40];
    int i, n;
    for (i = 0; i < 39; i++) line[i] = ' ';
    line[39] = 0;
    for (n = 0; name[n] && n < 9; n++) line[n] = name[n];
    line[10] = ok ? 'O' : 'B'; line[11] = ok ? 'K' : 'A'; line[12] = ok ? ' ' : 'D';
    hex8(line + 14, got);
    hex8(line + 23, want);
    g2d_ttext(16, 24 + n_line * 10, line, ok ? 2 : 3);
    n_line++;
}
/* Integer words bit for bit; the 12 matrix words (1..12) bit for bit too, since both come from the
 * same COP that built the expected matrix. A failing line shows the first word that differs. */
static int check_words(const char *name, const volatile u32 *got, const u32 *want, int n) {
    int i;
    for (i = 0; i < n - 1 && got[i] == want[i]; i++) ;
    report(name, got[i] == want[i], got[i], want[i]);
    return got[i] == want[i];
}

static void show_results(void) {
    static const char *nw[2] = { "obj1", "obj2" }, *nc[2] = { "obj1 p", "obj2 p" };
    int k, bad = 0;
    n_line = 0;
    for (k = 0; k < 2; k++) {
        bad += !check_words(nw[k], g_obj_words[k], g_want[k], NWORDS);
        bad += !check_words(nc[k], g_obj_counts[k], g_want_counts[k], 2);
    }
    g_obj_bad = (u32)bad;
}

int main(void) {
    int i;
    m2_init();
    m2_silicon_boot();
    m2_3d_solid(MESH_CB, M2_RGB(31, 20, 4));
    for (i = 0; i < 8; i++) { m2_frame_begin(); m2_frame_end(); }   /* prime the GEO */
    for (i = 0; i < 4; i++) { draw_objects(i == 3); m2_vsync(); }

    g2d_init();
    g2d_background(M2_RGB(0, 2, 10));
    g2d_color(1, M2_RGB(31, 31, 31));
    g2d_color(2, M2_RGB(10, 31, 10));
    g2d_color(3, M2_RGB(31, 8, 6));
    m2_font_atlas();
    for (;;) {
        g2d_begin();
        g2d_ttext(16, 8, M2_BOARD_NAME " object submit test", 1);
        show_results();
        g2d_ttext(16, 24 + 4 * 10 + 6, g_obj_bad ? "SUBMIT WORDS DIFFER" : "ALL SUBMITS OK", g_obj_bad ? 3 : 2);
        g_obj_done++;
        g2d_end();
        m2_vsync();
    }
    return 0;
}
