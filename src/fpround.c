/*
 * fpround.c — i960KB round-to-nearest test: does an exact .5 go to the EVEN integer?
 *
 * Intel, 80960KB Programmer's Reference Manual (1988), Table 12-5: rounding mode 00
 * (AC bits 31-30) is "round to nearest (even)": "If two values are equally close, the
 * result is the even value". cvtri, cvtril, roundr and roundrl round in that mode.
 * MAME's i960 core (round_to_int, i960.cpp) uses C round(), which sends a tie AWAY from
 * zero: 2.5 -> 3, -2.5 -> -3. https://github.com/biggestsonicfan/mame/issues/1
 *
 * Expected results:
 *   real 80960KB (Model 2B):  every line PASS, "ALL PASS" (green)
 *   MAME before the fix:      the ties marked * FAIL (red), the controls still PASS
 * The controls (no tie, or a tie whose two answers agree, or modes 01/10/11) show the
 * test itself works on both: only round-to-nearest ties may differ.
 *
 * Each case shows the source bits, the rounding mode, the result the chip returned and
 * the expected (ties-to-even) value, in hex. The same report goes out the aux UART (i8251).
 *
 * The FP instructions are inline asm on general registers; the rest of the program is
 * the default soft-float build. The 80960KB has the FPU (STF itself runs cvtri on
 * exactly these ties, e.g. 10922.5 at 0x30B2C); m2emulator does NOT, so it faults there.
 *
 *   build:  cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-i960-elf.cmake \
 *                 -DM2_GAME=fpround && ninja -C build
 */
#include "m2.h"

#define AC_MASK  0xFF000000u   /* rounding (31-30), normalizing (29), FP fault masks (28-24) */
#define AC_BASE  0x3F000000u   /* all FP faults masked + normalizing: STF/kx_init's default  */

enum { RM_NEAR = 0, RM_DOWN = 1, RM_UP = 2, RM_TRUNC = 3 };
enum { OP_CVTRI, OP_CVTRIL, OP_ROUNDR, OP_ROUNDRL };

typedef struct {
    u8  op, mode, tie;     /* tie: 1 = MAME's round() gives a different answer      */
    u32 src_hi, src_lo;    /* single real: src_lo only; long real: hi:lo             */
    u32 exp_hi, exp_lo;    /* cvtri/roundr: exp_lo; cvtril/roundrl: hi:lo             */
    const char *what;
} fpcase_t;

static const fpcase_t cases[] = {
    /* round to nearest (even): the ties MAME gets wrong */
    { OP_CVTRI,   RM_NEAR, 1, 0, 0x3F000000u, 0, 0x00000000u,      "0.5 -> 0"         },
    { OP_CVTRI,   RM_NEAR, 1, 0, 0x40200000u, 0, 0x00000002u,      "2.5 -> 2"         },
    { OP_CVTRI,   RM_NEAR, 1, 0, 0xC0200000u, 0, 0xFFFFFFFEu,      "-2.5 -> -2"       },
    { OP_CVTRI,   RM_NEAR, 1, 0, 0x462AAA00u, 0, 0x00002AAAu,      "10922.5 -> 10922" },  /* STF 0x30B2C */
    { OP_CVTRI,   RM_NEAR, 1, 0, 0x47867440u, 0, 0x00010CE8u,      "68840.5 -> 68840" },  /* STF 0x89C70 */
    { OP_CVTRIL,  RM_NEAR, 1, 0, 0x40200000u, 0, 0x00000002u,      "2.5 -> 2 (64)"    },
    { OP_CVTRIL,  RM_NEAR, 1, 0, 0xBF000000u, 0, 0x00000000u,      "-0.5 -> 0 (64)"   },
    { OP_ROUNDR,  RM_NEAR, 1, 0, 0x40200000u, 0, 0x40000000u,      "2.5 -> 2.0"       },
    { OP_ROUNDR,  RM_NEAR, 1, 0, 0xC0900000u, 0, 0xC0800000u,      "-4.5 -> -4.0"     },
    { OP_ROUNDRL, RM_NEAR, 1, 0x40040000u, 0, 0x40000000u, 0,      "2.5 -> 2.0 (L)"   },
    { OP_ROUNDRL, RM_NEAR, 1, 0x42000000u, 0x00040000u,
                              0x42000000u, 0x00000000u,            "2^33+.5 -> 2^33"  },
    /* controls: same answer either way */
    { OP_CVTRI,   RM_NEAR, 0, 0, 0x3FC00000u, 0, 0x00000002u,      "1.5 -> 2"         },
    { OP_CVTRI,   RM_NEAR, 0, 0, 0x46FFFF00u, 0, 0x00008000u,      "32767.5 -> 32768" },
    { OP_CVTRI,   RM_NEAR, 0, 0, 0x40333333u, 0, 0x00000003u,      "2.8 -> 3"         },
    { OP_CVTRI,   RM_DOWN, 0, 0, 0xC0200000u, 0, 0xFFFFFFFDu,      "-2.5 dn -> -3"    },
    { OP_CVTRI,   RM_UP,   0, 0, 0x40200000u, 0, 0x00000003u,      "2.5 up -> 3"      },
    { OP_CVTRI,   RM_TRUNC,0, 0, 0xC0200000u, 0, 0xFFFFFFFEu,      "-2.5 tr -> -2"    },
};
#define NCASES ((int)(sizeof(cases) / sizeof(cases[0])))

/* Run one case with AC rounding mode `mode` (old AC restored afterwards). The FP op works
 * on g4-g7 so the long-real/long-int pairs are aligned; results come back as hi:lo. */
static void run_case(const fpcase_t *c, u32 *hi, u32 *lo) {
    u32 ac = AC_BASE | ((u32)c->mode << 30), mask = AC_MASK, rhi, rlo;
#define FP_RUN(INSN)                                                              \
    __asm__ volatile (                                                            \
        "mov    %2, g4\n\t"                                                       \
        "mov    %3, g5\n\t"                                                       \
        "modac  %4, %5, g8\n\t"       /* g8 = old AC; AC = mode + masks */        \
        INSN "\n\t"                                                               \
        "modac  %4, g8, g8\n\t"       /* restore the old AC */                    \
        "mov    g6, %0\n\t"                                                       \
        "mov    g7, %1"                                                           \
        : "=&r"(rlo), "=&r"(rhi)                                                  \
        : "r"(c->src_lo),                                                         \
          "r"(c->src_hi), "r"(mask), "r"(ac)                                      \
        : "g4", "g5", "g6", "g7", "g8", "cc")
    switch (c->op) {
    case OP_CVTRI:   FP_RUN("cvtri   g4, g6"); rhi = 0; break;
    case OP_CVTRIL:  FP_RUN("cvtril  g4, g6"); break;
    case OP_ROUNDR:  FP_RUN("roundr  g4, g6"); rhi = 0; break;
    default:         FP_RUN("roundrl g4, g6"); break;
    }
#undef FP_RUN
    *hi = rhi; *lo = rlo;
}

/* hex of hi:lo (16 digits) or lo (8 digits, padded to 16) into p[0..15] */
static void hexval(char *p, u32 hi, u32 lo, int wide) {
    int i;
    for (i = 0; i < 16; i++) {
        u32 v = (i < 8) ? hi : lo;
        p[i] = (!wide && i < 8) ? ' ' : "0123456789ABCDEF"[(v >> (28 - 4 * (i & 7))) & 0xF];
    }
}

static const char *const opname[] = { "CVTRI  ", "CVTRIL ", "ROUNDR ", "ROUNDRL" };
static const char modename[] = "NDUT";   /* nearest, down, up, truncate */

enum { PB_WHITE = 1, PB_GREEN = 2, PB_RED = 3, PB_GREY = 4 };

static char hexline[NCASES][53];
static u8   passed[NCASES];

int main(void) {
    int i, k, fails = 0;
    char n[3];

    m2_init();
    m2_textpal(PB_WHITE, M2_RGB(31, 31, 31));
    m2_textpal(PB_GREEN, M2_RGB(4, 31, 4));
    m2_textpal(PB_RED,   M2_RGB(31, 4, 4));
    m2_textpal(PB_GREY,  M2_RGB(18, 18, 18));

    m2_print(2, 1, "I960 ROUND TO NEAREST (EVEN) TEST", PB_WHITE);
    m2_print(2, 2, "* = TIE MAME ROUNDS AWAY FROM ZERO   MODE N D U T", PB_GREY);
    m2_print(4, 3, "SOURCE           RESULT           EXPECTED", PB_GREY);

    for (i = 0; i < NCASES; i++) {
        const fpcase_t *c = &cases[i];
        int wide_src = (c->op == OP_ROUNDRL);
        int wide_dst = (c->op == OP_CVTRIL || c->op == OP_ROUNDRL);
        int row = 5 + 2 * i;
        char *hex = hexline[i];
        u32 hi, lo, ok;

        run_case(c, &hi, &lo);
        ok = (lo == c->exp_lo) && (!wide_dst || hi == c->exp_hi);
        passed[i] = (u8)ok;
        if (!ok) fails++;

        for (k = 0; k < 52; k++) hex[k] = ' ';
        hex[52] = 0;
        hexval(hex,      c->src_hi, c->src_lo, wide_src);
        hexval(hex + 17, hi,        lo,        wide_dst);
        hexval(hex + 34, c->exp_hi, c->exp_lo, wide_dst);

        m2_print(2,  row, ok ? "PASS" : "FAIL", ok ? PB_GREEN : PB_RED);
        m2_print(7,  row, c->tie ? "*" : " ", PB_WHITE);
        m2_print(9,  row, opname[c->op], PB_WHITE);
        { char m[2] = { modename[c->mode], 0 }; m2_print(17, row, m, PB_GREY); }
        m2_print(19, row, c->what, ok ? PB_WHITE : PB_RED);
        m2_print(4,  row + 1, hex, ok ? PB_GREY : PB_RED);
    }

    n[0] = (char)('0' + fails / 10); n[1] = (char)('0' + fails % 10); n[2] = 0;
    if (fails == 0) {
        m2_print(2, 6 + 2 * NCASES, "ALL PASS: TIES ROUND TO EVEN", PB_GREEN);
    } else {
        m2_print(2, 6 + 2 * NCASES, n, PB_RED);
        m2_print(5, 6 + 2 * NCASES, "FAILED: TIES NOT ROUNDED TO EVEN", PB_RED);
    }

    /* the same report on the aux UART, after the screen (each byte waits on TxRDY) */
    m2_uart_puts("\r\nfpround: i960 round to nearest (even) test\r\n");
    for (i = 0; i < NCASES; i++) {
        m2_uart_puts(passed[i] ? "PASS " : "FAIL ");
        m2_uart_puts(opname[cases[i].op]);
        m2_uart_puts(" ");
        m2_uart_puts(hexline[i]);
        m2_uart_puts("  ");
        m2_uart_puts(cases[i].what);
        m2_uart_puts("\r\n");
    }
    m2_uart_puts(fails ? "fpround: FAILED " : "fpround: ALL PASS");
    if (fails) m2_uart_puts(n);
    m2_uart_puts("\r\n");

    for (;;) m2_vsync();
}
