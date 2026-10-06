@echo off
rem Switch the game settings to ultrawide 21:9 (3440x1440). Takes effect the next time the game starts.
copy /y "%~dp0config\rr6_recomp.ultrawide.toml" "%~dp0out\build\win-amd64-release\rr6_recomp.toml" >nul && echo Settings switched to ultrawide 21:9 (3440x1440).
pause
