# Building

The game's program is recompiled on your PC from your own copy. Nothing made
from the game may be committed or redistributed through this repository:
`generated/default`, `analysis/default.bin` and the game files are ignored by
git for that reason.

## You need

- Your own Ridge Racer 6 (USA) disc image. The build only matches the USA
  executable: `default.xex` SHA-256
  `39D3C0004EC62AEB0FE3E7E1889CF25D98FBD27987B6BC6B5FF30A56FFBA6C00`.
- Visual Studio 2022 Build Tools with the "Desktop development with C++"
  workload (it brings CMake and Ninja).
- LLVM/Clang 18 or newer.
- The [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) v0.10.0, Windows
  package.
- Python 3, for the disc image tool.

## Folders

The scripts expect the game files and the SDK next to the checkout, not inside
it:

    some-folder\
      RidgeRacer6-Recompilation\  this repository
      sdk\win-amd64\            the unpacked SDK package (bin, lib, include, ...)
      game\                     the contents of your disc (default.xex and the rest)
      dist\                     created by the packaging script

## Steps

1. Unpack the SDK's Windows package into `sdk\win-amd64`.
2. Copy the game files out of your disc image, from inside the checkout:

       python tools\extract_xiso.py path\to\your.iso extract ..\game

3. Run `build-windows.bat` (double-click it, or from any command prompt). It
   finds Visual Studio and Clang by itself. On the first build it runs the
   SDK's recompiler on `rr6_recomp_manifest.toml`, which writes about 1.5
   million lines of C++ into `generated\default`, and then compiles them. That
   takes several minutes. The full output goes to `logs\build-windows.log`.
4. Run `run.bat` to start the game, or `run-debug.bat` for a run with a
   detailed log.

The first-build step is new and has not been tried from a fresh checkout on
Windows yet. If it fails, run the recompiler by hand and build again:

    ..\sdk\win-amd64\bin\rexglue.exe codegen rr6_recomp_manifest.toml

## The launcher

`launcher/` is a separate small Win32 program. `launcher/build.sh` builds it
with MinGW-w64, on Linux or in an MSYS2 MinGW shell on Windows; the "Launcher"
workflow in this repository builds it too and prints its SHA-256. Copy the
resulting `RR6 Launcher.exe` into the checkout's top folder. Started from
there it finds the game build in `out\build\win-amd64-release` and the game
files in `..\game`.

## A package for other people

`make-tester-package.bat` assembles `..\dist\RidgeRacer6-PC-TestBuild-NN.zip`
from the current build: the launcher, the game executable and the SDK's
runtime files, default settings, the controller database, the README and the
licences. It refuses to include anything that looks like game data. It also
puts the linker map next to the zip; with it, a crash address from a bug
report can be turned into a function name
(`python tools\map_lookup.py <map file> <offset>`).

## Where things are

    rr6_recomp_manifest.toml   what the recompiler has to be told about this game
    src/                       fixes and additions, compiled into the game executable
    launcher/                  the launcher
    config/                    settings templates
    package/                   the fixed files of a package, and the script that builds one
    tools/                     small Python tools: disc image reader, executable
                               inspection, crash address lookup, a Linux test rig
    docs/                      development notes and SDK findings
    generated/rexglue.cmake    SDK build boilerplate (the rest of generated/ is made by the build)

## Linux

The same sources build against the SDK's Linux package (Vulkan) with the
`linux-*` presets in `CMakePresets.json`. That build has only been used for
automated checks with software rendering; see `tools/linux-rig/README.md`.
