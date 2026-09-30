# Sonic the Hedgehog (Mega Drive) on the Model 2B

MAME's Mega Drive, as far as Sonic the Hedgehog needs it, ported to the Model 2B's i960KB
(`src/sonic.c`). It runs as a replacement for Sonic the Fighters (`sfight`), like Pac-Man
(`src/pacman.c`, the m2-pacman repository documents that port): three EPROMs are swapped,
the rest of the board and romset is stock.

| File | What | Made from |
|---|---|---|
| `epr-19001.15`, `epr-19002.16` | i960 program | `src/sonic.c` + the cartridge |
| `epr-19021.31` | sound board 68000 program | `snd/scsp_passthru.s`, Pac-Man's SCSP relay, unchanged |

The cartridge is compiled into the program EPROMs; none of it is in the repository.

## Building

```sh
python3 tools/mdrom.py Sonic_The_Hedgehog.bin      # -> src/sonic_rom.h (Sega data, gitignored)
tools/sonic_recomp.sh                              # -> src/sonic_recomp.h (optional, ~1 min: the
                                                   #    recompiled hot code; without it: interpreted, ~40% speed)
cmake -G "Unix Makefiles" -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-i960-elf.cmake -DM2_GAME=sonic
make -C build                                      # -> roms/sonic/
```

Checked with Sonic the Hedgehog (W) (REV00), 512 KB, sha1 `4c3b606bec697b0b934ba2f650a8b83c0e3b77c7`.
The program ROM is 1 MB, half of it the game: `tools/m68krecomp.py --budget` (4300
instructions) is set so everything fits with ~30 KB to spare.

## Running

MAME's Model 2 driver (the same as for Pac-Man): the three files in a folder named
`sfight`, listed before the stock `sfight.zip` / `schamp.zip` / `segabill.zip`:

```sh
mame sfight -rompath "/path/with/sfight-folder;/path/to/stock/roms"
```

Pinboard: idea #139's Launch button (the `mame-m2` web build, `roms/sonic/` as its files).

Controls (Model 2 -> Mega Drive pad): stick = D-pad, button 1 = A, 2 = B, 3 = C, START 1 =
START. Level select as on the Mega Drive: Up, Down, Left, Right at the title, then hold A and
press START.

The text around the picture: `SCSP SOUND` when the sound EPROM answered (`NO SOUND` with the
stock one: the game runs silent), `SPEED` (100% = the Mega Drive's 59.92 Hz), pictures drawn
per second, and the i960's time per Mega Drive frame and per picture, in thousands of cycles
(its budget is 417 at 59.92 Hz).

## How it works

- **68000:** `src/m2_m68k.h`, an interpreter with Musashi's flags and cycle counts
  (`src/m2_m68k_cyc.h`: Musashi's per-opcode table, 5.5 KB). Checked against Musashi one
  instruction at a time (`tools/mdlock`, below). The hot code is statically recompiled
  (`tools/m68krecomp.py`, the pattern of Pac-Man's `z80recomp.py`): the 4300 most run
  instructions (97.9% of what runs) become C with constant operands, flags computed only
  where read, gotos between blocks and a hash for returns and indirect jumps; everything
  else runs on the interpreter. An idle skip ends the frame in the game's WaitForVBla loop.
- **Board:** `src/md_hw.h`: memory map, the VDP's ports, DMA, VINT/HINT, 3-button pads, as
  MAME's `megadriv.cpp` and `315_5313.cpp`. The Z80 is not run (the 68000 gets its bus at
  once); YM2612 and PSG writes are queued for the sound code.
- **Video:** `src/md_s24.h` puts the picture on the Model 2's System 24 tilemaps: plane B and
  plane A on the two scrolling layers (the hardware does the per-line scroll), each
  (pattern, flip, palette line) as its own char (the char number fixes the palette), the
  sprites composited into plane A's cells: one-line cells as they are, mixed ones with their
  colours in shared 15-colour banks. Composites are built in one half of a group pool while
  the other half is on screen, then swapped in one pass. The 320x224 picture is at (88,80) of
  the 496x384 screen.
- **Timing:** the Mega Drive runs at 59.92 Hz, the Model 2 at 57.52 Hz; each vblank runs the
  frames due (up to 4) and draws once, so busy scenes draw fewer pictures instead of slowing
  down.
- **Sound:** the serial line to the sound board carries ~50 bytes a frame, so the chips are
  re-voiced, not emulated (`src/md_snd.h`): each FM channel plays a wavetable of its
  instrument rendered on the i960 (all 8 algorithms, feedback, multipliers; modulators at
  their sustain level; cached in sound RAM), with the carrier's envelope on the SCSP's; the
  PSG plays a square wave and the SCSP's noise; the drums are decoded from the Z80 DAC
  driver the game loads into Z80 RAM and played at its rate. The UART is fed by its
  interrupt (board IRQ bit 10, `m2_scsp_irq_start`), so a busy i960 still sends at full rate.

## How it was checked

| What | How | Result |
|---|---|---|
| 68000 core | `tools/mdlock`: every instruction (and interrupt) run again on Musashi, its bus replaying the core's accesses | 199M instructions (30000 frames of the attract mode) and 3000-frame plays of 3 zones: registers, SR, both SPs, cycles and every write identical |
| recompiled code | `m68krecomp.py --exact` vs the interpreter, RAM per frame (`mdhost -m`) | identical, 7 zones x 2500 frames |
| System 24 video | `tools/mds24.c`: the tile/char/palette RAM drawn as MAME's `model2_v.cpp` + `segaic24.cpp` compose it, against a reference renderer (`src/md_render.h`) | 0 pixels differ in 596 of 600 sampled frames (20 zones); 14 pixels in the other 4 (sprite priority, below) |
| sound | ymfm (MAME's YM2612) rendering the same register writes vs MAME's recording of the Model 2 | the pitches agree (e.g. 98/100, 247/246, 488/492 Hz) |
| MAME | native and web (Pinboard) Model 2 builds | attract mode and play at 99-100% game speed, 20-50 pictures/s |

`tools/mdhost.c` runs the whole thing on a PC (pictures, RAM, profiles); `tools/m68k_cyc.c`
makes the cycle table from Musashi.

## Not done / known differences

- Sprites keep one priority bit per 8x8 cell: where a sprite meets plane B's high-priority
  pixels it can be wrong (a few pixels, rarely).
- Per-line palette changes (Labyrinth Zone's water colours) are not shown: one palette per
  frame. No window plane, shadow/highlight or sprite line limits (Sonic 1 doesn't need them).
- Sound: FM detune, LFO, SSG-EG and envelope curves are approximate; the SEGA voice at boot
  (the 68000 drives the DAC itself) is silent; the timpani uploads last (~20 s after boot).
- Busy scenes (Marble Zone's lava, the Special Stage) draw 12-25 pictures a second.
- Runs in MAME only so far: not tried on real hardware or m2emulator (the sound relay's
  caveats are Pac-Man's: m2-pacman `docs/sound.md`).
