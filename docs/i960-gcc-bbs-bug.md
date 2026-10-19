# i960 GCC 11.5: a branch on a stale compare after `bbs`/`bbc`

At `-O2` the SDK's compiler can put a `cmpi`/`cmpo` before a `bbs` or `bbc` and then
branch on that compare after the `bbs`/`bbc`. On the i960, `bbs` and `bbc` overwrite the
condition code, so the later branch tests the bit instead of the compare. The cause is in
the compiler's i960 backend; the SDK's code is fine. The toolchain isn't fixed yet; a
tested backend patch is below, filed upstream as
[DrItanium/gcc-i960-11.3.0#1](https://github.com/DrItanium/gcc-i960-11.3.0/issues/1).

## Symptom

`m2_input(1)` (player 2) reports `M2_START` while only START1 is held. `src/inputtest.c`
shows this on 2O, 2A and 2B, so it does not depend on the board or on the inputs. The
code comes from `m2_io.h`:

```c
if (m2_start() & (player ? 2u : 1u)) r |= M2_START;
```

`m2_start()` is inlined. The compiler merges the two `player` cases. From a 2B build of
`inputtest` (`i960-elf-objdump -d`):

```
13c4: cmpi   g6,0        # cc = compare(player, 0)
13c8: bbs    5,g4,13d8   # START2 not held: branch. bbs also sets cc.
13cc: be     13dc        # START2 held: tests the bbs cc, not player
13d0: setbit 7,g0,g0
13d4: b      13f8
13d8: bne    13f8        # meant to test "player != 0"; cc is "equal" after a taken bbs
13dc: cmpi   g5,0        # so P2 falls into the START1 test
13e0: bne    13f8
13e4: setbit 7,g0,g0     # M2_START
```

## The toolchain

- `i960-elf-gcc --version` reports `11.5.0`, installed in `/opt/i960`.
- It was built by `/opt/scripts/build-i960-toolchain.sh` (the Windows CLANG64 build uses
  the same commits) from DrItanium's fork,
  <https://github.com/DrItanium/gcc-i960-11.3.0>, at commit `46d38531f8c1`.
- The configure line is `--target=i960-elf --with-newlib`.
- The flags are the SDK's: `-mkb -O2`.

## Minimal reproducer

```c
/* f(p, v): bit 4 of v clear and p == 0, or bit 5 clear and p != 0 */
unsigned f(int p, unsigned v) {
    unsigned s = (!(v & 0x10) ? 1u : 0u) | (!(v & 0x20) ? 2u : 0u);
    return (s & (p ? 2u : 1u)) ? 0x80u : 0u;
}
```

`i960-elf-gcc -mkb -O2 -S repro.c` gives:

```
f:  shro    4,g1,g4
    notand  1,g4,g4         # g4 = bit 4 clear
    cmpi    g0,0            # cc = compare(p, 0)
    bbs     5,g1,.L2        # cc = 010 if bit 5 set, else 000
    be      .L7             # bit 5 clear: tests the bbs cc, not p
    setbit  7,0,g0
    b       .L9
.L2: bne    .L6             # bit 5 set: cc is always 010 (equal) here
.L7: shlo   7,g4,g0
    b       .L9
.L6: mov    0,g0
.L9: ret
```

`f(1, 0x20)` should be 0 (p != 0 and bit 5 is set). The code returns 0x80: `bbs` is taken
and sets cc to "equal", so `bne .L6` falls through to the p == 0 path. That is the
`m2_input(1)` case with START1 held and START2 released.

## Why it is wrong

On the i960, `bbs` sets the condition code to 010 if the bit is set and to 000 if it is
clear. `bbc` does the same. MAME's core does this (`src/devices/cpu/i960/i960.cpp`, case
0x30 `bbc` and case 0x37 `bbs`: `m_AC = (m_AC & ~7) | 2` or `m_AC &= ~7`), as Intel's i960 manuals describe.

The backend doesn't model this. In the fork's `gcc/config/i960/i960.md`, all four
bit-branch patterns (`*branch_if_bit_set_insn0`/`1` and `*branch_if_bit_clear_insn0`/`1`,
lines 168-222) contain only `(set (pc) (if_then_else ...))`. None has a
`(clobber (reg:CC 36))`, and register 36 is the condition code (`i960.h:190`). GCC
therefore treats cc as unchanged across `bbs`/`bbc`, and is free to move a compare above
one or to reuse a compare across one. These four patterns are the only cc-setting
instructions in `i960.md` and `peepholes.md` that are missing the clobber.

## Which options matter

- Only `-O2` shows the bug. `-O1` and `-Os` don't.
- It is not scheduling. `-fno-schedule-insns`, `-fno-schedule-insns2`,
  `-fno-if-conversion`, `-fno-if-conversion2`, `-fno-thread-jumps` and
  `-fno-tree-dominator-opts` still give the bad code.
- In the reproducer, each of `-fno-crossjumping`, `-fno-expensive-optimizations`,
  `-fno-tree-pre` and `-fno-tree-vrp` gives correct code: either a fresh `cmpi` after
  every `bbs`, or no `bbs` at all. They only change the shape of the code so that the
  compare isn't shared, though. Any code that ends up with a compare shared across a
  `bbs` is still compiled wrong.

## Workarounds

1. **Fix the backend (the real fix).** Add the clobber to each of the four patterns,
   for example:

   ```
   (define_insn "*branch_if_bit_set_insn1"
     [(set (pc)
           (if_then_else
            (ne (zero_extract:SI (match_operand:SI 0 "register_operand" "d")
                                 (const_int 1)
                                 (match_operand:SI 1 "arith_operand" "dI"))
                (const_int 0))
            (label_ref (match_operand 2 "" ""))
            (pc)))
      (clobber (reg:CC 36))]
     ""
     "bbs\t%1,%0,%l2"
     [(set_attr "type" "branch")])
   ```

   Then rebuild the toolchain with `build-i960-toolchain.sh` (`GCC_REV` set to the
   patched commit). Tested 2026-10-19 with a C-only `make all-gcc` of the fork with the
   clobber added to all four patterns. The build passed, including GCC's selftests.
   - The reproducer now redoes the compare on each path:
     `bbs 5,g1,.L2; cmpi g0,0; be .L7; ...; .L2: cmpi g0,0; bne .L6`.
   - All 15 programs in `src/` (2B, plus `abstest` for 2C) compile. In each one the only
     change is the bad pair in `m2_input`, 3 lines of assembly.
   - `inputtest` in MAME (sfight) with START1 held: the stock compiler gives
     `dec 018080` (M2_START on P1 and P2); the patched one gives `dec 010080` (P1 only).
2. **A flag.** Add `-fno-crossjumping` (or one of the other three) to the SDK's
   flags. It is cheap, but it only makes the bug less likely; see above.
3. **Rewrite the C at the affected spot.** For example, pick the bit by `player` first
   and test one bit: `if (!(m2_io_in(0) & (player ? M2_IN0_START2 : M2_IN0_START1)))`.
   That fixes `m2_input` but no other code.

## Finding it in a build

Look for a `cmpi`/`cmpo` right before a `bbs`/`bbc`. This prints each pair:

```sh
i960-elf-objdump -d build/game.elf |
  awk '/\tcmp[io][^b]/ {c = NR; l = $0; next} /\tbb[sc]\t/ && NR == c + 1 {print l; print}'
```

Every program in `src/` has exactly one pair, in `m2_input`. A pair isn't always wrong: it is wrong when a
conditional branch after the `bbs`/`bbc` (on either path) still relies on the compare.
The compare can also sit a few instructions above the `bbs`/`bbc`, which this misses.

`tools/inputtest_check.lua` with `src/inputtest.c` checks the inputs. It checks P1 only
(`dec & 0x10080`), so it passes while this bug is still present. The bug shows as bit
0x8000 in `held ... dec` (`018080` instead of `010080`).
