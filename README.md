# m2-sdk — Sega Model 2B (i960KB) homebrew SDK

A small, header-only SDK for writing homebrew that runs on the **Sega Model 2B**
board (Intel **i960KB** main CPU), built into the program ROM pair
`roms/epr-19001.15` + `roms/epr-19002.16`. It runs in MAME on the `sfight`
(Sonic the Fighters) romset by overriding just those two program EPROMs.

## Quick start

```sh
# msys2 clang64 i960-elf toolchain + cmake/ninja must be on PATH (see "Toolchain")
cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-i960-elf.cmake
ninja -C build
# -> roms/epr-19001.15  roms/epr-19002.16   (the bundled src/demo.c)
```

Build a different program with `-DM2_GAME=<name>` (compiles `src/<name>.c`).
Drop your own `.c` in `src/` and reconfigure. Or use the equivalent batch script:
`cmd /c ".\build_clang64.bat demo"`.

### Soft-float (real hardware / m2emulator)

**m2emulator does not emulate the i960's FPU** — native i960 FP opcodes
(`mulr`/`addr`/`cvtir`/…) run on MAME's i960 core (and the real Model 2 i960KB,
which has a working FPU) but are **invalid opcodes on m2emulator**. To run there,
build soft-float so every float op becomes a libgcc call (zero i960 FP):

```sh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-i960-elf.cmake -DM2_SOFTFLOAT=ON
```

`M2_SOFTFLOAT` (and the `m2sdk_softfloat()` helper) live in
[`cmake/m2sdk.cmake`](cmake/m2sdk.cmake); a consumer includes it and calls the
helper. It applies `-msoft-float`, links the soft-float libgcc, and aliases the
`-fleading-underscore` libcalls (`___mulsf3` → `__mulsf3`). Note: g2d's *buffered*
`direct_data` rendering (`m2_gfx2d.h`) is MAME-HLE-only — a hardware build must use
the `object_data` path (`geo_obj_*` / m2_geo.h) instead. See m2-snake's Tempest, which
selects render path + soft-float together via its `-DM2_HW` profile.

### Real SHARC/GEO vs MAME HLE

The SDK now boots the **real SHARC/GEO geometrizer** (see `m2_3d.h` GEO boot,
`m2_geo.h` `geo_initialize`/`geometry_stuff`). Run MAME with **`M2_HLE_GEO_OFF`**
set in the environment to exercise the real geometry program rather than MAME's
HLE'd `geo_parse`. The proven on-silicon render path is **object_data**
(`geo_initialize` once, then per-frame `geo_begin`…`geo_end`/`geo_flush`); see
m2-snake's cube and Tempest `-DM2_HW`. There is also a newer DIRECT-FIFO path
(`m2_text.h`/`m2_draw.h`, GEO cmd `0x01000202`) for pixel-coord 2D shapes/text:
our own screen-space verts + per-quad material, giving multi-colour (a colorbase
per quad) and a clean per-quad z-layer. It is wrapped in a frame loop —
**`m2_frame_begin` / `m2_frame_commit` / `m2_frame_end`** (`m2_geo.h`) — and is
MAME-proven with `M2_HLE_GEO_OFF` across multi-colour shapes, text, outlines, an
xeyes window, and a live serial-input cursor demo (re-committed every frame); a
silicon burn is still pending. The host owns the one-time GEO/COP boot + consumer
prime (a few empty `m2_frame_begin`/`m2_frame_end` at startup); see `m2_frame_*`.
g2d's buffered `direct_data` games render nothing with HLE off.

`src/demo.c` is a ~40-line example: a bouncing ball, frame, slider and line, all
drawn with the hardware-2D API.

## Modules (`src/`)

Header-only; include from exactly ONE `.c` (they define the boot stubs):

| Header | What it gives you |
|---|---|
| `m2.h` | Core board: video bring-up, palette/tiles/text, input, sound, vblank pacing. Pulls in `m2font.h` + `m2_rand.h`. Include this first. |
| `m2_gfx2d.h` | **Hardware-2D / vector graphics** via the GEO `direct_data` path (no coprocessor). Flat-colour `rect`/`circle`/`line`/`quad`/`tri` (2D, painter's order) **and** real-Z depth-sorted 3D primitives (`g2d_vquad`/`g2d_vline`/`g2d_vtri`, `g2d_luma` shading, `g2d_zsort_fine`). |
| `m2_color.h` | 3D colour pipeline (colorxlat/lumaram) — needed by `m2_gfx2d.h`. |
| `m2_3d.h` | Coprocessor (COP/SHARC) bring-up + matrix/trig command interface. Pulls in the `cpres1/2` firmware + `m2_fastmath.h`. Only needed for COP-backed 3D math. |
| `m2_geo.h` | Descriptive GEO display-list builder (object_data for real-hardware-portable geometry; direct_data primitives). Includes `geo_initialize`/`geometry_stuff` — the real-silicon GEO boot/frame model. |
| `m2_color.h` | 3D colour pipeline (`m2_color_init` = colorxlat + LUMA2 ramp; `m2_load_poly_palette` = STF 1024 colorbase colours). Needed by `m2_gfx2d.h` and the silicon object_data path. |
| `m2_text.h` | **Silicon-capable** text: COP model-456 glyphs (`m2_draw_text`, world coords) + `m2_text_screen` (pixel coords). gFont → 128×64 texram0 atlas. |
| `m2_draw.h` | 2D filled shapes via the DIRECT-FIFO path: `m2_fill_rect`/`fill_ellipse`/`fill_circle` (+ `_o` outlines), `m2_draw_text_px`. Pixel-coord, multi-colour, z-layered; wrap in `m2_frame_begin`…`m2_frame_commit`/`m2_frame_end`. MAME-proven (`M2_HLE_GEO_OFF`); silicon burn pending. |
| `m2_scroll.h` | Tile-layer CG/pattern loader + 2×3 message font + line-scroll wave. |
| `m2_rs422.h` | Silicon-validated RS-422 (315-5649) host serial transport. |
| `m2_fastmath.h` | Native i960 scalar float (overrides the COP FIFO round-trips). |

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
lib/      testlinkrom_elf.ld   (GNU ld script: ROM@0, RAM@0x500000, cs1 checksum)
tools/    stfbin2rom.py (split the ROM image), bin2c.py
CMakeLists.txt, toolchain-i960-elf.cmake, build_clang64.bat
```

## Credits

- 8×8 bitmap font (`src/m2font.h`): Steve J's Daytona USA Test ROM project —
  https://github.com/stevej0/DaytonaTestRom
