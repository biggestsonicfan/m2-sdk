# Model 2A, 2B and 2C

The SDK was written for Model 2B (the `sfight` set). It also builds for the 2A and 2C
boards:

```sh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-i960-elf.cmake -DM2_BOARD=2A -DM2_GAME=fontdemo
```

| `-DM2_BOARD` | Board | Coprocessor | ROM pair written | Run on |
|---|---|---|---|---|
| `2A` | Model 2A-CRX | TGP (Fujitsu MB86234) | `epr-20930.12` + `epr-20931.13` | `dynamcop` |
| `2B` (default) | Model 2B-CRX | SHARC ADSP-21062 COP + SHARC GEO | `epr-19001.15` + `epr-19002.16` | `sfight` |
| `2C` | Model 2C-CRX | TGPx4 (Fujitsu MB86235) | `epr-20952.15` + `epr-20953.16` | `dynamcopc` |

## Where the boards differ

Dynamite Cop shipped on all three boards, one program image per board. Its upper program
ROM (epr-20932/20933) is byte-identical on all three. Its lower program differs only in
the places below; offsets are in the joined lower image (`ROM_LOAD32_WORD`). MAME
`src/mame/sega/model2.cpp` (`model2_tgp_mem`, `model2b_crx_mem`, `model2c_crx_mem`) maps
the same addresses.

| What | 2A (dynamcop) | 2B (dynamcopb) | 2C (dynamcopc) |
|---|---|---|---|
| Render mode (0x10000000) | 4 (@6ee10) | 0x4004 | 0x4004 |
| Texture RAM | 0x12000000 / 0x12400000, one 16-bit halfword per 32-bit bus word | 0x11000000 / 0x11200000 | as 2B |
| Luma RAM | 0x12800000, one byte per u32 | 0x11400000, one byte per u16 | as 2B |
| Coprocessor program | TGP, u32 words (@b6084) | two SHARCs, IOP DMA (@be090) | TGPx4, u32 pairs (@ba260) |

`src/m2_board.h` holds these. `m2_tex_wr32` / `m2_tex_rd32` (`m2font.h`) and
`m2_luma_wr` (`m2_color.h`) hide the 2A bus layout. Only STF's data ROM has the luma
table, so on 2A and 2C the SDK builds STF's polygon luma curve instead
(`M2_LUMA_STF_CURVE`).

## The 2A coprocessor: the SDK's TGP program

On 2B the COP runs Sonic the Fighters' own SHARC program (`cpres1`). 2A has a TGP, so the
SDK brings its own program, `src/m2_tgp_kernel.s`. It reads the same FIFO protocol as
cpres1: the i960 writes `COP_CMD(n)` and the arguments to 0x884000 and reads the results
from the same address. `m2_cop_boot()` uploads it. The upload sets bit 31 of the COP
control register (0x980000), which halts the TGP and resets the upload counter. Each FIFO
write then stores one program word. Clearing bit 31 starts the program (model2.cpp
`copro_ctl1_w` / `copro_fifo_w`).

Supported commands. Argument order and results are the same as cpres1:

| | |
|---|---|
| Scalar | `FADD` `FSUB` `FMUL` `FDIV` `INT2F` `F2INT` `SQRT` |
| Trig (16-bit angle) | `SIN` `COS` `TAN` `SINSCALE` `COSSCALE` `ATAN2` |
| Vector | `DIST2D` `DIST3D` `MAG2D` `DOT2D` `ADD3` `SUB3` `SCALE3` |
| Matrix | `IDENTITY` `SET_POS` `SCALE` `ANG_X` `ANG_Y` `ANG_Z` `RMATRIX` `PUSH` `POP` |
| Display list | `SUBMIT` |

Any other command is ignored. Its arguments are then read as commands, so don't send
one on 2A. `m2_fastmath.h` (the default) does the scalar math on the i960 and works on
every board.

### The matrix commands

The kernel keeps one matrix, laid out as cpres1's: three columns, then the translation.
`RMATRIX` returns col0, col1, col2, T (12 floats). The commands are the transform ones
`m2_obj_submit()` sends:

- `IDENTITY`: rotation = I, T = 0.
- `SET_POS x y z`: T += x*col0 + y*col1 + z*col2.
- `SCALE sx sy sz`: col0 *= sx, col1 *= sy, col2 *= sz.
- `ANG_X a`: col1' = c*col1 - s*col2, col2' = s*col1 + c*col2.
- `ANG_Y a`: col0' = c*col0 + s*col2, col2' = c*col2 - s*col0.
- `ANG_Z a`: col0' = c*col0 - s*col1, col1' = s*col0 + c*col1.

Here s and c are the sine and cosine of the 16-bit angle `a`. The formulas are m2-hle2's
cpres1 model (`src/board/sharc_exec.h`).

`PUSH` and `POP` work as cpres1's (stf-sharc `cpres1.asm`, `Fn_push_matrix` /
`Fn_pop_matrix`). There are 8 slots, so 7 pushes are saved. cpres1 updates the depth in
its jump's delay slots, so the depth goes on counting past a full stack. A `PUSH` at depth
7 or more changes only the count, and so does a `POP` at depth 8 or more. After 8 pushes
the first `POP` leaves the matrix as it is. A `POP` at depth 0 does nothing.

Still missing on 2A: `WMATRIX` and the bone commands.

### SUBMIT

`SUBMIT wr flag tpa tha oba obc p2 p` (0x78, cpres1 `Fn_put_poly`) is the commit in
`m2_obj_submit()`. It writes 18 words (0x48 bytes) to buffer RAM at byte offset `wr`:

| Words | Value |
|---|---|
| 0 | `0x05800b0b`, the GEO matrix command |
| 1-12 | the current matrix (col0, col1, col2, T) |
| 13 | `0x00800101`, the GEO object command |
| 14-17 | `tpa`, `tha`, `oba`, `obc & 0xffff` |

It returns `p2` and `p + n`, where n = `obc & 0xffff`. With a nonzero `flag`, it also adds
`obc >> 14` to `tha` and n to `p2`. The SDK always sends `flag` = 0.

The TGP reaches buffer RAM through its bank register: writing 0x400000 to `rf3` maps io
0x0000-0x7fff to buffer RAM (model2.cpp `copro_tgp_memory_w`). That hides the sin/cos and
divide tables, so `SUBMIT` writes 0 to `rf3` when it is done.

### The 3D path on 2A

`m2_silicon_boot()`, `m2_frame_begin()`, `m2_obj_frame_setup()`, the submit and
`m2_frame_commit()` run unchanged on 2A: the COP write cursor (0x802008) and the GEO
registers are at the same addresses, and every COP command they send is in the kernel.
The one 2B-only piece is the model table. `m2_obj_submit()` reads its header from
`M2_MODEL_TABLE` (model 456 for `m2_solid_quad()`), which is in STF's data ROM. On
`dynamcop` that address holds Dynamite Cop's data. Use `m2_obj_submit_hdr()` instead: it
takes the 4-word header `{tpa, tha, oba, obc}` from wherever you keep it, for example for
a mesh you put in polygon RAM with `GEO_OP_POLYDATA`. `m2_load_poly_palette()` also copies
from STF's data ROM, so set your colours with `m2_3d_solid()` or `m2_setcolor()` on 2A.

`src/objtest.c` checks this path. It boots with `m2_silicon_boot()`, then submits two
objects that use a two-quad mesh in polygon RAM. After each submit it reads the 19 words
in buffer RAM (`SUBMIT`'s 18 and the END mark after them) and the returned counts, and
compares them with the header and with the matrix `RMATRIX` returns after the same
commands. `dynamcop` and `sfight` both show ALL SUBMITS OK. Comparing the two boards'
`g_obj_words` / `g_obj_counts` with `tools/objtest_cmp.py`, the integer words and counts
are identical. The matrix words agree to within 1.24e-6 of their column's largest element.
That bound is per column because an element near 0 can come out as -0 on the TGP and
1.3e-6 on cpres1. Against exact sines, the TGP is the closer of the two.

`src/meshcube.c` draws that path: a spinning cube of six one-quad meshes in polygon RAM,
each submitted with `m2_obj_submit_hdr()` and its own flat colorbase. In MAME it looks the
same on `dynamcop` and `sfight`. In a one-quad mesh (mode NP_NS) the corners go lead 0,
lead 1, then P0 next to lead 0 and P1 next to lead 1: corners `-u-v, +u-v, -u+v, +u+v`. The
order around the edge (`-u-v, +u-v, +u+v, -u+v`) draws a bow tie.

The flat-colour patch at (0, 896) is filled with `m2_texram_fill()` (`m2font.h`), which
writes whole words and reads nothing back. On 2A in MAME a texture RAM read doesn't return
what `tex0_w` stored, so `m2_texram_texel_at()`'s read-modify-write speckles a block built
one texel at a time.

`m2_silicon_boot()` itself sends one COP command, `m2_cop_initialize()` (command 0). It
skips it on 2A. On 2B cpres1 needs that command before `RMATRIX` returns the matrix;
without it the read returns zeros.

### ATAN2

`ATAN2 x1 x2 z1 z2` (`m2_cop_atan2`, sent by `m2_obj_frame_setup()`) returns the angle of
atan2(z2 - z1, x2 - x1) as `floor(rad * 32768/pi) & 0xffff`. The kernel runs cpres1's
algorithm with cpres1's constants (m2-hle2 `src/board/sharc.h`, `sharc_fw_atan2`): t is
min/max of |x| and |y|, reduced by 30° above tan 15°, then a rational polynomial, then the
octant and quadrant. (0, 0) gives 0. cpres1's pi/2 and pi scale to just under 0x4000 and
0x8000, so the +z axis is 0x3fff and the -x axis 0x7fff, on both boards.

The TGP has an atan table (io 0x24-0x27, model2.cpp `copro_atan_r`), but it is only good to
about one unit, so the kernel doesn't use it. Each divide gets one correction from its
residual, so the float angle stays very close to cpres1's, but not bit-exact: the word can
differ by one when the angle falls right at a unit boundary.

Divide and square root use the TGP's seed tables (io 0x28/0x29 and 0x2a/0x2b) with one
correction step, as Dynamite Cop's program does. They match the i960's result to within
1e-5 relative, but not bit for bit.

**`F2INT` rounds down.** cpres1 in MAME turns -3.7 into -4 and 5.9 into 5. The TGP
program does the same: it switches to round-toward-minus for its `cfxd`.
`m2_fastmath.h`'s `(int)f` truncates, so negative non-integers differ by one between
the two.

### Assembler

`tools/tgpasm.py` assembles and disassembles MB86233/MB86234 code. The syntax is MAME's
(`mb86233d.cpp`).

```sh
python3 tools/tgpasm.py src/m2_tgp_kernel.s --header src/m2_tgp_kernel.h --name m2_tgp_kernel
python3 tools/tgpasm.py -d prog.bin              # disassemble
python3 tools/tgpasm.py --selftest listing.txt   # check against a MAME dasm listing
```

The self-test on Dynamite Cop's 2A program (4096 words) reassembles every word exactly.
`src/m2_tgp_kernel.h` is generated from the `.s`: edit the `.s`, then regenerate the
header.

## The 2C coprocessor

There is no TGPx4 program yet. On 2C `m2_cop_boot()` does nothing. Use `m2_fastmath.h`
(the default) for math. The 2D paths (`m2_gfx2d.h`, the font) work.

## Test: `src/coptest.c`

`-DM2_GAME=coptest` sends each supported command through the COP and compares the result
with the i960's own math. Each command is drawn in green when it passes, or as `BAD` plus
the result bits in red. Every line also goes to the UART on the first pass. In MAME
(shared build, 300 frames):

- `dynamcop` (2A, SDK TGP program): ALL COMMANDS OK
- `sfight` (2B, STF cpres1; needs `src/cpres1.h`): ALL COMMANDS OK
- `dynamcopc` (2C): "Model 2C: no COP program yet"

The twelve `m.` lines (`m.c0x` .. `m.tz`) send IDENTITY, SET_POS, ANG_Y, ANG_X, ANG_Z,
SCALE and SET_POS, then read the matrix back. On `dynamcop` the result is bit-exact to
the i960 model of the formulas above. On `sfight` cpres1 differs from it by a few ULPs,
and from 2A by at most 1.7e-6 relative. Most of that comes from the sine tables: cpres1's
sin 45° is 0x3F3504F7, the TGP's 0x3F3504F3.

Next come eight `ATAN2` lines: all four quadrants, both axes and (0, 0). Their expected
words come from the cpres1 model, and `dynamcop` and `sfight` return the same eight words.

The last nine lines test the matrix stack and `SUBMIT`. All of them must match exactly:

- `pop`, `pop2`: the matrix after one and two `POP`s equals the one read before each `PUSH`.
- `pop0`: a `POP` on an empty stack changes nothing.
- `push8`, `pop8`: after 8 pushes, one `POP` leaves the matrix alone, and 8 bring back the first.
- `sub`, `subf`: the 18 words in buffer RAM, with `flag` 0 and 1.
- `subr`, `subfr`: the two counts `SUBMIT` returns.

The test writes at the GEO write cursor and leaves it there, so the frame's display list
writes over those words afterwards.
