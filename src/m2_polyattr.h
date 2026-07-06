/*
 * m2_polyattr.h — named bitfields for the Model 2B polygon pipeline words.
 *
 * The GEO's per-polygon words were, until now, hand-packed as bare hex at ~half a
 * dozen sites (four near-identical texture-header writers, the direct_data attribute
 * word in three files, two z-sort magic constants). This header gives every field a
 * name, straight off the SEGA Hardware R&D manual, and _Static_asserts that the named
 * compositions still equal the values the silicon-validated code shipped — so the
 * de-magic-ing can never silently drift from the proven bit patterns.
 *
 * Doc provenance (SEGA "Hardware R&D" polygon manual):
 *   - Fig 9-5  Polygon Data Structure ....... the attribute word (shape/link/sided/...)
 *   - Texture Header Data (4 words/polygon) .. th0..th3 field layout
 *   - Fig 9-16 Z-Sort Mode Structure ......... the z-sort granularity word
 *   - Fig 4-4  Polygon Color RAM ............. the palette SET bit (M2_PAL_SET)
 *
 * Pure integer-literal macros only (no typedefs) — it is the tail of m2_constants.h's
 * hardware map and is pulled in from there; it needs GEO_POLY_QUAD (defined above it).
 */
#ifndef M2_POLYATTR_H
#define M2_POLYATTR_H

#include "m2_constants.h"   /* GEO_POLY_QUAD, GEO_TEXRAM_BIT, M2_PALRAM (defined above the tail include) */

/* ==== Polygon attribute word — Fig 9-5 "Polygon Data Structure" =================================
 * The first word of a direct_data polygon unit ({attribute, Nx,Ny,Nz, P0xyz, P1xyz}, each an
 * IEEE-754 float). Only the fields the shipped code exercises are given usable macros; the rest of
 * Fig 9-5 (z-sort type, texture-header offset, texture code, distance-coefficient index) is
 * documented but left un-macro'd because its exact bit positions are not code-verified here. */
#define M2_POLY_END      0u        /* shape (bits 1:0) = 00: end of strip                    */
#define M2_POLY_QUAD     1u        /* shape = 01: quadrangle                                 */
#define M2_POLY_TRI      2u        /* shape = 10: triangle  (unlocked — code faked these as quads) */
#define M2_POLY_INHIBIT  3u        /* shape = 11: inhibit                                    */
#define M2_POLY_LINK0    (0u << 8) /* link shape (bits 9:8): how this poly links to the next */
#define M2_POLY_LINK1    (1u << 8)
#define M2_POLY_LINK2    (2u << 8)
#define M2_POLY_LINK3    (3u << 8)
#define M2_POLY_SINGLE   (0u << 17) /* sidedness (bit 17): single-sided (back-face culled)   */
#define M2_POLY_DOUBLE   (1u << 17) /* sidedness (bit 17): double-sided                      */

/* The value every draw path already emits for a flat double-sided quad. */
_Static_assert((M2_POLY_QUAD | M2_POLY_LINK1 | M2_POLY_DOUBLE) == GEO_POLY_QUAD,
               "m2_polyattr: quad|link1|double must equal GEO_POLY_QUAD");

/* ==== Texture Header Data — 4 words/polygon =====================================================
 * Streamed to texture_ram via a GEO 0x04 (texture-data) command, referenced by tha bit23
 * (GEO_TEXRAM_BIT). th0 = flags + map size, th1 = lumabase, th2 = tile origin + sheet, th3 = color. */
/* --- th0: flags (high bits) + Map Size (log2, low bits) --- */
#define M2_TH0_TEX       0x4000u          /* en tex  (bit14): 1 = textured                */
#define M2_TH0_XLUC      0x2000u          /* xluc    (bit13): 1 = translucent             */
#define M2_TH0_CHECKER   0x8000u          /* checkerboard/mesh (bit15)                     */
#define M2_TH0_MAPX(wb)  ((wb) & 7u)      /* Map Size x = log2(width)  (bits 2:0)          */
#define M2_TH0_MAPY(hb)  (((hb) & 7u) << 3) /* Map Size y = log2(height) (bits 5:3)        */
/* --- th1: luminance base --- */
#define M2_TH1_LUMABASE(l) ((l) & 0xffu)
/* --- th2: tile origin (32-texel granular) + sheet select --- */
#define M2_TH2_TILE(tx,ty) ((((tx) / 32u) & 0x3fu) | ((((ty) / 32u) & 0x1fu) << 6))
#define M2_TH2_SHEET1    0x1000u          /* select the odd/1 texture sheet bank          */
/* --- th3: Color = the polygon's colorbase (index into Polygon Color RAM) --- */
#define M2_TH3_COLORBASE(cb) (((cb) & 0x3ffu) << 6)

/* The three th0 values shipped across m2_geo/m2_obj/m2_draw (128x128 flat, 64x64 disc, 128x64 atlas). */
_Static_assert((M2_TH0_TEX | M2_TH0_MAPX(2u) | M2_TH0_MAPY(2u)) == 0x4012u, "m2_polyattr: th0 128x128");
_Static_assert((M2_TH0_TEX | M2_TH0_XLUC | M2_TH0_MAPX(1u) | M2_TH0_MAPY(1u)) == 0x6009u, "m2_polyattr: th0 64x64 xluc");
_Static_assert((M2_TH0_TEX | M2_TH0_XLUC | M2_TH0_MAPX(2u) | M2_TH0_MAPY(1u)) == 0x600Au, "m2_polyattr: th0 128x64 xluc");

/* ==== Z-Sort Mode — Fig 9-16 "Z-Sort Mode Structure" ===========================================
 * The word after GEO_OP_ZSORT sets the sort granularity. The GEO converts the sorted z to a 1-bit
 * sign / 4-bit exponent / 8-bit mantissa form; the mode word is an IEEE-754 scale whose exponent
 * (E - 127 = "exponent point" in the manual) sets the z-value range: 2^(E-12) <= |z| < 2^(E+15).
 * A larger value = coarser buckets (2D painter's order); a smaller value = finer coplanar sorting. */
#define M2_ZSORT_COARSE  0x40800000u      /* ~4.0f : 2D painter's-order granularity        */
#define M2_ZSORT_FINE    0x3C23D70Au      /* ~0.01f: fine coplanar 3D overlay separation    */

/* ==== Polygon Color RAM SET bit — Fig 4-4 ======================================================
 * Each Polygon Color RAM entry (palram[colorbase + 0x1000]) is 15-bit BGR (bits 14:0) plus bit15
 * "SET": per the manual an entry is only usable with SET = 1 ("Palette 0 is a space and cannot be
 * used"). The colorbase writers OR this in. Verified a no-op on MAME (model2rd.ipp uses only bits
 * 14:0 of palram[cb+0x1000] in both the flat and textured paths) and correct on silicon. Override
 * to 0u to restore the pre-doc behavior. */
#ifndef M2_PAL_SET
#define M2_PAL_SET       0x8000u
#endif

#endif /* M2_POLYATTR_H */
