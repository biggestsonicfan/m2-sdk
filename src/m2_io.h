/*
 * m2_io.h — the Sega 315-5649 on-board I/O chip, mapped at 0x01C00000.
 *
 * One struct for the whole chip: byte registers on a 2-byte stride (each real
 * register sits at an even offset; the _pNN fields are the odd-address gaps).
 * Access via M2_IO.<reg>. Covers the digital input bank + player ports, the
 * RS-422 host link (m2_rs422.h), and the STF bring-up handshake (m2_init).
 *
 * Include via m2.h (needs u8). NB the i8251 debug UART is a SEPARATE chip at
 * 0x01C80000 (see m2.h M2_UART_*), not part of this I/O chip.
 */
#ifndef M2_IO_H
#define M2_IO_H

typedef struct {
    u8 bank, _p01;    /* 0x00  W: digital input bank select        */
    u8 in0,  _p03;    /* 0x02  R: system inputs (bit4 = coin/start) */
    u8 in1,  _p05;    /* 0x04  R: P1 buttons (active-low)          */
    u8 in2,  _p07;    /* 0x06  R: P2 buttons (active-low)          */
    u8 _r08[0x0A];    /* 0x08..0x11                                */
    u8 txd1, _p13;    /* 0x12  W: RS-422 command / strobe          */
    u8 txd2, _p15;    /* 0x14  W: RS-422 data byte                 */
    u8 rxd1, _p17;    /* 0x16  R: RS-422 status (bit0 = valid)     */
    u8 rxd2, _p19;    /* 0x18  R: RS-422 inbound data byte         */
    u8 flag, _p1b;    /* 0x1A  R: RS-422 TX/RX buffer flags        */
    u8 mode, _p1d;    /* 0x1C  W: RS-422 LOOP / satellite / sat#   */
    u8 _r1e[0x06];    /* 0x1E..0x23                                */
    u8 hs,   _p25;    /* 0x24  W: bring-up handshake (STF writes 1) */
    u8 _r26[0x0E];    /* 0x26..0x33                                */
    u8 sig0, _p35;    /* 0x34  W: 'S' \                            */
    u8 sig1, _p37;    /* 0x36  W: 'E'  |  STF "SEGA" bring-up sig  */
    u8 sig2, _p39;    /* 0x38  W: 'G'  |                           */
    u8 sig3, _p3b;    /* 0x3A  W: 'A' /                            */
    u8 _r3c[0x04];    /* 0x3C..0x3F                                */
    u8 enable;        /* 0x40  W: I/O enable (STF bring-up: 0)     */
} m2_io_t;

#define M2_IO (*(volatile m2_io_t *)0x01C00000u)

/* sanity: catch any struct padding that would slide a register off its offset. */
_Static_assert(__builtin_offsetof(m2_io_t, txd1)   == 0x12, "m2_io_t.txd1");
_Static_assert(__builtin_offsetof(m2_io_t, mode)   == 0x1C, "m2_io_t.mode");
_Static_assert(__builtin_offsetof(m2_io_t, hs)     == 0x24, "m2_io_t.hs");
_Static_assert(__builtin_offsetof(m2_io_t, sig0)   == 0x34, "m2_io_t.sig0");
_Static_assert(__builtin_offsetof(m2_io_t, enable) == 0x40, "m2_io_t.enable");

#endif /* M2_IO_H */
