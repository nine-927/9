@echo off
rem ==========================================================
rem  Launcher for the step-4 tetris playground.
rem  Opens a console window big enough for the 10x20 board.
rem ==========================================================

mode con: cols=62 lines=32
title Tetris - arrow keys to move, q to quit
cd /d "%~dp0"

step5_fall.exe

echo.
pause
