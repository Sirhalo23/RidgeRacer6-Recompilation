# Release notes

Windows and Edge warn about every release zip, because the programs are not
code-signed and a new file has no download history. The README says what to
do: [If Windows warns about the download](README.md#if-windows-warns-about-the-download).
Each release page has the zip's SHA-256 in `SHA256SUMS.txt`.

Some antivirus programs flag `bin\rexruntime.dll`, the ReXGlue SDK's runtime
file: 28 of 71 scanners for the SDK's own build of it in v0.1.0 and v0.1.2,
8 of 71 for the build from our fork in v0.1.3. The programs built from this
repository are not flagged. Details and how to check the file:
[If your antivirus flags rexruntime.dll](README.md#if-your-antivirus-flags-rexruntimedll).

## Not released yet

- Music loops again: tracks that repeat, such as the main menu's, stopped
  after their first pass (issue #17). The game tells the console's audio
  decoder where a loop starts and ends, and the SDK read that information
  from the wrong place, so it saw "play once" where the game said "repeat".
  The game program now passes it on correctly.
- Ultrawide: the rear-view mirror's picture fits its frame (issue #12). The
  mirror is a small 3D view inside the picture; it is now narrowed like the
  HUD around it.
- Windows: the game asks Windows for a 1 ms timer when it starts. Without
  it, the short waits that pace the game last 15.6 ms on current Windows, so
  the 60 Hz tick arrived in uneven steps. The log notes how long a short
  wait took before and after (issue #7).
- Launcher 1.5: "Sync to my screen" on the Display page, for a picture
  without tearing (issue #16). It needs the runtime from our SDK fork's
  v0.10.0.101, which also lets the game's 60 Hz clock follow a 60 or 120 Hz
  screen so frames are not repeated or dropped; with an older runtime the
  box is greyed out.
- Laptops with two graphics chips (one built into the processor, plus a
  graphics card) now use the graphics card. The game took whichever one
  Windows listed first, usually the slower built-in one. The log names the
  adapters and which one is used. `rr6_prefer_fast_gpu = false` turns this
  off; a `d3d12_adapter` set by hand is kept.
- The log gets a line every 30 seconds with the busiest threads and how much
  of a processor core each used, so a report from a slow PC shows what holds
  the game back (issue #7).

## v0.1.5 and Linux test build 06 (2026-10-09)

Inside the Windows zip it calls itself "test build 09". The Linux release has
the desktop archive only for now; the Steam Deck package of build 06 follows
once it has been tried on a Deck.

- Language: the game can now be played in any of the disc's six languages
  (English, Japanese, German, French, Spanish, Italian): the launcher's
  Language setting on the Display tab, or `--language` on Linux. Until now the
  game was always told to use English. Asked for in issue #2.
- A `DLC` folder comes with the game: content files put there are added each
  time the game starts (only new or changed ones). Asked for in issue #2.
- Adding downloadable content is safer: each package's list of files is
  checked before anything is written (a doctored package could otherwise
  write files outside the content folder), files are unpacked with every
  write checked into a separate folder and only then moved into place, and a
  package added again replaces the old copy only once the new one is
  complete. A folder search that could not look everywhere says so.
- Copying the game from the disc image refuses a damaged image instead of
  copying the part that could be read, and an interrupted copy made with
  "Play without the launcher" is no longer taken for a finished one.
- The launcher (version 1.4) keeps settings changed elsewhere. It used to
  write all of its settings again at every Save or Play, from what it had read
  when it started, so a render size chosen in the game's F4 window was undone
  at the next start (issue #15). It now reads the file again first and writes
  only what was changed in the launcher; the Sharpness choice follows the
  render size in the file. Lines it does not look after are kept exactly.
- F3 shows the frame rate. The F3 window was an empty box: the game program
  never gave it the numbers. The log also gets a line every 30 seconds with
  the frame rate and the slowest frame, so bug reports show how fast the game
  ran.
- Linux: `--install-dlc` is no longer lost when the script reopens itself in a
  terminal on the first start.
- Checked on Windows before release: German and Japanese in the menus (in
  Japanese the menu titles and course names stay in English, as the game's own
  Japanese text has them), the frame rate in F3 during a race (60), and five
  music tracks added through the DLC folder and listed under Change BGM. On
  Linux, with software rendering only: German and Japanese, the DLC folder
  with stand-in packages, and the `[fps]` log lines.
- The runtime files are the same as in v0.1.3 and v0.1.4 (our fork of the
  SDK, v0.10.0.100).

## v0.1.4 and Linux test build 05 (2026-10-07)

Inside the Windows zip it calls itself "test build 08".

- Downloadable content: your own content packages can be added. Windows: the
  launcher's new DLC page (the launcher is now version 1.3). Linux and Steam
  Deck: `./ridge-racer-6.sh --install-dlc FILE-OR-FOLDER`. The game program
  checks each package (it must be downloadable content for this game),
  unpacks it into the save data folder and closes again. A folder is searched
  together with the folders inside it.
- On Windows, five downloadable music tracks were added and then chosen and
  played in a race ("Player Disc" under Change BGM). The downloadable car
  designs are untried, and on Linux only stand-in packages have been through
  it.
- The runtime files are the same as in v0.1.3 (our fork of the SDK,
  v0.10.0.100).

## v0.1.3 (2026-10-07)

Inside the zip it calls itself "test build 07". The game is the same as in
v0.1.2.

- `bin\rexruntime.dll` and `bin\rexgpu-xenos.dll` are built from
  [our fork of the ReXGlue SDK](https://github.com/Sirhalo23/rexglue-sdk)
  (v0.10.0.100) instead of being copied out of the SDK's download. On
  VirusTotal the zip is flagged by 8 of 67 scanners on the day it was made,
  all one Bitdefender-engine verdict; the v0.1.2 zip by 20 of 67.
- `BUILD-INFO.txt` names the SDK version and gives the SHA-256 of
  `rexruntime.dll`.

Built and run on Windows: started, one race driven.

## v0.1.2 (2026-10-07)

Inside the zip it calls itself "test build 06". The first public Windows
release since v0.1.0: v0.1.1 was prepared but not published, and what it
added is in here.

- Achievements: the game's 36, with a pop-up and a chime when one is earned
  (after the race, with the save, as the game itself does it), a list in the
  game that works with a controller (F7, or Y from the quit question), a marker
  on the 15 that need Xbox Live, and an Achievements page in the launcher. On
  Linux since test build 02.
- The quit question of v0.1.1 (below).
- The README inside the zip says how to unblock the zip so that Windows does
  not ask about each program.

On Windows so far: "360!" was earned, the launcher's page shows the list, and
the game starts and quits through the quit question. The new pop-up and its
chime have been seen on the Linux build only.

## Linux test build 04 (2026-10-07)

- The black track of builds 01 and 02 is fixed where it came from: the SDK's
  Vulkan code read a texture's brightness exponent from the wrong word. The
  fix is in [our fork of the ReXGlue SDK](https://github.com/Sirhalo23/rexglue-sdk)
  (v0.10.0.100), and this build is made with that fork's Linux package.
- Build 03's way around the fault, setting the game's texture LOD bias to
  zero, is off by default again, so distant track textures are as sharp as on
  Windows. `rr6_zero_lod_bias = true` in the settings file brings it back.

Checked with software rendering: the same race as for build 03, textured
road and scenery. Not yet run on a Steam Deck or a desktop graphics card.

## Linux test build 03 (2026-10-07)

- Fixed: the track was drawn black (road, scenery, buildings and sky; cars,
  signs and the race display were right). It happened on every machine with
  builds 01 and 02, and was reported from a Steam Deck. The cause is in the
  SDK's Vulkan code, which turns a texture's LOD bias into a brightness
  factor; the game's LOD bias is now set to zero on Linux (`rr6_zero_lod_bias`).
  Distant textures are slightly softer than on Windows as a result.
- Steam Deck: the start script notices when it was not started from Steam,
  where the Deck's buttons act as a keyboard and mouse and the game's controls
  are wrong, and offers to add the game to Steam. The Deck instructions say so
  too.

Checked with software rendering; not yet run again on a Steam Deck.

## Linux test build 02 (2026-10-06)

Linux test build 01 with the achievements work listed above. Superseded by
build 03.

## v0.1.1 (2026-10-06)

Inside the zip it calls itself "test build 05".

- Esc, or Back + Start held for a second on a controller, asks "Quit Ridge
  Racer 6?" (Enter or A quits, Esc or B goes back). Until now the only way out
  was Alt+F4, which still works.

## Linux test build 01 (2026-10-06)

First builds for desktop Linux and for the Steam Deck: the same program in two
archives, the second with the Deck's settings and instructions.

- Start script that copies the game files out of your own disc image on the
  first start, chooses settings for the screen, and starts the game.
- The quit question of v0.1.1 is in these builds.
- A script that packs logs and a description of the system for a bug report.

Experimental: run only with software rendering so far, on no real graphics
card and on no Steam Deck. Needs glibc 2.35 and the C++ library of GCC 13.2
(Ubuntu 24.04, Debian 13, Fedora 39, SteamOS 3.6, Arch) and a Vulkan driver.
There is no settings window; settings are a text file.

## v0.1.0 (2026-10-06)

First test release. Inside the zip it calls itself "test build 04".

What is in it:

- The game, running natively on Windows through Direct3D 12: menus, races,
  sound, intro videos, saving.
- A launcher that copies the game files out of your own disc image, checks that
  it is the USA version, and holds the display and control settings.
- Render resolution up to 4x, FXAA, 16x texture filtering.
- Ultrawide support with the race HUD moved to the screen edges.
- Xbox and PlayStation controllers, and a keyboard with changeable keys.
- A bug-report helper on the launcher's Troubleshooting page.

No game data is included: you need your own Ridge Racer 6 (USA) disc image.

Known limitations:

- Played on one PC only (NVIDIA RTX 3060 Ti, 3440x1440), in short sessions.
- AMD and Intel graphics, PlayStation controllers, most tracks and cars, long
  sessions and the ending videos are untested.
- Online play does not work.
- On ultrawide screens the videos and the loading screen are stretched.
- The programs are not code-signed, so Windows shows a warning on first start.
