/*
 * m2_memory.h — STF work-RAM control globals, mapped as one struct at 0x00500000.
 *
 * The bottom of work RAM (RAM @ 0x500000) holds a handful of fixed-address STF globals.
 * This overlays them as M2_MEM.<field>; the _gN / _pN fields are the unmapped gaps (the
 * rest of STF's work RAM, not modelled here).
 *
 * SHARED WITH THE BOOT/IRQ ASSEMBLY: M2_MEM.vsync and M2_MEM.timer_flag are the SAME
 * memory the i_handle.s ISRs touch via the linker symbols _RAMBASE_START / _timerFlag
 * (the vblank ISR increments vsync; the timer ISR sets timer_flag). Those linker symbols
 * stay — this struct is just C's named view of the same RAM. The buffer-control block
 * (0x501000) is C-only (the GEO display-list rotation).
 *
 * Include via m2.h (needs u8/u32). The reset/boot stack + IRQ tables live higher in RAM
 * (kx_init.s); the launcher exit hook is M2_EXIT_CTL (m2.h).
 */
#ifndef M2_MEMORY_H
#define M2_MEMORY_H

typedef struct {
    u32 vsync;                 /* 0x0000  RAMBASE_START — vblank-ISR counter; interrupt_wait spins on it */
    u8  _g0[0x88];             /* 0x0004..0x008B                                                         */
    u32 timer_flag;            /* 0x008C  STF byte_50008C — timer-ISR flag                               */
    u8  _g1a[0x1C0];           /* 0x0090..0x024F                                                         */
    u32 texram_ptr[4];         /* 0x0250  STF wait_data texram bank pointers — the boot writes
                                *         {sheet window, TEXRAM_0_1, TEXRAM_1, M2_LUMARAM}               */
    u8  _g1b_a[0x4A0];         /* 0x0260..0x06FF                                                         */
    /* STF read_sw/write_sw/io_request_chk work-RAM (rom_code1.s labels; m2_swio.h). Active-HIGH
     * held bitmask packs system(IN0)@byte0, P1(IN1)@byte1, P2(IN2)@byte2, coin/service@byte3. */
    u32 in_held;               /* 0x0700  INTERUPT_FLAGS_HELD — held this frame (active-high)            */
    u32 in_momentary;          /* 0x0704  INTERUPT_FLAGS_MOMENTARY — newly pressed this frame            */
    u32 in_momen_on_rel;       /* 0x0708  INTERUPT_FLAGS_MOMEN_ON_REL — released this frame              */
    u32 in_held_prev;          /* 0x070C  INTERUPT_FLAGS_HELD_PREV_FRAME                                 */
    u32 io_prev0;              /* 0x0710  read_sw 0x500710 — masked ~IN[0x0C], this frame                */
    u32 io_prev1;              /* 0x0714  read_sw 0x500714 — previous frame's io_prev0                   */
    u8  coin_flags; u8 _pc[3]; /* 0x0718  COIN_INTERUPT_FLAGS (0 = INSERT COINS; write_sw output)        */
    u8  analog_ram[8];         /* 0x071C  analog_to_digital_ram — 8 ADC channels (read_analog_to_ram)    */
    u8  _g1b_b[0x7DC];         /* 0x0724..0x0EFF                                                         */
    u8  wait_data;             /* 0x0F00  STF wait_data mode byte (the boot writes 2)                    */
    u8  _g1c[0xFF];            /* 0x0F01..0x0FFF                                                         */
    u8  poly_bank;  u8 _p[3];  /* 0x1000  change_poly_bank cache (low byte of the COP poly bank)         */
    u32 buff_add;              /* 0x1004  current display-list buffer address                           */
    u32 buff_max;              /* 0x1008  BUFF_MAX                                                       */
    u8  buff_index; u8 _q[3];  /* 0x100C  0..3 rotating display-list buffer index                        */
} m2_mem_t;

#define M2_MEM (*(volatile m2_mem_t *)0x00500000u)

/* sanity: catch any struct padding that would slide a global off its STF address. */
_Static_assert(__builtin_offsetof(m2_mem_t, vsync)      == 0x0000, "m2_mem_t.vsync");
_Static_assert(__builtin_offsetof(m2_mem_t, timer_flag) == 0x008C, "m2_mem_t.timer_flag");
_Static_assert(__builtin_offsetof(m2_mem_t, texram_ptr) == 0x0250, "m2_mem_t.texram_ptr");
_Static_assert(__builtin_offsetof(m2_mem_t, in_held)    == 0x0700, "m2_mem_t.in_held");
_Static_assert(__builtin_offsetof(m2_mem_t, coin_flags) == 0x0718, "m2_mem_t.coin_flags");
_Static_assert(__builtin_offsetof(m2_mem_t, analog_ram) == 0x071C, "m2_mem_t.analog_ram");
_Static_assert(__builtin_offsetof(m2_mem_t, wait_data)  == 0x0F00, "m2_mem_t.wait_data");
_Static_assert(__builtin_offsetof(m2_mem_t, poly_bank)  == 0x1000, "m2_mem_t.poly_bank");
_Static_assert(__builtin_offsetof(m2_mem_t, buff_add)   == 0x1004, "m2_mem_t.buff_add");
_Static_assert(__builtin_offsetof(m2_mem_t, buff_max)   == 0x1008, "m2_mem_t.buff_max");
_Static_assert(__builtin_offsetof(m2_mem_t, buff_index) == 0x100C, "m2_mem_t.buff_index");

#endif /* M2_MEMORY_H */
