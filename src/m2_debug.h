/*
 * m2_debug.h — compile-time SERIAL debug for the m2 SDK.
 *
 * OFF by default: with M2_DEBUG undefined every M2_DBG / M2_DBGH expands to nothing,
 * so a normal (release) build emits zero code and pays zero cost. Define M2_DEBUG (the
 * build scripts add -DM2_DEBUG when M2_DEBUG=1) to stream progress/trace text over the
 * board's serial link — invaluable for "how far did it get before it froze?".
 *
 *   M2_DBG("[stage] reached warmup\r\n");   // literal string
 *   M2_DBGH("cop_wpos", val);               // label + 0xHHHHHHHH hex + CRLF
 *
 * The SDK does not own a serial port, so the PROJECT supplies the sink by defining
 *     void m2_dbg_puts(const char *s);
 * e.g. a geoserial app routes it to the kernel's RS-422 writer:
 *     #ifdef M2_DEBUG
 *     void m2_dbg_puts(const char *s) { gs_puts(s); }
 *     #endif
 * Include AFTER m2.h (needs u32). A no-op M2_DEBUG-off build needs no sink.
 */
#ifndef M2_DEBUG_H
#define M2_DEBUG_H

#ifdef M2_DEBUG

/* project-provided serial sink (raw bytes out the board->host link). */
void m2_dbg_puts(const char *s);

/* label + 32-bit hex + CRLF, via the sink. static (per-TU) + unused-safe. */
__attribute__((unused))
static void m2_dbg_hex(const char *label, u32 v) {
    static const char hx[16] = "0123456789ABCDEF";
    char buf[16];   /* '=0x' + 8 hex + CRLF + NUL = 14 */
    int i = 0, j;
    m2_dbg_puts(label);
    buf[i++] = '='; buf[i++] = '0'; buf[i++] = 'x';
    for (j = 28; j >= 0; j -= 4) buf[i++] = hx[(v >> j) & 0xFu];
    buf[i++] = '\r'; buf[i++] = '\n'; buf[i] = 0;
    m2_dbg_puts(buf);
}

#define M2_DBG(s)      m2_dbg_puts(s)
#define M2_DBGH(l, v)  m2_dbg_hex((l), (u32)(v))

#else  /* !M2_DEBUG — zero code */

#define M2_DBG(s)      ((void)0)
#define M2_DBGH(l, v)  ((void)0)

#endif /* M2_DEBUG */

#endif /* M2_DEBUG_H */
