@echo off
rem Ridge Racer 6 - PC test build. Starts the game without the settings launcher
rem (use "RR6 Launcher.exe" normally; this is the fallback if it will not open).
rem First run: extracts the game files from the .iso in PUT-ISO-HERE (one time).
setlocal EnableExtensions
cd /d "%~dp0"
title Ridge Racer 6 - PC test build
if not exist logs mkdir logs

if not exist "bin\rr6_recomp.exe" goto broken
if not exist "tools\prepare-game.ps1" goto broken
if not exist "%SystemRoot%\System32\vcruntime140_1.dll" goto need_vcredist
if not exist "%SystemRoot%\System32\msvcp140_atomic_wait.dll" goto need_vcredist

powershell -NoProfile -ExecutionPolicy Bypass -File "tools\prepare-game.ps1"
if errorlevel 1 goto prepare_failed

rem Settings come from bin\rr6_recomp.toml (edit it with Notepad, or use the launcher).
start "" "bin\rr6_recomp.exe" --game_data_root "%~dp0game" --gpu_plugin=xenos --log_file "%~dp0logs\run.log"
exit /b 0

:broken
echo This folder is incomplete. Extract the whole zip again, then run this file
echo from inside the extracted folder (not from inside the zip).
goto stop
:need_vcredist
echo The Microsoft Visual C++ runtime is missing on this PC.
echo Install "Visual C++ Redistributable for Visual Studio 2015-2022 (x64)" from:
echo     https://aka.ms/vs/17/release/vc_redist.x64.exe
echo then start the game again.
goto stop
:prepare_failed
echo.
echo The game files are not ready, so the game was not started. See the message above.
:stop
echo.
pause
exit /b 1
