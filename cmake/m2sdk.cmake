# m2sdk.cmake — reusable CMake helpers for building against m2-sdk.
#
# Include from a game's CMakeLists after add_executable():
#     include("${M2_SDK}/cmake/m2sdk.cmake")
#     if(M2_SOFTFLOAT)
#         m2sdk_softfloat(game.elf)
#     endif()

# ---------------------------------------------------------------------------
# Soft-float toggle.
#
# m2emulator does not emulate the i960's FPU: native i960 FP opcodes
# (mulr/addr/cvtir/...) run on MAME's i960 core (and the real Model 2 i960KB, which
# has a working FPU) but are INVALID OPCODES on m2emulator. Turn this ON to compile
# every float operation into a libgcc soft-float call instead (zero i960 hardware
# FP) — required to run on m2emulator, a bit slower than native FP on MAME.
option(M2_SOFTFLOAT "Soft-float: no i960 hardware FP (needed for m2emulator, which lacks the FPU)" OFF)

# m2sdk_softfloat(<target>) — apply the soft-float profile to <target>.
#  * -msoft-float on compile AND link (selects the soft-float libgcc multilib).
#  * GCC with -fleading-underscore (the SDK's symbol convention) names the
#    soft-float libcalls ___xxx, but libgcc provides them as __xxx; alias each.
#  * link that soft-float libgcc (after the objects, so __xxx resolve).
function(m2sdk_softfloat target)
    target_compile_options(${target} PRIVATE -msoft-float)
    target_link_options(${target} PRIVATE -msoft-float)
    execute_process(COMMAND ${CMAKE_C_COMPILER} -mkb -msoft-float -print-libgcc-file-name
                    OUTPUT_VARIABLE _m2sdk_libgcc OUTPUT_STRIP_TRAILING_WHITESPACE)
    foreach(s addsf3 subsf3 mulsf3 divsf3 fixsfsi fixunssfsi floatsisf floatunsisf
              gesf2 gtsf2 lesf2 ltsf2)
        target_link_options(${target} PRIVATE "-Wl,--defsym,___${s}=__${s}")
    endforeach()
    target_link_libraries(${target} "${_m2sdk_libgcc}")
endfunction()
