# Release notes

## Not yet released

In the source, not yet in a release zip:

- Esc, or Back + Start held for a second on a controller, asks "Quit Ridge
  Racer 6?" (Enter or A quits, Esc or B goes back). Until now the only way out
  was Alt+F4.

## Linux test build 01 (2026-10-06)

First builds for desktop Linux and for the Steam Deck: the same program in two
archives, the second with the Deck's settings and instructions.

- Start script that copies the game files out of your own disc image on the
  first start, chooses settings for the screen, and starts the game.
- The quit question described above is in these builds.
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
