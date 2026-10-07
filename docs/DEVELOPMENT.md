# Ridge Racer 6 static recompilation (work in progress)

> Working notes, kept as they were written. Paths are those of the working
> folder, where the project lives in `rr6-recomp/` with `tools/`, `game/`,
> `sdk/` and `dist/` next to it; in this repository the project folder is the
> top level and `tools/` is inside it.

Xbox 360 title `4E4D07D3` (USA disc, executable built 2005-11-01, XDK 2135),
recompiled to C++ with ReXGlue SDK v0.10.0. No game files belong in this folder:
the disc contents live in `../game` and are never to be redistributed.

## Where it stands (2026-10-06)

**Playable on Windows** (RTX 3060 Ti, Direct3D 12, 3440x1440): logos, menus,
racing, sound, Xbox controller, saving, 2x render scale, ultrawide.

- Disc extracted: 45 files, 6.49 GB, sizes verified against the disc directory.
- `rexglue codegen` completes with **0 analysis errors**: about 11,800 functions,
  ~1.53 M lines of C++, no unimplemented PowerPC instructions.
- Builds with Clang + Visual Studio 2022 tools via `build-windows.bat`.

What has been checked where:

| Area | Windows (real PC) | Linux test rig (software rendering) |
|---|---|---|
| Boot, menus, a race | yes | yes |
| Xbox controller | yes | - |
| Keyboard controls | **not yet** | yes (whole menu flow driven by keyboard) |
| PlayStation controller | **not yet** | - |
| Save created / loaded | created yes; loaded: see "Save data" | yes, incl. the corrupted-save cause |
| Ultrawide 3D view | yes | yes |
| Ultrawide race HUD at the edges | yes (earlier revision) | yes (current revision) |
| Ultrawide menus | current source reported as looking right | fixed in the current source |
| Launcher, first version | wrote the right settings for 3440x1440 and started the game (exit code 0) | yes, under Wine |
| Launcher, new look and disc-image chooser | yes, from the build 03 package (reported working as intended) | yes, under Wine |
| Tester package scripts | **not yet** | yes, PowerShell 7 on Linux |

The game source was last built and played on Windows on 2026-10-06 at 11:21
(`logs\build-windows.log`, `logs\run-debug.log`), including
`src/depth_bias_fix.cpp`. The restyled launcher was run on Windows from the
build 03 tester package the same day.

## Starting the game

- `RR6 Launcher.exe` (in this folder): choose settings, press Play. It finds the
  build in `out\build\win-amd64-release` and the game files in `..\game`.
- `run.bat` / `run-debug.bat`: start directly with the settings already in
  `out\build\win-amd64-release\rr6_recomp.toml`.

## Launcher (`launcher/`)

A small native Win32 program (no dependencies beyond Windows; built with
MinGW-w64 by `launcher/build.sh`). It only edits the game's settings file and
starts the game; nothing in the game depends on it.

Source: `launcher.cpp`, `disc_image.cpp/.h` (copies the game files out of the
player's disc image), `movie_still.c/.h` and `pl_mpeg_sofdec.h` (stills from
the opening movie), `launcher.rc`, `launcher.manifest`, `launcher.ico`.

**Game files.** While the game files are not there, the big button reads
"Choose disc image..." instead of "Play". It opens a file chooser, the player
points at their `.iso` wherever it is, and the launcher copies the game files
out of it into the `game` folder, showing a progress bar and a Stop button in
the bottom bar. The image is only read. Before anything is copied, the
`default.xex` inside the image is checked against the USA executable's
SHA-256, so the wrong game or region is refused straight away. A copy that was
stopped, or cut off by closing the window, carries on from the files already
there the next time. "Copy game files again..." on the Troubleshooting page
redoes all of it. An image lying in `PUT-ISO-HERE` is offered first in the
chooser. `disc_image.cpp` reads the disc's file system itself (the same logic
as `tools\prepare-game.ps1`, which stays in the package for the `.bat`
starters), so the launcher no longer needs PowerShell for this. The game folder
counts as ready when the copy has left its `.rr6-ready` marker; files put
there by hand, as in this development tree, count as ready without it.

**The picture at the top.** No artwork from the game is built into the
launcher or shipped in the tester package. The banner shows, in this order:

1. `launcher-art.png` / `.jpg` / `.bmp` next to the launcher, if the player puts
   one there (their own picture, for example a scan of their copy's cover);
2. a still from `game\opening.sfd`, the opening movie of the player's own copy
   (the night highway, about 12 s in), with the game's title cut out of a later
   frame of the same movie. Decoded on the player's PC the first time the movie
   is there, then kept in `game\.rr6-launcher-art` (4 MB, next to the files it
   was made from). Only the USA movie file is used (checked by size);
3. a drawn banner in the colours of the game's menus (honeycomb, lime bands,
   plain lettering). This is what every tester sees on the first start, before
   their disc image has been unpacked.

The rest of the look follows the game's menus: white pages, the lime accent
(sampled from the title screen), charcoal bars, and the stretched hexagon of the
menu highlight as the shape of the Play button. Headings use Bahnschrift
(Windows 10/11), falling back to Segoe UI. The icon is an original drawing.

Pages:

- Display: full screen / window; picture shape (fill an ultrawide screen or
  keep 16:9), computed from the detected screen size, any aspect ratio; HUD
  position; sharpness (render scale, automatic = next multiple of 720 lines at
  or above the screen height, wider horizontally when filling an ultrawide
  screen); FXAA; anisotropic filtering; the foliage fix.
- Controls: keyboard on/off and one or more keys per controller button, with
  Xbox or PlayStation button names. Controllers need no settings: the SDK uses
  SDL3, which handles Xbox and PlayStation pads itself. `gamecontrollerdb.txt`
  (community SDL mappings) covers less common pads.
- Troubleshooting: "Record a detailed log" (starts the game with debug logging;
  after a crash, or after any such run, `tools\collect-report.ps1`, tester
  package only, packs logs, exit code, settings, Windows' crash record and PC
  details into `bug-report.zip`), open the logs folder, open the save data
  folder, restore default settings, and a short note on what this build is.

Before starting the game it also: warns if Windows has no sound output device
(the game dies without one), deletes an oversized pipeline cache (see "Known
issues") and removes save thumbnails left by older builds (see "Save data").
Enter presses Play; Ctrl+Tab changes the page. `--save-and-exit` and
`--screen WxH` exist for automated checks.

Checked under Wine (new look): all three pages at 100% and 150% scaling, a
1024x620 screen (the banner gives up height), the three banner sources, key
capture, Enter and Ctrl+Tab, start / normal exit / crash message with a
stand-in game executable. The settings files it writes are byte-for-byte the
same as the first version's for five screen sizes. Wine has neither Bahnschrift
nor Segoe UI, so the lettering on Windows has not been seen yet.

Checked for the disc-image copy: see "Tester package" below.

How the movie is read (worth knowing for any other Sofdec work): `.sfd` is an
MPEG program stream with MPEG-1 video from a TMPGEnc-based encoder ("TMPGEXS"
user data). Three things differ from textbook MPEG-1, and a stock decoder
shows garbage without them: DC coefficients are 11-bit (size codes up to 11,
predictor reset 1024, no multiplication by 8); the sequence header is repeated
with different quantiser matrices (31 headers, 6 different sets in
`opening.sfd`), so the matrices must be re-read each time; and the video is
full-range (black is 0). `movie_still.c` decodes single intra pictures with
pl_mpeg (MIT) plus those changes; compared with FFmpeg on the same picture the
mean luma difference is about 1 level.

## Controls

Keyboard support is the SDK's `mnk_mode`: keys become buttons of controller 1
and work alongside a real pad. Defaults (`config/*.toml`, launcher): arrows or
WASD steer and move in menus, Up/W is also RT (accelerate), Down/S also LT
(brake), Space = A, Backspace/B = B, X, Y, Q/E = LB/RB, Enter/P = Start,
Tab = Back, numpad 8/2/4/6 = D-pad, IJKL = right stick. Keys are ignored while
Shift, Ctrl or Alt is held. Steering is digital (full lock or nothing).

`rr6_log_input = true` (`src/input_fixes.cpp`) logs every change of the pad
state the game reads, for "my controller does nothing" reports.

**Leaving the game** (`src/quit_prompt.cpp`). The game has no way out of its
own, and a full-screen window has no close button, so the only way used to be
Alt+F4. Now Esc, or Back + Start held for a second on a controller, asks "Quit
Ridge Racer 6?" in the SDK's overlay: Enter or A quits, Esc or B goes back, and
the two buttons can be clicked. Quitting asks the window to close, which is
what Alt+F4 does. The game keeps running behind the question; it is given an
idle controller while the question is up and until the answering button is let
go, and a question left unanswered for half a minute goes away. Back + Start
reaches the game before the question appears, so in a race the game's own
pause menu is up behind it. `rr6_quit_prompt = false` turns it off;
`rr6_quit_key` names the key. Checked on the Linux rig with the keyboard
standing in for the pad (tap, long hold with key repeat, Back + Start hold,
both answers, mouse, clean exit). Built on Windows on 2026-10-06 (17:18) and
tried there the same day: Esc brought the question up and quit the game, and
so did the Back + Start hold on an Xbox controller.

## Display settings

The game stays at its native 60 fps (its speed is tied to the display tick).
Image quality is set in `rr6_recomp.toml` next to the exe (templates in
`config/`), by the launcher, or in game with F4 (settings) and F3 (frame
statistics). Confirmed on a 3440x1440 monitor: 2x scale and 16x anisotropic
are clearly sharper with no frame-rate cost. The prebuilt runtime has no
CAS/FSR sharpening (`present_effect` only accepts `bilinear`).

### Ultrawide (`src/widescreen.cpp`)

- 3D view: four mid-asm hooks replace the projection aspect the game loads
  (16:9 float at 0x82001C54) with `rr6_aspect_ratio`; the 16:9 frame is then
  stretched to the screen (`present_letterbox = false`).
- 2D (`rr6_hud_fix`): the eleven 2D primitive routines are wrapped, and at
  EndVertices (sub_82254080) the clip-space X of each 2D draw is scaled toward
  the centre, so menus and HUD keep their shape. Draws spanning the full
  width (fades, backgrounds) are left alone.
- Edge layout (`rr6_hud_edges`): 2D is recorded into display lists first.
  Everything one sprite-group call (sub_82178648, the race HUD widgets)
  records is moved sideways as one unit, far enough to land at the real screen
  edge; a group covering both sides falls back to moving each command on its
  own. Menus are not sprite groups and stay in the central 16:9 area, which
  is what fixed the uneven letter spacing of the previous revision.
- `rr6_hud_stats = true` logs what the edge layout did every five seconds.

Known cosmetic limits in ultrawide: full-screen pictures are stretched (the
intro/attract videos and the Pac-Man loading screen); menu tickers, the
honeycomb pattern and some lines end at the 16:9 boundary.

## Save data (`src/save_fixes.cpp`)

Saves go to `Documents\rr6_recomp`. Save calls are logged with a `[save]` prefix.

1. The game only accepts ERROR_FUNCTION_FAILED as "no save yet"; the SDK
   returned ERROR_PATH_NOT_FOUND, so the game reported the hard drive as
   unreadable and never created a save. Fixed (confirmed on Windows).
2. **"Game Data is corrupted" on load.** The SDK stores the save's thumbnail
   as `__thumbnail.png` inside the save folder. The game lists that folder and
   takes the first file as its save data (its data file gets a new encoded
   name on every save). When the thumbnail is listed first, the load fails.
   Reproduced on the Linux rig: same folder, thumbnail first -> corrupted;
   thumbnail deleted -> loads. On Windows the listing is alphabetical, so it
   only strikes for some file names, i.e. now and then. Fix: the thumbnail is
   no longer stored (`rr6_save_thumbnail`), and the launcher deletes old ones.

Still to confirm on Windows: progress survives a restart; saving after a race.

## Known issues

- **Pipeline cache growth (Direct3D 12): fixed, confirmed on Windows.** During
  a race the game sets a different
  slope-scaled depth bias (`D3DRS_SLOPESCALEDEPTHBIAS`) for about 13 draws per
  frame, all with one shader pair (VS 9AFE6F56B57E8F8D / PS 699D9C919C528F8D).
  The SDK's Direct3D 12 backend bakes that number into the pipeline and stores
  every pipeline to rebuild them at the next start. Measured in
  `logs\run-debug.log` of 2026-10-06: no new pipelines in the menus, then about
  770 per second from the moment the race starts. The cache copied by that run
  holds 273,327 pipelines, 273,202 of them this shader pair, different from
  each other in one field only, `depth_bias_slope_scaled` (0.25 to 74,475;
  `python tools/xpso_stats.py logs/4E4D07D3.rtv.d3d12.xpso`).
  `src/depth_bias_fix.cpp` now rounds the value in the game's render-state
  setter (sub_82255CD8, the only code that writes those registers) to eight
  values per power of two, rounding up. On the Linux rig, four and a half
  minutes into a race: set 2,573 times, 1,598 different values asked for, 23
  passed on, and 22 different values in the register copy when it is sent to
  the GPU. What the rig cannot show is the Direct3D 12 side (Vulkan sets depth
  bias dynamically and never had the problem) or whether the rounding is
  visible. Windows, 2026-10-06 11:21, three minutes including a race
  (`logs\run-debug.log`): the value was set 73,929 times, 54,486 different
  values asked for, 103 passed on; 199 pipelines were created in the whole
  run, 102 of them for this shader pair, where the build before created about
  770 per second; the stored cache is 14 KB instead of 19.7 MB; no visual
  difference was noticed while playing. `rr6_quantize_depth_bias = false`
  switches the rounding off.
  The launcher still deletes a cache file above 4 MB, which gets rid of the
  one left by earlier builds.
  (An earlier note here said the growth already happened in the main menu and
  that the setter could not be the source. Both were wrong: the setter is
  simply not called outside races, which is all the rig had been used to check
  at that point.)
- No sound output device: the game dereferences a null pointer shortly after
  audio initialisation. The launcher warns; there is no in-game guard.
- Not yet tested: every track and car, ending videos, long sessions, AMD and
  Intel graphics.

## What the manifest had to be told

`rr6_recomp_manifest.toml` holds every manual fix:

- 7 small leaf functions that are only reached by tail-call thunks.
- 36 functions that are only reached through pointers (vtable slots, callback
  tables, the static-constructor table). Without these the first run died with
  `Call to invalid or unregistered function at guest address 0x8232EF30`.
  `tools/find_orphan_targets.py` finds them; re-run it after any manifest change.
- `setjmp` (0x8230E2F0) and `longjmp` (0x8230E1D0), used by the bundled libpng
  and libjpeg error paths.
- Four mid-asm hooks for the projection aspect (see Ultrawide).

## Fixes outside the manifest (`src/`)

- `dispatch_fixes.cpp`: two functions that tail-call through a table of function
  pointers were compiled as a trapping `switch` (crash 1: illegal instruction).
- `host_thread_fixes.cpp`: the SDK never seeds the floating-point control word on
  its audio-worker and graphics-interrupt threads, so recompiled code unmasked FP
  exceptions there (crash 2: float inexact result in the audio mixer).
- `fp_guard.cpp`: safety net for the same problem elsewhere; logs to
  `logs\fp-guard.txt`.
- `lod_bias_fix.cpp`: sets the game's texture LOD bias to zero on Linux. The
  SDK's Vulkan shader translator reads a texture's exponent bias from the word
  that holds the LOD bias, so the game's bias of -1.0 on track textures
  multiplied their colour by 2^-16: a black track (`SDK-NOTES.md`, 7). Off by
  default on Windows.
- `save_fixes.cpp`, `widescreen.cpp`, `input_fixes.cpp`, `quit_prompt.cpp`: see
  above.
- `depth_bias_fix.cpp`: rounds the slope-scaled depth bias so that Direct3D 12
  does not build a new pipeline for every draw (see "Known issues").

`SDK-NOTES.md` lists the SDK-side findings in a form that can be reported
upstream.

## Debugging a crash

`run-debug.bat` (or the launcher with "Record a detailed log") saves the exit
code, a debug log and the Windows crash record in `logs\`. The crash record's
P8 value is an offset into the exe; resolve it with
`python tools/map_lookup.py out/build/win-amd64-release/rr6_recomp.map <offset>`
to get the recompiled function (`sub_<guest address>`).

## Linux test rig (how the overnight checks were done)

The same sources build on Linux against the SDK's Linux package (Vulkan
backend). With Mesa's software Vulkan driver the game runs without a GPU, at
a few frames per second: slow, but menus, saves, input and 2D layout can be
checked and screenshotted. What it needs:

- `Xvfb` with a window manager (`openbox`): without one, keyboard focus comes
  and goes and key presses are lost.
- A real-time audio sink (PulseAudio null sink, ALSA routed to it). ALSA's
  `null` device returns immediately and the audio thread then eats a core.
- `mesa-vulkan-drivers` (lavapipe), `xdotool`, ImageMagick.
- Start with `--rr6_log_input=true` and press keys until the game's own log
  shows the press arrived; at these frame rates a short key press can fall
  between two input polls.
- Do not lower the priority of the game's threads to speed up rendering: the
  loader thread starves and loading screens never finish.

Linux and Windows differ in the graphics backend (Vulkan vs Direct3D 12), so
the rig says nothing about D3D12-specific behaviour such as the pipeline cache.

## Things learned about the game

- Middleware: CRI ADX / Sofdec (Oct 2005 builds) for streamed audio and the
  `.sfd` videos, libpng, IJG libjpeg. The executable has its own `PSFD00` code
  section for the Sofdec decoder.
- Audio goes through the early XAudio render-driver interface plus 19 kernel
  XMA calls, not XAudio2.
- 193 kernel/XAM imports; the SDK runtime provides a symbol for every one.
- 2D pipeline: display lists recorded by sub_82143D38/sub_82143D50, executed
  by sub_82143F88; float primitives use the scale block at 0x8237325C; the
  game's D3D device pointer is at 0x824F0378; XInputGetState is sub_8224C9C0.
- Save file: `save:\<encoded name>`; the name changes on every save and the
  load picks the first file in the package.
- Networking (15 `NetDll_*` imports) and Xbox Live UI calls are present; online
  play is out of scope.

## Layout

    rr6_recomp_manifest.toml   codegen configuration and all manual fixes
    CMakeLists.txt, CMakePresets.json, src/   host application
    generated/default/         recompiled C++ (regenerated by the build; do not edit)
    config/                    settings templates (16:9 default, 3440x1440 ultrawide)
    launcher/                  launcher source, icon and build script
    RR6 Launcher.exe           the launcher (prebuilt)
    gamecontrollerdb.txt       SDL community controller mappings
    package/                   static files of the tester package + make-package.ps1
    linux/                     Linux start script, READMEs, bug-report and packaging scripts
    build-linux.sh             Linux build (game binary and launcher/rr6-extract)
    make-tester-package.bat    builds ..\dist\RidgeRacer6-PC-TestBuild-NN.zip
    SDK-NOTES.md               findings to report to the SDK
    analysis/                  import list, pointer-target report, unpacked image
    logs/                      build, codegen and run logs
    build-windows.bat, run.bat Windows build and launch
    build-and-test.bat, run-debug.bat   rebuild + diagnostic launch
    use-ultrawide.bat, use-16x9.bat     copy a settings template into place
    ../tools/                  extract_xiso.py, xexinfo.py, xex_unpack.py, ppcdis.py,
                               find_orphan_targets.py, map_lookup.py, xpso_stats.py, ...
    ../tools/linux-rig/        scripts for the Linux software-rendering test rig
    publish/                   the public repository's front page, build guide, licence, ignore rules
    ../github-source/          clean, source-only copy for publishing (tools/export_source.py)
    ../sdk/win-amd64/          ReXGlue SDK v0.10.0 for Windows (prebuilt)
    ../game/                   extracted disc contents

## Building on Windows

Needs the Visual Studio 2022 C++ build tools (which bring CMake and Ninja) and
LLVM/Clang 18 or newer. `build-windows.bat` finds both by itself, so it can be
double-clicked; its full output goes to `logs\build-windows.log`. On a fresh
checkout, where `generated\default` does not exist yet, it first runs
`rexglue codegen` on the manifest (that step has not been exercised on Windows:
this tree has had its generated code from the start).

## Tester package

`make-tester-package.bat` assembles `..\dist\RidgeRacer6-PC-TestBuild-NN.zip`
from the current Windows build: launcher, game executable and runtime DLLs,
default settings, controller database, ISO extractor, bug-report collector,
fallback `.bat` starters, README and licences. It refuses to package anything
that looks like game data and copies the linker map next to the zip (keep the
map private: it resolves crash offsets from testers' reports).

Testers start `RR6 Launcher.exe`, press "Choose disc image..." and pick their
own ISO; the launcher copies the game files out of it and refuses any
executable other than the USA one (see "Launcher"). `PUT-ISO-HERE` and
`tools\prepare-game.ps1` remain for "Play without the launcher.bat".

How the copy was checked: `disc_image.cpp` was compiled natively and run on the
real 7.8 GB disc image: the version check passed and all 45 files came out
byte-identical to the ones in `..\game`. Under Wine the launcher itself was
driven through choosing an image, the progress display, Stop and
carrying on, closing the window mid-copy, a file that is not a disc image, an
image of the wrong game, "Copy game files again...", and Play afterwards, on a
working copy of the disc image rebuilt from the real directory table and the
game files. It has not been run on Windows yet.

`dist\RidgeRacer6-PC-TestBuild-01.zip` predates the save fixes, the launcher
and ultrawide: do not hand it out any more.

Builds so far (all in `..\dist`, each with its private `.map`):

- 01: predates the save fixes, the launcher and ultrawide. Superseded.
- 02: game executable from before `src/depth_bias_fix.cpp`. Superseded.
- 03: game executable built 2026-10-06 11:21 with the depth-bias fix; restyled
  launcher with the disc-image chooser. Run on Windows from the zip: launcher,
  disc-image chooser and game reported working as intended.
- 04: the same programs as 03 (identical game executable, runtime and
  launcher). Changed: `tools\collect-report.ps1` now replaces the Windows user
  name in file paths (`\Users\<name>`) in the copies of the logs that go into
  `bug-report.zip`, so a report can be attached to a public issue; the README
  has a short safety note. The script change was checked with PowerShell 7 on
  Linux, not on Windows.
- 05: game executable built 2026-10-06 17:18 with the quit question
  (`src/quit_prompt.cpp`), played on Windows from the build folder (see
  "Controls"). The launcher differs from 04's in one line of text (Esc instead
  of Alt+F4); it was looked at under Wine, and this zip itself has not been
  run on Windows. Published name: `RidgeRacer6-PC-v0.1.1.zip`.

## Linux and Steam Deck packages (`linux/`, `build-linux.sh`)

Made on request after the first release; first packages on 2026-10-06 as
`..\dist\RidgeRacer6-Linux-TestBuild-01.tar.gz` and
`RidgeRacer6-SteamDeck-TestBuild-01.tar.gz`. **Run on real hardware once so
far: build 02 on a Steam Deck (2026-10-07), see "First run on a Steam Deck"
below. Build 03 has only been run on the software-rendering rig.**

**The program.** The same sources, built by `build-linux.sh` (Clang 18, CMake,
Ninja) against the SDK's Linux package. On Linux the SDK is two shared
libraries, `librexruntime.so` and the graphics plugin `librexgpu-xenos.so`
(Vulkan), which go into the package next to `rr6_recomp`. What the three need
on the machine, as read from the binaries: glibc 2.35, `GLIBCXX_3.4.32` (the
C++ library of GCC 13.2; this comes from the SDK's prebuilt libraries and is
what rules out Ubuntu 22.04 and Debian 12), libX11, libX11-xcb, libxcb,
libwayland-client, and a processor with SSE4.1. Vulkan and the sound system
are loaded at run time. Save data and the shader cache go to
`~/.local/share/rr6_recomp`.

**The disc-image tool.** `launcher/rr6_extract.cpp` is a command-line face for
`disc_image.cpp`, the code the Windows launcher copies the game files with:
same version check, same markers in the game folder, progress as text. Linked
statically. Run on the real disc image on Ubuntu 22.04: 45 files, identical to
the copy the Windows side made.

**The start script.** `linux/ridge-racer-6.sh` stands in for the launcher:

- checks the processor and, with `ldd`, that the system's libraries are new
  enough, and says which are not;
- first start: takes the disc image from `--iso`, from an `.iso` put into the
  package folder, from a file-chooser window (kdialog or zenity), or from a
  path typed into the terminal, and runs the disc-image tool. Started from a
  file manager it opens itself in a terminal window for this;
- first start: writes `bin/rr6_recomp.toml` with the Windows launcher's
  "Automatic" rules (render size from the screen height, a wide screen filled),
  the screen size taken from `xrandr` or `xdpyinfo`. On a Steam Deck (the
  package's `steam-deck` marker file, or the machine's name Jupiter / Galileo)
  it writes 1280x720 with bars instead;
- starts the game through X11 (`SDL_VIDEO_DRIVER=x11`; `--wayland` leaves the
  choice to SDL, untested), and without Steam's library folders if the library
  check only passes without them;
- watches the log while the game runs. On Linux a fault in the game's own code
  does not end the program: the SDK logs "Unhandled guest access violation"
  and the same fault repeats without end, at full processor load, with a
  frozen picture, and the log (5 MB files, ten kept) loses the cause within a
  minute. Seen with the stand-in disc image below. The script keeps the 400
  lines up to the first fault in `logs/fault.txt` and stops the game.

`linux/collect-report.sh` packs logs, settings and a description of the system
into `bug-report.tar.gz`, with the user name, home folder and host name
replaced. `linux/make-package.sh <n>` builds both archives and keeps the
unstripped binary next to them as the private symbols file.

**The two packages** hold the same files except for the README and the
`steam-deck` marker. Layout: `ridge-racer-6.sh`, `README.txt`, `BUILD.txt`,
`bin/` (program, the two libraries, `rr6-extract`, `gamecontrollerdb.txt`, the
settings once written), `tools/collect-report.sh`, `licenses/`; `game/` and
`logs/` appear on the first start.

**Checked** (all on the software-rendering rig unless said otherwise):

- first start from a "file manager" (no terminal): terminal window opens, the
  file chooser picks the image, copy, settings, game starts;
- first start in a terminal with the image in the package folder (a name with
  spaces), Steam Deck package: Deck settings written;
- these two used a stand-in image: the real disc's layout and real
  `default.xex`, every other file empty. The game then faults, which is how
  the endless-fault behaviour and the watchdog were seen;
- with the real game files: start from both packages, the quit question by
  keyboard and by the Back + Start hold, exit status 0, windowed start;
- the bug report's contents; the "system too old" message on a real Ubuntu
  22.04; `build-linux.sh` and `make-package.sh` from a fresh copy of the
  repository (recompile, build, package).

**Not checked:** build 03 on any real graphics card or on a Steam Deck,
Steam's Game Mode, the "started from outside Steam" notice and
`steamos-add-to-steam` on a real Deck, a Wayland desktop, real sound output,
controllers on Linux, KDE's kdialog chooser, terminal programs other than
xterm.

**First run on a Steam Deck (build 02, 2026-10-07).** Two reports:

1. *The track is very dark, as if textures were missing.* The rig showed the
   same thing, and had done so in every race since the first Linux run; it had
   been taken for a side effect of software rendering. It is the SDK fault
   described under `lod_bias_fix.cpp` above. How it was found, for the next
   graphics problem: RenderDoc 1.36 (the Linux tarball from renderdoc.org;
   `renderdoccmd vulkanlayer --register --user`, then
   `renderdoccmd capture -c <file> ./rr6_recomp ... --vulkan_sparse_shared_memory=false`,
   F12 held for several seconds on the rig). A capture is the time between two
   presents, which is not one game frame, so take several and use a large one.
   `qrenderdoc --python script.py` runs analysis scripts without the window
   being used (answer its first-run question once): the list of draws with
   their render targets, pixel history for one road pixel, then the shader
   debugger on the draw that wrote it. The debugger refuses the SDK's shaders
   ("Unsupported capability RoundingModeRTE") until the float-controls
   capability, extension and execution modes are cut out of a copy of the
   SPIR-V and the copy is put in place with `ReplaceResource`. Build 03 on the
   rig: the first track looks like the Windows screenshots.
2. *Y selects in the menus and nothing accelerates.* The game had been started
   by a double click in Desktop Mode, as the README then said. There the
   Deck's buttons are a keyboard and mouse (Steam's desktop layout: Y is
   Space, A is Enter, the triggers are mouse buttons), and the game's keyboard
   layout has Space as A. Not a fault in the game: under Steam the Deck is an
   Xbox controller. Since build 03 the start script stops on a Deck when it
   was not started by Steam (`SteamGameId` and similar variables, or
   gamescope), explains, and offers "Add to Steam" (`steamos-add-to-steam`),
   "Start anyway" (remembered in `bin/.outside-steam-ok`; `--outside-steam`
   does the same for one start) or closing. Untested on a Deck.

## Publishing the source

`python tools/export_source.py <folder>` copies the publishable part of the
project (own source, scripts, notes; about 80 files, 1 MB) into a clean folder
laid out as the public repository: the project folder is the top level,
`tools/` moves inside it, these notes go to `docs/`, and the public-facing
files (`README.md`, `BUILDING.md`, `RELEASE_NOTES.md`, `LICENSE`, notices,
ignore rules, a workflow that builds the launcher) come from `publish/`. It
refuses executables, game files and anything large. `..\github-source` is such
a folder. Never published: `generated\default`, `analysis\default.bin`,
`..\game`, `..\sdk`, `..\dist`, `logs` (they contain the Windows user name) and
any executable.

## Next steps

1. Hand out build 03 and collect reports (other GPUs, long sessions).
2. The rest of the checklist: save and reload, keyboard, PlayStation pad.
3. Wider play-testing: all tracks, videos, long sessions, other GPUs.
4. Report the SDK findings upstream (`SDK-NOTES.md`).
5. Get the Linux packages run on real hardware: a desktop with a graphics card
   and a Steam Deck. First things to learn: does it start, at what speed, and
   does anything look wrong under a real Vulkan driver.
