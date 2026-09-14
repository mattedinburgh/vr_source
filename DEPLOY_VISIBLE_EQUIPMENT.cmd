@echo off
setlocal
cd /d "%~dp0"

echo Checking visible-equipment PowerShell syntax...
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$tokens=$null; $errors=$null; [System.Management.Automation.Language.Parser]::ParseFile((Join-Path (Get-Location) 'DEPLOY_VISIBLE_EQUIPMENT.ps1'), [ref]$tokens, [ref]$errors) ^| Out-Null; if (@($errors).Count -gt 0) { $errors ^| ForEach-Object { Write-Host ('Parser: ' + $_.Message) -ForegroundColor Red }; exit 87 }"
if errorlevel 1 goto :syntaxfail

echo Syntax OK.
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0DEPLOY_VISIBLE_EQUIPMENT.ps1" %*
set RC=%ERRORLEVEL%
goto :done

:syntaxfail
set RC=87

:done
echo.
if not "%RC%"=="0" (
  echo Visible-equipment deployment FAILED with exit code %RC%.
) else (
  echo Visible-equipment deployment completed successfully.
)
pause
exit /b %RC%
