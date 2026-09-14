@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0DEPLOY_AMBIENCE.ps1" %*
exit /b %ERRORLEVEL%
