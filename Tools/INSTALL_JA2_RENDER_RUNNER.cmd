@echo off
setlocal
title Install JA2 Autonomous Render Runner

echo.
echo JA2 AUTONOMOUS RENDER RUNNER
echo.
echo This is a one-time setup.
echo It installs a GitHub Actions runner as a Windows service.
echo.
set /p TOKEN=Paste the GitHub self-hosted runner registration token: 
if "%TOKEN%"=="" (
  echo No token supplied.
  pause
  exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0INSTALL_JA2_RENDER_RUNNER.ps1" -RegistrationToken "%TOKEN%"
set RC=%ERRORLEVEL%
echo.
if not "%RC%"=="0" echo Setup failed with exit code %RC%.
pause
exit /b %RC%