@echo off
rem 俄罗斯方块启动器（带诊断）：
rem 游戏退出后窗口会保留，并显示退出码，方便排查启动问题。
cd /d D:\tetris
tetris.exe
set EC=%errorlevel%
echo.
echo ==============================================
echo 游戏已退出，退出码 %EC%
echo （0 = 正常退出；其他值请把本窗口截图发给助手）
echo ==============================================
pause >nul
