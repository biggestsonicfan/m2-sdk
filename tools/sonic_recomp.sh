#!/bin/sh
# sonic_recomp.sh [scratch dir]: make src/sonic_recomp.h, the statically recompiled hot
# 68000 code of src/sonic.c. Profiles Sonic on the host (tools/mdhost.c): the attract mode
# (30000 frames), a Green Hill play, and every zone through the level select
# (tools/mdzone.py), then translates the 4300 most run instructions (tools/m68krecomp.py).
# Needs src/sonic_rom.h (tools/mdrom.py). The output is derived from the game: gitignored.
set -e
cd "$(dirname "$0")/.."
W=${1:-/tmp/sonic_recomp}
mkdir -p "$W"
cc -O2 -Isrc -o "$W/mdhost" tools/mdhost.c
"$W/mdhost" -f 30000 -P "$W/prof" > /dev/null
"$W/mdhost" -f 6000 -i 620:7f -i 630:ff -i 1000:f7 -i 1100:b7 -i 1130:f7 -i 1400:bf -i 1420:f7 \
    -i 2000:f7 -i 2100:b7 -i 2200:f7 -i 3000:f7 -i 3050:bf -i 3100:f7 -P "$W/prof" -A > /dev/null
for z in $(seq 0 19); do
    "$W/mdhost" -f 3000 $(python3 tools/mdzone.py $z 2500) -P "$W/prof" -A > /dev/null
done
python3 tools/m68krecomp.py src/sonic_rom.h src/sonic_recomp.h -p "$W/prof"
echo "profile in $W (prof.op/.pc/.ent, 64 MB): delete it when done"
