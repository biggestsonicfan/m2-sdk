# m2-sdk — guide for a Claude session

Header-only homebrew SDK for the **Sega Model 2B** (Intel **i960KB**, baremetal).
You write one `.c`, build it into the program ROM pair, and run it in MAME on the
`sfight` romset. This file is the fast path; `README.md` has the full reference.

**Worked example: [`src/fontdemo.c`](src/fontdemo.c)** — hardware-2D primitives +
the textured bitmap font. Read it first; it shows the whole loop.

## Build + run

```sh
# msys2 CLANG64 i960-elf toolchain must be on PATH (C:\msys64\clang64\bin)
cmd /c ".\build_clang64.bat fontdemo"     # compiles src/fontdemo.c -> roms/epr-19001.15 + .16
```
Then run by overriding sfight's two program EPROMs with those files. The MCP
bridge at `../claude_mame` does this: stage the two ROMs into
`claude_mame/mame/roms/sfight/`, then `python mcp_server/snap_mame.py` launches
MAME and writes a PNG to `claude_mame/mame/snap/sfight/NNNN.png` — Read that PNG to
see the result.

- **CLOBBERING:** the m2-snake project and the SDK build all target the *same*
  sfight ROM pair. **Re-stage your ROMs immediately before each MAME launch**, or a
  parallel build silently overwrites them and you'll be looking at the wrong game.
- The SDK build uses `-fleading-underscore`; **do not** hand-zero `.bss` (the boot
  asm does it). The sibling `m2-x11` build is newlib/ELF and differs — don't copy
  its `extern char _bss_start[]` idiom here.

## Which render path? (read this first)

The SDK boots the **real SHARC/GEO**; test it by launching MAME with
**`M2_HLE_GEO_OFF`** set. Builds default to **soft-float** (the real i960 has no
usable FPU). The rules:
- **g2d (`m2_gfx2d.h`, below) is MAME-HLE-only.** Its buffered `direct_data` lists
  render nothing under `M2_HLE_GEO_OFF` or on real hardware. Fine for HLE/quick demos.
- **3D model-table objects on silicon → `m2_obj.h`** (COP-bridge, proven by m2-snake's
  ice-cube sandbox). `m2_silicon_boot()` once; per frame `m2_frame_begin` →
  `m2_obj_frame_setup()` (projection ONCE/frame) → `m2_obj_submit(...)` per object /
  `m2_solid_quad(...)` for flat-colour quads → `m2_frame_commit`. The COP must drive
  the geometry — a raw GEO-list/BUFF_RAM write does NOT commit on silicon.
- **2D pixel-coord shapes/text on silicon → `m2_draw.h`/`m2_text.h`** (DIRECT-FIFO cmd
  `0x01000202`; proven by Pong). `m2_frame_begin` → `m2_draw_frame_setup()`
  (render-state ONCE/frame) → `m2_fill_rect`/`m2_draw_text_px`/… → `m2_frame_commit`.
- **Sprites (indexed images) → `m2_sprite.h`**: textured DIRECT quads, one per pen;
  `m2_spr_frame_setup()` once/frame → `m2_spr_draw(...)`. Overlap needs polygon attr
  bit 10 (sort by nearest z): plain `GEO_POLY_QUAD` sorts every quad into one bucket.
  Example `src/spritedemo.c`.
- **Pacing:** `m2_frame_commit()`+`m2_vsync()` = 60fps; `m2_frame_end()` = 30fps.
  Both `*_frame_setup` belong ONCE per frame — re-emitting per object/quad is the
  classic perf killer. Prime the GEO with a few empty `m2_frame_begin/end` at startup.
- **Colour constraint:** a texel only supplies LUMA of one `colorbase` hue — NO
  per-texel palette colour (MAME `model2rd.ipp`). Multi-hue ⇒ multiple colorbase quads.

## Drawing: the g2d hardware-2D API (`m2_gfx2d.h`)  — MAME-HLE only

```c
m2_init(); g2d_init();                 // g2d_init also loads the colour pipeline
g2d_background(M2_RGB(0,2,10));
g2d_color(1, M2_RGB(31,31,31));        // define colorbase 1 = white (1..15)
g2d_font_atlas();                      // ONCE, after g2d_init, before drawing text
for (;;) {
    g2d_begin();                       // start the frame's GEO display list
    g2d_rect(x,y,w,h, 1);              // flat 2D: rect/fill_circle/line/quad/tri
    g2d_ttext(x,y, "hello", 1);        // textured font, transparent, colorbase 1
    g2d_ttext_scaled(x,y,"BIG",1,4.f); // scaled
    g2d_end();                         // submit; GEO rasterizes next vblank
    m2_vsync();
}
```
2D primitives get an automatic painter's-order depth (later call = on top). There
is also a real-Z 3D vector path (`g2d_vquad`/`g2d_vline`/…) — see README.

## Textured font — what makes it work (don't re-derive these)

Hard-won facts already baked into `g2d_font_atlas`/`g2d_ttext`; see
[`memory`](../m2-x11) note `model2-texture-format` and the inline comments:
- **`gFont` is two-layer:** nibble value 1 = letter strokes, 2 = fill/shadow.
  Ink **only value 1** (inking any-nonzero fills counters into blobs).
- **`gFont` columns are scrambled** vs screen order (two 4-px halves, right-half
  first, each reversed). The atlas write undoes it: `dst col = 7 - ((gx+4)&7)`.
- **The GEO texture path applies NO geometric flip** (verified with an "L"+corner
  probe). So orientation bugs are in the *data*, not the renderer.
- Textured quads need **texlod biased to LOD 0** (`G2D_TEX_LOD0`, set for you) or
  they sample a tiny mip and look flat; UVs are `texel*8`, vertex order TL,TR,BR,BL.
- Atlas lives at texram0 `(0,0)..(128,64)`; keep other textures off that region.
- **Quality:** the GEO unit is always bilinear + the transparent path expands ink
  by ½ texel, so a 1-bit 8px font softens as you scale. For crisp tiny UI text the
  nearest flat-quad `g2d_text` is sharper; use `g2d_ttext` for tinted/transparent/
  scaled strings and fewer polygons.

## Debugging tips that paid off

- **Decode font/texture data in Python, do not guess from blurry MAME snapshots.**
  [`tools/fontdump.py`](tools/fontdump.py) prints `gFont` glyphs as ASCII and can
  apply candidate transforms (`swapx`/`flipx`/`flipy`) — that is how the column
  de-scramble was found, pixel-for-pixel.
- MAME logs each textured polygon's `texlod`/colorbase/lumabase/header to
  `geo_tiles.log` (in MAME's cwd) — decisive for confirming `texlod`, sheet, etc.
- To inspect texels actually in VRAM, read `texram0` at `0x11000000` over the
  bridge (2×2-swizzled 4bpp; see `model2-texture-format`).
