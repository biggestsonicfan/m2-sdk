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

M2_API int xtransport_trygetc(void) {
    u8 f;
    if (xt_pb >= 0) { int b = xt_pb; xt_pb = -1; return b; }

    f = M2_IO.flag;

    /* The real 315-5649 idles with FLAG bit6 (0x40) SET; MAME never sets it (its FLAG is
     * only 0x00..0x0C). So bit6 cleanly selects SILICON vs MAME. On SILICON do PURE
     * flag-polled RX with NO strobe (the idle strobe both floods the link and disturbs
     * the single-channel RX). Read on ANY RXBF bit: a single byte lands in RXD1 (RX1BF,
     * FLAG=0x44), a rapid multi-byte frame sets BOTH (0x0C). RX1BF has priority. */
    if (f & 0x40u) {                              /* real silicon */
        if (f & 0x04u) return (int)M2_IO.rxd1;       /* RX1BF (alone or with RX2BF) */
        if (f & 0x08u) return (int)M2_IO.rxd2;       /* RX2BF only */
        return -1;                                /* no byte; pure poll, no strobe */
    }

    /* MAME (bit6 clear): strobe to pop the host-injection FIFO — delivered synchronously
     * (BOTH RXBF set + status 0x01 in RXD1 / data in RXD2). No spin. */
    M2_IO.txd2 = 0; M2_IO.txd1 = XT_CMD_IDLE;
    if ((M2_IO.flag & XT_RXBF_BOTH) == XT_RXBF_BOTH) {
        u8 st = M2_IO.rxd1, dt = M2_IO.rxd2;
        if (st & XT_RX_VALID) return (int)dt;
    }
    return -1;
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
