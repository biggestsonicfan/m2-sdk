/*
 * m2_boot.h — one-call silicon bring-up for the COP/GEO 3D pipeline.
 *
 * Composes the per-layer inits — m2_3d.h (firmware boot + COP arm), m2_color.h (colour
 * pipeline), m2_geo.h (GEO display-list init) — into m2_silicon_boot(). Call ONCE after
 * m2_init(), before the per-frame draw loop.
 *
 * Boot is path-specific: this is the COP-driven OBJECT_DATA variant (it arms the COP and
 * seeds the GEO regions so the COP can commit geometry — see m2_obj.h). The MAME-only g2d
 * path and the lighter DIRECT-FIFO path (m2_draw/m2_text) boot differently.
 *
 * Include via m2_obj.h (or directly, AFTER m2.h).
 */
#ifndef M2_BOOT_H
#define M2_BOOT_H

#include "m2_3d.h"      /* m2_3d_boot, m2_cop_initialize          */
#include "m2_geo.h"     /* geo_func, geo_initialize, geometry_stuff */
#include "m2_color.h"   /* m2_color_init, m2_load_poly_palette     */

/* One-call silicon bring-up — the PROVEN order (each omission breaks it: missing geometry_stuff =>
 * the real GEO firmware walks into an unimplemented SHARC IOP write; missing m2_cop_initialize =>
 * the COP never commits, GEO stuck on header words). Call ONCE after m2_init(); set backdrop/palette
 * after (m2_color_init has loaded the default 1024-colour polygon palette). */
static void m2_silicon_boot(void) {
    m2_3d_boot();            /* COP (cpres1) + real GEO geometrizer (cpres2) firmware upload */
    m2_color_init();         /* colorxlat + lumaram (else everything renders black)          */
    m2_load_poly_palette();  /* STF 1024-colour default polygon palette                      */
    geo_func();              /* prime the GEO command region (GEO_RELATED)                    */
    m2_cop_initialize();     /* arm the COP (wait-ready + write 0)                            */
    geo_initialize();        /* STF GEO init: 4 rotating display-list buffers                */
    geometry_stuff();        /* seed the 5 GEO regions (clip/matrix/microcode) — REQUIRED    */
}

#endif /* M2_BOOT_H */
