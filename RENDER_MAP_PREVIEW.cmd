@echo off
setlocal EnableExtensions

rem Engine-driven JA2 sector visual preview.
rem Expected layout:
rem   game root : C:\VENGENCE\Jagged Alliance 2
rem   vr_source : C:\VENGENCE\Jagged Alliance 2\00000
rem
rem Usage:
rem   RENDER_MAP_PREVIEW.cmd A3.dat
rem If omitted, A3.dat is used.

set "REPO=%~dp0"
for %%I in ("%REPO%..") do set "GAME=%%~fI"
set "MAP=%~1"
if "%MAP%"=="" set "MAP=A3.dat"

set "EDITOR=%REPO%bin\VS2013\MapEditor_EN_Release.exe"
set "GAME_PREVIEW=%GAME%\MAP_PREVIEWS"
set "REPO_PREVIEW=%REPO%MapPreviews"

echo.
echo JA2 engine map preview
echo Map       : %MAP%
echo Source    : %REPO%
echo Game root : %GAME%
echo.

if not exist "%EDITOR%" (
    echo ERROR: MapEditor build was not found:
    echo   %EDITOR%
    echo.
    echo Build configuration MapEditor ^| Win32 first.
    pause
    exit /b 1
)

if not exist "%REPO_PREVIEW%" mkdir "%REPO_PREVIEW%"

pushd "%GAME%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%REPO%Tools\Invoke-OnSecondaryMonitor.ps1" -FilePath "%EDITOR%" -ArgumentList "-MAPSHOT=%MAP%" -WorkingDirectory "%GAME%"
set "RC=%ERRORLEVEL%"
popd

if not "%RC%"=="0" (
    echo.
    echo ERROR: Map preview process returned exit code %RC%.
    pause
    exit /b %RC%
)

set "BASENAME=%MAP%"
for %%F in ("%BASENAME%") do set "BASENAME=%%~nF"
set "OUTPUT=%GAME_PREVIEW%\%BASENAME%_overview.bmp"

if not exist "%OUTPUT%" (
    echo.
    echo ERROR: Expected preview was not created:
    echo   %OUTPUT%
    pause
    exit /b 2
)

copy /Y "%OUTPUT%" "%REPO_PREVIEW%\%BASENAME%_overview.bmp" >nul
if errorlevel 1 (
    echo.
    echo ERROR: Could not copy preview into repository.
    pause
    exit /b 3
)

echo.
echo DONE.
echo Full engine-rendered preview:
echo   %REPO_PREVIEW%\%BASENAME%_overview.bmp
echo.
echo Attach that BMP to ChatGPT for direct visual inspection.
echo No manual in-game camera positioning or screenshots are required.
echo.
pause
exit /b 0
