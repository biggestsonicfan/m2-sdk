@echo off
rem build_clang64.bat [game]   (default: demo -> src\demo.c)
rem Thin wrapper around the CMake/Ninja build (CMakeLists.txt is the single source of
rem truth for flags, the asm list, linking and the ROM split). ninja ships with the
rem msys2 clang64 toolchain; cmake must also be on PATH.
rem ALWAYS invoke via PowerShell:  cmd /c "...\build_clang64.bat demo".
setlocal
cd /d %~dp0

set CLANG64=C:\msys64\clang64\bin
if not exist "%CLANG64%\i960-elf-gcc.exe" (echo i960-elf toolchain not found in %CLANG64% & exit /b 1)
set PATH=%CLANG64%;%PATH%

set GAME=%1
if "%GAME%"=="" set GAME=demo

rem Soft-float opt-in (real hardware / m2emulator):  set M2_SOFTFLOAT=ON  before building.
set SF=
if defined M2_SOFTFLOAT set SF=-DM2_SOFTFLOAT=%M2_SOFTFLOAT%

rem Absolute toolchain path: CMake resolves a RELATIVE -DCMAKE_TOOLCHAIN_FILE against
rem the build dir, so "%~dp0toolchain-i960-elf.cmake" (repo root) is required.
cmake -G Ninja -B build_cmake -DCMAKE_TOOLCHAIN_FILE="%~dp0toolchain-i960-elf.cmake" -DM2_GAME=%GAME% %SF% || goto :err
ninja -C build_cmake || goto :err
goto :eof

:err
echo.
echo BUILD FAILED (errorlevel %errorlevel%)
exit /b 1
