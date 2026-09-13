@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0DEPLOY_LOADING_SCREENS.ps1"
set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" (
  echo Deployment FAILED with exit code %RC%.
) else (
  echo Deployment finished successfully.
)
pause
exit /b %RC%
