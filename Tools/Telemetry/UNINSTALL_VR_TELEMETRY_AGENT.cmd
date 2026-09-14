@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0VR_TelemetryAgent.ps1" -Uninstall
echo.
echo Vengeance telemetry auto-start removed.
pause
