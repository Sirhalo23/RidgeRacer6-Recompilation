# Release notes

Windows and Edge warn about every release zip, because the programs are not
code-signed and a new file has no download history. The README says what to
do: [If Windows warns about the download](README.md#if-windows-warns-about-the-download).
Each release page has the zip's SHA-256 in `SHA256SUMS.txt`.

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
