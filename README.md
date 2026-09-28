# m2-sdk — Sega Model 2B (i960KB) homebrew SDK

A small, header-only SDK for writing homebrew that runs on the **Sega Model 2B**
board (Intel **i960KB** main CPU), built into the program ROM pair
`roms/epr-19001.15` + `roms/epr-19002.16`. It runs in MAME on the `sfight`
(Sonic the Fighters) romset by overriding just those two program EPROMs.

> **AI disclosure:** essentially all source in this repo was written by Claude (Anthropic)
> under human direction; the hardware facts behind it came from ROM disassembly, MAME
> source and real-board measurement, not from the AI. Read
> [`AI-DISCLOSURE.md`](AI-DISCLOSURE.md) before trusting a "verified on silicon" comment.

## Quick start

**First, one prerequisite:** the SDK ships **no Sega ROM data**. Before a coprocessor
build will compile you must extract `src/cpres1.h` + `src/cpres2.h` from your own ROM
dump — see [`docs/firmware-extraction.md`](docs/firmware-extraction.md). Those headers
are git-ignored; the build stops with a pointer to that doc if they are missing.

```sh
# msys2 clang64 i960-elf toolchain + cmake/ninja must be on PATH (see "Toolchain")
cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-i960-elf.cmake
ninja -C build
# -> roms/epr-19001.15  roms/epr-19002.16   (the bundled src/demo.c)
```

Build a different program with `-DM2_GAME=<name>` (compiles `src/<name>.c`).
Drop your own `.c` in `src/` and reconfigure. Or use the equivalent batch script:
`cmd /c ".\build_clang64.bat demo"`.

### Soft-float (DEFAULT — the real i960 has no usable FPU)

**The real Model 2 i960 has no usable FPU.** MAME *emulates* one, so native i960 FP
opcodes (`mulr`/`addr`/`cvtir`/…) "work" in MAME but are **invalid opcodes on real
hardware AND m2emulator**. So the build **defaults to soft-float**: every float op
becomes a libgcc call (zero i960 FP), and the program runs on silicon and m2emulator
out of the box. You don't need to ask for it:

```sh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-i960-elf.cmake   # soft-float by default
```

Opt **out** only for a MAME-only hard-float build: `-DM2_SOFTFLOAT=OFF` (cmake) or
`set M2_SOFTFLOAT=OFF` (build_clang64.bat).

The `M2_SOFTFLOAT` option + the `m2sdk_softfloat()` helper live in
[`cmake/m2sdk.cmake`](cmake/m2sdk.cmake) (included by the SDK's `CMakeLists.txt`, and
by consumer projects). It applies `-msoft-float`, links the soft-float libgcc, and
aliases the `-fleading-underscore` libcalls (`___mulsf3` → `__mulsf3`). Soft-float is
necessary but not sufficient for silicon: a hardware build must also use a render path
that drives the **real GEO** — `m2_obj.h` object_data (see `src/hwdemo.c`) — not the
MAME-HLE g2d `direct_data` path. (`src/demo.c`/`fontdemo.c` use g2d, so they stay
MAME-only regardless of float mode.)

### Real SHARC/GEO vs MAME HLE

The SDK boots the **real SHARC/GEO geometrizer**. Run MAME with **`M2_HLE_GEO_OFF`**
set in the environment to exercise the real geometry program rather than MAME's
HLE'd `geo_parse`. g2d's buffered `direct_data` (`m2_gfx2d.h`) renders **nothing**
with HLE off. Two paths DO render on the real GEO:

- **3D model-table objects → `m2_obj.h`** (the COP-bridge path; m2-snake's ice-cube
  sandbox). One-call boot **`m2_silicon_boot()`**, then per frame: `m2_frame_begin()`
  → **`m2_obj_frame_setup()`** (projection basis, ONCE/frame) → `m2_obj_submit(...)`
  per object → `m2_solid_quad(...)` for flat-colour quads → `m2_frame_commit()`. The
  COP must drive the geometry (a raw BUFF_RAM write doesn't commit on silicon).
- **2D pixel-coord shapes/text → `m2_draw.h`/`m2_text.h`** (DIRECT-FIFO cmd
  `0x01000202`; m2-snake's Pong). `m2_frame_begin()` → **`m2_draw_frame_setup()`**
  (render-state prelude, ONCE/frame) → `m2_fill_rect`/`m2_draw_text_px`/… →
  `m2_frame_commit()`. Multi-colour (a colorbase per quad), per-quad z-layer.

Pacing: `m2_frame_commit()` + `m2_vsync()` = 60fps; `m2_frame_end()` = 2-vblank/30fps
(STF parity). Prime the GEO once at startup with a few empty `m2_frame_begin/end`.

**Hard colour constraint:** a textured polygon's texel only supplies the **luma**
(brightness) of its single `colorbase` hue — there is **no per-texel palette-colour**
mode on this GEO (verified in MAME `model2rd.ipp`). Multi-hue scenes therefore need
multiple colorbases (one per quad): a gradient = N colour bands, a checker =
interleaved colorbase cells. (`m2_solid_quad` is one colorbase per call.)

`src/demo.c` is a ~40-line example: a bouncing ball, frame, slider and line, all
drawn with the hardware-2D API (g2d, **MAME-only**).

`src/hwdemo.c` is the **silicon / m2emulator** sibling — the same kind of scene
(border, colour bands, outlined panel, bouncing block), built on the **object_data
COP-bridge path** (`m2_obj.h`: `m2_silicon_boot` + `m2_solid_quad`) and a soft-float
build, so it runs on **m2emulator** and real hardware (and MAME under
`M2_HLE_GEO_OFF`): `set M2_SOFTFLOAT=ON` then `build_clang64.bat hwdemo` (or
`-DM2_GAME=hwdemo -DM2_SOFTFLOAT=ON`). NOTE: the DIRECT-FIFO 2D path (`m2_draw.h`)
renders under MAME's HLE-off but did **not** display on m2emulator — `m2_obj.h`
object_data is the portable silicon path (as m2-snake's `-DM2_HW` profile uses).

### Performance: soft-float vs the COP

Per-frame transform math (rotating vertices/columns) is the usual hot spot: under the
default soft-float build each `mul`/`add` is a ~100-cycle libgcc call, so a few hundred
rotations can blow the 16 ms frame budget. Routing those through the COP's own ops —
`m2_cop_rot2d` (rotate a 2-vector), `cop_sincos`, `m2_cop_wmatrix`/`m2_cop_m2w` — is
much faster: measured **~6×** on isolated rotations and **~2×** on the full shaded sphere
(`src/spheres.c` shows both side by side, spun by their on-device-measured throughput).
The scalar helpers (`m2_cop_fadd/fmul/dot2d/sqrt`) are folded to native by `m2_fastmath.h`
unless you define `M2_NO_FASTMATH`; the vector/trig ops above always use the FIFO.

Two caveats: (1) drive the COP only on the **display-list render path** (`geo_matrix`/
`geo_object`) — per-frame COP math that shares the FIFO with the op-0x78 geometry submit
crashed m2emulator. (2) The four board timers at `0x00F00000` are STF one-shot countdowns
that read ~static unless you program them, so use the vblank counter (`frameVBL`) — which
the IRQ always ticks at 60 Hz — as the reliable clock for on-device timing.

## Modules (`src/`)

Header-only; include from exactly ONE `.c` (they define the boot stubs):

| Header | What it gives you |
|---|---|
| `m2.h` | Core board: video bring-up, palette/tiles/text, input, sound, vblank pacing. Pulls in `m2font.h` + `m2_rand.h` + `m2_io.h` + `m2_memory.h`. Include this first. |
| `m2_memory.h` | STF work-RAM control globals as one struct (`M2_MEM`): the vsync counter, timer flag, and the GEO display-list buffer block. `M2_MEM.vsync`/`timer_flag` alias the boot/IRQ asm's `_RAMBASE_START`/`_timerFlag` linker symbols (same RAM). |
| `m2_gfx2d.h` | **Hardware-2D / vector graphics** via the GEO `direct_data` path (no coprocessor). Flat-colour `rect`/`circle`/`line`/`quad`/`tri` (2D, painter's order) **and** real-Z depth-sorted 3D primitives (`g2d_vquad`/`g2d_vline`/`g2d_vtri`, `g2d_luma` shading, `g2d_zsort_fine`). |
| `m2_color.h` | 3D colour pipeline (`m2_color_init` = colorxlat + LUMA2 ramp; `m2_load_poly_palette` = STF 1024 colorbase colours). Needed by `m2_gfx2d.h` and the silicon object_data path. |
| `m2_3d.h` | Coprocessor (COP/SHARC) bring-up + camera + object-submit. Pulls in the `cpres1/2` firmware + `m2_math.h`. Only needed for COP-backed 3D. |
| `m2_geo.h` | Descriptive GEO display-list builder (object_data for real-hardware-portable geometry; direct_data primitives). Includes `geo_initialize`/`geometry_stuff` — the real-silicon GEO boot/frame model. |
| `m2_text.h` | **Silicon-capable** text: COP model-456 glyphs (`m2_draw_text`, world coords) + `m2_text_screen` (pixel coords). gFont → 128×64 texram0 atlas. |
| `m2_draw.h` | 2D filled shapes via the DIRECT-FIFO path: `m2_fill_rect`/`fill_ellipse`/`fill_circle` (+ `_o` outlines), `m2_draw_text_px`. Pixel-coord, multi-colour, z-layered. Per frame: `m2_frame_begin` → **`m2_draw_frame_setup`** (render-state ONCE/frame) → draws → `m2_frame_commit`. MAME-proven (`M2_HLE_GEO_OFF`). |
| `m2_boot.h` | One-call silicon bring-up `m2_silicon_boot()` — composes the COP/GEO/colour inits (m2_3d + m2_geo + m2_color) in the proven order for the object_data path. |
| `m2_obj.h` | **Silicon 3D objects** (COP-bridge): `m2_obj_frame_setup` (projection ONCE/frame), `m2_obj_submit` (a model-table object), `m2_solid_quad` (flat-colour quad). Pulls in `m2_boot.h` (for `m2_silicon_boot`) + `stf_cop_preamble.h`. m2-snake's ice-cube sandbox. |
| `m2_tex_codec.h` | Decode an STF compressed texture page from the texture ROM into a GEO sheet (`tex_load_atlas`) — textures straight from the ROM source, no embedded blob. |
| `m2_scroll.h` | Tile-layer CG/pattern loader + 2×3 message font + line-scroll wave. |
| `m2_io.h` | The 315-5649 on-board I/O chip as one struct (`M2_IO`): input bank + player ports (raw `M2_IN0_*`/`M2_INP_*` masks + decoded `m2_player_t`/`m2_player`) and the RS-422 link registers. Pulled in by `m2.h`. |
| `m2_rs422.h` | Silicon-validated RS-422 (315-5649) host serial transport (uses `M2_IO`). |
| `m2_math.h` | Math-coprocessor (COP/cpres1) layer: command FIFO, the `COP_*` opcodes, the scalar/vector/matrix helpers (`m2_cop_fadd`/`cop_sincos`/`m2_cop_atan2`/`m2_cop_wmatrix`/…) + explicit-FIFO op emitters, and the `m2_fastmath.h` override. Semantics verified vs the cpres1 disassembly; `m2_3d.h` builds on it. |
| `m2_fastmath.h` | Native i960 scalar float (overrides the COP FIFO round-trips). |
| `m2_z80.h` | A Z80 interpreter (passes zexdoc) for emulating Z80 boards on the i960; bus hooks are macros you define. Portable C — also builds on the host. Used by `src/pacman.c`. |

The i960 reset/boot + interrupt tables are `src/kx_init.s`, `kx_ftbl.s`,
`i_table.s`, `i_handle.s` (assembled automatically by the build).

### Hardware-2D vs. 3D vector (`m2_gfx2d.h`)

- **2D** (`g2d_rect`, `g2d_fill_circle`, `g2d_line`, `g2d_quad`, `g2d_tri`): give
  screen pixels; each primitive gets an auto painter's-order depth (later = on top).
- **3D vector** (`g2d_vquad`, `g2d_vline`, `g2d_vtri`, `…l` luma variants): give
  vertices in `direct_data` space (`screen = center + X/Z`) and the real Z is kept,
  so the GEO z-sorts them. For a camera-space point use `g2d_v3(out, focal, x,y,z)`.
  **Gotcha:** when an overlay line lies ON a filled surface (wireframe-on-panel)
  they are coplanar and the default coarse z-sort ties — call `g2d_zsort_fine()`
  right after `g2d_begin()` so a small forward Z bias on the line wins. Shade flat
  faces with `g2d_luma(nx,ny,nz, lx,ly,lz)`. (The Tempest tube in the m2-snake
  project is the worked example.)

Both g2d paths render on MAME's HLE'd GEO and need **no coprocessor/firmware** —
but neither survives `M2_HLE_GEO_OFF` (the real geometrizer). For silicon, use the
object_data path (`m2_geo.h` `geo_obj_*`) or `m2_text.h`/`m2_draw.h`.

## Emulating another board: Pac-Man (`src/pacman.c`)

MAME's `pacman` driver, ported to the i960: the Z80 is interpreted by `m2_z80.h`, and
`src/pacman_hw.h` is the rest of the board (memory map, IM 2 vblank IRQ, inputs, the
tilemap + 8 sprites, both PROMs). The 224x288 portrait screen sits cell-aligned on the
System 24 tile plane (`m2_tilefb.h`), and each frame only the changed cells are copied
to char RAM. It has no sound (Namco WSG) and no cocktail flip.

```sh
python3 tools/pacrom.py path/to/pacman.zip   # -> src/pacman_roms.h (Namco data: git-ignored)
cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-i960-elf.cmake -DM2_GAME=pacman
ninja -C build
```

Without `src/pacman_roms.h` it builds a homebrew board test instead (`src/pactest_rom.h`,
made by `tools/pactest.py`, all original data). The test has text, colours, a frame
counter, the raw IN0/IN1 bytes, a Pac-Man you steer with the P1 stick, and three ghosts.
Inputs: P1/P2 sticks, COIN1/2, START1/2, SERVICE = credit.

- **Speed:** in MAME, the Z80 alone can do ~104% of real time on the 25 MHz i960 (test
  ROM with the Z80 never idle). With drawing it's ~90%, so after a frame that overruns its
  vblank, the next one skips drawing (never two in a row), which holds ~99%. The panel on
  the left shows the rate; `-DPAC_BENCH` removes the vblank cap to show the headroom.
  One Pac-Man frame runs per Model 2 vblank, so game time follows the Model 2 refresh,
  not Pac-Man's 60.6 Hz. Not tried on silicon.
- **Tests:** `tools/z80test/z80test.c` runs zexdoc (67/67 pass) against the core on the
  host, and `tools/pachost.c` runs the whole board on the host and writes a PPM.
- **Run on a stock sfight romset:** every build also writes `roms/<game>/game.bin`, and
  `tools/m2_load.lua` (an `-autoboot_script`) copies it over the program ROM region and
  resets. That's how Pinboard's web MAME launches it.
- **Why interpret?** The Model 2 has an i960, and its sound board a 68000 and the SCSP, so
  in principle a guest board built on those chips could hand its code to the matching
  part instead of interpreting it (not built yet). Pac-Man's Z80 has no match here, so
  it goes through the interpreter; the machine layer (`pacman_hw.h`) only sees the bus
  hooks, not how the CPU runs.

## Toolchain

Build with the **msys2 CLANG64** `i960-elf` GNU toolchain (GCC 11 + binutils),
i.e. `C:\msys64\clang64\bin`. That directory **must be on `PATH`** — otherwise
`cc1.exe` fails silently on a missing `libisl-23.dll`. `cmake`, `ninja`,
`i960-elf-gcc`/`-as`/`-ld`/`-objcopy` all live there. Flags that matter
(baked into `toolchain-i960-elf.cmake`): `-mkb` (i960KB), `-ffreestanding`
(no libc), `-fleading-underscore` (match the boot asm's `_main`/`_frameVBL`/…).

## Layout

```
src/      SDK headers (m2*.h) + i960 boot/IRQ asm (*.s) + your game .c
          (+ your extracted cpres*.h firmware blobs — git-ignored)
lib/      testlinkrom_elf.ld   (GNU ld script: ROM@0, RAM@0x500000, cs1 checksum)
tools/    stfbin2rom.py (split the ROM image), bin2c.py, m2_load.lua (boot game.bin in MAME),
          pacrom.py / pactest.py / pachost.c / z80test/ (Pac-Man port)
CMakeLists.txt, toolchain-i960-elf.cmake, build_clang64.bat
```

## Credits and provenance

- 8×8 bitmap font (`src/m2font.h`): Steve J's Daytona USA Test ROM project —
  https://github.com/stevej0/DaytonaTestRom
- i960 reset/startup asm (`src/kx_init.s`, `src/kx_ftbl.s`): Intel Corporation, 1989 —
  redistributed under the permission notice at the top of those files.
- **No Sega ROM data ships with this SDK.** The coprocessor firmware and palette blobs
  (`src/cpres1.h`, `src/cpres2.h`, `src/psled_cpres1.h`, `src/psled_cpres2.h`,
  `src/psled_palette.h`) are Sega-copyrighted and are **git-ignored, not distributed**;
  extract them from your own dumps per
  [`docs/firmware-extraction.md`](docs/firmware-extraction.md), with
  [stf-tools](https://github.com/biggestsonicfan/stf-tools)'s `extract-rom.mjs` as the
  extractor for the two Sonic the Fighters blobs.
  Built ROM images (`roms/`) and a packed Pac-Man romset (`src/pacman_roms.h`) are
  git-ignored too.
- **How this was built:** written with AI assistance (Claude, via Claude Code) throughout.
  See [`AI-DISCLOSURE.md`](AI-DISCLOSURE.md) for what the AI did and did not contribute,
  and how much to trust the comments.

## Status

Experimental homebrew reverse-engineering work, not a validated product. There is no test
suite; "correct" here means someone watched it run in MAME, on m2emulator, or on a real
Model 2B. Comments may be stale or over-confident — corrections of the form "this comment
claims X, the board actually does Y" are the most valuable contribution you can make.
