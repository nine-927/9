@echo off
rem ==========================================================
rem  Tetris learning steps - one click build
rem  Needs Visual Studio installed on this machine.
rem  Usage: double-click this file, or run build.bat in cmd
rem ==========================================================

call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" >nul
cd /d "%~dp0"

echo ==== compiling step1_shape.c ====
cl /nologo /utf-8 /W3 step1_shape.c
echo ==== compiling step2_move.c ====
cl /nologo /utf-8 /W3 step2_move.c
echo ==== compiling step3_keys.c ====
cl /nologo /utf-8 /W3 step3_keys.c
echo ==== compiling step4_wall.c ====
cl /nologo /utf-8 /W3 step4_wall.c
echo ==== compiling step5_fall.c ====
cl /nologo /utf-8 /W3 step5_fall.c

del *.obj >nul 2>nul

echo.
echo ==== build finished, generated exe: ====
dir /b *.exe
pause
