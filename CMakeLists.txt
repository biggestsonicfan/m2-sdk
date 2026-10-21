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

# Target board (src/m2_board.h). Each one names the program EPROM pair after the MAME set
# the build replaces: 2B = sfight (Sonic the Fighters), 2A = dynamcop, 2C = dynamcopc
# (Dynamite Cop, which shipped on all three boards), 2O = the original Model 2: desert
# (Desert Tank; complete in this container) or daytona (-DM2_2O_SET=daytona; MAME needs
# blank stand-ins for its drive and comms board ROMs, see docs/model2-boards.md). Its first
# program pair is 2 x 128 KB, so the image must fit in 256 KB.
set(M2_BOARD "2B" CACHE STRING "Model 2 board: 2O, 2A, 2B or 2C")
set_property(CACHE M2_BOARD PROPERTY STRINGS 2O 2A 2B 2C)
set(M2_ROM_SIZE 0x100000)
set(M2_2O_SET "desert" CACHE STRING "Original Model 2 set the 2O build replaces: desert or daytona")
if(M2_BOARD STREQUAL "2O")
    if(M2_2O_SET STREQUAL "daytona")
        set(M2_ROM_NAMES epr-16722a.12 epr-16723a.13)
    elseif(M2_2O_SET STREQUAL "desert")
        set(M2_ROM_NAMES epr-16976.12 epr-16977.13)
    else()
        message(FATAL_ERROR "M2_2O_SET must be desert or daytona (got '${M2_2O_SET}')")
    endif()
    set(M2_ROM_SIZE 0x40000)
elseif(M2_BOARD STREQUAL "2A")
    set(M2_ROM_NAMES epr-20930.12 epr-20931.13)
elseif(M2_BOARD STREQUAL "2B")
    set(M2_ROM_NAMES epr-19001.15 epr-19002.16)
elseif(M2_BOARD STREQUAL "2C")
    set(M2_ROM_NAMES epr-20952.15 epr-20953.16)
else()
    message(FATAL_ERROR "M2_BOARD must be 2O, 2A, 2B or 2C (got '${M2_BOARD}')")
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
target_compile_definitions(game.elf PRIVATE M2_BOARD_${M2_BOARD})
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
            --input game.bin --output "${CMAKE_SOURCE_DIR}/roms" --names ${M2_ROM_NAMES} --size ${M2_ROM_SIZE}
    COMMAND ${CMAKE_COMMAND} -E echo
            "BUILD OK (${M2_GAME}, Model ${M2_BOARD}):  roms/${M2_ROM_NAMES}"
    DEPENDS game.elf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM)

# ninja -C build show-asm   (disassemble for real i960 mnemonics)
add_custom_target(show-asm
    COMMAND ${CMAKE_OBJDUMP} -d $<TARGET_FILE:game.elf>
    DEPENDS game.elf
    COMMENT "Disassembling ${M2_GAME} game.elf for i960-elf"
    VERBATIM)

# ---- roms/<game>.gsx: the game as a geoserial app (-DM2_GSX=ON) ----------------------------
# Uploaded over RS-422 to m2-x11's geoserial kernel (burned in place of the game) and run from
# its manager: no EPROM burn per build. The kernel booted the board, so the app is built the way
# m2-x11's build_app.bat builds one: src/<game>.c with M2_GSX defined (the game skips what the
# kernel already did), src/<game>_gsx.c for the kernel's syscalls (gs_app.h), m2-x11's GSX1
# header and entry shim, lib/app.ld at the run base 0x540000, gsx_stamp.py for the flags and
# pair-sum. No -fleading-underscore: app.ld names __gsx_start as C spells it. 2B only (the
# kernel's board). Upload and run it with tools/gsx_upload.py.
option(M2_GSX "Also build roms/<game>.gsx, a geoserial app (needs m2-x11)" OFF)
set(M2_X11 "${CMAKE_SOURCE_DIR}/../m2-x11" CACHE PATH "Path to the m2-x11 checkout (geoserial kernel)")
if(M2_GSX)
    if(NOT EXISTS "${M2_X11}/lib/app.ld")
        message(FATAL_ERROR "M2_GSX: m2-x11 not found at ${M2_X11} (-DM2_X11=<path>)")
    endif()
    if(NOT M2_BOARD STREQUAL "2B")
        message(FATAL_ERROR "M2_GSX: the geoserial kernel runs on Model 2B only")
    endif()
    set(X11B "${CMAKE_BINARY_DIR}/x11")
    set(APP_CFLAGS -mkb -msoft-float -O2 -fno-tree-loop-distribute-patterns -fno-builtin)
    set(M2_GSX_DEFS "" CACHE STRING "Extra -D flags for the game in the .gsx build (e.g. -DVB_TRACE)")
    set(APP_SDK_FLAGS ${APP_CFLAGS} ${M2_GSX_DEFS} -DM2_GSX -DM2_BOARD_2B -I${CMAKE_SOURCE_DIR}/src)
    set(APP_X11_FLAGS ${APP_CFLAGS} -I${M2_X11}/src -I${M2_X11}/src/gs -I${X11B}/temp)
    set(GSX "${CMAKE_SOURCE_DIR}/roms/${M2_GAME}.gsx")
    set(GSX_SYS "${CMAKE_SOURCE_DIR}/src/${M2_GAME}_gsx.c")
    if(NOT EXISTS "${GSX_SYS}")
        message(FATAL_ERROR "M2_GSX: src/${M2_GAME}_gsx.c (the game's syscall wrappers) not found")
    endif()
    # gen_gs_api.py writes beside itself: run a copy, so m2-x11's tree is left alone
    add_custom_command(OUTPUT ${X11B}/temp/gs_sdk.h ${X11B}/temp/gs_api_id.h ${X11B}/temp/gs_api_compat.c
        COMMAND ${CMAKE_COMMAND} -E copy "${M2_X11}/tools/gen_gs_api.py" "${X11B}/tools/gen_gs_api.py"
        COMMAND ${CMAKE_COMMAND} -E copy "${M2_X11}/src/gs/gs_api.list" "${X11B}/src/gs/gs_api.list"
        COMMAND ${Python3_EXECUTABLE} "${X11B}/tools/gen_gs_api.py"
        DEPENDS "${M2_X11}/tools/gen_gs_api.py" "${M2_X11}/src/gs/gs_api.list"
        VERBATIM)
    add_custom_command(OUTPUT ${GSX}
        COMMAND ${CMAKE_C_COMPILER} ${APP_SDK_FLAGS} -c "${CMAKE_SOURCE_DIR}/src/${M2_GAME}.c" -o gsx_game.o
        COMMAND ${CMAKE_C_COMPILER} ${APP_X11_FLAGS} -c "${GSX_SYS}" -o gsx_sys.o
        COMMAND ${CMAKE_C_COMPILER} ${APP_X11_FLAGS} -c "${M2_X11}/src/app_hdr.c" -o app_hdr.o
        COMMAND ${CMAKE_C_COMPILER} ${APP_X11_FLAGS} -c "${M2_X11}/src/app_start.c" -o app_start.o
        COMMAND ${CMAKE_C_COMPILER} ${APP_X11_FLAGS} -c "${X11B}/temp/gs_api_compat.c" -o gs_api_compat.o
        COMMAND ${CMAKE_C_COMPILER} -mkb -msoft-float -nostartfiles -nostdlib -T "${M2_X11}/lib/app.ld"
                -o gsx_app.elf app_hdr.o app_start.o gsx_game.o gsx_sys.o gs_api_compat.o
                -Wl,--start-group -lgcc -Wl,--end-group -Wl,-Map,gsx_app.map
        COMMAND ${CMAKE_OBJCOPY} -O binary gsx_app.elf "${GSX}"
        COMMAND ${Python3_EXECUTABLE} "${M2_X11}/tools/gsx_stamp.py" "${GSX}" gsx_app.elf gsx_game.o
        COMMAND ${CMAKE_COMMAND} -E echo "BUILD OK (${M2_GAME}, geoserial app):  roms/${M2_GAME}.gsx"
        DEPENDS src/${M2_GAME}.c ${GSX_SYS} ${X11B}/temp/gs_sdk.h ${X11B}/temp/gs_api_compat.c
                "${M2_X11}/lib/app.ld" "${M2_X11}/src/app_hdr.c" "${M2_X11}/src/app_start.c"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        VERBATIM)
    add_custom_target(gsx ALL DEPENDS ${GSX})
endif()
