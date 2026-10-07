# Release notes

Windows and Edge warn about every release zip, because the programs are not
code-signed and a new file has no download history. The README says what to
do: [If Windows warns about the download](README.md#if-windows-warns-about-the-download).
Each release page has the zip's SHA-256 in `SHA256SUMS.txt`.

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
