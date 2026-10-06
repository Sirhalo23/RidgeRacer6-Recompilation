@echo off
rem Launch the recompiled game against the extracted disc contents in ..\game
setlocal
set "EXE=%~dp0out\build\win-amd64-release\rr6_recomp.exe"
if not exist "%EXE%" (echo Build first: build-windows.bat & exit /b 1)
"%EXE%" --game_data_root "%~dp0..\game" --gpu_plugin=xenos --use_fuzzy_alpha_epsilon --log_file "%~dp0logs\run.log" %*
