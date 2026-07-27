@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0unmount-sd-from-wsl.ps1"
set "exit_code=%errorlevel%"
if not "%exit_code%"=="0" (
    echo.
    echo Script failed with exit code %exit_code%.
    pause
)
exit /b %exit_code%
