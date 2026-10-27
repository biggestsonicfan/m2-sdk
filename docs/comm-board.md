# Model 2 communication board (837-10537 / 837-11525 / 837-11615) — reference

The comm board links Model 2 cabinets over a fibre ring (TOSlink). Daytona, Sega
Rally, Virtual-On, Motor Raid, Power Sled and others use it. The board has a Z80,
16 KB of RAM shared with the i960, and an EPROM (EPR-16726, or EPR-18643(A) on
Virtual-On and Touring Car). MAME's `sfight` machine has one, so homebrew on the
sfight set can link two cabinets. m2-megamix's two-cabinet versus is a working
example (`board/src/link.c`).

**How sure this is.** The i960 side, meaning the address map and what Sega's own
games write and read, comes from two games' code: Motor Raid (YAMP
`docs/mr-comm-packet.md`) and Virtual-On (YAMP `docs/von-comm-packet.md`). The Z80
side, meaning how the ring comes up and when frames move, is **known only from MAME's
simulation** (`src/mame/sega/m2comm.cpp`, `M2COMM_SIMULATION`). Nobody has checked it
against EPR-16726 or a real board. Code that works in MAME and m2-hle2 could still
time out on hardware.

## Address map

| i960 address | what |
|---|---|
| `0x01A00000`–`0x01A03FFF` | shared RAM, mirrored at `0x01A10000` (Virtual-On, Motor Raid and Power Sled use the low mirror) |
| `0x01A04000` / `0x01A14000` | **CN** (byte): 1 starts the board, 0 stops it |
| `0x01A04002` / `0x01A14002` | **FG** (byte): bit 0 = master (EPR-16726's way); bit 7 toggles as frames come in |

The shared RAM, at the offsets the board defines:

| offset | what |
|---|---|
| `+0x00` | ring: 0 not up yet, 1 up, 0xFF failed |
| `+0x01` | the cabinet's role, written by the program once the board runs: 1 master, 2 slave (2 is the board's reset value), 0 relay |
| `+0x02` | this node's id (master 1), once the ring is up |
| `+0x03` | the number of nodes on the ring, once it is up |
| `+0x12` | frame size, u16 little-endian (MAME: 0x0E00) |
| `+0x14` | frame offset, u16 little-endian (MAME: 0x01C0, or 0x0180 for Power Sled) |
| `+0x2000` | this node's frame: what the Z80 sends |
| `+0x2000 + offset` | the frame taken from the node one hop up the ring (on a ring of two, the other cabinet's) |

**Frame offset.** 0x1C0 is the frame size divided by 8 (0xE00 / 8). Motor Raid reads
`+0x12` and shifts it right by 3. Virtual-On halves it instead, so its window is
0x700 and the peer's frame lands at `+0x2700` (m2-hle2 `src/board/m2comm.h`). Sources
disagree on who writes `+0x14`. MAME's board writes it when CN goes to 1. YAMP says
Motor Raid stores its shifted value "to +0x14", and it is not settled whether that
means the shared RAM or the game's own variable. Homebrew should read `+0x14` after
the board starts and fall back to 0x1C0 if the value is out of range, as
m2-megamix's `frame_offset()` does.

**Motor Raid's slots** (`_clearPacket`, `_checkNetwork`). Its frame is 0x1C0 bytes at
`+0x2000`. The frames received sit at `+0x21C0 + k·0x1C0`, where slot k holds the
node k+1 hops up the ring. Its own frame comes back last, in slot `count − 1`. On a
ring of two the peer is slot 0 (`+0x21C0`) and the echo is slot 1.

## Using it

The order Sega's games follow (Motor Raid's `_InitCommunication`) and m2-megamix copies:

1. **Is there a board?** Write CN = 0 and read it back. A board returns bit 0 as
   written (0). With no board the bus reads all ones, so bit 0 is set
   (`_checkBoard`: "Nothing Communication Board!").
2. **Start it.** Write FG = 1 on the master (0 on a slave), then CN = 1. That clears
   the shared RAM and sets `+0x01` = 2. Then write the role to `+0x01`. The role has
   to be known before this point: games read it from their EEPROM or backup RAM
   (Motor Raid's `_GAMEASSIGN`). m2-megamix keeps it in a block at backup RAM
   `0x01D03FF0`: "MMX1", the role, then the role's complement.
3. **Wait for the ring.** Poll `+0x00` until it reads 1, then read the node id (`+0x02`)
   and the count (`+0x03`). Under MAME the master waits 0xE8 vblanks (about 4 s)
   after both boards run before it sends the id token. If the count is below 2, the
   other cabinet isn't there: Motor Raid stops the board and starts over from step 1.
4. **Every frame**, write your packet at `+0x2000` and read the peer's at
   `+0x2000 + offset`. The Z80 makes one pass per vblank: the master sends its frame,
   and the slave answers each frame it takes with its own. A received frame stays in
   RAM until the next one replaces it.
5. **Stop** with CN = 0 (Motor Raid's `_ResetCommBoard` writes 0 to both CN and FG).
   Start again from step 2 to bring the ring back up after the other cabinet returns.

Rules a packet has to follow, learned the hard way by Motor Raid, Virtual-On and
m2-megamix:

- **Expect torn frames.** The Z80 can write a frame while the i960 reads it. Put a
  checksum in the packet and read again if it doesn't add up (m2-megamix's
  `pkt_take` tries three times).
- **Expect stale and repeated frames.** Stamp every packet with a counter that
  advances each frame. Motor Raid uses a mod-3 alive counter in byte +1 bits 2-3
  (`_ChkNetworkLink`); Virtual-On uses `seq ^ 0xAE5E`. A peer whose counter stops is
  gone, even if its last frame is still in RAM.
- **Don't let a one-frame state get lost.** A state that lives in one frame can be
  overwritten before the other board reads it. Repeat it until it is acknowledged.
  m2-megamix sends its last 32 frames of pads in every packet.
- **The master owns the settings.** The slave takes them from the master's packet,
  as Motor Raid's `_MasterParaTx` and Virtual-On's operator block do.
- **A ring of one hands a node its own frame back.** Put the sender's role in the
  packet and ignore your own.

## AM2's names

Motor Raid's ROM was built with its symbol table, and the PC module (`mr-pxd-w64`) ships it, so
these are AM2's own names (YAMP `docs/mr-comm-packet.md`; the source path
`_am1_users2_yasuda_mb_src_network` survives in the names):

| name | Motor Raid address | what |
|---|---|---|
| `_InitCommunication` | 0x4B150 | the boot check's state machine |
| `_startComCheck` | | sets the sizes: `_SizePacket` 0x1C0, `_SizeNetStruct` 0x28 |
| `_checkBoard` | | step 1 and the start in step 2 |
| `_checkNetwork` | 0x4A890 | step 3; works out the slot indices |
| `_waitCommunication` | | waits until every cabinet's status reads 1 |
| `_clearPacket` | 0x4A210 | zeroes its own frame and every receive slot |
| `_SendNetData` / `_GetNetData` | 0x4B210 / 0x4B300 | the per-frame copy out and in |
| `_ChkNetworkLink` | 0x4CE40 | the alive-counter watchdog |
| `_ResetCommBoard` | 0x4A470 | CN = FG = 0 |
| `_CheckNetworkBoard` | 0x4B180 | the service menu's board probe |

This doc gives no symbol count for the table. YAMP's two docs read the same table at
the same address (module RVA 0x17A700) and disagree: `mr-comm-packet.md` says 3987
entries, `m2-rom-symbol-tables.md` says 15248. The second is the later, deliberate
survey, which merges runs that a name filter splits and says 15248 "is not a
mis-parse". So 3987 is likely a split run, but nobody has re-read the table to
confirm it. The names above don't depend on the count.

## Emulators

- **MAME** (`m2comm.cpp`): each cabinet is a MAME process, and the ring is TCP
  between them, set with `-comm_localhost`/`-comm_localport` (listen) and
  `-comm_remotehost`/`-comm_remoteport` (connect). m2-megamix's
  `tools/mame/link-cabinets.sh` starts a linked pair. Upstream MAME died of SIGPIPE
  when the other cabinet closed, and never relinked. The owner's fork fixes both
  (branch `m2comm-drop`, merged into `shared`): the board goes back to waiting, and a
  restarted cabinet links again.
- **m2-hle2** (`src/board/m2comm.h`): the same ring for two nodes, over datagrams:
  `--comm-master` / `--comm-slave PORT:TOKEN` on one machine, or an RPCN room.
  Homebrew on the sfight set gets the board through the `sfight_homebrew` profile
  (Pinboard #1632).
