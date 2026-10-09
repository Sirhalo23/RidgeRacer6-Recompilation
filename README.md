![Ridge Racer 6 Recomp: unofficial native Windows version](docs/images/banner.png)

# Ridge Racer 6 Recomp

An unofficial, fan-made native Windows version of **Ridge Racer 6** (Xbox 360,
2005). The game's PowerPC program is translated to C++ and compiled into a
Windows executable that runs on top of the
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (from v0.1.3, our
[fork](https://github.com/Sirhalo23/rexglue-sdk) of it). Status: early test
release.

> **You need your own copy of Ridge Racer 6 for the Xbox 360** (USA disc, title
> ID `4E4D07D3`). This repository contains no game code and no game data. The
> releases contain the recompiled program and none of the game's data: tracks,
> cars, sound and video all come from your own disc image.

Not affiliated with or endorsed by Bandai Namco Entertainment, who own Ridge
Racer and its trademarks.

## Install

1. Download the latest `RidgeRacer6-PC-<version>.zip` from
   [Releases](../../releases).
2. Unzip it to a normal, writable folder (not Program Files, and do not run it
   from inside the zip).
3. Start `RR6 Launcher.exe`. Windows may warn about it; see
   [If Windows warns about the download](#if-windows-warns-about-the-download).
4. Press **Choose disc image...** and pick your Ridge Racer 6 `.iso`, wherever
   it is. The launcher checks that it is the right game and copies the game
   files out of it (about 6 GB, a few minutes, once). The image is only read and
   is not needed again afterwards.
5. Choose your settings (the defaults suit most PCs) and press **Play**.

`README.txt` in the zip has the details, including how to report a problem.

## If Windows warns about the download

The programs in the zip are not code-signed, and every new release is a file
that few people have downloaded yet. Windows and Edge warn about any download
like that, whatever is in it. The package does not install anything and does
not ask for administrator rights.

- **Edge blocks the download** ("isn't commonly downloaded"): open the
  downloads list, choose the three dots next to the file, then **Keep**, then
  **Show more**, then **Keep anyway**.
- **Before unzipping:** right-click the zip, choose **Properties**, tick
  **Unblock** and press OK. Otherwise Windows may ask again for each program
  in the folder.
- **"Windows protected your PC" on the first start:** choose **More info**,
  then **Run anyway**.

To check that the file is the one published here, compare its SHA-256 with
`SHA256SUMS.txt` on the release page. In PowerShell, with the file's name:

    Get-FileHash .\RidgeRacer6-PC-v0.1.5.zip

If your antivirus names a threat instead of giving one of the warnings above,
see the next section.

## If your antivirus flags rexruntime.dll

Some antivirus programs flag one file of the package, `bin\rexruntime.dll`.
It is the runtime library of the ReXGlue SDK, the toolkit this port is made
with: the part that stands in for the console.

**From v0.1.3 on**, that file is built from
[our fork of the SDK](https://github.com/Sirhalo23/rexglue-sdk) by the fork's
public build workflow, and far fewer scanners flag it. On
[VirusTotal](https://www.virustotal.com), on the day it was built
(7 October 2026):

| File | Flagged by | Where it comes from |
| --- | --- | --- |
| `RidgeRacer6-PC-v0.1.3.zip` | [8 of 67](https://www.virustotal.com/gui/file/fa3932dcb54baff57c327f4d22eff78d988cdf60eda67a3213c055b704033557) | the release |
| `bin\rexruntime.dll` | [8 of 71](https://www.virustotal.com/gui/file/172a80fa46f85b2c66e3b517c89c3f8a3cde298ec7dd4cfc4955ba74fb846e13) | the fork's release v0.10.0.100 |
| `RR6 Launcher.exe` | [0 of 71](https://www.virustotal.com/gui/file/d5a7e97493515bf119f6dfba7f3cd11b2743be968388728fbbf85feec048bf19) | built from this repository |
| `bin\rr6_recomp.exe` | no report of its own yet | built from this repository |
| `bin\rexgpu-xenos.dll` | no report of its own yet | the fork's release v0.10.0.100 |
| the `.bat` and `.ps1` scripts | 0 | this repository, readable as text |

The eight are one verdict, `Gen:Variant.Yogi.85276`, given by Bitdefender and
by seven products that use its engine (ALYac, Arcabit, CTX, Emsisoft, eScan,
GData, VIPRE). The zip's eight flags are the same eight. A new file's result
can change during its first days; the links show the current state. Later
releases contain the same `rexruntime.dll` until the SDK changes; the
checksum below tells you which one you have.

**v0.1.0 and v0.1.2** contain the SDK's own build of the file, copied byte
for byte out of its download (`rexglue-sdk-0.10.0-win-amd64.zip` on the SDK's
[v0.10.0 release page](https://github.com/rexglue/rexglue-sdk/releases/tag/v0.10.0)).
That one is flagged by
[28 of 71](https://www.virustotal.com/gui/file/e359209fb2b0570e693c966d4c1d99a82465d36ef70d033833fae56adb2f1b7a),
under general-purpose names such as `Wacatac.B!ml`, `Ulise` and
`Mal/Generic`, and the v0.1.2 zip by 20 of 67. Every game ported with that
SDK version ships the same file, and their downloads get the same result;
VirusTotal lists them under the file's Relations.

Neither build of the file is packed or scrambled. Both are unsigned, and both
do what a console runtime has to do and what scanners that score a file by
its ingredients count against an unknown program: the file reserves a large
block of memory and changes its protection (the console's memory), contains
decryption and decompression code (for the game's own files), imports
network functions (the console's network interface, passed on to Windows),
and reads the keyboard and controllers. None of the names above identifies
a known piece of malware; they are the labels such scanners give to a file
they score as suspicious.

That is an explanation, not a guarantee. What you can check yourself:

- Which file you have. In PowerShell, in the game's folder:

      Get-FileHash .\bin\rexruntime.dll

  v0.1.3 to v0.1.5:
  `172A80FA46F85B2C66E3B517C89C3F8A3CDE298EC7DD4CFC4955BA74FB846E13`,
  the file in `rexglue-sdk-0.10.0.100-win-amd64.zip` on the fork's
  [release page](https://github.com/Sirhalo23/rexglue-sdk/releases/tag/v0.10.0.100).
  v0.1.0 and v0.1.2:
  `E359209FB2B0570E693C966D4C1D99A82465D36EF70D033833FAE56ADB2F1B7A`, the
  file in the SDK's own zip.
- How the fork's file was made: GitHub built it from the tagged source, and
  the run is public under the fork's Actions tab. What the fork changes is
  listed in its `FORK.md`.
- The source code: the SDK's, the fork's, and this repository's, which is
  everything else in the package. [Building it yourself](#building-it-yourself)
  gives you your own launcher and game program; `rexruntime.dll` still comes
  out of an SDK package unless you build the SDK from its source as well.

If the game does not start and `rexruntime.dll` is gone from the `bin`
folder, your antivirus has removed it. In Windows Security that is under
**Virus & threat protection** > **Protection history**: open the entry,
choose **Actions**, then **Restore**. Whether to do that is your decision.

If your antivirus flags any other file of the package, please
[open an issue](../../issues) with its exact wording.

## Linux and Steam Deck (experimental)

There is a test build for desktop Linux,
`RidgeRacer6-Linux-TestBuild-<n>.tar.gz`, under [Releases](../../releases)
(marked as a pre-release). It is drawn with Vulkan. A Steam Deck package of
the same program, `RidgeRacer6-SteamDeck-TestBuild-<n>.tar.gz`, with settings
and instructions for the Deck, is being tested on a Deck and will be added to
the same release.

It is experimental. Test build 02 was tried once on a Steam Deck: it started,
but drew the track black. Builds 03 and later fix that, from 04 on in the
graphics code itself (they are made with our fork of the SDK). They have only
been run on a machine without a graphics card, with software rendering, so
expect problems, and please report what you find.

On a Steam Deck, add the game to Steam and start it from there: only then do
the Deck's buttons work as a controller.

1. Unpack the archive in your home folder.
2. Start `ridge-racer-6.sh`. The first time, it asks for your Ridge Racer 6
   `.iso` (or uses one you put into the folder), checks it, and copies the game
   files out of it.
3. The game starts. Settings are in `bin/rr6_recomp.toml`, written for your
   screen on the first start; F4 in the game changes them.

Needs a 64-bit system from 2024 or later (glibc 2.35 and the C++ library of
GCC 13.2: Ubuntu 24.04, Debian 13, Fedora 39, SteamOS 3.6, Arch) and a Vulkan
driver. `README.txt` in each archive has the details.

## Features

- **Launcher** with display settings, key bindings and the disc-image copy.
  The picture at its top is taken on your PC from the opening movie of your own
  copy; no artwork is shipped.
- **Sharper picture:** renders at up to 4x the original 720 lines, chosen
  automatically for your screen, with optional FXAA and 16x texture filtering.
- **Ultrawide:** fills screens wider than 16:9 (played at 21:9), with the race
  HUD moved to the screen edges or kept where 16:9 had it.
- **Languages:** English, Japanese, German, French, Spanish or Italian (the
  launcher's Language setting); the disc has all six.
- **Controllers:** Xbox and PlayStation pads through SDL, with no setup.
- **Keyboard:** works alongside a controller; every key can be changed.
- **Saving** to `Documents\rr6_recomp`.
- **Quitting** from the keyboard or the controller, with a question first.
- **Achievements:** the game's 36, with a pop-up and a sound when you earn
  one, a list in the game (F7, or Y from the quit question) and in the
  launcher. The 15 that need Xbox Live are shown apart.
- **Bug reports:** the launcher can record a detailed log and pack it, with
  your PC's specifications and without your Windows user name, into one zip.

The game runs at 60 frames per second, as it did on the console.

## Requirements

- Windows 10 or 11, 64-bit
- A graphics card with DirectX 12 support
- About 6.5 GB of free disk space
- The Microsoft Visual C++ 2015-2022 runtime (x64); the launcher says so if it
  is missing
- Your own Ridge Racer 6 (USA) disc image

## Controls

Controllers work as soon as they are connected. The game shows Xbox button
names.

| Keyboard | Controller |
|---|---|
| Arrow keys or W A S D | Left stick (Up or W also accelerates, Down or S also brakes) |
| Space | A (confirm) |
| Backspace or B | B (cancel) |
| X, Y | X, Y |
| Q, E | LB, RB |
| Enter or P | Start (pause) |
| Tab | Back |
| Numpad 8 2 4 6 | D-pad |
| I K J L | Right stick |

While playing: F4 opens more settings (press **Save to config** there to keep
changes for the next start), and F3 shows the frame rate.

To leave the game, press Esc, or hold Back + Start on a controller for a
second. The game asks whether to quit: Enter or A quits, Esc or B goes back.

## Achievements

Ridge Racer 6 has 36 achievements worth 1000 gamerscore, and they work here:
the game reports them as it did on the console, a pop-up with a sound appears,
and the unlock is kept with your save data. Press F7 while playing for the
list, or Esc and then Y; on a controller hold Back + Start for a second and
then press Y. The launcher has an Achievements page as well.

Fifteen of them need Xbox Live play (the online-battle ones, and those that
need cars only given for online battles), which this version does not have.
They are listed apart, and progress is counted against the 21 that can be
earned, 565 of the 1000 gamerscore.

The sound is an original chime. To use another, put your own `achievement.wav`
next to the launcher (on Linux, next to `ridge-racer-6.sh`).

## Downloadable content

If you own downloadable content for Ridge Racer 6, you can add it. You need
the content files from your own Xbox 360's storage: they are in
`Content\0000000000000000\4E4D07D3\00000002` and have long names without an
extension; keep those names. (A PC cannot read an Xbox 360 drive directly; a reader program for
the console's disk format is needed.) No content is included here, and none
is downloaded.

- **Any system:** put the files in the `DLC` folder that comes with the game
  (the folders inside it are searched too). Each time the game starts, it
  adds the ones that are new or have changed.
- **Windows:** or the launcher's **DLC** page, **Add content files...** or
  **Add a folder...**, from anywhere.
- **Linux and Steam Deck:** or `./ridge-racer-6.sh --install-dlc FILE-OR-FOLDER`.

The game program checks that each file is downloadable content for this game,
unpacks it into the save data folder, and closes again; the game looks for
added content each time it starts. To remove content, delete its folder
(the launcher's **Open content folder**), and take its file out of the `DLC`
folder if it is there.

This is new. On Windows, five of the game's downloadable music tracks were
added this way and then chosen and played in a race (the game lists them as
"Player Disc" under Change BGM). The downloadable car designs have not been
tried, and on Linux only stand-in packages made for testing have. Reports are
welcome.

## Known limitations

- Played on one PC only so far (NVIDIA RTX 3060 Ti, 3440x1440), in short
  sessions. AMD and Intel graphics are untested, as are PlayStation
  controllers, most tracks and cars, long sessions and the ending videos.
- Online play and Xbox Live features do not work.
- Only the USA disc is accepted.
- On ultrawide screens the videos and the loading screen are stretched, and a
  few menu decorations stop where the 16:9 picture would end.
- Keyboard steering is all-or-nothing; a controller is the better way to play.
- Without a sound output device the game closes right after starting (the
  launcher warns about this).
- The Linux and Steam Deck builds have hardly been run on real graphics
  hardware, and have no settings window: settings are a text file.

## Building it yourself

See [BUILDING.md](BUILDING.md). In short: your own `default.xex`, the ReXGlue
SDK (our fork's package, or the original v0.10.0), Visual Studio's C++ build
tools and Clang, then
`build-windows.bat`; on Linux, Clang, CMake and Ninja, then `build-linux.sh`.

## Reporting problems

Open an issue and say what you were doing (track, car, mode), what went wrong,
and which graphics card you have. A screenshot helps for anything visual. For
crashes, tick "Record a detailed log" on the launcher's Troubleshooting page,
reproduce the problem, and attach the `bug-report.zip` it creates.

## Good to know

- Use your own disc image and your own save data. The launcher checks that an
  image is the right game, but the game trusts everything else in it and in
  save files, as it did on the console.
- The launcher has no network features. The game's online modes do not work,
  and what they attempt on a PC has not been tested: leave "Online Battle"
  alone.
- Each release lists SHA-256 values for its files so that a download can be
  checked.

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk), the recompiler and
  runtime this is built on, and the [Xenia](https://xenia.jp) project it
  derives from
- [SDL](https://libsdl.org) and the community
  [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB)
- [pl_mpeg](https://github.com/phoboslab/pl_mpeg) by Dominic Szablewski

## Licence

The project's own files are under the BSD 3-Clause licence in `LICENSE`.
Third-party parts keep their own licences: see `THIRD_PARTY_NOTICES.md`.
Ridge Racer 6 itself belongs to Bandai Namco Entertainment.

`docs/` holds the development notes: what had to be found and fixed to get the
game running, and findings about the SDK written up for reporting upstream.
