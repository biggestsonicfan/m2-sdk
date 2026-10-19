#!/bin/sh
# Build the COP extension program (the cpres1 commands the 2A/2C kernels run in their VM) and
# write src/m2_cop_ext_image.h. Needs clang-18 and ld.lld-18 (apt: clang-18 lld-18) and
# llvm-objcopy-18 / llvm-nm-18.
#   src/cop_ext/build.sh [workdir]          workdir defaults to a fresh mktemp -d
#   src/cop_ext/build.sh --check [workdir]  also run tools/cop_ext_gen.py tests through the
#                                           C built natively and through tools/cvm.py, and diff
set -e
CHECK=
[ "$1" = "--check" ] && { CHECK=1; shift; }
D=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$D/../.." && pwd)
W=${1:-$(mktemp -d)}
mkdir -p "$W"
CF="--target=riscv32 -march=rv32if -mabi=ilp32f -Oz -ffp-contract=off -ffreestanding -fno-builtin \
 -fno-jump-tables -fno-strict-aliasing -fno-math-errno -ffunction-sections -fdata-sections"
clang-18 $CF -c "$D/start.s" -o "$W/start.o"
clang-18 $CF -c "$D/cop_ext.c" -o "$W/cop_ext.o"
ld.lld-18 -T "$D/vm.ld" --relax --gc-sections "$W/start.o" "$W/cop_ext.o" -o "$W/vm.elf"
python3 "$R/tools/rv2cvm.py" "$W/vm.elf" --header "$R/src/m2_cop_ext_image.h" --name m2_cop_ext \
    --bin "$W/vm.cvm"
if [ -n "$CHECK" ]; then
    gcc -O1 -DVM_HOST -ffp-contract=off -fno-builtin -w -I"$D" "$D/host.c" "$D/cop_ext.c" -lm -o "$W/host"
    gcc -O1 -w -I"$D" "$D/argc.c" -lm -o "$W/argc" && "$W/argc" > "$W/argc.txt"
    for s in 1 2 3 4; do
        python3 "$R/tools/cop_ext_gen.py" "$s" "$W/argc.txt" > "$W/t$s.txt"
        "$W/host" < "$W/t$s.txt" > "$W/h$s.txt"
        python3 "$R/tools/cvm.py" "$W/vm.cvm" < "$W/t$s.txt" > "$W/v$s.txt"
        if cmp -s "$W/h$s.txt" "$W/v$s.txt"; then echo "seed $s: host = VM"; else
            echo "seed $s: $(diff "$W/h$s.txt" "$W/v$s.txt" | grep -c '^<') lines differ (NaN sign bits are expected)"; fi
    done
fi
echo "image: $R/src/m2_cop_ext_image.h (work files in $W)"
