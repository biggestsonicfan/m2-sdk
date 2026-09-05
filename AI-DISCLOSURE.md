# AI disclosure

**Short version:** essentially all of the source in this repository was written by an
AI assistant (Anthropic's Claude, driven through Claude Code) working under human
direction. The AI did **not** provide the hardware knowledge that makes it correct —
that came from ROM disassembly, MAME source, logic-analyzer captures, and repeated
runs on a real Sega Model 2B board. Read [What the AI did not provide](#what-the-ai-did-not-provide)
before you trust any comment in `src/` that says "verified".

## Scope

Every commit in this repository (90 of 90 at the time of writing) carries a
`Co-Authored-By: Claude` trailer. The split by model, from `git log`:

| Model trailer | Commits |
|---|---|
| `Claude Opus 4.8` | 66 |
| `Claude Opus 4.8 (1M context)` | 17 |
| `Claude Fable 5` | 6 |
| `Claude Opus 5 (1M context)` | 1 |

There is no "hand-written before the AI" era to point at: the repo was AI-assisted from
its initial commit. `CLAUDE.md` in the repo root is the instruction file that steers
those sessions, and is itself part of the disclosure — it records the working rules and
the hard-won facts the assistant was told not to re-derive.

## What the AI wrote

Approximate tracked line counts, excluding binary-derived blobs:

| Area | Lines | AI involvement |
|---|---|---|
| SDK headers (`src/m2*.h`, `stf_cop_preamble.h`) | ~5,100 | Drafted and refactored by the AI, essentially in full |
| Example programs (`src/*.c`) | ~930 | Drafted by the AI |
| Build system (CMake, toolchain file, `.bat`, linker script) | ~420 | Drafted by the AI |
| Python tools (`tools/*.py`) | (included above) | Drafted by the AI |
| `README.md`, `docs/`, code comments | — | Drafted by the AI |

Concretely, the AI did the following:

- **Wrote the API surface.** The header-only module split (`m2.h`, `m2_geo.h`,
  `m2_math.h`, `m2_obj.h`, `m2_draw.h`, `m2_io.h`, `m2_memory.h`, …), the naming, the
  frame-orchestration model (`m2_frame_begin`/`commit`/`end`), and the struct overlays
  for the hardware registers.
- **Translated reverse-engineering findings into code.** Given a `cpres1` disassembly,
  MAME's `model2.cpp`/`model2rd.ipp`, and captured FIFO byte sequences, it turned raw
  magic numbers into named opcodes and documented helpers — e.g. `stf_cop_preamble.h`,
  which re-expresses a verbatim 46-word FIFO capture as equivalent C emitters.
- **Refactored and deduplicated** across ~90 commits, and kept `README.md`, `CLAUDE.md`
  and the inline comments in sync with what was learned.
- **Ran the emulator loop.** It built ROMs and inspected MAME screenshots and logs
  through a local bridge, which is how the MAME-side behaviour was iterated.
- **Wrote the tooling** (`bin2c.py`, `fontdump.py`, `stfbin2rom.py`), including the
  font-atlas de-scramble that was found by decoding the data in Python rather than
  guessing from screenshots.

## What the AI did not provide

This is the important half.

- **Real-hardware validation.** The AI has no access to a Model 2B. Every claim in the
  source marked *"verified on silicon"*, *"silicon-validated"*, *"proven by"*, or
  *"confirmed on a logic analyzer"* — including the RS-422 wire format in
  `src/m2_rs422.h` and `docs/rs422-serial-protocol.md` (8-N-1, LSB-first, 2.0 Mbaud,
  the 50 %-of-wire-bytes `0x07` strobe) — reflects a **human running it on the real
  board, on m2emulator, and on a logic analyzer / FTDI capture**, then reporting the
  result back. The AI wrote the sentence; it did not make the measurement.
- **The ground truth it was reasoning from.** The `cpres1`/`cpres2` disassembly, the COP
  FIFO captures, the MAME source excerpts, and the decision about which board and which
  romset to target were all supplied by the human. There are **no public datasheets** for
  the Model 2 custom chips (315-5649 and friends) — nothing here is quoting a vendor
  document, and the AI was explicitly instructed never to imply otherwise.
- **The binary data.** The coprocessor firmware and palette blobs the SDK boots
  (`cpres1.h`, `cpres2.h`, `psled_cpres1.h`, `psled_cpres2.h`, `psled_palette.h`) are
  **coprocessor firmware and palette data from original Sega game ROMs** (Sonic the
  Fighters and Power Sled). They are **not distributed with this repository** — you
  extract your own per [`docs/firmware-extraction.md`](docs/firmware-extraction.md).
  During development the AI ran the `bin2c.py` conversion over blobs the maintainer had
  already extracted; it neither authored nor obtained that data.
- **The i960 boot code.** `src/kx_init.s` and `src/kx_ftbl.s` originate from **Intel's
  1989 i960 startup sources** and carry Intel's own copyright and redistribution notice
  at the top of the file. `src/i_table.s` / `src/i_handle.s` are the interrupt tables
  built around them.
- **The font.** The 8×8 bitmap font in `src/m2font.h` (`gFont`) comes from
  [Steve J's Daytona USA Test ROM project](https://github.com/stevej0/DaytonaTestRom).
- **Judgement about what to build.** Direction, priorities, the decision to target
  soft-float by default, and every "no, that's wrong, here's what the board actually
  did" correction came from the human maintainer.

## How to treat the code

Please assume this is **experimental homebrew reverse-engineering work, not a validated
product**, and verify anything you depend on:

- **Comments can be stale or over-confident.** The AI wrote them from evidence that was
  accurate at the time; hardware behaviour was often revised a few commits later.
- **Not every path runs on real hardware.** `m2_gfx2d.h` (the g2d API) is
  **MAME-HLE-only** and renders nothing on silicon or with `M2_HLE_GEO_OFF`. Only the
  `m2_obj.h` object_data path and the `m2_draw.h`/`m2_text.h` DIRECT-FIFO path are
  hardware paths, and even there the DIRECT-FIFO 2D path did not display on m2emulator.
  The README's "Which render path?" section is the authority.
- **MAME and silicon diverge.** MAME emulates an FPU the real i960KB does not usefully
  have, and several COP opcodes behave differently under emulation than on the board —
  which is why the build defaults to soft-float. A program that looks correct in MAME
  is not thereby correct on hardware.
- **There is no test suite.** Correctness here means "someone watched it run".

Bug reports that say *"this comment claims X, the board actually does Y"* are exactly
the kind of correction this project needs.

## Third-party content

Before redistributing this repository or anything built from it, note:

| Path | Origin | Notes |
|---|---|---|
| `src/kx_init.s`, `src/kx_ftbl.s` | Intel Corporation, 1989 | Redistributable under the permission notice in the file header; keep the notice, mark modifications. |
| `src/m2font.h` | [stevej0/DaytonaTestRom](https://github.com/stevej0/DaytonaTestRom) | Credit retained in the file and the README. |
| `cpres1.h`, `cpres2.h`, `psled_cpres1.h`, `psled_cpres2.h`, `psled_palette.h` | Sega arcade ROM data | **Not tracked in this repository.** Sega retains copyright; git-ignored and user-extracted — see [`docs/firmware-extraction.md`](docs/firmware-extraction.md). |

The SDK ships **no Sega ROM data at all**: no game ROM images (`roms/` is git-ignored,
and the build produces the program EPROM pair locally) and no coprocessor firmware. You
supply your own dumps.

---

*This file was itself written by Claude (Opus 5) at the maintainer's request, from the
repository's git history and source — the same disclosure it describes applies to it.*
