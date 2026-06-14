/*
 * m2_rand.h — Sonic the Fighters' random-number generator, ported to the m2.h SDK.
 *
 * STF's `rand` (i960 @0x66b0) is a hardware-entropy PRNG: it folds the four
 * free-running board timer counters (TIMERS_START..TIMER_04 at 0x00F00000) into a
 * 32-bit running state and returns 16 bits of it:
 *
 *     random += (t0 << 4) + (t1 << 8) + (t2 << 12) + (t3 << 16)
 *     return (random >> 4) & 0xffff
 *
 * Faithful port — identical state update and bits[19:4] output. Included by m2.h
 * (relies on its u32 typedef); include "m2.h" first if using this header alone.
 */
#ifndef M2_RAND_H
#define M2_RAND_H

/* Free-running hardware timer counters: TIMERS_START, TIMER_02/03/04. */
#define M2_TIMERS ((volatile u32 *)0x00F00000u)

/* STF's `random` state global (was i960 RAM @0x500098). */
static u32 m2__rand_state = 0u;

/* rand (STF @0x66b0). */
static u32 m2_rand(void) {
    m2__rand_state += (M2_TIMERS[0] << 4) + (M2_TIMERS[1] << 8)
                    + (M2_TIMERS[2] << 12) + (M2_TIMERS[3] << 16);
    return (m2__rand_state >> 4) & 0xffffu;
}

/* Seed the running state (STF has no srand; provided for reproducible sequences). */
static void m2_srand(u32 seed) { m2__rand_state = seed; }

#endif /* M2_RAND_H */
