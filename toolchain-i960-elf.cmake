# CMake toolchain file: cross-compile to bare-metal i960-elf (Sega Model 2 / i960KB).
# Use with:  cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=toolchain-i960-elf.cmake ..
#
# Ninja is target-agnostic: the SAME host ninja.exe runs this build. The
# cross-ness lives entirely in the compiler/flags selected below. This file is
# the i960-elf port of build_clang64.bat's toolchain choices.

set(CMAKE_SYSTEM_NAME      Generic)   # "Generic" = bare metal, no host OS
set(CMAKE_SYSTEM_PROCESSOR i960)

# Bare-metal compiler check: build a static lib, never a full executable
# (a full link needs the linker script + startup objects, not the probe's job).
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TOOLCHAIN_PREFIX i960-elf-)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_AR           ${TOOLCHAIN_PREFIX}ar)
set(CMAKE_RANLIB       ${TOOLCHAIN_PREFIX}ranlib)
set(CMAKE_OBJCOPY      ${TOOLCHAIN_PREFIX}objcopy CACHE FILEPATH "")
set(CMAKE_OBJDUMP      ${TOOLCHAIN_PREFIX}objdump CACHE FILEPATH "")

# i960KB arch select (was gcc960 -AKB / gas960 -AKB). -fleading-underscore makes
# ELF C symbols match the underscored names the startup .s files reference
# (_main, _frameVBL, ...); the game is freestanding so nothing clashes.
set(CMAKE_C_FLAGS_INIT   "-mkb -ffreestanding -fleading-underscore")
set(CMAKE_ASM_FLAGS_INIT "-mkb")

# Find target libs/headers in the cross sysroot, host programs on the host.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
