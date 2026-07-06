/*
 * knot.c — a spinning, tilted TREFOIL TORUS-KNOT tube: every arithmetic op runs on the
 * math coprocessor (cpres1 / COP), rendered through as much of the GEO command set as the
 * silicon-proven flat-quad path exposes.
 *
 * THE VISUAL: a (2,3) torus knot swept into a hexagonal tube of NSEG*NRING flat quads.
 * A rainbow flows ALONG the rope (per-frame hue phase), an orbiting light sweeps a moving
 * specular-ish highlight across the faceted surface (per-quad COP dot lighting), and the
 * whole knot is tilted and spun about Y so its over/under crossings read as 3D.
 *
 * "USING ONLY COPROCESSOR MATH": the default SDK build is soft-float, and m2_fastmath.h
 * folds m2_cop_fadd/fmul/dot2d/sqrt/f2int to native/soft-float ops that NEVER touch the COP.
 * To genuinely run on the coprocessor we bypass those macros and push every op straight to
 * the COP command FIFO (cop_* helpers below), so the SHARC does all of it: the knot/frame
 * construction, the per-frame column rotations, the per-quad lighting, and the colour scale.
 *
 * We use ONLY opcodes whose meaning is IDENTICAL on real cpres1 silicon and MAME's HLE COP —
 * DOT2D(0x59), FADD/FSUB/FMUL/FDIV, SQRT, F2INT, SIN/COS, SCALE3/ADD3/SUB3 — and deliberately
 * AVOID the six ⚠ HLE-divergent ops (rot2d 0x5B, dot3 0x2A, mag3d 0x2E, norm3 0x30, ...; see
 * the opcode table in m2_constants.h). Rotation is expressed as x' = dot2d(cos,x,-sin,z); a
 * 3-vector dot as dot2d + one fmul+fadd. So it is correct under both COP paths, not just HLE.
 *
 * GEO COMMANDS EXERCISED per frame (the buffered display-list builder in m2_geo.h): ZSORT,
 * MODE, TEXPARAM, FOCAL, LIGHT, LOD, WINDOW, then per quad TEXDATA (colour header) +
 * MATRIX + OBJECT (model-456 flat-quad instance), closed with END and committed via the
 * STF 4-buffer rotate (geo_flush_flip). That is the whole silicon-proven object_data set.
 *
 *   build:  cmd /c ".\build_clang64.bat knot"
 *   run  :  override sfight's epr-19001.15 / .16, launch MAME (M2_HLE_GEO_OFF for real cpres2).
 */

#include "m2.h"
#include "m2_geo.h"
#include "m2_boot.h"        /* m2_silicon_boot; pulls in m2_math.h (raw COP FIFO + opcodes) */

/* ---- knot / tube geometry ---------------------------------------------------------------- */
#define P_KNOT   2u          /* torus-knot winding p */
#define Q_KNOT   3u          /* torus-knot winding q  (gcd(p,q)=1 -> a trefoil)              */
#define NSEG     30          /* samples along the knot centreline (wraps)                    */
#define NRING    6           /* tube cross-section resolution (hexagon)                      */
#define NCELL    (NSEG*NRING)
#define RS       3.5f        /* centreline scale                                             */
#define TR       1.3f        /* tube radius                                                  */
#define ZS       1.5f        /* z-stretch of the centreline (more depth)                     */
#define SZ       44.0f       /* camera depth the whole knot sits at                          */
#define TILT_ANG 0x1900u     /* fixed X-tilt (~35deg) baked into the rest pose               */

/* ---- animation / shading ----------------------------------------------------------------- */
#define DSPIN    0x0140u     /* knot spin about Y per frame (i16 angle)                      */
#define DLIGHT   0x0090u     /* light orbit per frame                                        */
#define HUE_DIV  4u          /* rainbow flows one segment every HUE_DIV frames               */
#define AMB      0.30f       /* ambient floor                                                */
#define LR       0.72f       /* light xz radius (before normalise)                           */
#define LY0      0.55f       /* light y                                                      */
#define CB       1u          /* first colorbase (cells use CB .. CB+NCELL-1)                 */

/* ==========================================================================================
 * RAW COP MATH — every one of these pushes a command + operands to the COP FIFO and blocks
 * the i960 on the SHARC's answer. Written against the raw FIFO primitives (m2_cop / m2_cop_pf
 * / m2_cop_gf, NOT overridden by m2_fastmath.h) so the coprocessor does the work in EVERY
 * build. Only universal opcodes (same on cpres1 silicon and MAME HLE).
 * ========================================================================================== */
static float cop_fadd(float a, float b){ m2_cop(COP_FADD); m2_cop_pf(a); m2_cop_pf(b); return m2_cop_gf(); }
static float cop_fsub(float a, float b){ m2_cop(COP_FSUB); m2_cop_pf(a); m2_cop_pf(b); return m2_cop_gf(); }
static float cop_fmul(float a, float b){ m2_cop(COP_FMUL); m2_cop_pf(a); m2_cop_pf(b); return m2_cop_gf(); }
static float cop_fdiv(float a, float b){ m2_cop(COP_FDIV); m2_cop_pf(a); m2_cop_pf(b); return m2_cop_gf(); }
static float cop_sqrtf(float x)        { m2_cop(COP_SQRT); m2_cop_pf(x);              return m2_cop_gf(); }
static int   cop_f2int(float x)        { m2_cop(COP_F2INT); m2_cop_pf(x); return (int)M2_COPFIFO; }
/* dot2D: a*b + c*d  (opcode 0x59) — the workhorse; a rotation and half a 3-dot each need it. */
static float cop_dot2(float a, float b, float c, float d){
    m2_cop(COP_DOT2D); m2_cop_pf(a); m2_cop_pf(b); m2_cop_pf(c); m2_cop_pf(d); return m2_cop_gf();
}
/* 3-vector helpers on the COP: dot via dot2d+fmul+fadd; cross via fmul/fsub; add/sub/scale via
 * the native COP vector ops (0x5C/0x5D/0x5E — universal, never fastmath-overridden). */
static float c3dot(const float a[3], const float b[3]){
    return cop_fadd(cop_dot2(a[0], b[0], a[1], b[1]), cop_fmul(a[2], b[2]));
}
static void c3cross(const float a[3], const float b[3], float o[3]){
    o[0] = cop_fsub(cop_fmul(a[1], b[2]), cop_fmul(a[2], b[1]));
    o[1] = cop_fsub(cop_fmul(a[2], b[0]), cop_fmul(a[0], b[2]));
    o[2] = cop_fsub(cop_fmul(a[0], b[1]), cop_fmul(a[1], b[0]));
}
static void c3sub  (const float a[3], const float b[3], float o[3]){ m2_cop_sub3(a, b, o); }
static void c3add  (const float a[3], const float b[3], float o[3]){ m2_cop_add3(a, b, o); }
static void c3scale(float s, const float v[3], float o[3])         { m2_cop_scale3(s, v, o); }
static void c3norm (const float v[3], float o[3]){
    float m = cop_sqrtf(c3dot(v, v));
    float inv = (m > 1e-9f) ? cop_fdiv(1.0f, m) : 0.0f;
    c3scale(inv, v, o);
}

/* ==========================================================================================
 * STATE
 * ========================================================================================== */
static float g_base[NCELL][12];   /* rest-pose cell matrices (col0,normal,col2,T), tilted     */
static float g_hue[NSEG][3];      /* rainbow LUT along the rope (RGB 0..31, float)            */
static u8    g_seg[NCELL];        /* centreline segment index of each cell (for flowing hue)  */
static float g_m[NCELL][12];      /* per-frame spun matrices                                  */
static u32   g_theta;             /* measured tube-frame holonomy (i16), spread to close seam */

static const float ONE_MINUS_AMB = 1.0f - AMB;

static void setpal(u32 cb, int r, int g, int b){
    *(volatile u16 *)(0x01800000u + (cb + 0x1000u) * 2u) = (u16)(M2_RGB(r, g, b) | M2_PAL_SET);
}
static int clamp31(int v){ return (v < 0) ? 0 : (v > 31 ? 31 : v); }

/* Rainbow: three phase-shifted cosines -> RGB in 0..31 (all COP). */
static void make_hue(u32 h, float out[3]){
    float sr, cr, sg, cg, sb, cb;
    cop_sincos(h,                 &sr, &cr);
    cop_sincos((h + 0x5555u) & 0xffffu, &sg, &cg);   /* +120deg */
    cop_sincos((h + 0xAAAAu) & 0xffffu, &sb, &cb);   /* +240deg */
    out[0] = cop_fmul(cop_fadd(cop_fmul(sr, 0.5f), 0.5f), 31.0f);
    out[1] = cop_fmul(cop_fadd(cop_fmul(sg, 0.5f), 0.5f), 31.0f);
    out[2] = cop_fmul(cop_fadd(cop_fmul(sb, 0.5f), 0.5f), 31.0f);
    (void)cr; (void)cg; (void)cb;
}

/* Build one cell's rest matrix from its four ring corners (A,B,C,D cyclic). Mirrors the
 * proven geo_quad_matrix mapping (model-456 affine) but does it on the COP and orients the
 * normal OUTWARD from the tube centreline point Pmid (not the world origin). */
static void cell_matrix(const float A[3], const float B[3], const float C[3], const float D[3],
                        const float Pmid[3], float m[12]){
    float u1[3], u2[3], e[3], col0[3], col2[3], nrm[3], cen[3], ref[3];
    c3sub(B, A, u1); c3sub(C, D, u2); c3add(u1, u2, e); c3scale(1.0f / 24.0f, e, col0);  /* col0: u edge */
    c3sub(A, D, u1); c3sub(B, C, u2); c3add(u1, u2, e); c3scale(1.0f / 24.0f, e, col2);  /* col2: v edge */
    c3add(A, B, e);  c3add(e, C, e);  c3add(e, D, e);   c3scale(0.25f, e, cen);          /* T: centroid */
    c3cross(col0, col2, nrm);
    c3sub(cen, Pmid, ref);                                                               /* outward ref */
    if (c3dot(nrm, ref) < 0.0f){ nrm[0] = cop_fsub(0.0f, nrm[0]); nrm[1] = cop_fsub(0.0f, nrm[1]); nrm[2] = cop_fsub(0.0f, nrm[2]); }
    c3norm(nrm, nrm);
    m[0]=col0[0]; m[1]=col0[1]; m[2]=col0[2];
    m[3]=nrm[0];  m[4]=nrm[1];  m[5]=nrm[2];
    m[6]=col2[0]; m[7]=col2[1]; m[8]=col2[2];
    m[9]=cen[0];  m[10]=cen[1]; m[11]=cen[2];
}

/* Rotate all four columns (x,y,z) of a matrix about X by (ct,st) — bakes the fixed tilt. */
static void tilt_cols(float m[12], float ct, float st, float nst){
    int k;
    for (k = 0; k < 4; k++){
        float y = m[3*k+1], z = m[3*k+2];
        m[3*k+1] = cop_dot2(ct, y, nst, z);   /* y' = y*cos - z*sin */
        m[3*k+2] = cop_dot2(st, y, ct,  z);   /* z' = y*sin + z*cos */
    }
}

/* Build the knot centreline, a parallel-transport tube frame, the ring vertices, the cell
 * matrices, and the rainbow LUT — ALL on the COP. One-time; runs after m2_silicon_boot arms it. */
static void build(void){
    static float P [NSEG][3];
    static float Nf[NSEG][3], Bf[NSEG][3];
    static float V [NSEG][NRING][3];
    int i, j, c;
    float ct, st, nst;

    /* 1. centreline P(t): x=RS(2+cos qt)cos pt, y=RS(2+cos qt)sin pt, z=RS*ZS*sin qt */
    for (i = 0; i < NSEG; i++){
        u32 ap = ((u32)i * P_KNOT * 65536u / NSEG) & 0xffffu;
        u32 aq = ((u32)i * Q_KNOT * 65536u / NSEG) & 0xffffu;
        float cp, sp, cq, sq, rr;
        cop_sincos(ap, &sp, &cp);
        cop_sincos(aq, &sq, &cq);
        rr = cop_fadd(2.0f, cq);
        P[i][0] = cop_fmul(RS, cop_fmul(rr, cp));
        P[i][1] = cop_fmul(RS, cop_fmul(rr, sp));
        P[i][2] = cop_fmul(RS, cop_fmul(ZS, sq));
    }

    /* 2. tube frame via approximate parallel transport (Gram-Schmidt against previous N). */
    {
        float T0[3], a[3], proj[3], tmp[3], d;
        c3sub(P[1], P[NSEG-1], T0); c3norm(T0, T0);
        a[0]=0.0f; a[1]=1.0f; a[2]=0.0f;
        d = c3dot(a, T0); if (d < 0.0f) d = -d;
        if (d > 0.9f){ a[0]=1.0f; a[1]=0.0f; a[2]=0.0f; }       /* avoid up ~parallel to tangent */
        c3scale(c3dot(a, T0), T0, proj); c3sub(a, proj, tmp); c3norm(tmp, Nf[0]);
        c3cross(T0, Nf[0], Bf[0]);
        for (i = 1; i < NSEG; i++){
            float Ti[3], pr[3], nn[3];
            c3sub(P[(i+1)%NSEG], P[i-1], Ti); c3norm(Ti, Ti);
            c3scale(c3dot(Nf[i-1], Ti), Ti, pr); c3sub(Nf[i-1], pr, nn); c3norm(nn, Nf[i]);
            c3cross(Ti, Nf[i], Bf[i]);
        }
    }

    /* 2b. holonomy: transport is not periodic — carrying N once around the closed knot lands
     * θ off the seed frame. Measure that total twist (i16, via COP atan2) so step 3 can spread
     * an equal-and-opposite untwist across the segments and close the tube seam smoothly. */
    {
        float Ti0[3], proj[3], nl[3], cc, ss;
        c3sub(P[1], P[NSEG-1], Ti0); c3norm(Ti0, Ti0);
        c3scale(c3dot(Nf[NSEG-1], Ti0), Ti0, proj);
        c3sub(Nf[NSEG-1], proj, nl); c3norm(nl, nl);
        cc = c3dot(nl, Nf[0]); ss = c3dot(nl, Bf[0]);
        g_theta = m2_cop_atan2f(cc, ss);          /* total accumulated twist error (i16) */
    }

    /* 3. ring vertices V[i][j] = P[i] + TR*(cos φ * N + sin φ * B), where φ untwists the seam:
     * φ = ring-angle - θ*(i/NSEG). At i=0 no correction; by the wrap it cancels θ exactly. */
    for (i = 0; i < NSEG; i++){
        u32 corr = (g_theta * (u32)i / (u32)NSEG) & 0xffffu;
        for (j = 0; j < NRING; j++){
            u32 phi = ((u32)j * 65536u / NRING + 65536u - corr) & 0xffffu;
            float cj, sj, tn[3], tb[3], off[3];
            cop_sincos(phi, &sj, &cj);
            c3scale(cop_fmul(TR, cj), Nf[i], tn);
            c3scale(cop_fmul(TR, sj), Bf[i], tb);
            c3add(tn, tb, off);
            c3add(P[i], off, V[i][j]);
        }
    }

    /* 4. cell matrices (+ bake the fixed tilt), and remember each cell's segment for hue flow. */
    cop_sincos(TILT_ANG, &st, &ct); nst = cop_fsub(0.0f, st);
    c = 0;
    for (i = 0; i < NSEG; i++){
        int i1 = (i + 1) % NSEG;
        for (j = 0; j < NRING; j++){
            int j1 = (j + 1) % NRING;
            float Pmid[3], e[3];
            c3add(P[i], P[i1], e); c3scale(0.5f, e, Pmid);
            cell_matrix(V[i][j], V[i][j1], V[i1][j1], V[i1][j], Pmid, g_base[c]);
            tilt_cols(g_base[c], ct, st, nst);
            g_seg[c] = (u8)i;
            c++;
        }
    }

    /* 5. rainbow LUT along the rope. */
    for (i = 0; i < NSEG; i++)
        make_hue(((u32)i * 65536u / NSEG) & 0xffffu, g_hue[i]);
}

/* Spin one rest cell about Y by (cs,sn) and push it to depth SZ — all on the COP. */
static void spin_cell(const float b[12], float cs, float sn, float nsn, float m[12]){
    int k;
    for (k = 0; k < 4; k++){
        float x = b[3*k], z = b[3*k+2];
        m[3*k]   = cop_dot2(cs, x, nsn, z);   /* x' = x*cos - z*sin */
        m[3*k+1] = b[3*k+1];
        m[3*k+2] = cop_dot2(sn, x, cs,  z);   /* z' = x*sin + z*cos */
    }
    m[11] = cop_fadd(m[11], SZ);
}

/* Light one cell by its spun normal and write its colorbase palette entry — all on the COP. */
static void shade_cell(u32 cb, const float N[3], const float L[3], const float hue[3]){
    float d = cop_fadd(cop_dot2(L[0], N[0], L[1], N[1]), cop_fmul(L[2], N[2]));   /* L . N */
    float lit, rgb[3];
    if (d < 0.0f) d = 0.0f;
    lit = cop_fadd(AMB, cop_fmul(ONE_MINUS_AMB, d));
    c3scale(lit, hue, rgb);
    setpal(cb, clamp31(cop_f2int(rgb[0])), clamp31(cop_f2int(rgb[1])), clamp31(cop_f2int(rgb[2])));
}

int main(void){
    u32 spinAng = 0u, lightAng = 0u, frame = 0u;
    int c;

    m2_init();
    m2_silicon_boot();       /* boots + arms the COP (cpres1) and the GEO (cpres2)            */
    geo_flatquad_init();     /* resolve the model-456 flat-quad mesh pointer                  */
    geo_fill_color_patch();  /* uniform-luma texram patch the flat colour headers sample      */
    build();                 /* one-time knot construction — every op on the COP              */

    for (c = 0; c < 8; c++){ geo_clear(); m2_vsync(); }   /* prime the GEO as a consumer      */

    for (;;){
        float sn, cs, nsn, ls, lc, Lin[3], L[3], lm, inv;
        u32 phaseSeg;

        /* per-frame trig + orbiting light, on the COP. */
        spinAng  = (spinAng + DSPIN) & 0xffffu;
        cop_sincos(spinAng, &sn, &cs); nsn = cop_fsub(0.0f, sn);
        lightAng = (lightAng + DLIGHT) & 0xffffu;
        cop_sincos(lightAng, &ls, &lc);
        Lin[0] = cop_fmul(ls, LR); Lin[1] = LY0; Lin[2] = cop_fmul(lc, LR);
        lm  = cop_sqrtf(c3dot(Lin, Lin));
        inv = cop_fdiv(1.0f, lm);
        c3scale(inv, Lin, L);                                   /* normalised light           */
        phaseSeg = (frame / HUE_DIV) % (u32)NSEG;               /* rainbow flow offset        */

        /* ---- GEO display list: per-frame render state, then a colour header + quad per cell. */
        geo_begin();
        geo_zsort(M2_ZSORT_COARSE);
        geo_mode(GEO_MODE_NP_NS);
        geo_texparam_flat(0xFFu, 0x60u);
        geo_focal(280.0f, 280.0f);
        geo_light(L[0], L[1], L[2]);
        geo_lod(2560.0f);
        geo_window_fullscreen();
        for (c = 0; c < NCELL; c++){
            u32 cb = CB + (u32)c;
            u32 seg = ((u32)g_seg[c] + phaseSeg) % (u32)NSEG;
            const float *nm;
            spin_cell(g_base[c], cs, sn, nsn, g_m[c]);
            nm = &g_m[c][3];                                     /* col1 = world normal        */
            shade_cell(cb, nm, L, g_hue[seg]);
            geo_color_header(cb);
            geo_quad_emit(g_m[c], cb);
        }
        geo_end();
        geo_flush_flip();
        *(volatile u32 *)ZCLIP_REG = 0xFFFF00FFu;

        m2_vsync();
        frame++;
    }
    return 0;
}
