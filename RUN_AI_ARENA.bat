@echo off
setlocal EnableExtensions EnableDelayedExpansion
title Vengeance AI Battle Arena

cd /d "%~dp0"

echo ============================================================
echo         VENGEANCE AI BATTLE ARENA - OVERNIGHT RUN
echo ============================================================
echo.
echo This runs unattended AI-vs-AI tactical battles and writes:
echo   AI SelfPlay Runs.tsv
echo   AI SelfPlay Decisions.tsv
echo   AI SelfPlay Batch.txt
echo   Campaign Tactical Black Box.tsv
echo.

set "ARENA_EXE="
if exist "JA2_EN_Release.exe" set "ARENA_EXE=JA2_EN_Release.exe"
if not defined ARENA_EXE if exist "JA2_Vengeance.exe" set "ARENA_EXE=JA2_Vengeance.exe"
if not defined ARENA_EXE if exist "ja2.exe" set "ARENA_EXE=ja2.exe"

if not defined ARENA_EXE (
  echo ERROR: No JA2 executable found in this folder.
  echo Build the arena branch in Visual Studio first, then run this file again.
  pause
  exit /b 2
)

set /p "MAP=Map/sector [example A9]: "
if not defined MAP set "MAP=A9"

set /p "RUNS=Number of battles [default 500]: "
if not defined RUNS set "RUNS=500"

set /p "LABEL=Experiment label [default overnight]: "
if not defined LABEL set "LABEL=overnight"

set "BASESEED=50000"
set "MAXTURNS=1200"

echo.
echo Executable : %ARENA_EXE%
echo Map        : %MAP%
echo Battles    : %RUNS%
echo Seeds      : %BASESEED% onward
echo Label      : %LABEL%
echo Max turns  : %MAXTURNS%
echo.
echo Starting unattended arena. The game window will hide itself.
echo Close this console only if you want to stop the batch.
echo.

"%ARENA_EXE%" -SELFPLAY=%MAP%,%RUNS%,%BASESEED%,%MAXTURNS%,%LABEL%
set "RC=%ERRORLEVEL%"

echo.
echo ============================================================
if "%RC%"=="0" (
  echo Arena batch finished successfully.
) else (
  echo Arena exited with code %RC%.
)
echo Results are in this game folder.
echo ============================================================

if exist "tools\analyze_selfplay.py" (
  where python >nul 2>nul
  if not errorlevel 1 (
    echo Running summary analyzer...
    python "tools\analyze_selfplay.py"
  )
)

pause
exit /b %RC%
