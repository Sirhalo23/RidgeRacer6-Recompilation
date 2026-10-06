@echo off
rem Builds a tester package (zip without game data) in ..\dist from the current
rem Windows build. Build and test the game first (build-and-test.bat).
setlocal EnableExtensions
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "package\make-package.ps1" %*
echo.
pause
