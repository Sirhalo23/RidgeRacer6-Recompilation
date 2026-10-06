@echo off
rem Build rr6_recomp.exe. Double-click this file, or run it from any command prompt.
rem It finds Visual Studio and LLVM itself; the full output goes to logs\build-windows.log.
setlocal EnableExtensions
cd /d "%~dp0"
if not exist logs mkdir logs
set "LOG=%~dp0logs\build-windows.log"
set "SDK=%~dp0..\sdk\win-amd64"
set "RC=1"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

rem --- Visual Studio C++ tools (compiler libraries, linker, Windows SDK)
where cl >nul 2>nul
if not errorlevel 1 goto have_vs
if not exist "%VSWHERE%" goto no_vs
set "VSDIR="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%I"
if not defined VSDIR goto no_vs
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl >nul 2>nul
if errorlevel 1 goto no_vs
:have_vs

rem --- Clang (standalone LLVM first, then the copy Visual Studio can install)
where clang++ >nul 2>nul
if not errorlevel 1 goto have_clang
if exist "%ProgramFiles%\LLVM\bin\clang++.exe" set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"
where clang++ >nul 2>nul
if not errorlevel 1 goto have_clang
if defined VCINSTALLDIR if exist "%VCINSTALLDIR%Tools\Llvm\x64\bin\clang++.exe" set "PATH=%VCINSTALLDIR%Tools\Llvm\x64\bin;%PATH%"
where clang++ >nul 2>nul
if errorlevel 1 goto no_clang
:have_clang

where cmake >nul 2>nul
if errorlevel 1 goto no_cmake
where ninja >nul 2>nul
if errorlevel 1 goto no_ninja

echo Tools found:
for /f "tokens=*" %%V in ('clang++ --version') do (echo   %%V & goto shown_clang)
:shown_clang
for /f "tokens=*" %%V in ('cmake --version') do (echo   %%V & goto shown_cmake)
:shown_cmake
for /f "tokens=*" %%V in ('ninja --version') do echo   ninja %%V

echo ==== tools ==== > "%LOG%"
where cl clang++ cmake ninja >> "%LOG%" 2>&1
clang++ --version >> "%LOG%" 2>&1
cmake --version >> "%LOG%" 2>&1

rem --- First build after a fresh checkout: turn the game's executable into C++
rem (generated\default). Later builds redo this by themselves when the manifest changes.
if exist "generated\default\sources.cmake" goto have_generated
if not exist "..\game\default.xex" goto no_game
if not exist "%SDK%\bin\rexglue.exe" goto no_sdk
echo.
echo Recompiling the game's executable into C++ (first build only)...
echo ==== codegen ==== >> "%LOG%"
"%SDK%\bin\rexglue.exe" codegen rr6_recomp_manifest.toml >> "%LOG%" 2>&1
if errorlevel 1 goto failed
:have_generated

echo.
echo Configuring...
echo ==== configure ==== >> "%LOG%"
cmake --preset win-amd64-release -DCMAKE_PREFIX_PATH="%SDK%" >> "%LOG%" 2>&1
if errorlevel 1 goto failed

echo Compiling 1.5 million lines of generated C++ - this takes several minutes...
echo ==== build ==== >> "%LOG%"
cmake --build out\build\win-amd64-release >> "%LOG%" 2>&1
if errorlevel 1 goto failed

echo ==== done ==== >> "%LOG%"
if not exist "out\build\win-amd64-release\rr6_recomp.toml" copy /y "config\rr6_recomp.default.toml" "out\build\win-amd64-release\rr6_recomp.toml" >nul
echo.
echo Built: out\build\win-amd64-release\rr6_recomp.exe
echo Start the game with run.bat
set "RC=0"
goto end

:no_vs
echo Visual Studio 2022 C++ build tools were not found. Install them first.
echo NO_VS > "%LOG%"
goto end
:no_clang
echo clang++ was not found. Install LLVM first.
echo NO_CLANG > "%LOG%"
goto end
:no_cmake
echo cmake was not found. It normally comes with the Visual Studio C++ workload.
echo NO_CMAKE > "%LOG%"
goto end
:no_game
echo The game files were not found: ..\game\default.xex is missing.
echo Copy the files of your own Ridge Racer 6 (USA) disc image there first, with
echo the extract_xiso.py tool: see BUILDING.md.
echo NO_GAME > "%LOG%"
goto end
:no_sdk
echo The ReXGlue SDK was not found in ..\sdk\win-amd64 (rexglue.exe is missing).
echo Unpack the SDK's Windows package there first.
echo NO_SDK > "%LOG%"
goto end
:no_ninja
echo ninja was not found. It normally comes with the Visual Studio C++ workload.
echo NO_NINJA > "%LOG%"
goto end
:failed
echo.
echo BUILD FAILED. Last lines of logs\build-windows.log:
powershell -NoProfile -Command "Get-Content -Tail 30 -LiteralPath '%LOG%'"
echo ==== FAILED ==== >> "%LOG%"
:end
echo.
if /i not "%~1"=="nopause" pause
exit /b %RC%
