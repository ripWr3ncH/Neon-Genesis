@echo off
REM ==========================================================
REM  Neon Genesis - build and run
REM  Double-click this file, or type  build  in a terminal.
REM ==========================================================

REM Work from this script's own folder, whatever the current directory is.
cd /d "%~dp0"

REM MSYS2's mingw64 toolchain: g++, GLFW and GLM all live here.
set PATH=C:\msys64\mingw64\bin;%PATH%

echo Compiling Neon Genesis...
REM -O2 turns on the compiler's optimiser. The city issues a few thousand
REM draw calls a frame, and an unoptimised build spends noticeably longer
REM preparing them.
REM -lwinmm is Windows' built-in multimedia library, used for the music.
REM -lgdiplus is Windows' built-in image library, used for billboard pictures.
g++ -O2 src/main.cpp src/glad.c -Iinclude -o neon.exe -lglfw3 -lopengl32 -lgdi32 -lwinmm -lgdiplus

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo BUILD FAILED - see the errors above.
    pause
    exit /b 1
)

echo Build OK. Running...
echo.
REM ".\" so cmd looks in this folder, not just on PATH.
".\neon.exe"

pause
