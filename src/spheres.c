/*
 * spheres.c — two spinning spheres side by side, SOFT-FLOAT (left) vs COP (right), each
 * spinning at a rate proportional to its OWN math throughput, so you can eyeball whether
 * the coprocessor is actually worth it — independent of raw emulator speed.
 *
 * The trick: at startup we CALIBRATE — count how many soft-float column-transforms vs
 * COP (m2_cop_rot2d) column-transforms complete in a fixed 60Hz-vblank window (frameVBL,
 * which always runs) — giving each path's throughput on-device. Then both spheres are
 * drawn every frame (same display rate) but each is spun ∝ its own measured throughput.
 * So the faster-math sphere sweeps more degrees/sec: if the COP is much faster its ball
 * visibly out-spins the soft one; if the difference is negligible they spin together.
 * Emulator speed scales both windows equally and cancels out of the ratio.
 *
 *   LEFT  sphere = native soft-float (column rotations + lighting/colour in shade_store)
 *   RIGHT sphere = COP: m2_cop_rot2d column rotations + m2_cop_dot3 / m2_cop_scale3 in the
 *                 split cop_shade_store — offloading the shading too lifts the win from
 *                 ~2x (rotations only) to ~3.5x here.
 *
 *   build:  cmd /c ".\build_clang64.bat spheres"
 *   run  :  override sfight's epr-19001.15 / .16, MAME with M2_HLE_GEO_OFF (real cpres2).
 */

#include "m2.h"
#include "m2_geo.h"
#include "m2_boot.h"        /* m2_silicon_boot; pulls in m2_math.h (m2_cop_rot2d) */

#define NLAT   8
#define NLON   12
#define NCELL  (NLAT*NLON)
#define SR     8.0f          /* sphere radius            */
#define SZ     40.0f         /* depth                    */
#define XOFF   11.0f         /* +/- world-X of each ball */
#define DSPIN  0x0140        /* base spin/frame for the SLOWER ball; faster scales up */
#define CAL_VBL 60u          /* calibration window (vblanks) per path                  */

#define PI     3.14159265f
#define TWO_PI 6.28318531f
#define LX     0.40f
#define LY     0.50f
#define LZ    -0.77f
#define AMB    0.28f
#define FIT    1.18f

#define CB_A   1u                                   /* left  ball colorbases 1..96   */
#define CB_B   (CB_A + NCELL)                        /* right ball colorbases 97..192 */

static float g_base[NCELL][12];                     /* shared base cell matrices (origin) */
static u8    g_hr[NCELL], g_hg[NCELL], g_hb[NCELL]; /* base hue per cell (by longitude)   */
static float g_mA[NCELL][12], g_mB[NCELL][12];      /* per-ball computed matrices         */

static void sincos2(float x, float *s, float *c){
    float x2;
    while (x >  PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;
    x2 = x*x;
    *s = x * (1.0f + x2*(-1.0f/6.0f + x2*(1.0f/120.0f + x2*(-1.0f/5040.0f))));
    *c = 1.0f + x2*(-0.5f + x2*(1.0f/24.0f + x2*(-1.0f/720.0f)));
}
static void hue(int j, u8 *r, u8 *g, u8 *b){
    float h=TWO_PI*(float)j/(float)NLON, sr,cr,sg,cg,sb,cb; sincos2(h,&sr,&cr); sincos2(h+TWO_PI/3.0f,&sg,&cg); sincos2(h-TWO_PI/3.0f,&sb,&cb);
    *r=(u8)((sr*0.5f+0.5f)*31.0f+0.5f); *g=(u8)((sg*0.5f+0.5f)*31.0f+0.5f); *b=(u8)((sb*0.5f+0.5f)*31.0f+0.5f);
}
static void setpal(u32 cb,int r,int g,int b){ *(volatile u16*)(0x01800000u+(cb+0x1000u)*2u)=(u16)(M2_RGB(r,g,b)|M2_PAL_SET); }

static void build(void){
    static float V[NLAT+1][NLON+1][3];
    int i,j,k=0;
    for (i=0;i<=NLAT;i++){ float st,ct; sincos2(PI*(float)i/(float)NLAT,&st,&ct);
        for (j=0;j<=NLON;j++){ float sp,cp; sincos2(TWO_PI*(float)j/(float)NLON,&sp,&cp);
            V[i][j][0]=SR*st*sp; V[i][j][1]=SR*ct; V[i][j][2]=SR*st*cp; } }
    for (i=0;i<NLAT;i++) for (j=0;j<NLON;j++){
        float *m=g_base[k];
        geo_quad_matrix(V[i][j],V[i][j+1],V[i+1][j+1],V[i+1][j],m);
        m[3]=-m[3]; m[4]=-m[4]; m[5]=-m[5];
        m[0]*=FIT; m[1]*=FIT; m[2]*=FIT; m[6]*=FIT; m[7]*=FIT; m[8]*=FIT;
        hue(j,&g_hr[k],&g_hg[k],&g_hb[k]); k++;
    }
}

static const float g_light[3] = { LX, LY, LZ };

/* shade the rotated cell (normal = col1) and store its matrix — SOFT-FLOAT (left ball). */
static void shade_store(int c, u32 cbBase, float xoff, float *m, float *out){
    float d=m[3]*LX+m[4]*LY+m[5]*LZ, lit; int k;
    if (d<0.0f) d=0.0f; lit=AMB+(1.0f-AMB)*d;
    m[9]+=xoff; m[11]+=SZ;
    setpal(cbBase+(u32)c,(int)((float)g_hr[c]*lit),(int)((float)g_hg[c]*lit),(int)((float)g_hb[c]*lit));
    for (k=0;k<12;k++) out[k]=m[k];
}

/* same, but the light dot and the colour scale run on the COP — COP variant (right ball).
 * m2_cop_dot3 (light·normal) and m2_cop_scale3 (lit * hue) each replace a handful of
 * soft-float ops with one FIFO round-trip. (dot3 is an HLE-COP op, like rot2d — fine in
 * MAME/M2_HLE_GEO_OFF, not silicon; scale3 is portable.) */
static void cop_shade_store(int c, u32 cbBase, float xoff, float *m, float *out){
    float n[3], hue[3], rgb[3], d, lit; int k;
    n[0]=m[3]; n[1]=m[4]; n[2]=m[5];
    d = m2_cop_dot3(n, g_light);                 /* COP: light · normal */
    if (d<0.0f) d=0.0f; lit=AMB+(1.0f-AMB)*d;
    m[9]+=xoff; m[11]+=SZ;
    hue[0]=(float)g_hr[c]; hue[1]=(float)g_hg[c]; hue[2]=(float)g_hb[c];
    m2_cop_scale3(lit, hue, rgb);                /* COP: lit * (hr,hg,hb) */
    setpal(cbBase+(u32)c,(int)rgb[0],(int)rgb[1],(int)rgb[2]);
    for (k=0;k<12;k++) out[k]=m[k];
}

/* LEFT ball — native soft-float column rotations. */
static void compute_soft(u32 spin, float xoff, u32 cbBase, float dst[NCELL][12]){
    float sn,cs; int c,k; sincos2((float)spin*(TWO_PI/65536.0f),&sn,&cs);
    for (c=0;c<NCELL;c++){ const float *b=g_base[c]; float m[12];
        for (k=0;k<4;k++){ float x=b[3*k], z=b[3*k+2]; m[3*k]=x*cs+z*sn; m[3*k+1]=b[3*k+1]; m[3*k+2]=-x*sn+z*cs; }
        shade_store(c,cbBase,xoff,m,dst[c]); }
}
/* RIGHT ball — COP column rotations (one m2_cop_rot2d FIFO round-trip each). */
static void compute_cop(u32 spin, float xoff, u32 cbBase, float dst[NCELL][12]){
    u32 negspin=(0x10000u-spin)&0xFFFFu; int c,k;
    for (c=0;c<NCELL;c++){ const float *b=g_base[c]; float m[12], out[2];
        for (k=0;k<4;k++){ m2_cop_rot2d(negspin,b[3*k],b[3*k+2],out); m[3*k]=out[0]; m[3*k+1]=b[3*k+1]; m[3*k+2]=out[1]; }
        cop_shade_store(c,cbBase,xoff,m,dst[c]); }
}

static void render_ball(float src[NCELL][12], u32 cbBase){
    int c; for (c=0;c<NCELL;c++){ geo_color_header(cbBase+(u32)c); geo_quad_emit(src[c], cbBase+(u32)c); }
}

/* count how many times `fn` completes within CAL_VBL vblanks (frameVBL always ticks). */
static u32 calibrate_soft(void){ u32 base=frameVBL, n=0; while ((frameVBL-base)<CAL_VBL){ compute_soft(0u,-XOFF,CB_A,g_mA); n++; } return n; }
static u32 calibrate_cop (void){ u32 base=frameVBL, n=0; while ((frameVBL-base)<CAL_VBL){ compute_cop (0u, XOFF,CB_B,g_mB); n++; } return n; }

int main(void){
    float spinA=0.0f, spinB=0.0f, ratio;
    u32 nSoft, nCop;
    int c;

    m2_init();
    m2_silicon_boot();
    geo_flatquad_init();
    geo_fill_color_patch();
    build();

    for (c=0;c<8;c++){ geo_clear(); m2_vsync(); }

    /* on-device throughput calibration: soft-float vs COP column-transforms per window. */
    nSoft = calibrate_soft();
    nCop  = calibrate_cop();
    ratio = (nSoft>0u) ? ((float)nCop/(float)nSoft) : 1.0f;   /* COP speedup (>1 = COP faster) */
    { volatile u32 *dbg=(volatile u32*)0x005F8100u; dbg[0]=nSoft; dbg[1]=nCop; dbg[2]=(u32)(ratio*1000.0f); }

    for (;;){
        spinA += (float)DSPIN;                 /* soft ball: reference rate      */
        spinB += (float)DSPIN * ratio;         /* COP ball: ∝ measured throughput */
        if (spinA>=65536.0f) spinA-=65536.0f;
        if (spinB>=65536.0f) spinB-=65536.0f;

        compute_soft((u32)spinA & 0xFFFFu, -XOFF, CB_A, g_mA);
        compute_cop ((u32)spinB & 0xFFFFu,  XOFF, CB_B, g_mB);

        geo_begin();
        geo_zsort(M2_ZSORT_COARSE); geo_mode(GEO_MODE_NP_NS); geo_texparam_flat(0xFFu,0x60u);
        geo_focal(280.0f,280.0f); geo_light(LX,LY,LZ); geo_lod(2560.0f); geo_window_fullscreen();
        render_ball(g_mA, CB_A);
        render_ball(g_mB, CB_B);
        geo_end(); geo_flush_flip();

        *(volatile u32 *)ZCLIP_REG = 0xFFFF00FFu;
        m2_vsync();
    }
    return 0;
}
