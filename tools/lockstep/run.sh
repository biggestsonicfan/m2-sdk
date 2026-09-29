#!/bin/bash
# tools/lockstep/run.sh: record MAME's pacman driver and the i960 port (under MAME's Model 2
# driver) on the same input script, then compare them frame by frame.
#   MAME_PACMAN  a MAME with the pacman driver (make SUBTARGET=pacman SOURCES=src/mame/pacman/pacman.cpp)
#   MAME_M2      a MAME with the Model 2 driver
#   ROMS_PACMAN  a folder with pacman.zip;  ROMS_M2  one with sfight.zip, schamp.zip, segabill.zip
#   GAME         the build dir of the game (game.elf + game.bin), e.g. build for M2_GAME=pacman_web
#   EPROMS       optional: roms/<game>/ -- run those three EPROMs over the stock sfight set (what
#                a real board or Pinboard runs) instead of loading game.bin with tools/m2_load.lua
#   LS_FRAMES (3000), LS_OUT (/tmp/lockstep)
set -e
here=$(cd "$(dirname "$0")" && pwd); sdk=$(cd "$here/../.." && pwd)
export LS_TOOLS=$here LS_FRAMES=${LS_FRAMES:-3000} LS_OUT=${LS_OUT:-/tmp/lockstep}
mkdir -p "$LS_OUT"
i960-elf-nm "$GAME/game.elf" | grep -E " _pac_(frames|ram|spr_xy|in0|in1|fb)$" > "$LS_OUT/port.syms"
common="-video none -sound none -nothrottle -skip_gameinfo"
SDL_VIDEODRIVER=dummy "$MAME_PACMAN" pacman -rompath "$ROMS_PACMAN" $common -autoboot_script "$here/ref.lua" > "$LS_OUT/ref.log" 2>&1
if [ -n "$EPROMS" ]; then
  mkdir -p "$LS_OUT/roms/sfight"
  cp "$EPROMS/epr-19001.15" "$EPROMS/epr-19002.16" "$EPROMS/epr-19021.31" "$LS_OUT/roms/sfight/"
  SDL_VIDEODRIVER=dummy "$MAME_M2" sfight -rompath "$LS_OUT/roms;$ROMS_M2" $common \
    -autoboot_script "$here/port.lua" > "$LS_OUT/port.log" 2>&1
else
  M2_GAME_BIN="$GAME/game.bin" M2_SOUND_BIN="$sdk/snd/scsp_passthru.bin" SDL_VIDEODRIVER=dummy \
    "$MAME_M2" sfight -rompath "$ROMS_M2" $common -autoboot_script "$here/port.lua" > "$LS_OUT/port.log" 2>&1
fi
python3 "$here/compare.py" "$LS_OUT" "$sdk/src/pacman_roms.h"
