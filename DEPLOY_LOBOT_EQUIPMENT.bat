@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0DEPLOY_LOBOT_EQUIPMENT.ps1"
set ERR=%ERRORLEVEL%
echo.
if not "%ERR%"=="0" echo LOBOT deployment FAILED with exit code %ERR%.
if "%ERR%"=="0" echo LOBOT deployment completed.
pause
exit /b %ERR%
