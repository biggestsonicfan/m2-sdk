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

# Standalone single-image ROM: emit m2.h's boot stubs (kickGEO/waitVBL/handleSerialIRQ,
# called by the boot asm) as GLOBAL symbols. m2.h defaults M2_API to `static` for the
# app-inside-a-kernel (GS_STANDALONE) build; a lone game .c has no kernel TU to collide
# with, so it must be global or the link fails with undefined _kickGEO/_waitVBL/... .
target_compile_definitions(game.elf PRIVATE $<$<COMPILE_LANGUAGE:C>:M2_API=>)
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

# m2emulator masks shift counts to 5 bits; GCC's i960 backend computes `x == 0` as
# `shro x,1,y` (1 >> x), which then reads every non-zero multiple of 32 as zero.
# tools/emu_shro.py rewrites those shifts in the compiler's assembly into sequences that
# give the same result on silicon, MAME and m2emulator. -DM2_EMU_SHRO=OFF to skip it.
option(M2_EMU_SHRO "Rewrite GCC's shro equality idiom for m2emulator" ON)
if(M2_EMU_SHRO)
    set_target_properties(game.elf PROPERTIES C_COMPILER_LAUNCHER
        "${Python3_EXECUTABLE};${CMAKE_SOURCE_DIR}/tools/emu_shro.py")
endif()

# ROM image (raw binary, IMI/PRCB at offset 0) -> split into the program ROM pair.
# Always-run target (not a POST_BUILD step): the sibling m2-snake build targets the
# SAME sfight ROM pair, so every build must (re)stamp roms/ even when game.elf itself
# is unchanged — otherwise a clobbered/deleted roms/ wouldn't be regenerated.
add_custom_target(rom ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/roms"
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:game.elf> game.bin
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/stfbin2rom.py"
            --input game.bin --output "${CMAKE_SOURCE_DIR}/roms"
    # the sound program EPROM too (snd/scsp_passthru.bin: the i960 drives the SCSP), so
    # roms/ holds a complete standalone set: sfight + these three files
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/snd2rom.py"
            "${CMAKE_SOURCE_DIR}/snd/scsp_passthru.bin" "${CMAKE_SOURCE_DIR}/roms"
    # and per game (not clobbered by other builds): the three EPROMs, and the flat image
    # tools/m2_load.lua boots on a stock sfight romset
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/roms/${M2_GAME}"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/roms/epr-19001.15" "${CMAKE_SOURCE_DIR}/roms/epr-19002.16"
            "${CMAKE_SOURCE_DIR}/roms/epr-19021.31" "${CMAKE_SOURCE_DIR}/roms/${M2_GAME}/"
    COMMAND ${CMAKE_COMMAND} -E copy game.bin "${CMAKE_SOURCE_DIR}/roms/${M2_GAME}/game.bin"
    COMMAND ${CMAKE_COMMAND} -E echo
            "BUILD OK (${M2_GAME}):  roms/${M2_GAME}/: epr-19001.15  epr-19002.16  epr-19021.31 (sound)  game.bin"
    DEPENDS game.elf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM)

# ninja -C build show-asm   (disassemble for real i960 mnemonics)
add_custom_target(show-asm
    COMMAND ${CMAKE_OBJDUMP} -d $<TARGET_FILE:game.elf>
    DEPENDS game.elf
    COMMENT "Disassembling ${M2_GAME} game.elf for i960-elf"
    VERBATIM)

# ninja -C build snd   (re-assemble the sound board's 68000 program, snd/scsp_passthru.bin;
# the .bin is committed, so this is only needed after editing snd/scsp_passthru.s)
find_program(M68K_AS NAMES m68k-linux-gnu-as m68k-elf-as)
find_program(M68K_LD NAMES m68k-linux-gnu-ld m68k-elf-ld)
if(M68K_AS AND M68K_LD)
    add_custom_target(snd
        COMMAND ${M68K_AS} -m68000 -o scsp_passthru.o "${CMAKE_SOURCE_DIR}/snd/scsp_passthru.s"
        COMMAND ${M68K_LD} -Ttext=0x600000 --oformat binary
                -o "${CMAKE_SOURCE_DIR}/snd/scsp_passthru.bin" scsp_passthru.o
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Assembling snd/scsp_passthru.s (68000 sound program)"
        VERBATIM)
endif()
