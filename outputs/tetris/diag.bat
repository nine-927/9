@echo off
rem Diagnostic launcher - ASCII only. Log goes to D:\tetris\diag.log
cd /d D:\tetris
echo [%DATE% %TIME%] diag start, cd=%CD% > diag.log
if not exist tetris.exe echo [%TIME%] tetris.exe NOT FOUND >> diag.log
tetris.exe
echo [%DATE% %TIME%] tetris exited, code=%ERRORLEVEL% >> diag.log
echo.
echo ====== DIAG DONE - exit code = %ERRORLEVEL% ======
echo (0 = clean exit;  -1073741510 = killed by Ctrl-C-like signal;
echo  other negative hex codes = abnormal termination)
echo A copy was saved to D:\tetris\diag.log - press any key to close.
pause >nul
