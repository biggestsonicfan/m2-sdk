/*
 * m2_rs422.h — Sega Model 2 host round-trip link over the 315-5649 RS-422 channel.
 *
 * Promoted into the SDK from m2-x11/src/xtransport.h (2026-06-19): the low-level,
 * silicon-validated transport any Model 2 program can use to talk to a host. The
 * 315-5649 link is a half-duplex, two-channel, lockstep exchange — NOT a plain UART:
 *
 *   - TXD1 (0x12) is the COMMAND/strobe register; writing it triggers a transfer.
 *   - TXD2 (0x14) holds the DATA byte (latched before the strobe).
 *   - the handshake spins until FLAG (0x1A) bits 2,3 (RX1BF|RX2BF) are BOTH set,
 *     i.e. the peer has answered on both channels.
 *   - then RXD1 (0x16) and RXD2 (0x18) are read back.
 *
 * Carry a byte stream full-duplex by framing: channel 0 (cmd/TXD1, status/RXD1) =
 * control, channel 1 (data/TXD2,RXD2) = byte. Inbound bytes buffer in a small ring so
 * send/receive don't clobber each other.
 *
 * WIRE FORMAT (confirmed on a logic analyzer + live FTDI read): the TTL line out of the
 * 315-5649 (before the SN75179B differential driver) is a plain async UART: 8-N-1,
 * LSB-first, idle-HIGH, at 2.0 Mbaud (FT232R locks it at 3MHz/1.5 = exactly 2.000M).
 * Because every xt_transact() writes TXD2 (data) AND TXD1 (the 0x07 strobe), BOTH
 * serialize onto the wire, so a 0x07 precedes every payload byte; a host reader must
 * DROP the 0x07 strobes (exactly 50% of wire bytes) to recover the payload.
 *
 * Public API (M2_API): xtransport_init, xt_link_init, xtransport_trygetc, xtransport_putc,
 * xtransport_write, xtransport_loopback, xtransport_unget. Include AFTER m2.h.
 */
#ifndef M2_RS422_H
#define M2_RS422_H

#include "m2.h"
#include "m2_io.h"   /* the 315-5649 I/O chip struct: M2_IO.txd1/txd2/rxd1/rxd2/flag/mode */

#define XT_RXBF_BOTH 0x0Cu   /* FLAG bits 2,3: RX1BF & RX2BF (handshake target) */
#define XT_RX_VALID  0x01u   /* RXD1 status bit0: inbound data valid            */
#define XT_LOOP      0x10u   /* MODE bit4: hardware loopback                    */

/* command codes (match serial_stuff: 0x07 = data word, 0x82 = sync/idle poll) */
#define XT_CMD_DATA  0x07u
#define XT_CMD_IDLE  0x82u

#define XT_SPIN  200000u     /* bounded wait so a dead peer can't hang us */

/* inbound software ring (decouples receive from the lockstep transactions) */
#define XT_RING 256
static u8  xt_ring[XT_RING];
static u16 xt_rh, xt_rt;     /* head (read), tail (write) */

M2_API void xtransport_init(void) {
    M2_WRITE_TWICE(M2_IO.mode, 0x00u);   /* master, no loopback (g14 shim, m2_workaround.h) */
    xt_rh = xt_rt = 0;
}

M2_API void xtransport_loopback(int on) {
    u8 v = on ? XT_LOOP : 0x00u;
    M2_WRITE_TWICE(M2_IO.mode, v);
}

/* Wait for the two-channel handshake: both receive buffers full (bounded + recorded). */
static void xt_wait_ack(void) {
    (void)m2_wait_mask8(M2W_XT_RX, (u32)&M2_IO.flag, XT_RXBF_BOTH, XT_RXBF_BOTH, XT_SPIN);
}

/* One faithful transaction: latch data into TXD2, strobe the command into TXD1,
 * wait for the peer, then read status (RXD1) + inbound data (RXD2). Any valid
 * inbound byte is pushed into the receive ring. */
static void xt_transact(u8 cmd, u8 data) {
    u8 st, dt, nt;
    M2_IO.txd2 = data;          /* data first ... */
    M2_IO.txd1 = cmd;           /* ... then the strobe (triggers the transfer) */
    xt_wait_ack();
    st = M2_IO.rxd1;            /* channel-0 status */
    dt = M2_IO.rxd2;            /* channel-1 inbound data */
    if (st & XT_RX_VALID) {
        nt = (u8)((xt_rt + 1) % XT_RING);
        if (nt != xt_rh) { xt_ring[xt_rt] = dt; xt_rt = nt; }
    }
}

/* ---- 315-5649 link bring-up (faithful to the real firmware io_flag_setup @0x1380) ----
 * The real firmware runs this command/handshake sequence on the 315-5649 BEFORE any
 * traffic. Commands are the literal constants from io_flag_setup; each is gated by the
 * peer handshake (bounded via xt_wait_ack so a dead link can't hang the boot). Run with
 * a peer/loopback present (each handshake needs RX to fill). */
static void xt_link_cmd(u8 cmd) {            /* TXD1-only (TXD2 unchanged), drain RX */
    M2_IO.txd1 = cmd; xt_wait_ack(); (void)M2_IO.rxd1; (void)M2_IO.rxd2;
}
static u8 xt_link_cmd_d(u8 cmd, u8 data) {   /* TXD2=data, strobe, drain; ret RXD2   */
    M2_IO.txd2 = data; M2_IO.txd1 = cmd; xt_wait_ack(); (void)M2_IO.rxd1; return M2_IO.rxd2;
}
M2_API void xt_link_init(void) {
    u8 r; volatile u32 d;
    xt_link_cmd_d(0x01u, 0xFFu);                 /* 1 */
    xt_link_cmd_d(0x08u, 0x7Du);                 /* 2 */
    xt_link_cmd(0x81u);                          /* 3 */
    xt_link_cmd(0x88u);                          /* 4 */
    xt_link_cmd(0x81u); r = M2_IO.rxd2;             /* 5 */
    xt_link_cmd_d(0x01u, (u8)(r & ~1u));         /* 6: echo RXD2, bit0 clear */
    for (d = 0; d < 1000u; d++) { }              /* 7: STF 1000-iter settle */
    xt_link_cmd(0x81u); r = M2_IO.rxd2;             /* 8 */
    xt_link_cmd_d(0x01u, (u8)(r | 1u));          /* 9: echo RXD2, bit0 set   */
    xt_link_cmd(0x82u);                          /* 10 */
}

/* Non-blocking receive: returns 0..255, or -1 if nothing is waiting. */
static int xt_pb = -1;                       /* one-byte pushback (peek support) */
M2_API void xtransport_unget(int b) { xt_pb = b; }

/* Runtime host environment for the RX idle path (see xtransport_trygetc):
 *   -1 = unknown (self-calibrate on the first idle poll)
 *    0 = SILICON   — pure FLAG-poll, NEVER strobe
 *    1 = MAME      — strobe TXD1 each idle poll to pop the host-injection FIFO
 * Historically this was inferred every poll from FLAG bit6 (0x40 = line-idle), but that
 * gate REGRESSED: the deployment board now idles bit6 CLEAR (FLAG0=0x00) just like MAME,
 * so bit6 can no longer tell them apart. Detect the environment ONCE, at the wire, via a
 * behaviour only MAME exhibits (see xt_env_probe below), then latch it here. */
static int xt_env = -1;

/* Seed the environment from a controlled boot-time probe (e.g. serial_selftest's strobe
 * test), so the serve loop never has to speculatively strobe on silicon. mame != 0 => MAME. */
M2_API void xtransport_set_env(int mame) { xt_env = mame ? 1 : 0; }

M2_API int xtransport_trygetc(void) {
    u8 f;
    if (xt_pb >= 0) { int b = xt_pb; xt_pb = -1; return b; }

    f = M2_IO.flag;

    /* A received byte is present whenever an RXBF bit is set (bit2=RX1BF, bit3=RX2BF) — read it
     * REGARDLESS of bit6. On silicon a paced host byte lands here directly (FLAG=0x04, RXD1=byte,
     * bit6 CLEAR — proven 2026-07-11 with the GS_RXPROBE build); on MAME a strobed byte lands here
     * as RX1BF|RX2BF. Either way this is the fast, non-intrusive path and it is tried first. */
    if (f & 0x04u) return (int)M2_IO.rxd1;       /* RX1BF -> data in RXD1 */
    if (f & 0x08u) return (int)M2_IO.rxd2;       /* RX2BF -> data in RXD2 */

    /* No byte by pure poll. The idle behaviour now DIFFERS by host and must not be guessed from
     * bit6 (regressed to 0 on the deployment board):
     *   - SILICON: the 315-5649 deserialises inbound bytes straight into RXD1/RXD2, so no strobe is
     *     needed; worse, an idle strobe (txd1=XT_CMD_IDLE) fires every poll and floods board->host
     *     with 00 82 pairs (~192 KB/s), burying the gs_reply frames — the exact bug that made the
     *     host see PING go unanswered even though the board went CONNECTED.
     *   - MAME (315_5649.cpp): a host byte sits in the host-injection FIFO and only pops when the
     *     board STROBES TXD1; FLAG stays 0x00 until then. The strobe unconditionally enqueues a
     *     status+data pair, so FLAG comes back 0x0C (RX1BF|RX2BF) — which is also how we detect it.
     * xt_env latches which world we're in: silicon (0) => never strobe; MAME (1) => strobe. When
     * still unknown (-1) — no boot seed via xtransport_set_env — self-calibrate with ONE strobe
     * (harmless on silicon: it is exactly serial_selftest's proven-safe probe). */
    if (xt_env != 0) {                            /* MAME, or unknown -> probe once */
        M2_IO.txd2 = 0; M2_IO.txd1 = XT_CMD_IDLE; /* strobe to pop the MAME host FIFO / probe silicon */
        if ((M2_IO.flag & XT_RXBF_BOTH) == XT_RXBF_BOTH) {
            u8 st = M2_IO.rxd1, dt = M2_IO.rxd2;  /* drain BOTH channels (status + data) */
            xt_env = 1;                           /* strobe produced a reply => MAME */
            if (st & XT_RX_VALID) return (int)dt;
            return -1;
        }
        if (xt_env < 0) xt_env = 0;               /* strobe drew nothing => SILICON: stop strobing */
    }
    return -1;                                     /* silicon idle, or MAME strobe with no data */
}

/* Send one byte (carried in TXD2 with the DATA command, lockstep). Returns 1. */
M2_API int xtransport_putc(u8 b) {
    xt_transact(XT_CMD_DATA, b);
    return 1;
}

M2_API int xtransport_write(const u8 *p, int n) {
    int i;
    for (i = 0; i < n; i++) xtransport_putc(p[i]);
    return n;
}

#endif /* M2_RS422_H */
