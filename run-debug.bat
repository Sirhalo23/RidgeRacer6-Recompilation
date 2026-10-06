@echo off
rem Diagnostic launch: verbose log, exit code, and the Windows crash record if it dies.
setlocal EnableExtensions
cd /d "%~dp0"
set "EXE=%~dp0out\build\win-amd64-release\rr6_recomp.exe"
if not exist "%EXE%" (echo Build first: build-windows.bat & pause & exit /b 1)
if not exist logs mkdir logs
del /q logs\run-debug*.log logs\run-stdout.txt logs\run-exit.txt logs\crash-event.txt logs\fp-guard.txt 2>nul

rem A pipeline cache left by builds from before the depth-bias fix (hundreds of
rem thousands of entries) would hide what this run does: remove it if it is over
rem 4 MB, as the launcher does.
powershell -NoProfile -Command "$d = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'rr6_recomp\cache\shaders\shareable'; Get-ChildItem $d -Filter '*.d3d12.xpso' -ErrorAction SilentlyContinue | Where-Object { $_.Length -gt 4MB } | Remove-Item -Force -ErrorAction SilentlyContinue"

echo Starting the game with diagnostic logging...
"%EXE%" --game_data_root "%~dp0..\game" --gpu_plugin=xenos --use_fuzzy_alpha_epsilon --log_level debug --log_flush_interval 1 --log_max_file_size_mb 200 --rr6_hud_stats=true --rr6_log_input=true --rr6_depth_bias_stats=true --log_file "%~dp0logs\run-debug.log" %* > logs\run-stdout.txt 2>&1
set "EC=%ERRORLEVEL%"
for /f "usebackq" %%H in (`powershell -NoProfile -Command "'{0:X8}' -f %EC%"`) do set "ECHEX=%%H"
echo exit code %EC% (0x%ECHEX%) > logs\run-exit.txt
echo Game exited with code %EC% (0x%ECHEX%)

rem Give Windows a moment to write its crash record, then save it.
timeout /t 3 /nobreak >nul
powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddMinutes(-5)} -ErrorAction SilentlyContinue | Where-Object { $_.Message -match 'rr6_recomp' } | Select-Object -First 4 | Format-List TimeCreated,ProviderName,Id,Message | Out-File -Encoding utf8 'logs\crash-event.txt'"
rem For analysis: a copy of the graphics pipeline cache, and the names and sizes of
rem the files in the save folder (Documents\rr6_recomp; nothing is read from them).
powershell -NoProfile -Command "$d = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'rr6_recomp'; Copy-Item (Join-Path $d 'cache\shaders\shareable\*.d3d12.xpso') 'logs' -Force -ErrorAction SilentlyContinue; Get-ChildItem -Recurse -File $d -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notmatch '\\cache\\' } | ForEach-Object { '{0,10}  {1}' -f $_.Length, $_.FullName.Substring($d.Length) } | Out-File -Encoding utf8 'logs\user-data-listing.txt'"
echo Diagnostics saved in the logs folder.
echo.
pause
