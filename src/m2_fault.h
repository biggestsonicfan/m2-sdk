/*
 * m2_fault.h — the fault RECORD + RECOVERY hook for the kx_ftbl.s fault handlers.
 *
 * kx_ftbl.s (the STF-modelled user fault table) already catches every i960 fault and
 * prints the fault name + faulting IP to the tile layer — but then HALTED forever.
 * Now it also writes this record at a FIXED address and, if a project has ARMED the
 * recovery hook, pivots to a fresh stack and branches into the project's recovery
 * entry instead of halting: the monitor survives the program (fault containment).
 *
 * Unarmed projects keep the old behaviour exactly (print + halt) — arming is opt-in
 * per project via m2_fault_arm(). The record lives in the diagnostics region (POST
 * @0x5F0000, THIS @0x5F0040, frameVBL @0x5F00F0, API table @0x5F0100) and survives a
 * soft reset, so the next boot can report the previous run's fault (like POST prev).
 *
 * TARGET NOTES: real silicon dispatches faults through this table (STF ships the same
 * scheme). MAME's i960 core has NO fault dispatch (invalid ops fatalerror the emulator),
 * so in MAME the chain is exercised via the synthetic entry (kx_ftbl fault_selftest),
 * which branches into the same handler path the CPU would.
 */
#ifndef M2_FAULT_H
#define M2_FAULT_H

typedef struct {
    u32 magic;       /* 'M2FR' = 0x4D325246                                          */
    u32 count;       /* faults since cold RAM (survives soft reset; magic-gated)     */
    u32 type;        /* fault-table index (kx_ftbl per-handler constant; 0xFF = unknown) */
    u32 ip;          /* faulting IP (the fault frame's RIP)                          */
    u32 recover_fn;  /* 0 = halt after printing (legacy); else pivot + branch here   */
    u32 recover_sp;  /* fresh frame base for the recovery entry                      */
    u32 busy;        /* FAULT-STORM GUARD: set before branching to recovery, cleared
                      * when recovery reaches steady state — a fault WHILE recovering
                      * (trashed kernel state, wedged bus) halts instead of looping   */
} m2_fault_t;

#define M2_FAULT_MAGIC 0x4D325246u   /* 'M2FR' */
#define M2_FAULT_ADDR  0x005F0040u   /* FIXED — kx_ftbl.s writes these offsets in asm */
#define M2_FAULT       ((volatile m2_fault_t *)M2_FAULT_ADDR)

/* kx_ftbl.s indexes the record in asm — pin the layout */
_Static_assert(__builtin_offsetof(m2_fault_t, magic)      ==  0, "m2_fault_t.magic");
_Static_assert(__builtin_offsetof(m2_fault_t, count)      ==  4, "m2_fault_t.count");
_Static_assert(__builtin_offsetof(m2_fault_t, type)       ==  8, "m2_fault_t.type");
_Static_assert(__builtin_offsetof(m2_fault_t, ip)         == 12, "m2_fault_t.ip");
_Static_assert(__builtin_offsetof(m2_fault_t, recover_fn) == 16, "m2_fault_t.recover_fn");
_Static_assert(__builtin_offsetof(m2_fault_t, recover_sp) == 20, "m2_fault_t.recover_sp");
_Static_assert(__builtin_offsetof(m2_fault_t, busy)       == 24, "m2_fault_t.busy");
_Static_assert(sizeof(m2_fault_t) == 28, "m2_fault_t size");

/* ---- captured register frame — NINDY register_set[] layout ------------------------- *
 * On a fault, kx_ftbl.s snapshots the faulting context here in the EXACT order Intel's
 * NINDY monitor uses (regs.h): r0-r15, g0-g15, pc, ac, ip, tc. That makes a period
 * gdb960 `target nindy` connection's `r`/`R` register commands a straight copy of this
 * record. r0=pfp, r1=sp, r2=rip; g15=fp. FIXED at 0x5F0060, sized to abut frameVBL.
 * NOTE: g0-g15 + ip are captured solidly (globals are live at fault entry); r0-r15 come
 * from the faulting frame via pfp (NINDY faultasm recipe) and pc/ac/tc are a Tier-B/
 * silicon refinement (MAME can't dispatch real faults to validate the frame offsets). */
typedef struct {
    u32 r[16];   /* +0    r0-r15  (r0=pfp, r1=sp, r2=rip)  */
    u32 g[16];   /* +64   g0-g15  (g15=fp)                 */
    u32 pc;      /* +128  process controls                */
    u32 ac;      /* +132  arithmetic controls             */
    u32 ip;      /* +136  faulting instruction pointer    */
    u32 tc;      /* +140  trace controls                  */
} m2_regs_t;     /* 144 bytes = NINDY register_set[36]     */

#define M2_REGS_ADDR 0x005F0060u     /* FIXED — kx_ftbl.s writes these offsets in asm */
#define M2_REGS      ((volatile m2_regs_t *)M2_REGS_ADDR)
_Static_assert(__builtin_offsetof(m2_regs_t, g)  ==  64, "m2_regs_t.g");
_Static_assert(__builtin_offsetof(m2_regs_t, pc) == 128, "m2_regs_t.pc");
_Static_assert(__builtin_offsetof(m2_regs_t, ip) == 136, "m2_regs_t.ip");
_Static_assert(sizeof(m2_regs_t) == 144, "m2_regs_t size");
_Static_assert(M2_REGS_ADDR + 144u <= 0x005F00F0u, "m2_regs overruns the frameVBL cell");

/* Arm fault recovery: after printing + recording a fault, kx_ftbl pivots to a fresh
 * frame at `stack_base` and BRANCHES (never calls) into `fn`, which must not return
 * (re-enter the project's serve/idle loop). Preserves the fault count across a soft
 * reset for post-mortem; cold RAM (bad magic) starts a fresh record. */
static void m2_fault_arm(void (*fn)(void), u32 stack_base) {
    if (M2_FAULT->magic != M2_FAULT_MAGIC) {   /* cold RAM: fresh record */
        M2_FAULT->count = 0u;
        M2_FAULT->type  = 0u;
        M2_FAULT->ip    = 0u;
        M2_FAULT->magic = M2_FAULT_MAGIC;
    }
    M2_FAULT->busy = 0u;                       /* boot = steady state (a soft reset also
                                                * clears a stuck storm guard)           */
    M2_FAULT->recover_sp = stack_base;
    M2_FAULT->recover_fn = (u32)fn;            /* fn last: arms atomically-enough */
}

/* Synthetic fault (kx_ftbl.s): drives the record+pivot+recover chain without needing
 * CPU fault dispatch — the MAME-testable path; real faults join at the same handler. */
extern void fault_selftest(void);

#endif /* M2_FAULT_H */
