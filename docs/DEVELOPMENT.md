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

## Achievements (`src/achievements.cpp`, `src/unlock_sound.cpp`, `src/overlay_input.cpp`)

The game has 36 achievements, 1000 gamerscore. The SDK already did most of
the work: it reads names, descriptions and icons out of the game's program
file (the title resource at 0x82560000), has a handler that records an unlock
when the game writes one (the game imports XMsgStartIORequest, the call such a
write goes through; see "When the game awards" below), and keeps unlocks
in `<user data>\achievements\4E4D07D3.toml`. It also had a pop-up and a list
window (F7) of its own. Ours replace both (`CreateAchievementsOverlay` returns
nothing, `CreateAchievementNotificationDialog` returns ours):

- **Pop-up:** bottom centre, the icon in a circle, "Achievement unlocked",
  gamerscore and name, about five seconds. With it a sound: `achievement.wav`
  next to the launcher or next to the program if the player put one there,
  otherwise `sounds\achievement.wav`, an original chime
  (`tools/make_chime.py`, copied next to the program by the build). Windows
  plays it with `PlaySound`, Linux through the SDL inside the SDK's runtime
  library (on Windows the runtime does not export SDL). The Xbox 360's own
  unlock sound is not shipped; a player who wants it supplies the file.
- **List:** F7 (`rr6_achievements_key`), or Y from the quit question, which is
  the way to it with a controller. Scrolls with D-pad, stick, arrow and page
  keys, mouse wheel; B or Esc closes. Secret achievements (those the game's
  data does not flag as "show when locked") say nothing until unlocked.
- **Online only:** 15 need Xbox Live play: International Match, the 50 / 100 /
  200 online victories, the five Messages from Reiko, the five machine
  collections and All Machines (435 gamerscore). That list is from players'
  guides (the Japanese achievement wiki gives the number of online battles
  each collection needs; a second guide agrees on the messages), not from the
  game's data. They are shown apart and progress is counted against the other
  21 (565). Nothing prevents one from unlocking.
- **For the launcher:** at start-up and on every unlock the game writes
  `<user data>\achievements\list.txt` (one tab-separated line each: id,
  gamerscore, online only, secret, unlock time, image, name, both
  descriptions) and the icons as PNG files (`icons\<image id>.png`, copied out
  of the game's program in memory). The launcher's Achievements page reads
  those; until the game has run once it says so. It also has a button that
  plays the sound.
- **Input** for the list and the quit question is shared
  (`overlay_input.cpp`): one keyboard listener ahead of the SDK's, and the
  controller as the game reads it. Controller answers therefore only arrive
  while the game is reading the controller, which it does not do during some
  loading screens.
- `rr6_preview_achievement = N` shows the pop-up for achievement N a few
  seconds after start without unlocking anything; `rr6_achievement_sound =
  false` silences it.

Checked on the Linux rig: the pop-up by preview and by a real unlock made
through the SDK (a rig-only switch in `rig_probe.cpp`), the unlock file, the
list by F7 and by Esc then Y, scrolling by keys and by the stick, closing with
B without the game seeing it, the sound (recorded from the rig's sound output:
the chime's three notes are in it), the files for the launcher. Launcher page:
under Wine, with the rig's files.

On Windows (2026-10-07): "360!" was earned by playing (with the executable
of test build 05, so with the SDK's own pop-up; `XGIUserWriteAchievements:
id=1` in `logs\run.log`). The achievements code itself was first built on
Windows later that morning; the build before had failed on a local variable
named `small`, a macro in the Windows headers (`tools/check_windows_names.py`
now looks for such names, and the workflow runs it). With that build: the
icons and the list were written, the launcher's Achievements page showed
them, the game quit through the quit question. **Not yet seen on Windows:**
the new pop-up with its chime, and the list in the game. Test build 06 /
v0.1.2 is this build.

**When the game awards.** Not at the moment something is done, but in the
save sequence after a race. For "360!": the race update (sub_820F1E88) sets
bit 4 of the player's flags (+740) once the car's spin counter (+652) is not
zero; the code that closes a race (sub_821F5910, sub_821F2BC0, and the result
evaluators sub_821F1230 and sub_821A5970) then marks achievement index 0 as
earned in the achievement manager at 0x824939A0 (sub_82129818: 36 records of
20 bytes, byte 16 = earned, byte 17 = written); and state 20 of the save
sequence (sub_8218A4D8) calls sub_82129868, which sends one
`XUserWriteAchievements` request (XMsg 0x000B0008, through sub_8222C638) per
earned record, for the user index kept at 0x823B0C34. A race that is left
before the finish therefore gives nothing, as on the console. First report
from Windows (2026-10-07): a 360 spin, no pop-up; the log of that session has
no save after the race and no `XGIUserWriteAchievements` line, so the race
was not finished. The path from the manager on was then run on the rig
(`rig_game_award_after` in `rig_probe.cpp` marks index 0 and calls the write
step): user index 0, the SDK logged `XGIUserWriteAchievements: id=1` and
"Achievement unlocked", the request completed with result 0.

## Downloadable content (`src/dlc_install.cpp`, the launcher's DLC page)

Asked for in issue #2. The SDK already does everything the game needs for
content that has been unpacked into its folder layout under the user data
folder. It also has a routine that unpacks a console package into that layout
(`ContentManager::InstallContent`), which the first version used; since
2026-10-07 this file does the unpacking itself, because that routine trusts
the inside of a package (see "Outside review" below and SDK-NOTES section 8).

- `rr6_recomp --rr6_install_content="<file or folder>|..."` installs and
  leaves. `Rr6RecompApp::LaunchModule` is overridden for it: by then the
  executable is loaded, so the kernel state knows the title ID, and nothing
  of the game has started. It leaves through `window()->RequestClose()`, the
  SDK's own way out (which ends in a hard exit); ending the message loop with
  `QuitFromUIThread` instead hangs in the SDK's teardown.
- Each file's package header is read first
  (`StfsContainerDevice::ReadPackageHeader`). Accepted: title ID 4E4D07D3,
  content type 2 (marketplace content), an STFS volume. Everything else is
  refused with a reason when the file was named directly. A folder is
  searched with the folders inside it (six levels, 50,000 files and folders
  at most), and whatever in it is not content for this game is passed over
  silently, so the whole copied `Content` folder can be given. A search that
  hit a limit or could not open a folder says so in the result.
- Then the package's own file list is read (the SDK's STFS layout rules,
  every read checked against the end of the file) and every entry checked
  before anything is written: plain names only (no separators, no `.` or
  `..`, no Windows device names, nothing Windows would alter), folders before
  what is in them, no name twice in a folder, every block of every file
  inside the package. A package that fails is refused with the reason.
- The files are unpacked into `<user data>/dlc-staging/<package>` with every
  read, write and close checked, then moved into place. A copy installed
  earlier is moved aside first and put back if anything fails; the `.header`
  record is written last (through `ContentManager::WriteContentHeaderFile`)
  and its size checked.
- Results go to the file named by `--rr6_install_result` (the launcher and
  the Linux script pass `logs/dlc-install-result.txt`; without it,
  `dlc-install-result.txt` in the user data folder): one line per file,
  `installed|refused|failed`, a tab, the name or reason, a tab, the file
  name; the last line is `done` with the three counts. The file is written
  whole and renamed into place; the launcher and the script treat a file
  without the first line or the `done` line as an install that stopped part
  way. Paths are split on `|`, which a Windows file name cannot contain.
- `dlc-installed.txt` (folder name, tab, shown name) is rewritten at every
  start and after an install. The launcher's list is the folders that exist,
  named from that file.
- Layout on disk: `<user data>/0000000000000000/4E4D07D3/00000002/<package>/`
  and `.../4E4D07D3/Headers/00000002/<package>.header` (the content record
  plus the licence bits taken from the package).
- Launcher: page 3, "DLC" (Troubleshooting moved to 4). It runs the game
  program with `--fullscreen=false`, waits, and reads the result file. The
  launcher is now version 1.3.
- Linux: `ridge-racer-6.sh --install-dlc PATH` does the same and prints the
  result.
- The DLC folder (asked for in issue #2): the packages ship an empty `DLC`
  folder next to `bin`. `AddContentFromDlcFolder`, called from `LaunchModule`
  at every normal start, searches it (and `<game files>/DLC`, or the folder
  named by `rr6_dlc_folder`; `none` switches it off) and installs files that
  are new or changed, with the same checks. What was added from there is
  kept in `<user data>/dlc-folder-added.txt` (name, size, time), so an
  unchanged folder costs a directory listing; content whose installed folder
  was deleted is added again. Refused files are only logged. Both packaging
  scripts now refuse any file that starts like a content package (`LIVE`,
  `PIRS`, `CON `), so one left in `package/DLC` cannot ship.

Tested with `tools/make_test_content.py`, which writes a stand-in package
(`--break KIND` writes one of nine doctored or damaged ones; all nine are
refused and nothing is written outside the content folder, where the SDK's
routine wrote the `..` package's file next to the package folder). The
unpacked files are byte-identical to what the SDK's routine produced, and so
is the `.header` record apart from the SDK's uncleared padding. Earlier, with
the SDK's routine: a stand-in package
(made-up files, real container layout, nothing from any game), two packages
installed from a file and from a folder with a space in its name, with the
contents byte-identical afterwards; another game's package, a save, a random
file and a missing file refused; the game then reports "added 2 items" when
it lists content at the main menu, and runs on normally. The launcher page
was run under Wine against a stand-in for the game program, to check the
command line and the result handling.

First real packages (the owner's, 2026-10-07, Windows, built against the
fork's SDK): five `LIVE` packages of 32 to 59 MB, each one `.wav` file,
described in their headers as additional background music ("01: Highride",
"02: Warp Trooper", "03: Bassrider", "04: Pulse Phaze", "33: Mars Landing").
All five installed, and the launcher listed them. The game uses them: under
"Change BGM" on the screen before a race they appear as a "Player Disc" with
the five tracks, numbered 1 to 5, and one was chosen and heard during a race.
So the content list, the licence bits and the reading of a package's file
all work with real content on Direct3D 12 / Windows. Not tried: the
downloadable car designs (36, free, per the fan wiki), real content on Linux,
and the subfolder search on Windows. The game creates the content list at the main menu, after the
save has loaded; with the stand-ins it opened nothing afterwards.

## Language (`src/language.cpp`)

The disc has English, Japanese, German, French, Spanish and Italian. The game
asks `XGetLanguage`, which in the SDK always answers English (SDK-NOTES
section 10). `src/language.cpp` defines `__imp__XGetLanguage` in the game
program, which the game's code then calls instead of the runtime's, and
answers from the SDK's `user_language` setting (1 English, 2 Japanese,
3 German, 4 French, 5 Spanish, 6 Italian; anything else gives English). The
launcher's Display tab has a Language choice that writes `user_language`;
the Linux script has `--language NAME`, which writes the same line and keeps
it when the settings are written afresh.

Checked on the Linux rig (2026-10-08): the loading screen's line reads
"Drücke die START-Taste, um Ridge- Racer zu starten." with 3 and is in
Japanese with 2; the log shows the game asking once at start. Not checked:
French, Spanish, Italian, menus and races in each, and the Windows build
(the definition replacing the runtime's relies on the linker taking a symbol
from an object file before the runtime's import library). Asked for in
issue #2.

## Frame rate (`src/frame_stats.cpp`)

The SDK's F3 window shows "Guest: N FPS (M ms)" only when the program hands
it a provider (`ReXApp::SetGuestFrameStats`); until 2026-10-09 ours did not,
so F3 was an empty box for everyone (issue #7 found it). The game ends every
frame in sub_82259A28, the only caller of VdSwap, which is now hooked to
count frames. The F3 numbers are measured over half a second; the log gets
`[fps] N frames per second over the last 30 s, slowest frame M ms` every
30 seconds, so a bug report carries the player's real frame rate. On the
Linux rig (software rendering) it reads 2 to 3 frames per second.

## Music that loops (`src/xma_loop_fix.cpp`)

The game streams its music (and some other sounds) as XMA in blocks through
its own voice code (sub_822B02B8). For a block that repeats it calls
`XMASetLoopData(context, &loop_data)` with a 12-byte `XMA_LOOP_DATA`
(start and end as bit offsets, then count, subframe end, subframe skip).
The SDK reads that pointer as a whole XMA context (SDK-NOTES section 11),
so it took a loop count of 0 where the game asked for 255 ("for ever"), and
the track stopped after its first pass (issue #17, the main menu music).
`src/xma_loop_fix.cpp` defines `__imp__XMASetLoopData` in the game program
and writes the right fields, each context word with one atomic update so
the decoder's own changes made at the same moment are kept. The first four
calls are logged with what the SDK would have read; on the Linux rig, the
four calls on the way to a race all asked for 255 and the SDK would have
read 0 for each. `rr6_xma_loop_fix = false` gives the old behaviour back.
Not yet heard on Windows (the rig's audio output is silent).

## Windows timer (`src/timer_resolution.cpp`)

The SDK paces the guest with `Sleep(1)` polling: the vertical blank thread
(`graphics_system.cpp`) wakes every millisecond and marks the 60 Hz ticks
that have passed. On Windows 10 2004 and later a program that has not asked
for a finer timer sleeps at least 15.6 ms however short a sleep it asks
for, and the SDK does not ask (Xenia, which it comes from, does). The 60 Hz ticks
then arrive in uneven steps. The game program now calls `timeBeginPeriod(1)`
when it starts (`rr6_fine_timer`, default on) and logs how long a 1 ms
sleep took before and after: `[timer] a 1 ms sleep took 15.6 ms; asked for a
1 ms timer (granted), now 1.0 ms`. Whether this is what slows the game on
some PCs (issue #7) is not known; the log line will tell from the next
reports.

## Slow PCs: what the log now says (issue #7)

Two testers get 20-50 frames per second with the graphics card mostly idle
(steven44: i7-6700K, RTX 2060, processor about half used at full clock, GPU
29%; his latest log has about 90 s near 31 fps and then a steady 60 for seven
minutes). Sunspot77x has a laptop with a Radeon 780M built into the
processor and an RTX 4060, and lower settings barely help.

- `src/thread_stats.cpp`: every 30 s, `[threads] busiest over 30 s: ...`
  lists the six busiest threads of the process with their processor time as a
  percentage of one core (Windows: Toolhelp, `GetThreadTimes`,
  `GetThreadDescription`; Linux: `/proc/self/task`). The game's own threads
  are `XThreadNNNN` / `Main XThread`; the SDK's are named ("GPU Commands",
  "Audio Worker", "XMA Decoder", ...). A thread near 100% is the bottleneck.
  `rr6_thread_stats`, default on. On the Linux rig at the start: the game's
  threads 54% and 37%, the audio worker 33%, llvmpipe the rest.
- `src/gpu_choice.cpp`: the SDK's Direct3D 12 backend takes the first
  adapter that can run Direct3D 12 (`d3d12_adapter = -1`), on hybrid laptops
  usually the integrated one. In `OnPreSetup`, before the backend starts, the
  game program lists the adapters in the same order, asks
  `IDXGIFactory6::EnumAdapterByGpuPreference(HIGH_PERFORMANCE)` (fallback:
  most video memory) and sets `d3d12_adapter` to it when that is not the
  first. A hand-set value is kept unless it no longer names a usable adapter
  (the SDK would otherwise fail to start). F4's "Save to config" may store the
  chosen index; that is harmless for the same reason. `rr6_prefer_fast_gpu`.

On Linux the audio worker's 33% comes from the SDK's POSIX `WaitMultiple`,
which polls every millisecond instead of blocking; on Windows it uses
`WaitForMultipleObjects` and does not. Worth fixing in the fork for the
Steam Deck, not for the Windows reports.

## Sync to the display (issue #16)

The Direct3D 12 presenter shows each frame at once, without waiting for the
display (`Present(0, RESTART | ALLOW_TEARING)`), which tears. Forcing vsync
in the graphics driver makes the game's 60 Hz timer and the display's
refresh drift against each other, which shows as stutter (the two reports
in #16 had identical settings files, so the vsync came from the driver). Our SDK fork (PR #2 there,
`vsync_to_display`) presents with sync interval 1 and lets the guest's
vertical blank follow the display's (`IDXGIOutput::WaitForVBlank`) when the
display runs at a whole multiple of 60 Hz; other displays keep the timer.
On Vulkan it only selects FIFO presentation. The launcher's Display page has
"Sync to my screen", enabled only when `bin\rexruntime.dll` contains the
setting's name (fork v0.10.0.101 and later). The log says whether the
display or the timer drives the guest.

## Launcher settings and the F4 window

The SDK's F4 window writes `rr6_recomp.toml` only when its **Save to config**
button is pressed, and then writes every setting that differs from its
default. Up to launcher 1.4's first version, the launcher wrote all of its
settings at every Save or Play from what it had read when it started, and
recalculated the render size from its own Sharpness choice; a render size
set in F4 was therefore undone at the next start (issue #15). Now
`SaveAll` reads the file again, applies only the launcher settings whose
value differs from what the controls stood for when they were loaded (or
that the file lacks), keeps everything else, and reloads the controls from
the result. `LoadIntoControls` takes the Sharpness choice from
`draw_resolution_scale_y` in the file when that differs from the launcher's
own note. "Restore default settings" still writes the launcher's settings
afresh. Checked under Wine: a render size of 3 set outside the launcher
stays and shows as "3x"; `vsync` and `anisotropic_override` changed in the
file while the launcher was open survive Save; a language chosen in the
launcher is written.

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

- Rear-view mirror (`rr6_viewport_fix`, issue #12): the mirror is a second
  3D view set with D3DDevice_SetViewport (sub_82257488) inside a 2D frame.
  The frame was narrowed with the rest of the 2D layer, the viewport was not,
  so the mirror's picture was wider than its frame by the stretch factor.
  Every viewport covering only part of the 1280x720 frame is now narrowed
  around the centre by the same factor (a narrowed copy of the caller's
  D3DVIEWPORT9 goes on the guest stack). Full-width viewports (the race,
  split screen) and other surfaces are left alone. `rr6_viewport_log = true`
  logs each new viewport. Not yet seen in a race: the rig is too slow to
  drive to cockpit view (1 frame per second at 21:9).

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

## Outside review (2026-10-07)

The repository at 982e212 was given to another AI reviewer with a prompt that
asked for bugs and optimisations in a fixed report format. It found sixteen
things; each was checked against the code here before anything changed.

Fixed (branch `review-fixes`):

- DLC installer (RR6-01 to -03, -10, -12): see "Downloadable content" above.
- Linux `--install-dlc`: kept when the script reopens itself in a terminal
  (RR6-15); an empty value is an error; the exit status says whether
  everything was added.
- Disc copy (RR6-05 to -08): a link in a folder table that leads outside the
  table or back to an entry already read, and a folder whose table was read
  already, are now "the disc image is damaged" instead of being skipped (a
  copy of what could be reached would have looked complete); names ending in
  `.part`, the two marker names, Windows device names and two names that
  differ only in case are refused; cancelling works while the tree is read
  and the executable hashed. The launcher treats `.rr6-copying` as winning
  over `.rr6-ready`. `prepare-game.ps1` now creates `.rr6-copying` before it
  writes anything, so an interrupted run is not taken for a complete game on
  the next start, and it has the same limits and checks. The owner's real
  disc image (45 files, no folders) passes the stricter reader.
- Launcher settings (RR6-09): quoted values are decoded properly (escapes,
  `'literal'` strings) and settings the launcher did not change are written
  back exactly as they were. It no longer takes `user_data_root` from the
  settings file, which the game ignores (SDK-NOTES section 9).
- HUD statistics (RR6-14): counted only while `rr6_hud_stats` is on.
- CI (RR6-16): the workflow also runs for `src/` and the checker;
  `tools/check_windows_names.py` skips C++ keywords (`try` is a macro in two
  rarely used Windows headers).

Not changed:

- RR6-04, links (junctions, symlinks) already inside the game folder are
  followed by the disc copy: they can only be there if the player put them
  there.
- RR6-13, the achievement export and unlock sound run on the drawing thread:
  a short pause at an unlock at most, not the stuttering in races. Left until
  a frame-time capture points at it.
- The rarer disc-name cases of RR6-05 beyond the ones above.

None of it explains the stuttering report (issue #7).

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
- `save_fixes.cpp`, `widescreen.cpp`, `input_fixes.cpp`, `quit_prompt.cpp`,
  `overlay_input.cpp`, `achievements.cpp`, `unlock_sound.cpp`: see above.
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
- Once the save has loaded, the main menu draws a 3D scene behind itself and
  the rig drops to a frame every 4 to 16 seconds. A key held for one such
  frame can count as two presses, and a screenshot taken a few seconds later
  still shows the old state. `tools/linux-rig/p1.sh` releases the key the
  moment the log shows it and waits 40 s before the screenshot;
  `torace4.sh` does the way from "Single Race" to a race like that.

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
- removes `/dev/shm/xenia_memory_*` when no game is running, before and after
  a run: a game that is killed leaves its guest memory there (several hundred
  MB of memory each time, until the next restart);
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

## Our fork of the SDK (started 2026-10-07)

[Sirhalo23/rexglue-sdk](https://github.com/Sirhalo23/rexglue-sdk) is a fork
of the ReXGlue SDK (BSD 3-clause, so changing and redistributing it is
allowed with the notices kept). Two reasons for it: fixes in the SDK itself
instead of workarounds in `src/` (`SDK-NOTES.md` is the list to work from),
and runtime files built by us rather than taken from the SDK's download.

- Branch `rr6` is upstream's `v0.10.0` plus our changes; `FORK.md` there
  lists them. `main`, `development` and `release/*` are upstream's and stay
  untouched. Upstream's `development` was 24 commits past v0.10.0 on
  2026-10-07 (input, window and build changes; the Vulkan fault of
  `SDK-NOTES.md` 7 is still in it).
- Packages are built by the fork's own workflows (they came with the fork):
  a tag `v*` builds every platform and attaches the zips to the release of
  that tag. Our tags are `v0.10.0.100`, `.101`, ...: the SDK's version code
  only accepts numeric tags, and the fourth number keeps ours apart from
  upstream's. The nightly workflow is disabled in the fork.
- This session can push branches to the fork but not tags or releases
  (HTTP 403), so a release is made in the GitHub page: "Draft a new
  release", new tag, target `rr6`.
- First change: the Vulkan exponent-bias fix (`SDK-NOTES.md`, 7). With it,
  `src/lod_bias_fix.cpp` is not needed; it stays until a package built
  against the fork's SDK has been run on real hardware.

First builds against the fork (2026-10-07), both from release `v0.10.0.100`:

- Windows test build 07: the SDK's Windows package unpacked into
  `sdk\win-amd64` (the original moved to `sdk\win-amd64-stock`), then
  `build-windows.bat` as usual. `generated/rexglue.cmake` asks for version
  0.10.0 or newer, so 0.10.0.100 is accepted without changes. Started and a
  race driven on Windows. `rexruntime.dll` from the fork's package on
  VirusTotal the same day: 8 of 71, all the one Bitdefender-engine signature
  (`Gen:Variant.Yogi.85276`), against 28 of 71 for the stock file. A new
  file's result can still move over the following days.
- Linux and Steam Deck test build 04: built on the rig with clang 20 against
  the fork's Linux package. `rr6_zero_lod_bias` is off by default in such a
  build (`src/lod_bias_fix.cpp` looks at `REXGLUE_VERSION_TWEAK`). Same race
  as before: textured. The desktop package was started from its unpacked
  folder. The packaged programs need the same glibc and libstdc++ as build 03.
- When `build-linux.sh` finds a newer clang than the build folder was made
  with, CMake throws its cache away and then does not find the SDK. Delete
  `CMakeCache.txt` and `CMakeFiles` in the build folder and run it again.
- Both package scripts now write the SDK's version and where it came from
  into the build info file.

Building the SDK from source on Linux (Ubuntu 24.04), as done on the rig:

    git clone https://github.com/Sirhalo23/rexglue-sdk && cd rexglue-sdk
    git checkout rr6 && git submodule update --init --recursive --depth 1
    apt-get install clang-20 lld-20 cmake ninja-build autoconf libgtk-3-dev \
        libx11-xcb-dev libxss-dev libvulkan-dev libwayland-dev libwayland-bin \
        wayland-protocols libxkbcommon-dev libdecor-0-dev libasound2-dev \
        libpulse-dev libpipewire-0.3-dev libxext-dev libxrandr-dev \
        libxcursor-dev libxi-dev libxfixes-dev libxtst-dev
    cmake --preset linux-amd64 -DCMAKE_C_COMPILER=clang-20 \
        -DCMAKE_CXX_COMPILER=clang++-20 -DCMAKE_CONFIGURATION_TYPES=Release \
        -DCMAKE_DEFAULT_BUILD_TYPE=Release -DCMAKE_CROSS_CONFIGS= -DCMAKE_DEFAULT_CONFIGS=
    ninja -C out/build/linux-amd64 rexgpu-xenos     # builds rexruntime as well

Clang 18 does not do: with GCC 13's standard library it has no
`std::expected`, which the SDK uses. The build is 843 steps and took about
8 minutes on two cores. The results are `out/linux-amd64/Release/
librexruntime.so` and `librexgpu-xenos.so`.

Trying a rebuilt runtime with an existing game build needs no relinking: put
the two files in a folder that comes first in `LD_LIBRARY_PATH`, and replace
the copy of `librexgpu-xenos.so` next to the game program (the plugin is
loaded from there). `/proc/<pid>/maps` shows which files were really loaded.

## Next steps

1. Hand out build 03 and collect reports (other GPUs, long sessions).
2. The rest of the checklist: save and reload, keyboard, PlayStation pad.
3. Wider play-testing: all tracks, videos, long sessions, other GPUs.
4. Report the SDK findings upstream (`SDK-NOTES.md`), and fix them in our
   fork as they come up; the Vulkan texture fix is the first.
5. Get the Linux packages run on real hardware: a desktop with a graphics card
   and a Steam Deck. First things to learn: does it start, at what speed, and
   does anything look wrong under a real Vulkan driver.
