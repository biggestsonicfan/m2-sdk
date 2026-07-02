/*
 * m2_wait.h — instrumented hardware waits: "WHICH wait are we stuck in / did we time out?"
 *
 * Problem: every hardware wait in the kernel/SDK was a bare bounded spin whose TIMEOUT fell
 * through SILENTLY (the caller continued as if the condition had been met), and the one
 * unbounded wait (geo_interrupt_wait) hangs forever with no externally visible state. On
 * this half-documented board, "which wait?" is the first diagnostic question of every hang.
 *
 * Mechanism: a live record in kernel .bss (m2_waitdbg):
 *   in_site  — site ID of the wait being spun RIGHT NOW (0 = not in a wait)
 *   in_spins — coarse heartbeat (updated every 256 polls). Ticking = spinning normally;
 *              frozen with in_site != 0 = stuck INSIDE one bus access (COP FIFO read stall)
 *   to_count / to_site / to_addr / to_last — running timeout count + the last timeout's
 *              site, polled address, and final value read
 * Read it live via the MAME debug bridge or a host PEEK — the geoserial kernel prints the
 * struct's address in its boot log, and game.map carries the m2_waitdbg symbol. An optional
 * hook fires once per timeout; point it at a SCREEN printer only, never the serial path
 * (a serial-TX timeout inside a serial-printing hook would recurse).
 *
 * The polled address is a runtime parameter, so every access compiles register-indirect
 * (m2emu-safe: no absolute-displacement MEMB loads).
 *
 * NOT coverable: COP command-FIFO READS (m2_cop_gf, cop_drain) stall the i960 inside ONE
 * bus read — software cannot bound them. cop_drain sets in_site around its reads so the
 * stall is at least attributable (in_site = M2W_COP_DRAIN, heartbeat frozen).
 *
 * Include via m2.h (needs u8/u16/u32).
 */
#ifndef M2_WAIT_H
#define M2_WAIT_H

/* wait-site IDs (in_site / to_site values; 0 = none — keep these unique) */
#define M2W_RS422_TX2   0x01u  /* rs422_putc: TXD2 buffer-empty (geoserial)             */
#define M2W_RS422_TX1   0x02u  /* rs422_putc: TXD1 buffer-empty (geoserial)             */
#define M2W_RS422_DRAIN 0x03u  /* serial_selftest: RX drain (geoserial)                 */
#define M2W_RS422_RXRET 0x04u  /* serial_selftest: peer-reply wait (geoserial)          */
#define M2W_XT_RX       0x05u  /* xt_wait_ack: both RX buffers full (m2_rs422.h)        */
#define M2W_UART_TX     0x06u  /* m2_uart_putc: aux i8251 TxRDY (m2.h)                  */
#define M2W_SND_TX      0x07u  /* m2_sound_byte: sound i8251 TxRDY (m2.h)               */
#define M2W_COP_ARM     0x08u  /* cop_arm / m2_cop_initialize: COP ready (COP_STATUS)   */
#define M2W_COP_BOOT    0x09u  /* post-boot COP-up poll (COP_BOOTSTAT bit1)             */
#define M2W_COP_DRAIN   0x0Au  /* cop_drain: COP FIFO read (stall marker only)          */
#define M2W_GEO_IRQ     0x0Bu  /* geo_interrupt_wait: vblank counter (UNBOUNDED)        */

typedef struct {
    u32 magic;      /* 'M2WD' = 0x4D325744 — lets a host confirm it found the record */
    u32 in_site;    /* wait being spun right now (0 = none)                          */
    u32 in_spins;   /* heartbeat: poll count of the current wait, updated every 256  */
    u32 to_count;   /* timeouts since boot                                            */
    u32 to_site;    /* last timeout: site                                             */
    u32 to_addr;    /* last timeout: polled address                                   */
    u32 to_last;    /* last timeout: final value read                                 */
} m2_waitdbg_t;
#define M2_WAITDBG_MAGIC 0x4D325744u   /* 'M2WD' */

static volatile m2_waitdbg_t m2_waitdbg;
static void (*m2_wait_hook)(u32 site, u32 addr, u32 val);   /* fires once per timeout (0 = off) */

/* Bracket a NON-standard wait loop (compound condition / side effects per poll) yourself:
 * enter before the loop, beat(g) each iteration, exit on success. */
static void m2_wait_enter(u32 site) {
    m2_waitdbg.magic = M2_WAITDBG_MAGIC;
    m2_waitdbg.in_site = site; m2_waitdbg.in_spins = 0u;
}
static void m2_wait_exit(void)  { m2_waitdbg.in_site = 0u; }
static void m2_wait_beat(u32 g) { if (!(g & 0xFFu)) m2_waitdbg.in_spins = g; }

/* Record a timeout: count it, snapshot the evidence, fire the hook. Clears in_site —
 * a caller that keeps waiting afterwards (geo_interrupt_wait) re-sets it. */
static void m2_wait_timeout(u32 site, u32 addr, u32 val) {
    m2_waitdbg.to_count++;
    m2_waitdbg.to_site = site; m2_waitdbg.to_addr = addr; m2_waitdbg.to_last = val;
    m2_waitdbg.in_site = 0u;
    if (m2_wait_hook) m2_wait_hook(site, addr, val);
}

/* Spin until (*addr & mask) == want, or `spins` polls elapse.
 * Returns 1 = condition met, 0 = TIMED OUT (recorded + hook fired). Callers keep their
 * historical fall-through-on-timeout behavior — the difference is it is now VISIBLE. */
static int m2_wait_mask8(u32 site, u32 addr, u8 mask, u8 want, u32 spins) {
    volatile u8 *r = (volatile u8 *)addr;
    u32 g; u8 v = 0;
    m2_wait_enter(site);
    for (g = 0; g < spins; g++) {
        v = *r;
        if ((v & mask) == want) { m2_wait_exit(); return 1; }
        m2_wait_beat(g);
    }
    m2_wait_timeout(site, addr, (u32)v);
    return 0;
}
static int m2_wait_mask16(u32 site, u32 addr, u16 mask, u16 want, u32 spins) {
    volatile u16 *r = (volatile u16 *)addr;
    u32 g; u16 v = 0;
    m2_wait_enter(site);
    for (g = 0; g < spins; g++) {
        v = *r;
        if ((v & mask) == want) { m2_wait_exit(); return 1; }
        m2_wait_beat(g);
    }
    m2_wait_timeout(site, addr, (u32)v);
    return 0;
}
static int m2_wait_mask32(u32 site, u32 addr, u32 mask, u32 want, u32 spins) {
    volatile u32 *r = (volatile u32 *)addr;
    u32 g, v = 0;
    m2_wait_enter(site);
    for (g = 0; g < spins; g++) {
        v = *r;
        if ((v & mask) == want) { m2_wait_exit(); return 1; }
        m2_wait_beat(g);
    }
    m2_wait_timeout(site, addr, v);
    return 0;
}

/* PROBE variants — for waits whose timeout is a legitimate ANSWER, not a fault:
 * "is a host attached?" (selftest reply wait), "is this register modeled here?"
 * (the COP-up poll: 0x980014 is unmapped in MAME, so it can never succeed there).
 * A probe is marked in in_site while spinning (a genuine hang is still attributable)
 * but its timeout is NOT counted/recorded/hooked — the CALLER interprets the returned
 * status into its own verdict ("cop ---", "RS422 NO REPLY"). Using a fault wait for a
 * probe makes the timeout tripwire fire on every normal boot (alarm fatigue) and lets
 * the probe clobber the to_* evidence of a real fault. */
static int m2_wait_probe8(u32 site, u32 addr, u8 mask, u8 want, u32 spins) {
    volatile u8 *r = (volatile u8 *)addr;
    u32 g;
    m2_wait_enter(site);
    for (g = 0; g < spins; g++) {
        if ((*r & mask) == want) { m2_wait_exit(); return 1; }
        m2_wait_beat(g);
    }
    m2_wait_exit();
    return 0;
}
static int m2_wait_probe32(u32 site, u32 addr, u32 mask, u32 want, u32 spins) {
    volatile u32 *r = (volatile u32 *)addr;
    u32 g;
    m2_wait_enter(site);
    for (g = 0; g < spins; g++) {
        if ((*r & mask) == want) { m2_wait_exit(); return 1; }
        m2_wait_beat(g);
    }
    m2_wait_exit();
    return 0;
}

#endif /* M2_WAIT_H */
