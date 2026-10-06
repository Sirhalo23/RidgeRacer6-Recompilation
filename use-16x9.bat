@echo off
rem Switch the game settings to standard 16:9. Takes effect the next time the game starts.
copy /y "%~dp0config\rr6_recomp.default.toml" "%~dp0out\build\win-amd64-release\rr6_recomp.toml" >nul && echo Settings switched to standard 16:9.
pause
