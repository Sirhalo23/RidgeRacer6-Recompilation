@echo off
rem Same as "Play without the launcher.bat", but records a detailed log and then
rem packs it, the exit code, Windows' crash record and basic PC details into
rem bug-report.zip for sending back. (The launcher does the same when
rem "Record a detailed log" is ticked.)
setlocal EnableExtensions
cd /d "%~dp0"
title Ridge Racer 6 - PC test build (diagnostic mode)
if not exist logs mkdir logs

if not exist "bin\rr6_recomp.exe" goto broken
if not exist "tools\prepare-game.ps1" goto broken
if not exist "%SystemRoot%\System32\vcruntime140_1.dll" goto need_vcredist
if not exist "%SystemRoot%\System32\msvcp140_atomic_wait.dll" goto need_vcredist

powershell -NoProfile -ExecutionPolicy Bypass -File "tools\prepare-game.ps1"
if errorlevel 1 goto prepare_failed

del /q logs\run-debug*.log logs\last-exit.txt logs\crash-event.txt logs\fp-guard.txt logs\system-info.txt 2>nul
echo Starting the game with diagnostic logging. Play until the problem happens,
echo then close the game (or let it crash). Keep this window open.
"bin\rr6_recomp.exe" --game_data_root "%~dp0game" --gpu_plugin=xenos --log_level debug --log_flush_interval 1 --log_max_file_size_mb 50 --log_max_files 3 --log_file "%~dp0logs\run-debug.log"
set "EC=%ERRORLEVEL%"
for /f "usebackq" %%H in (`powershell -NoProfile -Command "'{0:X8}' -f %EC%"`) do set "ECHEX=%%H"
echo exit code %EC% (0x%ECHEX%) > logs\last-exit.txt
echo Game exited with code %EC% (0x%ECHEX%)

timeout /t 3 /nobreak >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\collect-report.ps1"
echo.
echo Send the file bug-report.zip from this folder along with a short description
echo of what you were doing and what went wrong.
goto stop

:broken
echo This folder is incomplete. Extract the whole zip again.
goto stop
:need_vcredist
echo The Microsoft Visual C++ runtime is missing on this PC. Install it from:
echo     https://aka.ms/vs/17/release/vc_redist.x64.exe
goto stop
:prepare_failed
echo.
echo The game files are not ready, so the game was not started. See the message above.
:stop
echo.
pause
