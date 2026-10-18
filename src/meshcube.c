/*
 * meshcube.c — a spinning cube of our own mesh, through the m2_obj.h COP path, on any board with a COP.
 *
 * Nothing comes from a game's model table, so it runs on dynamcop (2A, the SDK's TGP kernel) as well
 * as sfight (2B, cpres1). The six faces are six one-quad meshes put in polygon RAM with
 * GEO_OP_POLYDATA; each is submitted with m2_obj_submit_hdr under the same position and angles,
 * with its own flat colorbase. objtest.c checks the words this path writes; this one shows them.
 *
 *   cmake ... -DM2_GAME=meshcube -DM2_BOARD=2A     run on dynamcop
 *   cmake ... -DM2_GAME=meshcube                   run on sfight (needs cpres1.h)
 *
 * Model 2C has no COP program yet, so there it only says so.
 */
#include "m2.h"
#include "m2_obj.h"
#include "m2_polyattr.h"
#include "m2_gfx2d.h"

#define FACE_OBA  0x0100u          /* polygon RAM word index of face 0; face k at + 16k */
#define FACE_CB   8u               /* face k uses colorbase FACE_CB + k */
#define HALF      12.0f            /* half the cube's edge */
#define CUBE_Z    80.0f

/* Face k: centre n*HALF, edges along u and v. One quad in mode NP_NS: the 2 leading vertices, then
 * attr, normal and the 2 vertices that close it (16 words). */
static const signed char g_axes[6][3][3] = {
    /* normal       u             v */
    { { 1, 0, 0}, { 0, 1, 0}, { 0, 0, 1} },
    { {-1, 0, 0}, { 0, 0, 1}, { 0, 1, 0} },
    { { 0, 1, 0}, { 0, 0, 1}, { 1, 0, 0} },
    { { 0,-1, 0}, { 1, 0, 0}, { 0, 0, 1} },
    { { 0, 0, 1}, { 1, 0, 0}, { 0, 1, 0} },
    { { 0, 0,-1}, { 0, 1, 0}, { 1, 0, 0} },
};
static const u16 g_color[6] = {
    M2_RGB(31, 8, 6), M2_RGB(8, 31, 8), M2_RGB(6, 12, 31),
    M2_RGB(31, 28, 4), M2_RGB(31, 8, 31), M2_RGB(4, 28, 31),
};
static u32 g_hdr[6][4];

static void upload_faces(void) {
    volatile u32 *fifo = (volatile u32 *)M2_GEOFIFO_ADDR;
    static const signed char su[4] = { -1, 1, -1, 1 }, sv[4] = { -1, -1, 1, 1 };
    int k, c, i;
    for (k = 0; k < 6; k++) {
        const signed char *n = g_axes[k][0], *u = g_axes[k][1], *v = g_axes[k][2];
        u32 w[16], *p = w;
        for (c = 0; c < 4; c++) {
            if (c == 2) {                       /* the quad's unit starts after the 2 leading vertices */
                *p++ = M2_POLY_QUAD | M2_POLY_LINK2 | M2_POLY_DOUBLE;
                for (i = 0; i < 3; i++) *p++ = m2_obj_fb((float)n[i]);
            }
            for (i = 0; i < 3; i++) *p++ = m2_obj_fb(HALF * (float)(n[i] + su[c] * u[i] + sv[c] * v[i]));
        }
        *fifo = GEO_OP_POLYDATA; *fifo = FACE_OBA + 16u * k; *fifo = 16u;
        for (i = 0; i < 16; i++) *fifo = w[i];
    }
}

int main(void) {
    u32 t = 0;
    int k, i;
    m2_init();
#if defined(M2_BOARD_2C)
    g2d_init();
    g2d_background(M2_RGB(0, 2, 10));
    g2d_color(1, M2_RGB(31, 31, 31));
    m2_font_atlas();
    for (;;) {
        g2d_begin();
        g2d_ttext(16, 16, "Model 2C: no COP program yet", 1);
        g2d_end();
        m2_vsync();
    }
#else
    m2_silicon_boot();
    m2_obj_fill_patch();
    for (k = 0; k < 6; k++) {
        m2_3d_solid(FACE_CB + k, g_color[k]);
        g_hdr[k][0] = 0u;
        g_hdr[k][1] = GEO_TEXRAM_BIT | ((FACE_CB + k) * 4u);
        g_hdr[k][2] = FACE_OBA + 16u * k;
        g_hdr[k][3] = 1u;
    }
    for (i = 0; i < 8; i++) { m2_frame_begin(); m2_frame_end(); }   /* prime the GEO */
    for (;;) {
        m2_frame_begin();
        m2_obj_frame_setup();
        upload_faces();
        for (k = 0; k < 6; k++) m2_obj_color_header(FACE_CB + k);
        for (k = 0; k < 6; k++)
            m2_obj_submit_hdr(g_hdr[k], 0u, m2_obj_fb(0.0f), m2_obj_fb(0.0f), m2_obj_fb(CUBE_Z),
                              (t * 0x00B0u) & 0xFFFFu, (t * 0x0130u) & 0xFFFFu, (t * 0x0050u) & 0xFFFFu,
                              m2_obj_fb(1.0f), m2_obj_fb(1.0f), m2_obj_fb(1.0f));
        m2_frame_commit();
        m2_vsync();
        t++;
    }
#endif
    return 0;
}
