@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0VR_TelemetryAgent.ps1" -Install
if errorlevel 1 (
  echo.
  echo Telemetry agent installation FAILED.
  pause
  exit /b 1
)
echo.
echo Vengeance telemetry agent installed and started.
echo It will watch the game automatically and upload each completed session when Git authentication is available.
pause
