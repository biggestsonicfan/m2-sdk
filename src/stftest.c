/*
 * stftest.c — m2-sdk check: every cpres1 command answers the same on every board.
 *
 * On 2A and 2C the COP kernels run the commands they have no native code for in their VM
 * (M2_COP_EXT, src/cop_ext/). This sends the cases in stftest_cases.h (tools/cop_ext_gen.py:
 * a matrix, then each command with random arguments, three times over) through the COP FIFO,
 * reads each one's results and then the matrix (RMATRIX), and keeps them all in g_stf_out[]:
 * per case the number of words, then the words: its results, then 12 matrix words (a command
 * that answers with more words than the model shows them all). g_stf_done counts the cases
 * done; it ends at STF_NCASES. Nothing is drawn or printed: read g_stf_out with a debugger or a MAME Lua script
 * (the addresses are in game.map) and compare the boards, or the host model (src/cop_ext/build.sh
 * --check), with tools/stftest_cmp.py.
 *
 *   cmake ... -DM2_GAME=stftest -DM2_BOARD=2A     run on dynamcop
 *   cmake ... -DM2_GAME=stftest -DM2_BOARD=2C     run on dynamcopc
 *   cmake ... -DM2_GAME=stftest                   run on sfight (2B: cpres1 itself, the reference)
 */
#define M2_NO_FASTMATH   /* keep m2_cop_* on the COP */
#define M2_COP_EXT
#include "m2.h"
#include "m2_3d.h"
#include "stftest_cases.h"

/* per case: the word count, then every word the COP sent back (results, then the matrix) */
#define STF_MAXW 64
volatile u32 g_stf_out[STF_NOUT + 13 * STF_NCASES + STF_MAXW];
volatile u32 g_stf_done;
volatile u32 g_stf_frames;

int main(void) {
    const u32 *c = stf_cases;
    u32 k, i, idle, o = 0;
    m2_init();
    m2_cop_boot();
    m2_cop_initialize();   /* 2B: cpres1 ready, command 0 */
    /* the buffer RAM words the cases read (0x67-0x69 matrices, 0x4a's osage records) */
    for (c = stf_bufs; c[1]; c += 2 + c[1])
        for (i = 0; i < c[1]; i++) ((volatile u32 *)GEO_BUFFERRAM)[c[0] + i] = c[2 + i];
    c = stf_cases;
    for (k = 0; k < STF_NCASES; k++) {
        u32 n = c[0], na = c[1], nr = c[2];
        m2_cop(COP_CMD(n));
        for (i = 0; i < na; i++) m2_cop_pi(c[3 + i]);
        for (i = 0; i < nr; i++) g_stf_out[o + 1 + i] = M2_COPFIFO;
        m2_cop(COP_RMATRIX);
        for (; i < nr + 12; i++) g_stf_out[o + 1 + i] = M2_COPFIFO;
        /* a command that answers with more words than the model expects: take them too, until
           the output FIFO has stayed empty for a while (COP_STATUS_REG bit 0, MAME fifo_control_r) */
        for (idle = 0; idle < 20000 && i < STF_MAXW; ) {
            if (*(volatile u32 *)COP_STATUS_REG & 1u) { idle++; continue; }
            g_stf_out[o + 1 + i++] = M2_COPFIFO;
            idle = 0;
        }
        g_stf_out[o] = i;
        o += 1 + i;
        c += 3 + na;
        g_stf_done = k + 1;
    }
    for (;;) g_stf_frames++;
    return 0;
}
