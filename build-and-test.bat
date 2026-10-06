@echo off
rem Rebuild, then launch with diagnostics. One double-click per test round.
cd /d "%~dp0"
call build-windows.bat nopause
if errorlevel 1 (echo Build failed - not launching. & pause & exit /b 1)
call run-debug.bat
