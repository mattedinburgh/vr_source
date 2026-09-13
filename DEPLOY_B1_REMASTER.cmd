@echo off
setlocal
title Vengeance B1 Remaster Deployment
echo.
echo ============================================
echo   Vengeance B1 Remaster - One Click Deploy
echo ============================================
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0DEPLOY_B1_REMASTER.ps1" -GameRoot "C:\\VENGENCE\\Jagged Alliance 2"
set EXITCODE=%ERRORLEVEL%
echo.
if not "%EXITCODE%"=="0" (
  echo DEPLOYMENT FAILED with exit code %EXITCODE%.
) else (
  echo DEPLOYMENT COMPLETE.
)
echo.
pause
exit /b %EXITCODE%
