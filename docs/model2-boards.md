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

Every other cpres1 command (bones, motion curves, collision, afterimages, …) runs on 2A
and 2C too when you build with `M2_COP_EXT`: see [Every cpres1 command](#every-cpres1-command-m2_cop_ext)
below. Without it the kernels ignore those commands, and their arguments are then read as
commands. `m2_fastmath.h` (the default) does the scalar math on the i960 and works on
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

`WMATRIX` and the bone commands run in the VM (`M2_COP_EXT`, below).

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
commands. `dynamcop`, `dynamcopc` and `sfight` all show ALL SUBMITS OK. Comparing the two boards'
`g_obj_words` / `g_obj_counts` with `tools/objtest_cmp.py`, the integer words and counts
are identical. The matrix words agree to within 1.24e-6 of their column's largest element.
That bound is per column because an element near 0 can come out as -0 on the TGP and
1.3e-6 on cpres1. Against exact sines, the TGP is the closer of the two.

`src/meshcube.c` draws that path: a spinning cube of six one-quad meshes in polygon RAM,
each submitted with `m2_obj_submit_hdr()` and its own flat colorbase. In MAME it looks the
same on `dynamcop`, `dynamcopc` and `sfight`. In a one-quad mesh (mode NP_NS) the corners go lead 0,
lead 1, then P0 next to lead 0 and P1 next to lead 1: corners `-u-v, +u-v, -u+v, +u+v`. The
order around the edge (`-u-v, +u-v, +u+v, -u+v`) draws a bow tie.

Each face's attr word has `M2_POLY_ZMIN` (`m2_polyattr.h`, bit 10): the GEO sorts it by
its nearest vertex. Attr bits 11:10 pick the sort z (MAME `model2_v.cpp`): 0 reuses the
previous polygon's z, 1 the nearest vertex, 2 the farthest. With 0 every face of a
one-quad mesh got whatever z came before it, and the back faces drew over the front. With
the nearest vertex, the three front faces share the cube's nearest corner and win.

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

On 2C the COP runs the SDK's own TGPx4 (Fujitsu MB86235) program,
`src/m2_tgpx4_kernel.s`. It has the same commands as the 2A kernel (the cpres1 math and
matrix commands, PUSH/POP, SUBMIT, ATAN2, and the VM for the rest) and the same command
word: the kernel reads bits 30:23. It also does `0x00` (initialise: as cpres1, the matrix
goes back to the bottom stack slot and the stack empties) and `0x04` (`WMATRIX`) natively.

`m2_cop_boot()` uploads it (model2.cpp `copro_ctl1_w`, `model2c_state::copro_fifo_w`):
set bit 31 of the control register at 0x980000 (that resets the upload counter), write
each 64-bit instruction to the FIFO at 0x884000 as two u32s, low half first, then clear
bit 31 to start it. The TGPx4 is halted until then.

How it works:

- A table in A RAM 0x00-0xff holds one handler address per command. The loop reads a
  command word from the FIFO, shifts out bits 30:23 and jumps through the table
  (`DJMP A(AR1)`). Every handler ends with `DJMP loop`, the result write in its delay
  slot.
- Divide and square root use the TGPx4's `FRCP`/`FRSQ` seeds with two Newton steps, as
  Dynamite Cop's program does. Sine and cosine are a quadrant fold and a Taylor series
  (sin to x^11, cos to x^12). `F2INT` rounds down, as on 2A and 2B.
- The matrix is at A 0x100-0x10b, the stack at A 0x200. `SUBMIT` writes to buffer RAM
  over the external bus (EB = 0x400000).

The kernel writes fixed words as `A(@AR6+n)` with AR6 = 0, as Dynamite Cop does, so it runs
on any MAME. The absolute address mode (EA type 0x0c, `A(nnnH)` / `B(nnnH)`: the address is
the displacement field alone, no AR register) works too, but only on a core that has it:

- MAME: the fork's `shared` branch (branch `tgpx4-abs-ea`, `mb86235ops.cpp` `decode_ea`
  and the recompiler's `generate_ea`). Upstream MAME and older builds stop with
  "TGPx4: illegal decode_ea type 0c executed".
- m2-hle2: `src/board/tgpx4.h` `tgx_ea`, with a check in `tests/tgpx4_test.c`.

Why upstream lacks it: MAME's interpreter has only the modes some game runs. It began in
2018 (commit 3344b2f6) with eight modes and a fatal "unemulated decode_ea type" for the
rest; the 2025 rewrite (1cd73332, #13718) added the modes the 2C games needed and renamed
the error "illegal". No game needs 0x0c. The TGPx4 programs of every 2C set we have
(bel, dynamcopc, hotd, segawski, skisuprg, topskatr, waverunr: six different programs,
about 4,700 memory operands) never use it; they all address fixed words as `@ARn+disp`.
The mode exists: MAME's disassembler prints it as `A(nnnH)` and the recompiler front end
(`mb86235fe.cpp` `describe_ea`, "@disp") knows it. No program has checked it against a
board, so `src/abstest.c` checks it against the other address modes (below).

How wide the address is depends on the form, since it is the displacement field:

| Form | Field | Reaches |
|------|-------|---------|
| `MOV1` (two ALU ops) | 5 bits | A/B 0x00-0x1f |
| `MOV2` | 14 bits | A/B 0x000-0x3ff; on A, 0x400 and up is the external bus at `(EB & 0xffc000) + addr` |
| `MOV3` (immediate) | 12 bits | A/B 0x000-0x3ff, A 0x400-0xfff external |
| `MVD1`, `MVD2`, `MOVI` | none | (the assembler refuses) |

Other
things the core does that matter when you write TGPx4 code: an instruction's control
part (branch, call) runs before its ALU part, so a branch tests flags set by an earlier
instruction; `FSUB x, y` is y - x; every branch has one delay slot, and `DCALL` returns
to the call + 2.

### Assembler

`tools/tgpx4asm.py` assembles and disassembles MB86235 code in MAME's syntax
(`mb86235d.cpp`).

```sh
python3 tools/tgpx4asm.py src/m2_tgpx4_kernel.s --header src/m2_tgpx4_kernel.h --name m2_tgpx4_kernel
python3 tools/tgpx4asm.py -d prog.bin              # disassemble
python3 tools/tgpx4asm.py --selftest listing.txt   # check against a MAME dasm listing
```

The self-test on Dynamite Cop's 2C program (4096 words) disassembles and reassembles every
word exactly. An absolute address must fit its form's field (the table above), or the
assembler stops with an error. `src/m2_tgpx4_kernel.h` is generated from the `.s`.

## Every cpres1 command: `M2_COP_EXT`

The SDK is meant to reach all of the hardware, so the 2A and 2C kernels answer every
command cpres1 has, not just the ones the SDK's own 3D path sends. The ones they have no
native code for run in a small virtual machine inside each kernel:

```c
#define M2_COP_EXT          /* before the SDK headers */
#include "m2.h"
#include "m2_3d.h"
...
m2_cop_boot();             /* uploads the kernel, then the VM program */
```

On 2B the define changes nothing: cpres1 has those commands already.

**What runs where.** The kernels keep their native commands (the tables above). Every other
command from 0x00 to 0x87 runs in the VM, except the ones cpres1 itself ignores (0x33, 0x3c,
0x40, 0x4b-0x50, 0x52, 0x53, 0x5f-0x61, 0x64-0x66, 0x6c-0x6f). 0x88-0xff are ignored.
Argument order, argument count and results are cpres1's. So is the FIFO protocol: the VM
reads its arguments from the input FIFO and writes its results to the output FIFO itself.

**The program.** `src/cop_ext/` is m2-hle2's C model of cpres1 (`src/board/sharc*.h`: the
command switch, collision, the afterimage ring, atan2, the reciprocal table). Each file says
where it came from. `ext.inc` adds the cpres1 commands m2-hle2 has no case for (stf-sharc
`cpres1.asm`). `compat.h` maps the model's SHARC data memory onto plain arrays and its
FIFO, sin/cos, matrix and stack onto VM natives. One change from m2-hle2: command 0x32 runs
its cubic for any span, as cpres1 does (m2-hle2 returns v0 when span <= 0; `sfight`
disagrees). That fix is worth sending to m2-hle2.

`src/cop_ext/build.sh` compiles it with clang-18 for RV32IF (`-Oz`, no multiply/divide:
`compat.h` has `__mulsi3` and friends), links it with ld.lld-18 (`vm.ld`) and turns it into
`src/m2_cop_ext_image.h` with `tools/rv2cvm.py`. That tool translates each RISC-V
instruction into one 32-bit VM word: op 30:23 (63 ops, from `NOP` to `HALT`), A 22:18,
B 17:13, C 12:0. The kernels decode those fields cheaply, with shifts and masks. The header
is checked in, so a normal build doesn't need clang. To rebuild it: `apt install clang-18
lld-18 llvm-18`.

**Natives.** `IN`, `OUT` (the FIFOs), `SIN`, `COS` (the kernel's own sine, as its
`SIN`/`COS` commands), `IRD`, `IWR` (the kernel's matrix), `PUSH`, `POP` (the kernel's
matrix stack), `RSQRT` and `HALT` (back to the command loop). The C reads the matrix in on
entry and writes it back at the end, so native and VM commands share one matrix and one
stack.

**Memory.** The program, its data and stack live in buffer RAM words 0x4000-0x7d7f (i960
0x910000-0x91f5ff; `_start` is word 0x4001), which the COP reaches on both boards (2A: `rf3` = 0x400000 banks io
onto buffer RAM; 2C: the external bus at EB = 0x400000). With `M2_COP_EXT` on 2A/2C the GEO
display-list buffers shrink to the lower half (`m2_geo.h`: four buffers of 0x1000 words
instead of 0x2000), and `m2_gfx2d.h` puts its list at offset 0. The VM's registers are
internal RAM (2A: data RAM 0x00-0x3f; 2C: A RAM 0x300-0x33f). Commands that read or write
buffer RAM themselves (0x4a's osage records, the 0x67-0x69 matrices, and any cpres1 data
address that lands there) reach all of it except the VM's own words.

`m2_cop_ext_upload()` writes the program and zeroes its `.bss`. Image word 0 (buffer RAM
word 0x4000) is a mark, "M2VM" (0x4d32564d, `tools/rv2cvm.py`). A kernel runs a VM command
only when the mark is there. Without `M2_COP_EXT` that word belongs to a display list, so
the kernel ignores the command and doesn't jump into display-list words. The VM's own data
writes can't reach the mark.

**Speed.** Every VM instruction is a fetch, a decode and a handler on the COP. In the
`stftest` cases (seed 1, `tools/cvm.py`) a command runs a median of about 800 VM
instructions, 90% of them under 2,900. A collision command over many units runs far more
(0x70: 400,000 in one case). That is fine for a few calls a frame, not for every vertex.
`stftest` (below) sends 630 commands in 413 frames on `sfight`, 543 on `dynamcopc` and 672
on `dynamcop`.

**Checking the VM without a board.** `tools/cvm.py` is a reference interpreter for the
bytecode with the same natives. `src/cop_ext/build.sh --check DIR` also builds the C
natively (`host.c`, the "host model"), generates four seeds of random cases
(`tools/cop_ext_gen.py`) and checks that the host model and `cvm.py` agree on every word.
NaN sign bits are the only expected difference.

**The MAME fix it needed.** Comparing with `sfight` showed every matrix that cpres1 copies
with its dual-move loop (command 0x0b, PM 0x203F6) landing one word early in MAME. The
SHARC recompiler stored a dual move's new register value instead of the old one when the
instruction had no compute. The fix is on the fork's branch `sharc-drc-dual-move`
(`sharc: a dual move keeps the stored register when the PM read replaces it`), merged
into `shared`. Older MAMEs give wrong results for 0x0b on `sfight` itself.

## Test: `src/coptest.c`

`-DM2_GAME=coptest` sends each supported command through the COP and compares the result
with the i960's own math. Each command is drawn in green when it passes, or as `BAD` plus
the result bits in red. Every line also goes to the UART on the first pass. In MAME
(shared build, 300 frames):

- `dynamcop` (2A, SDK TGP program): ALL COMMANDS OK
- `sfight` (2B, STF cpres1; needs `src/cpres1.h`): ALL COMMANDS OK
- `dynamcopc` (2C, SDK TGPx4 program): ALL COMMANDS OK

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

## Test: `src/abstest.c` (2C)

`-DM2_GAME=abstest -DM2_BOARD=2C` loads its own TGPx4 program, `src/abstest_tgpx4.s`, in
place of the SDK kernel (`m2_cop_load`), and tests the absolute address mode in every form
that has it: MOV2 on A and B, MOV1, MOV3, and the external bus (buffer RAM) by MOV2 and MOV3.
Each test writes with one mode and reads back with the other (absolute, or `@AR7+n` with
AR7 = 0). AR0 is 0x40, and an absolute operand's AR field encodes AR0, so a core that added
the AR would miss the word. The last two lines check that AR0 and AR7 did not change.

The program sends 0x0AB5 once it is ready. It must: its `CLRF` empties the FIFO, so a word
the i960 sent before that is lost and the i960 then waits for an answer that never comes.

On `dynamcopc`, the shared MAME and m2-hle2 show thirteen OK lines and ALL ABSOLUTE MODES
OK. MAME without the fork branch stops at the first absolute operand. The results are also in
`g_abs_result[]`, `g_abs_bad` and `g_abs_done` for a debugger or a Lua script.

## Test: `src/stftest.c` (every command, every board)

`-DM2_GAME=stftest` sends a list of cases through the COP: a matrix, then each cpres1
command with random arguments, three times over (630 cases, 105 commands). After each case
it reads the results, then the matrix (`RMATRIX`), then anything more the COP sends, and
keeps them in `g_stf_out[]`. Nothing is drawn or printed. The cases come from
`src/stftest_cases.h`:

```sh
src/cop_ext/build.sh --check /tmp/w                     # t1..t4.txt cases, h1..h4.txt host model
python3 tools/stftest_gen.py /tmp/w/t1.txt /tmp/w/h1.txt > src/stftest_cases.h
cmake -B b2A ... -DM2_BOARD=2A -DM2_GAME=stftest && cmake --build b2A   # and 2B, 2C
```

Run each build in MAME with `-autoboot_script tools/stftest_dump.lua`. It writes
`g_stf_out` to `$STF_OUT` once the cases are done. Its environment variables are in its
header, and the addresses are in the build's `game.map`. Then compare any two runs, or a
run with the host model:

```sh
python3 tools/stftest_cmp.py /tmp/w/t1.txt out2A.txt /tmp/w/h1.txt     # -v lists every difference
```

A word matches exactly, is close (floats within 1e-4 relative or 1e-5 absolute), or differs.
Results with the shared MAME, seeds 1-3 (about 7,980 words each):

| | exact | close | differ |
|---|---|---|---|
| `dynamcop` (2A) vs host | 7875-7918 | 61-106 | 1-4 |
| `dynamcopc` (2C) vs host | 7817-7842 | 139-164 | 2-4 |
| `sfight` (cpres1) vs host | 7562-7581 | 396-412 | 6-14 |
| `dynamcopc` vs `dynamcop` | 7827-7847 | 133-154 | 1-3 |

"Close" is the boards' sine, divide and square root differing in the last bits: the
host model computes them in double, each COP with its own seeds and tables. The words that
differ are edge cases, the same on 2A and 2C, where each COP gives its own answer:

- 0x19 (1/sqrt) of a denormal. For 0x90: host 2.2e21, 2A 1.3e19, 2C inf, cpres1 4.4e19.
- 0x28 (`Fn_tri_shin`) with a denormal side: a divide by almost 0, then an angle of a value
  far outside [-1, 1].
- 0x31 (a + (b - a)·t / span) with a span of 0 or a denormal: a divide by (almost) zero.

`sfight` differs from the model in the same places, also in 0x28 when the three sides make
no triangle (the square root of a negative), and on seed 2 in nine 0x7c (afterimage) words.
