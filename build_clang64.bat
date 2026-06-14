@echo off
rem Build an m2-sdk program with the msys2 clang64 i960-elf GNU toolchain (GCC 11,
rem ELF). Usage:  build_clang64.bat [game]   (default: demo -> src\demo.c)
rem ALWAYS invoke via PowerShell:  cmd /c "...\build_clang64.bat demo".
rem The CMake+Ninja build (see README.md) is the primary path; this is the direct
rem equivalent and produces byte-identical ROMs.
setlocal
cd /d %~dp0

set CLANG64=C:\msys64\clang64\bin
if not exist "%CLANG64%\i960-elf-gcc.exe" (echo i960-elf toolchain not found in %CLANG64% & exit /b 1)
set PATH=%CLANG64%;%PATH%

set GAME=%1
if "%GAME%"=="" set GAME=demo

if not exist temp mkdir temp
if not exist roms mkdir roms
del /q temp\*.* 2>nul

if not exist src\%GAME%.c (echo SOURCE NOT FOUND: src\%GAME%.c & exit /b 1)

set CFLAGS=-mkb -O2 -ffreestanding -fleading-underscore -Isrc %M2_DEFS%

echo [1/4] compile + assemble  (%GAME%)
i960-elf-gcc %CFLAGS% -c src\%GAME%.c -o temp\game.o    || goto :err
i960-elf-as -AKB src\kx_init.s  -o temp\kx_init.o       || goto :err
i960-elf-as -AKB src\kx_ftbl.s  -o temp\kx_ftbl.o       || goto :err
i960-elf-as -AKB src\i_table.s  -o temp\i_table.o       || goto :err
i960-elf-as -AKB src\i_handle.s -o temp\i_handle.o      || goto :err

echo [2/4] link
i960-elf-ld -o temp\game.elf -T lib\testlinkrom_elf.ld -Map game.map ^
   temp\kx_init.o temp\kx_ftbl.o temp\i_table.o temp\i_handle.o temp\game.o || goto :err

echo [3/4] ROM image (binary, IMI/PRCB at offset 0)
i960-elf-objcopy -O binary temp\game.elf roms\game.bin  || goto :err

echo [4/4] split into program ROM pair
python tools\stfbin2rom.py --input roms\game.bin --output roms || goto :err

echo.
echo BUILD OK (%GAME%):  roms\epr-19001.15  roms\epr-19002.16
goto :eof

:err
echo.
echo BUILD FAILED (errorlevel %errorlevel%)
exit /b 1
