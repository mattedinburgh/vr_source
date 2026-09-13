@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -File "%~dp0DEPLOY_GORE.ps1"
set RC=%ERRORLEVEL%
echo.
if not "%RC%"=="0" echo Gore deployment FAILED.
if "%RC%"=="0" echo Gore deployment completed successfully.
pause
exit /b %RC%
