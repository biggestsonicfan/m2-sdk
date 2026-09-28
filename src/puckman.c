/*
 * puckman.c — Namco's original Puck Man set on the Pac-Man port (src/pacman.c).
 *
 * ROMs: `python3 tools/pacrom.py puckman.zip` writes src/puckman_roms.h (gitignored,
 * Namco data). Without it this builds the homebrew board test, like pacman.c.
 *
 * Build:  cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-i960-elf.cmake -DM2_GAME=puckman
 */
#define PAC_ROMS "puckman_roms.h"
#define PAC_NAME "PUCK MAN"
#include "pacman.c"
