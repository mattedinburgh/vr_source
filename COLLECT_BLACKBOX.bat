@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0COLLECT_BLACKBOX.ps1"
if errorlevel 1 (
  echo.
  echo Black-box collection FAILED.
  pause
  exit /b 1
)
echo.
pause
