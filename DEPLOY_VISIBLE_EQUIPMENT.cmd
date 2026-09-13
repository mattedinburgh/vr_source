@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0DEPLOY_VISIBLE_EQUIPMENT.ps1" %*
set RC=%ERRORLEVEL%
echo.
if not "%RC%"=="0" (
  echo Visible-equipment deployment FAILED with exit code %RC%.
) else (
  echo Visible-equipment deployment completed successfully.
)
pause
exit /b %RC%
