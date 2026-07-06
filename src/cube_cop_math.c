/*
 * cube_cop_math.c — spinning cube + COP math benchmark (B of the A/B pair).
 *
 * IDENTICAL to cube.c except math_load() runs its NLOAD*4 column-rotations on the COP
 * (cpres1) via m2_cop_rot2d — one COP FIFO round-trip per rotation — instead of native
 * soft-float. Everything else (the rendered 6-face cube, the throughput counter, the
 * frameVBL pacing) is the same, so comparing (Δcounter/Δframevbl) between this and cube.c
 * isolates the cost of routing the transform math through the coprocessor.
 *
 * m2_cop_rot2d/cop_sincos always hit the COP FIFO (fastmath only overrides the scalar
 * fadd/fmul/dot2d), so no build-flag change is needed. NB per-frame COP FIFO traffic is
 * the pattern flagged as an m2emulator crash risk; MAME under M2_HLE_GEO_OFF emulates the
 * COP and should run it — this benchmark is partly a test of whether it does.
 *
 *   build:  cmd /c ".\build_clang64.bat cube_cop_math"
 *   run  :  override sfight's epr-19001.15 / .16, MAME with M2_HLE_GEO_OFF.
 */

#include "m2.h"
#include "m2_geo.h"
#include "m2_boot.h"        /* pulls in m2_3d.h -> m2_math.h (m2_cop_rot2d) */

#define NLOAD       192u
#define BENCH_ADDR  0x005F8100u
#define CUBE_H      6.0f
#define CUBE_Z      26.0f
#define CUBE_TILT   0.5f
#define DSPIN       0x0140u

#define SP_PI       3.14159265f
#define SP_TWO_PI   6.28318531f

static float g_face[6][12];
static float g_load[12];

static void sp_sincos(float x, float *s, float *c){
    float x2;
    while (x >  SP_PI) x -= SP_TWO_PI;
    while (x < -SP_PI) x += SP_TWO_PI;
    x2 = x*x;
    *s = x * (1.0f + x2*(-1.0f/6.0f + x2*(1.0f/120.0f + x2*(-1.0f/5040.0f))));
    *c = 1.0f + x2*(-0.5f + x2*(1.0f/24.0f + x2*(-1.0f/720.0f)));
}

static void sp_setpal(u32 cb,int r,int g,int b){ *(volatile u16*)(0x01800000u+(cb+0x1000u)*2u)=(u16)(M2_RGB(r,g,b)|M2_PAL_SET); }

static void build_faces(void){
    static const float N[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    static const float U[6][3]={{0,1,0},{0,1,0},{0,0,1},{1,0,0},{1,0,0},{0,1,0}};
    static const float Vv[6][3]={{0,0,1},{0,0,-1},{1,0,0},{0,0,1},{0,1,0},{1,0,0}};
    float ct,st; int f,i; sp_sincos(CUBE_TILT,&st,&ct);
    for (f=0;f<6;f++){
        float col[4][3];
        for (i=0;i<3;i++){ col[0][i]=U[f][i]*(CUBE_H/6.0f); col[1][i]=N[f][i]; col[2][i]=Vv[f][i]*(CUBE_H/6.0f); col[3][i]=N[f][i]*CUBE_H; }
        for (i=0;i<4;i++){ float y=col[i][1], z=col[i][2]; col[i][1]=y*ct - z*st; col[i][2]=y*st + z*ct; }
        g_face[f][0]=col[0][0]; g_face[f][1]=col[0][1]; g_face[f][2]=col[0][2];
        g_face[f][3]=col[1][0]; g_face[f][4]=col[1][1]; g_face[f][5]=col[1][2];
        g_face[f][6]=col[2][0]; g_face[f][7]=col[2][1]; g_face[f][8]=col[2][2];
        g_face[f][9]=col[3][0]; g_face[f][10]=col[3][1]; g_face[f][11]=col[3][2];
    }
    for (i=0;i<12;i++) g_load[i] = (i%3==0)?1.0f:0.25f;
}

/* ---- THE MATH UNDER TEST: NLOAD*4 column-rotations about Y, via the COP (m2_cop_rot2d). ----
 * rot2d(ang,x,y) = (x*cos-y*sin, x*sin+y*cos); angle = -spin gives (x*cs+z*sn, -x*sn+z*cs). */
static void math_load(u32 negspin){
    u32 i; int k; float out[2];
    for (i=0;i<NLOAD;i++)
        for (k=0;k<4;k++){ m2_cop_rot2d(negspin, g_load[3*k], g_load[3*k+2], out); g_load[3*k]=out[0]; g_load[3*k+2]=out[1]; }
}

static void emit_face(int f, float cs, float sn){
    float m[12]; int k; u32 cb=(u32)(f+1);
    for (k=0;k<4;k++){ float x=g_face[f][3*k], z=g_face[f][3*k+2]; m[3*k]=x*cs+z*sn; m[3*k+1]=g_face[f][3*k+1]; m[3*k+2]=-x*sn+z*cs; }
    m[11]+=CUBE_Z;
    geo_color_header(cb); geo_quad_emit(m, cb);
}

int main(void){
    static const int COL[6][3]={{31,4,4},{4,31,4},{4,4,31},{31,31,4},{4,31,31},{31,4,31}};
    u32 frames=0; int f;

    m2_init();
    m2_silicon_boot();
    geo_flatquad_init();
    geo_fill_color_patch();
    build_faces();
    for (f=0;f<6;f++) sp_setpal((u32)(f+1), COL[f][0], COL[f][1], COL[f][2]);

    for (f=0;f<8;f++){ geo_clear(); m2_vsync(); }

    for (;;){
        u32 spin = (frameVBL * DSPIN) & 0xFFFFu;
        float sn,cs; sp_sincos((float)spin*(SP_TWO_PI/65536.0f), &sn, &cs);

        math_load((0x10000u - spin) & 0xFFFFu);       /* <-- the benchmarked work (COP) */

        geo_begin();
        geo_zsort(M2_ZSORT_COARSE); geo_mode(GEO_MODE_NP_NS); geo_texparam_flat(0xFFu,0x60u);
        geo_focal(280.0f,280.0f); geo_light(0.40f,0.55f,-0.73f); geo_lod(2560.0f); geo_window_fullscreen();
        for (f=0;f<6;f++) emit_face(f, cs, sn);
        geo_end(); geo_flush_flip();
        *(volatile u32 *)ZCLIP_REG = 0xFFFF00FFu;

        *(volatile u32 *)BENCH_ADDR = ++frames;
    }
    return 0;
}
