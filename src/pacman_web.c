/*
 * pacman_web.c — Pac-Man for the web (WASM) MAME launch: src/pacman.c without the SHARC.
 *
 * Booting the SHARC (m2_silicon_boot + an empty GEO frame per frame) keeps MAME emulating
 * its firmware's loop every frame; Pac-Man gives it no work, so leaving it out makes MAME
 * itself ~2.7x faster (native MAME, 30 s: 60% -> 160% of real time), which is what counts
 * in a browser. It also waits for vblank on `sinr` (see pac_idle in pacman.c): MAME charges
 * it 406 cycles for one host sin(), so the idle i960 costs MAME far less. That needs the
 * i960 FPU, so this build is for MAME (and real hardware), not m2emulator.
 * Use src/pacman.c to verify against the real SHARC (M2_HLE_GEO_OFF).
 *
 * Build:  cmake ... -DM2_GAME=pacman_web  ->  roms/pacman_web/game.bin
 */
#define PAC_NO_SHARC
#define PAC_IDLE_SINR                  /* wait for vblank on sinr: MAME idles ~100x cheaper */
#include "pacman.c"
