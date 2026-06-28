cmake_minimum_required(VERSION 3.20)
project(m2_sdk C ASM)

# m2-sdk — Sega Model 2B (i960KB) homebrew SDK. Header-only board/graphics layer
# (src/m2*.h) + the i960 boot/IRQ assembly (src/*.s), built into the program ROM
# pair (roms/epr-19001.15 + roms/epr-19002.16) with the i960-elf GNU toolchain.
#
#   cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-i960-elf.cmake
#   ninja -C build
#
# Builds ONE program ROM from src/<M2_GAME>.c. Pick it with -DM2_GAME=<name>
# (default = the bundled `demo`). Drop your own <name>.c in src/ and reconfigure.
set(M2_GAME "demo" CACHE STRING "Which src/<game>.c to build")

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/${M2_GAME}.c")
    message(FATAL_ERROR "SOURCE NOT FOUND: src/${M2_GAME}.c")
endif()

# i960 startup + IRQ assembly (GNU syntax; assembled -AKB via the gcc driver).
# kx_init.o(.text) is forced first by the linker script, so list order is free.
set(M2_STARTUP_ASM
    src/kx_init.s
    src/kx_ftbl.s
    src/i_table.s
    src/i_handle.s)

# Bare-metal ELF: game + startup, NO libc/crt0 (the SDK is freestanding; -nostdlib
# mirrors a direct `ld ... -T lib/testlinkrom_elf.ld`).
add_executable(game.elf ${M2_STARTUP_ASM} src/${M2_GAME}.c)
target_include_directories(game.elf PRIVATE src)
target_compile_options(game.elf PRIVATE $<$<COMPILE_LANGUAGE:C>:-O2>)
set_target_properties(game.elf PROPERTIES SUFFIX "")
target_link_options(game.elf PRIVATE
    -nostdlib
    "-T${CMAKE_SOURCE_DIR}/lib/testlinkrom_elf.ld"
    "-Wl,-Map=game.map")

# Soft-float (no i960 hardware FP) — the DEFAULT. The real i960 has no usable FPU, so a
# program for real hardware / m2emulator (e.g. hwdemo's m2_obj.h object_data path) must
# build soft-float. m2sdk.cmake defines the option (default ON). Opt out for a MAME-only
# hard-float build:  -DM2_SOFTFLOAT=OFF  (cmake)  /  set M2_SOFTFLOAT=OFF  (build_clang64.bat).
include("${CMAKE_SOURCE_DIR}/cmake/m2sdk.cmake")
if(M2_SOFTFLOAT)
    m2sdk_softfloat(game.elf)
endif()

# NOTE on scalar float: with soft-float ON (above), the SDK's m2_cop_fadd/fmul/... map to
# plain C operators (m2_fastmath.h) that compile to libgcc soft-float — already FP-free,
# no SHARC round-trip. Do NOT define M2_NO_FASTMATH to "use the coprocessor": that routes
# scalar math through the COP FIFO, which buys nothing here (soft-float is already
# FPU-free) and, issued per-frame, shares the FIFO with the geometry submit and crashes
# m2emulator. The SHARC already does the real per-frame work (the object_data transform).

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ROM image (raw binary, IMI/PRCB at offset 0) -> split into the program ROM pair.
# Always-run target (not a POST_BUILD step): the sibling m2-snake build targets the
# SAME sfight ROM pair, so every build must (re)stamp roms/ even when game.elf itself
# is unchanged — otherwise a clobbered/deleted roms/ wouldn't be regenerated.
add_custom_target(rom ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/roms"
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:game.elf> game.bin
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/stfbin2rom.py"
            --input game.bin --output "${CMAKE_SOURCE_DIR}/roms"
    COMMAND ${CMAKE_COMMAND} -E echo
            "BUILD OK (${M2_GAME}):  roms/epr-19001.15  roms/epr-19002.16"
    DEPENDS game.elf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM)

# ninja -C build show-asm   (disassemble for real i960 mnemonics)
add_custom_target(show-asm
    COMMAND ${CMAKE_OBJDUMP} -d $<TARGET_FILE:game.elf>
    DEPENDS game.elf
    COMMENT "Disassembling ${M2_GAME} game.elf for i960-elf"
    VERBATIM)
