/*
 * m2_shock.h — Sonic the Fighters' "squash and stretch" shock, ported to the SDK.
 *
 * The STF hit-reaction from patent JP H9-231409 (SEGA, 1996): when a character takes a
 * shock, its per-axis SCALE runs a damped oscillation, squashing along the impact axis and
 * bulging on the others, ringing back to normal. Faithful to the patent's steps 213-215:
 *
 *   impact (213):  S0 = current S (successive hits compose), V0 = per-axis impulse
 *   frame  (214):  S = e^(-kt) * ( S0*cos(wt) + (V0/w)*sin(wt) )      per axis
 *   draw   (215):  scale' = 1 + S
 *
 * Advanced INCREMENTALLY so there are NO per-frame transcendentals: the decay is a running
 * multiply by e^(-k*dt), and (cos wt, sin wt) is rotated by (cos w*dt, sin w*dt) each frame.
 * You supply those three per-step constants at config (compute once with a real cos/exp, or
 * hardcode them) — k is the material viscosity, w its hardness (rad/frame).
 *
 * COPROCESSOR MATH: the patent runs this on the float co-processor (co-processor 108 = the
 * COP on Model 2). Route it there by #defining the three arithmetic macros to the COP scalar
 * ops BEFORE including — a geoserial app to gs_cop_*, a standalone SDK app to m2_cop_* — off
 * the FPU-less i960. Undefined, they fall back to plain (soft-float) C operators.
 *
 *     #define M2_SHOCK_MUL(a,b) gs_cop_fmul((a),(b))
 *     #define M2_SHOCK_ADD(a,b) gs_cop_fadd((a),(b))
 *     #define M2_SHOCK_SUB(a,b) gs_cop_fsub((a),(b))
 *     #include "m2_shock.h"
 *
 * Needs the SDK u32 typedef (include "m2.h" / "gs_rt.h" first). Opt-in: m2.h does NOT pull
 * this in, so the routing macros are always yours to set.
 *
 * Usage:
 *     static m2_shock_t sh;
 *     m2_shock_config(&sh, KDECAY, COSW, SINW, CUTOFF);   // material, once
 *     ...on impact (face-first: squash local -Z, bulge X/Y):
 *     m2_shock_hit(&sh, +side, +side, -fwd);
 *     ...each frame, OUTSIDE the gs_frame_begin..commit window (COP round-trips):
 *     m2_shock_step(&sh);
 *     ...draw: sh.scale[] is ALWAYS valid (1,1,1 at rest), so no branch:
 *     gs_draw_object_rs(model, px,py,pz, 0,ay,0, sh.scale[0], sh.scale[1], sh.scale[2]);
 */
#ifndef M2_SHOCK_H
#define M2_SHOCK_H

#ifndef M2_SHOCK_MUL
#define M2_SHOCK_MUL(a, b) ((a) * (b))
#define M2_SHOCK_ADD(a, b) ((a) + (b))
#define M2_SHOCK_SUB(a, b) ((a) - (b))
#endif

typedef struct {
    int   active;                 /* 0 = at rest (scale = 1,1,1)                         */
    float kdecay, cosw, sinw;     /* per-step constants: e^(-k*dt), cos(w*dt), sin(w*dt) */
    float cutoff;                 /* decay floor -> snap back to rest                    */
    float decay, c, s;            /* running e^(-kt), cos(wt), sin(wt)                   */
    float S0[3], V0[3];           /* impact scale-change + scaled velocity V0/w (x,y,z)  */
    float S[3];                   /* current per-axis scale CHANGE (0 = rest)            */
    u32   scale[3];               /* 1+S as IEEE bits, ready for a SCALE object submit   */
} m2_shock_t;

static u32 m2__shock_fb(float f) { union { float f; u32 u; } x; x.f = f; return x.u; }

static void m2__shock_rest(m2_shock_t *sh) {
    sh->active = 0;
    sh->S[0] = sh->S[1] = sh->S[2] = 0.0f;
    sh->scale[0] = sh->scale[1] = sh->scale[2] = m2__shock_fb(1.0f);
}

/* material config: kdecay = e^(-k*dt), cosw/sinw = cos/sin(w*dt), cutoff = decay floor
 * (e.g. 0.04). Leaves the oscillator at rest (scale 1,1,1). */
static void m2_shock_config(m2_shock_t *sh, float kdecay, float cosw, float sinw, float cutoff) {
    sh->kdecay = kdecay; sh->cosw = cosw; sh->sinw = sinw; sh->cutoff = cutoff;
    m2__shock_rest(sh);
}

/* impact (patent step 213): seed S0 from the CURRENT S so back-to-back hits compose, set the
 * per-axis impulse v0 = V0/w (squash axis negative, bulge axes positive), restart the clock. */
static void m2_shock_hit(m2_shock_t *sh, float v0x, float v0y, float v0z) {
    sh->S0[0] = sh->S[0]; sh->S0[1] = sh->S[1]; sh->S0[2] = sh->S[2];
    sh->V0[0] = v0x; sh->V0[1] = v0y; sh->V0[2] = v0z;
    sh->decay = 1.0f; sh->c = 1.0f; sh->s = 0.0f;
    sh->active = 1;
}

/* one frame of the damped oscillation (patent steps 214+215) — every product/sum routes
 * through M2_SHOCK_{MUL,ADD,SUB}. Call once/frame OUTSIDE any frame-submit window if those
 * map to COP round-trips. No-op (keeps scale 1,1,1) once rung down. */
static void m2_shock_step(m2_shock_t *sh) {
    float nc, ns;
    int a;
    if (!sh->active) return;
    sh->decay = M2_SHOCK_MUL(sh->decay, sh->kdecay);                       /* e^(-kt) advance */
    nc = M2_SHOCK_SUB(M2_SHOCK_MUL(sh->c, sh->cosw), M2_SHOCK_MUL(sh->s, sh->sinw));  /* cos */
    ns = M2_SHOCK_ADD(M2_SHOCK_MUL(sh->s, sh->cosw), M2_SHOCK_MUL(sh->c, sh->sinw));  /* sin */
    sh->c = nc; sh->s = ns;
    for (a = 0; a < 3; a++) {                                              /* S = e^-kt(S0 c + V0 s) */
        sh->S[a] = M2_SHOCK_MUL(sh->decay,
                       M2_SHOCK_ADD(M2_SHOCK_MUL(sh->S0[a], sh->c), M2_SHOCK_MUL(sh->V0[a], sh->s)));
        sh->scale[a] = m2__shock_fb(M2_SHOCK_ADD(1.0f, sh->S[a]));         /* step 215: 1 + S */
    }
    if (sh->decay < sh->cutoff) m2__shock_rest(sh);                        /* rung down */
}

static int m2_shock_active(const m2_shock_t *sh) { return sh->active; }

#endif /* M2_SHOCK_H */
