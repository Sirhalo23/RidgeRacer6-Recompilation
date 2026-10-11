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

For a personalized local Windows package, extract the app badge from your
own USA disc instead of using the launcher's bundled original icon:

```powershell
$env:RR6_ICON_XEX = (Resolve-Path ..\game\default.xex).Path
.\make-tester-package.bat
```

Python 3 and the SDK's `bin/rexglue.exe` are needed only during packaging.
`RR6_ICON_REXGLUE` selects another SDK tool location. The packager embeds
the generated multi-size ICO into its staged launcher and game EXEs and
records hashes of those personalized files. It leaves the build EXEs alone.
Embed before any separate Authenticode signing step. Generated artwork stays
in the local package, with no new image assets committed to the repository.
Without `RR6_ICON_XEX`, the normal artwork-free distribution is unchanged.

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

The equivalent local Linux/Steam Deck icon option is:

```sh
RR6_ICON_XEX="$PWD/../game/default.xex" linux/make-package.sh 01 "$PWD/out/local-linux-package"
```

The SDK's `bin/rexglue` extracts the badge; `RR6_ICON_REXGLUE` can override
its location. Python 3 generates a PNG without third-party image libraries.
Each personalized archive includes `icons/rr6.png`. After unpacking into its
final folder, run `python3 tools/install-desktop.py` from there to add a
desktop-menu shortcut using that icon. Run it again if the folder moves.
The same PNG can be chosen as a custom icon for a non-Steam entry in Steam.
Ordinary packages include no disc-derived icon or shortcut installer.

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

Use the project's shared
[ReXGlue SDK fork](https://github.com/Sirhalo23/rexglue-sdk), branch `rr6`,
as Windows and Linux do. It includes the macOS source-build, presentation
and density-control changes from
[SDK PR #487](https://github.com/rexglue/rexglue-sdk/pull/487), plus the Vulkan
texture-exponent fix needed for correct track textures. PR #487 was closed
because upstream development already addresses its core build/presentation
issues; the shared fork also retains the optional density control.
This project uses the SDK through `REXSDK_DIR`; it does not patch the SDK.
Place a source checkout next to this repository:

    git clone --branch rr6 https://github.com/Sirhalo23/rexglue-sdk.git ../rexglue-sdk
    git -C ../rexglue-sdk checkout e4a7f75499071fa773d16a099b3ec2b4f941f786
    git -C ../rexglue-sdk submodule update --init --recursive

The shared SDK revision includes the upstream
[MoltenVK drawable-size fix](https://github.com/KhronosGroup/MoltenVK/commit/4d74f17e0bc44de5db4b6778313c90258dcce634)
for swapchain recreation leaving the drawable at 1x1 pixels.

The newer MoltenVK 1.4.3 dependency and standalone SDK deployment-target
update is proposed separately in
[shared SDK PR #1](https://github.com/Sirhalo23/rexglue-sdk/pull/1).
To match the feedback-review build while that PR is pending:

    git -C ../rexglue-sdk fetch origin pull/1/head
    git -C ../rexglue-sdk checkout FETCH_HEAD
    git -C ../rexglue-sdk submodule update --init --recursive

The performance build additionally uses
[shared SDK PR #5](https://github.com/Sirhalo23/rexglue-sdk/pull/5),
which adds presenter pipeline reuse, persistent driver caches, shutdown
flushing and idle timer waits. To build the full performance changes:

    git -C ../rexglue-sdk fetch origin pull/5/head
    git -C ../rexglue-sdk checkout FETCH_HEAD
    git -C ../rexglue-sdk submodule update --init --recursive

Rebuild the runtime and GPU plugin together: this SDK change extends their
shared graphics interface. Measured results and validation limits are in
[the performance findings](tools/macos-performance-results.md).

The SDK builds and stages its Vulkan loader and MoltenVK. From this checkout:

    ./build-macos.sh "/path/to/Ridge Racer 6 (USA).iso"
    ./run-macos.sh

The ISO argument is optional if the extracted disc already exists in
`../game`. The script verifies the executable hash, builds the recompiler,
generates the guest C++, reconfigures CMake to include those sources, and
builds the game. `REXSDK_DIR` selects another SDK source location;
`RR6_BUILD_JOBS` controls parallel compilation (default 8). `CMAKE` and
`PYTHON` can select tools installed outside `PATH`.
The macOS presets target macOS 14.0 for both the game and source-built SDK
dependencies. `RR6_MACOS_DEPLOYMENT_TARGET` can override that value when using
the build script; both configure passes apply it. Do not lower the target
without checking the resulting binaries and testing on that OS.

The app bundle is `out/build/mac-arm64-release/rr6_recomp.app` (or
`mac-amd64-release` on Intel). Use the launch script to provide the game-data
path. The executable and runtime libraries are inside `Contents/MacOS`.
First build copies macOS defaults to
`rr6_recomp.toml` beside the executable; later builds preserve your settings.
The launch script stores saves and caches in `out/userdata`, and writes
`logs/run-macos.log`. F4 opens settings and F3 opens statistics. Initial
rendering scale is native 720p. The shared SDK's texture-exponent fix lets the
game retain its original texture LOD bias, as on Windows. The macOS default
disables the zero-LOD-bias workaround, including source builds whose version
suffix is a commit count rather than the fork's package number.

The macOS defaults set `window_high_pixel_density = false`, using a logical
resolution presentation buffer rather than a Retina/HiDPI buffer. This
leaves the desktop display mode and the game's 720p rendering unchanged.
At 2x display density, it reduces the presentation pixel count by 75%, with
softer UI and output. Set it to `true` and restart to restore high-density
presentation. Existing settings are preserved, so add the setting manually
when upgrading an older build.

`async_shader_compilation = true` remains enabled. New screens can briefly
skip presentation while shader pipelines compile; keep the user-data cache
between runs. Lower presentation resolution does not eliminate shader
compilation drops. The runtime also persists Vulkan driver pipeline data;
on MoltenVK this reuses translated Metal shaders, while native Metal pipeline
compilation may still occur. Quit normally to flush newly learned cache entries.
For measured results and the required SDK changes, see
[the performance findings](tools/macos-performance-results.md).

On macOS, MoltenVK processes queue submissions on its serial dispatch queue
by default. This keeps Metal drawable waits out of the shared submission lock.
The startup log records `MoltenVK synchronous queue submits: 0`. To restore
synchronous submissions for a comparison, run:

    MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=1 ./run-macos.sh

An explicit environment value is preserved. This setting also applies to the
packaged app and leaves the game's guest vblank timing unchanged.

For repeatable CPU frame-time captures and separate cold/warm application
cache comparisons, see [the macOS performance measurement guide](tools/performance.md).
Its optional Release CSV measures guest-swap pacing and CPU swap work;
Metal System Trace provides separate native GPU evidence.

For a local app icon made from your own disc artwork, use the `default.xex`
already extracted by `build-macos.sh`. The SDK extracts its original title
badge, then packaging renders a rounded macOS tile and all ICNS sizes:

    RR6_ICON_XEX="$PWD/../game/default.xex" macos/make-package.sh "$PWD/out/local-package"

Only rendering/extraction source is kept in git. The PNGs are temporary and
the generated ICNS is placed inside the packaged app in ignored build output.
Without `RR6_ICON_XEX`, packaging includes no disc-derived icon. The original
badge is 64×64; larger icon sizes preserve that artwork rather than adding
invented detail. `RR6_ICON_REXGLUE` can select a different SDK tool executable.

`vsync = true` controls guest vblank timing; it does not force the Vulkan
presenter's display mode. For optional strict display VSync, add all three
settings below and restart. This forces FIFO presentation and may increase
latency; it does not fix shader compilation stalls:

    vulkan_allow_present_mode_immediate = false
    vulkan_allow_present_mode_mailbox = false
    vulkan_allow_present_mode_fifo_relaxed = false

### A single app for testers

After building, run:

    ./macos/make-package.sh

This creates `dist/RidgeRacer6-macOS-arm64-test.zip` (or `x86_64` on Intel),
containing one `Ridge Racer 6.app`. The package uses a whitelist of runtime
files rather than copying the development bundle, audits linked libraries,
includes third-party licenses, strips local/debug symbols and ad-hoc signs
the app. It contains no ISO or extracted disc files. Python and Xcode are
needed to make the package, but not on the machine running it.

Put the unpacked app beside the user's Ridge Racer 6 USA ISO and double-click
it. A native macOS launcher opens with Display, Controls and Game Files tabs,
Save Settings and Play. When exactly one ISO is beside the app, the first Play
selects it automatically; otherwise a native file picker asks for it. First Play validates the
executable against the supported USA SHA-256 and copies about 6 GB of files.
A progress window allows cancellation, and interrupted copies can resume.
Later launches reuse the complete cache without needing the ISO.

Extracted files, settings, saves and logs are stored in
`~/Library/Application Support/Ridge Racer 6/`, outside the app bundle.
The default settings are copied there once; later launches preserve edits.
Rebuilding the development target does not update an existing tester app.
Run `./macos/make-package.sh` again and replace the app from the new ZIP.
The release launcher uses the existing native `launcher/disc_image.cpp`
extractor, so there is no runtime Python or SDK installation requirement.

The Display tab offers language, full screen/windowed mode, render scale,
logical-resolution or Retina output, optional display VSync, FXAA, anisotropic
filtering and asynchronous shader compilation. The recommended starting point
is 1x (720p) and logical-resolution output. Higher scales are experimental on
Vulkan/MoltenVK and consume more GPU memory. Display VSync controls the three
present-mode flags above; it leaves the game's `vsync` timing setting alone.
Custom values outside the offered presets are displayed as custom and retained
until that control is changed. Startup window size is deliberately not exposed:
the current SDK also uses non-default window dimensions as a guest video mode.

The Controls tab uses the same bundled SDL3 runtime and controller database as
the game. Select a connected controller to see stick positions, analog trigger
values and held buttons; Test Vibration is enabled when SDL reports support.
Device detection refreshes automatically. This selection is for testing only;
the game assigns controllers using its existing connection order. The preview
releases its devices before Play and resumes when the game closes. Xbox and
PlayStation button names are a launcher reference; the game's prompts stay Xbox.
Keyboard input can be enabled alongside controllers. Select a row and Set Key
to capture a shortcut, or edit comma-separated bindings directly. Escape cancels
capture; Command shortcuts are reserved for macOS. Reset Keyboard restores the
macOS template's bindings. Controller inputs themselves retain SDL's mappings.

Save Settings and Play write only changed options to the existing settings file
with an atomic replacement, retaining unrelated values, comments and tables.
The file is re-read before saving to preserve other edits. Play runs the bundled
game as a child process and returns to the launcher on exit, reloading settings
changed through F4. Game Files offers ISO import and Finder shortcuts to the
external data, DLC and log folders. The launcher does not change the desktop's
display mode or delete saves or shader caches.

For CLI testing, `Contents/MacOS/rr6-launcher --play` bypasses the settings
window. `--data-root /absolute/folder` selects an isolated data directory;
`--prepare-only image.iso` performs a headless import. Pass game options after
`--`, for example `--play -- --user_language=1`. Explicit game flags override
the launcher's default paths. The native settings tests can be run separately:

    xcrun clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
      macos/settings.cpp macos/settings_test.cpp -o /tmp/rr6-launcher-settings-test
    /tmp/rr6-launcher-settings-test

The launcher creates `~/Library/Application Support/Ridge Racer 6/DLC/`
and passes it as `rr6_dlc_folder`. Put your own content packages there;
the game's existing installer validates and installs new or changed packages
at startup. Content stays outside the signed app, and the packager refuses
files with content-package headers. Development launches use `out/DLC/`.
An explicit `--rr6_dlc_folder` launch option can select another folder.

Set `user_language` in `rr6_recomp.toml` and restart: 1 English, 2 Japanese,
3 German, 4 French, 5 Spanish, 6 Italian. The default is English; unsupported
values fall back to English. Existing settings are preserved when upgrading.

The package's `LSMinimumSystemVersion` comes from the maximum minimum OS
version recorded in its bundled Mach-O files, including the launcher's
compile target. The default build target is macOS 14.0. Declaring that target
does not replace testing on macOS 14 hardware before a public release.
Intel packages also need their own build and runtime verification.

The generated package is a local test build, not a notarized public release.
For public distribution, use Developer ID signing and Apple's
[notarization workflow](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution).

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

The local feedback-review build uses the shared `rr6` SDK branch plus a local
MoltenVK 1.4.3 update matching upstream development
(`701747d61a0484e91c205e081a24ca592ffa12b4`). That dependency update is prepared
separately for the shared SDK; the stock shared revision in the recipe above
uses MoltenVK 1.4.2. The game build never modifies the dependency checkout.
All five staged game/runtime binaries and the Cocoa launcher report a
minimum OS of 14.0; the package declaration is derived from those binaries.
The new app loads its bundled MoltenVK 1.4.3 and has been confirmed running
on the current Apple M4 Max. English, German and Japanese `XGetLanguage`
results have been checked. With isolated synthetic DLC, a nested valid
package was installed, a package with a `..` entry was refused without
writing outside the destination, and the unchanged package was skipped on
the next launch. The packager refuses LIVE/PIRS/CON content-package headers.
Runtime validation on an actual macOS 14 machine still remains.

The ARM64 Release build has been tested on an Apple M4 Max. Native audio
and Bluetooth DualShock 4 menu input have been observed. The packaged app
has launched with its bundled Vulkan/MoltenVK runtime and shown stable Pac-Man
graphics using `--async_shader_compilation=false`. Its native ISO importer has
extracted the supported image and rejected malformed/wrong-version test images
before writing data. Bundle signature verification passes after launch.
With high-density presentation disabled, the initial window's swapchain was
verified at 1280x720 with contents scale 1, versus 2560x1440 at scale 2.
Fullscreen was also verified at 2560x1440 / scale 1, versus 5120x2880 /
scale 2 in the earlier log. The optional display VSync settings selected
FIFO presentation (mode 2). Frame-rate comparison still requires an
interactive test.
Intel builds, race rendering, analog steering/triggers, controller reconnect,
rumble, Xbox hardware and older macOS versions still require verification.
