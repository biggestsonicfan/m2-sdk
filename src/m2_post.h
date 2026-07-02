/*
 * m2_post.h — POST-code boot progress at a FIXED RAM address (the port-0x80 idea).
 *
 * Problem: a boot hang before the display/serial come up is a BLACK SCREEN with zero
 * evidence — the existing serial breadcrumbs ("cfg.11EF8", "geoinit ok") only cover the
 * post-m2_init stages, and only if the serial line is up and being watched. On this
 * board the blind window (COP halt/firmware-stream/GEO boot) is exactly where the
 * historical black-screens lived (iop-1st poison, MEMB decoder quirks, reset ordering).
 *
 * Mechanism: each boot stage writes its code to a record at a FIXED, symbol-free
 * address, so anything that can read RAM can bisect a hang:
 *   - MAME debug bridge / m2emu: read 0x5F0000 at any moment, even mid-hang
 *   - host PEEK over serial: once the server is up (stage/seen show the whole boot)
 *   - a soft reset (watchdog / re-entry): the record survives — the NEXT boot reports
 *     the stage the PREVIOUS run died in (prev) + a boots counter (cold RAM = garbage
 *     magic = fresh record, so stale reads are filtered)
 *
 * The address: 0x005F0000 — the documented free boundary in kx_init.s's RAM map
 * (heap ends 0x5F0000; M2_EXIT_CTL 0x5F8000; i960 tables/PRCB/stacks 0x5FF000+;
 * geoserial's app store ends 0x5E0000). Plain work RAM: safe to write from the very
 * first instruction of main() (only I/O-region writes are boot-poisonous).
 *
 * Convention: m2_post(code) = "stage <code> is now RUNNING". A hang leaves `stage` at
 * the culprit; whether it completed = did the next stage get entered (`seen` mask).
 * Cross-reference with m2_waitdbg (m2_wait.h): POST says WHICH STAGE, waitdbg says
 * WHICH WAIT inside it.
 */
#ifndef M2_POST_H
#define M2_POST_H

typedef struct {
    u32 magic;   /* 'M2PC' = 0x4D325043                                        */
    u32 stage;   /* last stage ENTERED this boot                               */
    u32 seen;    /* bitmask of stages entered this boot (1u << stage)          */
    u32 prev;    /* stage the PREVIOUS run ended in (soft-reset post-mortem)   */
    u32 boots;   /* boot counter since RAM was last cold                       */
} m2_post_t;

#define M2_POST_MAGIC 0x4D325043u   /* 'M2PC' */
#define M2_POST_ADDR  0x005F0000u   /* FIXED — keep host tools' copy in sync   */
#define M2_POST       ((volatile m2_post_t *)M2_POST_ADDR)

/* the host tools index these fields as raw words — pin the layout at compile time */
_Static_assert(__builtin_offsetof(m2_post_t, magic) ==  0, "m2_post_t.magic");
_Static_assert(__builtin_offsetof(m2_post_t, stage) ==  4, "m2_post_t.stage");
_Static_assert(__builtin_offsetof(m2_post_t, seen)  ==  8, "m2_post_t.seen");
_Static_assert(__builtin_offsetof(m2_post_t, prev)  == 12, "m2_post_t.prev");
_Static_assert(__builtin_offsetof(m2_post_t, boots) == 16, "m2_post_t.boots");
_Static_assert(sizeof(m2_post_t) == 20, "m2_post_t size");

/* ---- canonical boot stages (bits in `seen`; keep < 32 and in boot order) ----
 * The geoserial kernel enters them in this order; a standalone/other kernel may skip
 * some (that is what the `seen` mask is for). */
#define M2POST_MAIN      0x01u  /* main() entered, .bss zeroed                          */
#define M2POST_TEXPTRS   0x02u  /* texram/luma pointer cells written                    */
#define M2POST_COP_HALT  0x03u  /* COP halted + reset released + IOP boot regs          */
#define M2POST_COP_FW    0x04u  /* COP firmware streaming to the FIFO (14862 words)     */
#define M2POST_COP_RUN   0x05u  /* COP ctl restored -> running                          */
#define M2POST_COP_POLL  0x06u  /* COP-up poll (probe; RDY or --- either way)           */
#define M2POST_IO_IDLE   0x07u  /* 315-5649 idled (mode=0, TX line to MARK)             */
#define M2POST_GEO_BOOT  0x08u  /* GEO boot (copro_down2 + MSGR7)                       */
#define M2POST_BUFFRAM   0x09u  /* BUFF_RAM poison scrub (pre-display)                  */
#define M2POST_M2INIT    0x0Au  /* m2_init — display comes up after this                */
#define M2POST_SELFTEST  0x0Bu  /* RS-422 selftest (drain/strobe/echo verdicts)         */
#define M2POST_COLOR     0x0Cu  /* colorxlat + poly palette                             */
#define M2POST_GEOFUNC   0x0Du  /* geo_func: GEO_RELATED command-region prime           */
#define M2POST_COPARM    0x0Eu  /* cop_initialize / cop_arm                             */
#define M2POST_COPTEST   0x0Fu  /* COP fmul self-test (FIFO READ — can stall)           */
#define M2POST_GEOINIT   0x10u  /* geo_initialize: 4 rotating display-list buffers      */
#define M2POST_GEOMSTUFF 0x11u  /* geometry_stuff: seed the 5 GEO regions               */
#define M2POST_TEXFILL   0x12u  /* texram opaque fill                                   */
#define M2POST_LUMA      0x13u  /* make_luma_ram                                        */
#define M2POST_CFG50     0x14u  /* GEO config stream (STF sub_11EF8)                    */
#define M2POST_CFG40A    0x15u  /* GEO config stream (STF sub_12090)                    */
#define M2POST_CFG40B    0x16u  /* GEO config stream (STF sub_12138)                    */
#define M2POST_CFG140    0x17u  /* GEO slot-0x140 bit-packed LUT (STF sub_11E08)        */
#define M2POST_MATERIAL  0x18u  /* geo_material_init + BUFF_RAM re-clean                */
#define M2POST_PRIME     0x19u  /* 8 warm-up frames (GEO as continuous consumer)        */
#define M2POST_SERVE     0x1Au  /* BOOT COMPLETE: manager / server / standalone app     */
#define M2POST_APP       0x1Bu  /* an uploaded app is running (back to SERVE on exit)   */

/* Call FIRST in main(): salvage the previous run's stage (post-mortem), then start a
 * fresh record. Cold RAM fails the magic check -> prev=0, boots=1. */
static void m2_post_begin(void) {
    if (M2_POST->magic == M2_POST_MAGIC) {
        M2_POST->prev = M2_POST->stage;
        M2_POST->boots++;
    } else {
        M2_POST->magic = M2_POST_MAGIC;
        M2_POST->prev  = 0u;
        M2_POST->boots = 1u;
    }
    M2_POST->stage = 0u;
    M2_POST->seen  = 0u;
}

/* Mark stage `code` as running (and remember it was entered). */
static void m2_post(u32 code) {
    M2_POST->stage = code;
    M2_POST->seen |= 1u << code;
}

#endif /* M2_POST_H */
