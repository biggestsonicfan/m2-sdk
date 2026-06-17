# Model 2B RS-422 serial link (315-5649) — protocol reference

The Model 2B's native **RS-422 cabinet-link serial**, inside the **315-5649 I/O gate
array** (the same chip as the digital inputs), is usable as a general byte transport
to/from a host PC — e.g. the m2-x11 X server uses it as its X11 transport. This is the
reusable, hardware-confirmed reference for that link. (The transport implementation
lives in m2-x11 `src/xtransport.h`; this doc is the protocol itself.)

## Register map (i960 I/O region 0x01C00000)

| Reg  | i960 addr  | MAME dev off | Dir | Purpose                              |
|------|------------|--------------|-----|--------------------------------------|
| TXD1 | 0x01C00012 | 0x09 | W | command / strobe (writing it triggers a transfer) |
| TXD2 | 0x01C00014 | 0x0a | W | outbound data byte (latch before the strobe)      |
| RXD1 | 0x01C00016 | 0x0b | R | inbound status (bit0 = RXD2 holds a valid byte)   |
| RXD2 | 0x01C00018 | 0x0c | R | inbound data byte                                 |
| FLAG | 0x01C0001A | 0x0d | R | TX/RX buffer + framing status                     |
| MODE | 0x01C0001C | 0x0e | W | LOOP / satellite / satellite#                     |

MAME (`sega_315_5649_device`) packs each 16-bit reg into one device offset via
`umask32(0x00ff00ff)`. I/O map: `model2.cpp` ~1426.

- **FLAG bits:** D0=TX1BF, D1=TX2BF (1=TX buffer busy), D2=RX1BF, D3=RX2BF (1=RX data
  ready), D4=RX1FE, D5=RX2FE (framing error), D6=RX1IE, D7=RX2IE.
- **MODE bits:** D4=LOOP (1=hardware loopback, RX-only echo), D5=satellite, D0–D3=sat#,
  master = 0. There is **no signal-invert bit** — polarity is a wiring concern only.

## Firmware-level protocol (faithful to STF `serial_stuff` @0x7c190)

A **half-duplex, two-channel, lockstep** exchange — NOT a free-running UART at the
software level. One transaction:

1. latch `TXD2 = data`
2. write `TXD1 = cmd` (the strobe; triggers the transfer)
3. spin until `FLAG & 0x0C == 0x0C` (both RX buffers full = peer answered)
4. read `RXD1` (status, bit0 = valid) and `RXD2` (inbound data)

Channel framing for a full-duplex byte stream: channel 0 (TXD1/RXD1) = control,
channel 1 (TXD2/RXD2) = data. Command `0x07` = "this transfer carries an outbound
data byte"; `0x82` = idle/poll (no outbound byte). Link bring-up (`io_flag_setup`
@0x1380) runs a fixed command/handshake sequence (0x01/0xFF, 0x08/0x7D, 0x81, 0x88,
echo dance, 0x82) before traffic.

## Wire format (host side) — CONFIRMED on a logic analyzer + live FTDI read

The TTL line out of the 315-5649 (before the **SN75179B** TTL↔RS-422 driver) is a
plain async UART:

- **2.0 Mbaud, 8-N-1, LSB-first, idle-HIGH** (normal polarity).
  - A sigrok FX2 LA @12 MHz best-fit ~2.12 M (≈6% high); a real **FTDI FT232R** locks
    it cleanly at exactly **2,000,000** (= 3 MHz base / 1.5). Host rates that snap to
    the FT232R's 2.18 M divisor (2.12 M, 2.18 M) just garble — **request 2000000.**
  - Host adapter must reach 2.0 Mbaud RS-422: FT232R / FT232H / CP2102N do; cheap
    CH340s usually can't.
- **A `0x07` strobe precedes every data byte on the wire.** Each transaction writes
  both TXD2 (data) and TXD1 (the `0x07` command), so both serialize out. For the data
  stream (cmd `0x07`) the wire reads:
  ```
  07 54 07 45 07 53 07 54 07 0d 07 0a   ==   "TEST\r\n"
  ```
  `0x07` is exactly 50% of wire bytes. **A host reader must drop the `0x07` strobes**
  to recover the payload.

Because the firmware's `cprint()` mirrors every on-screen line to the link, resetting
the board streams its entire boot log over the wire, then test traffic per frame.

## Reading it from a PC

```
# live monitor: strips 0x07, prints text; reset the board to watch it boot
python tools/rs422_live.py COM5 2000000        # (m2-x11/tools)

# recover the baud from a saved logic-analyzer capture (PulseView .sr)
python tools/m2_period.py
```

`.sr` parse recipe: it's a zip — `metadata` has `samplerate`; `logic-1-N` are raw
samples, 1 byte/sample with bit *i* = channel D*i*. Find falling edges on the
idle-high channel, sample 8 data bits LSB-first at `f + spb*(k+1.5)`, `spb = sr/baud`.
On our rig the board's TX was the LA's **D4** (idle high); D0 was crosstalk, D2 a
~500 kHz clock — pick the idle-high channel with clean per-frame bursts.

## Status / next layer

TX is byte-accurate and confirmed live. For a reliable bidirectional link still to do:
(a) validate/wire the **RX** direction, (b) restore the per-byte **lockstep ack** for
flow control (the streaming test path skips it for throughput), (c) a host endpoint
speaking 2.0 Mbaud RS-422 and stripping `0x07`.
