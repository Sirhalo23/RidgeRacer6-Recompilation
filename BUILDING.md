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
- The ReXGlue SDK, Windows package. The releases are built with
  [our fork](https://github.com/Sirhalo23/rexglue-sdk/releases) of it
  (`rexglue-sdk-0.10.0.100-win-amd64.zip` or later), which is v0.10.0 plus the
  fixes listed in its `FORK.md`. The original
  [v0.10.0](https://github.com/rexglue/rexglue-sdk/releases/tag/v0.10.0)
  works as well.
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

The same sources build against the SDK's Linux package (Vulkan). Use the
fork's package here (`rexglue-sdk-0.10.0.100-linux-amd64.zip` or later): the
original v0.10.0 draws the track black on Vulkan, and a build against it
switches the game's texture sharpening off to get around that
(`src/lod_bias_fix.cpp`).

You need Clang 18 or newer, CMake 3.25 or newer, Ninja and g++, the SDK's
Linux package unpacked into `../sdk/linux-amd64` (or anywhere, with `REXSDK`
pointing at it), and the game files in `../game`:

    python3 tools/extract_xiso.py /path/to/your.iso extract ../game
    ./build-linux.sh

The first build runs the SDK's recompiler and then compiles its output; later
builds only redo what changed. The result is `out/build/linux-release/rr6_recomp`
and `launcher/rr6-extract`, the command-line disc-image tool.

`linux/make-package.sh <number>` assembles the two archives for other people
(`RidgeRacer6-Linux-TestBuild-NN.tar.gz` and `RidgeRacer6-SteamDeck-TestBuild-NN.tar.gz`)
in `../dist`: the game binary, the SDK's two runtime libraries, the disc-image
tool, the start script `linux/ridge-racer-6.sh`, a README and the licences.
Next to them it puts the game binary with its symbols, which stays private
like the Windows linker map.

A binary built this way needs, on the machine that runs it, at least the glibc
and C++ library versions of the SDK's own libraries (glibc 2.35, GCC 13.2) or
of the machine it was built on, whichever is newer.

`tools/linux-rig/README.md` describes how the game is run without a graphics
card for automated checks.

## macOS

The native build uses the SDK's SDL3 window, audio and controller backends,
with Vulkan translated to Metal by MoltenVK. Both `mac-arm64-release` (Apple
Silicon) and `mac-amd64-release` (Intel) presets are available. Build on the
Mac you will run on.

Install Xcode 16 or newer with its command-line tools, CMake 3.25 or newer,
Ninja and Python 3. With Homebrew: `brew install cmake ninja python`.

Use a ReXGlue SDK source checkout containing the source-build and
presentation fixes proposed in
[ReXGlue SDK PR #487](https://github.com/rexglue/rexglue-sdk/pull/487).
The stock v0.10.0 revision does not contain those fixes. This project uses
the SDK through `REXSDK_DIR`; it does not modify or patch the SDK.

Until an upstream revision includes the fixes, the following fork revision
provides the tested SDK dependency. Place it next to this repository:

    git clone https://github.com/abradburne/rexglue-sdk.git ../rexglue-sdk
    git -C ../rexglue-sdk checkout 6873c26d0c1067d810e25640de52d67eedff10e9
    git -C ../rexglue-sdk submodule update --init --recursive

Replace that temporary dependency with an upstream SDK revision containing
the fixes when one is available. The SDK revision includes the upstream
[MoltenVK drawable-size fix](https://github.com/KhronosGroup/MoltenVK/commit/4d74f17e0bc44de5db4b6778313c90258dcce634)
for swapchain recreation leaving the drawable at 1x1 pixels.

The SDK builds and stages its Vulkan loader and MoltenVK. From this checkout:

    ./build-macos.sh "/path/to/Ridge Racer 6 (USA).iso"
    ./run-macos.sh

The ISO argument is optional if the extracted disc already exists in
`../game`. The script verifies the executable hash, builds the recompiler,
generates the guest C++, reconfigures CMake to include those sources, and
builds the game. `REXSDK_DIR` selects another SDK source location;
`RR6_BUILD_JOBS` controls parallel compilation (default 8). `CMAKE` and
`PYTHON` can select tools installed outside `PATH`.

The app bundle is `out/build/mac-arm64-release/rr6_recomp.app` (or
`mac-amd64-release` on Intel). Use the launch script to provide the game-data
path. The executable and runtime libraries are inside `Contents/MacOS`.
First build copies macOS defaults to
`rr6_recomp.toml` beside the executable; later builds preserve your settings.
The launch script stores saves and caches in `out/userdata`, and writes
`logs/run-macos.log`. F4 opens settings and F3 opens statistics. Initial
rendering scale is native 720p. The stock SDK uses the same texture LOD bias
workaround as Linux (see above).

### Controllers on macOS

SDL3 handles analog sticks, analog triggers, buttons, hotplug and rumble
where the controller and macOS driver support it. Pair an Xbox or PS4
DualShock 4 controller in macOS Bluetooth settings or connect it by USB.
SDL maps it to the Xbox buttons the game expects; PlayStation Cross is A
and Circle is B. Keyboard controls also work (Space confirms, Return
starts/pauses, arrows steer and navigate).

The build stages `gamecontrollerdb.txt`. The launch script passes its
absolute path so mappings work regardless of the launching directory.
For diagnostics, run `./run-macos.sh --rr6_log_input=true`; the log records
SDL device detection and changes to player 1's guest input. Hardware
verification should cover steering, both triggers, menu buttons,
unplug/reconnect and vibration during a race. Check USB and Bluetooth
separately before claiming a controller model has been verified.

### Verification status

The ARM64 Release build has been tested on an Apple M4 Max. Native audio
and Bluetooth DualShock 4 menu input have been observed. Stable Pac-Man
presentation was observed with the drawable fix applied to the previous
MoltenVK pin. The final updated pin builds successfully but still needs a
visible-launch recheck. Intel builds, race rendering, analog steering/triggers,
controller reconnect, rumble and Xbox hardware still require verification.
