RIDGE RACER 6 - LINUX TEST BUILD @BUILD@
=========================================

An unofficial native Linux version of Ridge Racer 6 (Xbox 360, USA disc),
made by recompiling the game's program. It contains no game data: you need
your own Ridge Racer 6 (USA) disc image (.iso).

This build is experimental. It has been run from start to finish only on a
test machine without a graphics card (very slowly, with software rendering),
and an earlier build was tried once on a Steam Deck. Nobody has yet played it
on a desktop graphics card: you may be the first. Please report what you see
(see REPORTING A PROBLEM).

New in build 05: downloadable content you own can be added (see DOWNLOADABLE
CONTENT below).

Since build 04, the black track of builds 01 and 02 is fixed where it came
from, in the graphics code this build uses, which is our own build of the
ReXGlue SDK. Build 03 got around it by switching off the game's texture
sharpening; that is back, so the road ahead is as sharp as in the Windows
version.


WHAT YOU NEED
-------------
- A 64-bit Intel or AMD PC (the processor must have SSE4.1: anything since
  about 2011).
- A Linux system from 2024 or later: Ubuntu 24.04, Debian 13, Fedora 39,
  SteamOS 3.6, or newer; or a rolling system such as Arch or openSUSE
  Tumbleweed. (Technically: glibc 2.35 and the C++ library of GCC 13.2.
  The start script tells you if your system is too old.)
- A graphics card with a Vulkan driver, and the Vulkan loader installed
  (package "vulkan-loader" or "libvulkan1"; normally already there).
- Sound through PipeWire, PulseAudio or ALSA.
- About 6.5 GB of free disk space.
- Your Ridge Racer 6 (USA) disc image. Other regions do not work.

Steam Deck: there is a separate package for it, with its own README.


HOW TO START
------------
1. Unpack this archive somewhere in your home folder.
2. Start "ridge-racer-6.sh": double-click it and choose "Run" (or "Execute"),
   or open a terminal in this folder and type  ./ridge-racer-6.sh
   (If a double-click opens the file in a text editor instead, right-click it
   and choose "Run as a Program", or use the terminal.)
3. The first time, a terminal window asks for your disc image. Choose the
   .iso file. The game files are copied out of it into the "game" folder
   here (about 6 GB, a few minutes), after a check that it is the right
   version. The disc image itself is not changed and is not needed again.
   Shortcut: put the .iso into this folder first and it is used without
   asking; or pass it on the command line:  ./ridge-racer-6.sh --iso FILE
4. The game starts. From then on the script starts the game straight away.

To leave the game: press Esc, then Enter. On a controller: hold Back + Start
for a second, then press A. (Esc again, or B, goes back to the game.)


SETTINGS
--------
There is no settings window on Linux yet. On the first start the script
writes bin/rr6_recomp.toml with settings chosen for your screen:

- full screen;
- the game drawn at the next multiple of 1280x720 above your screen's size
  (2560x1440 for a 1080p or 1440p screen, 3840x2160 for 4K);
- a screen wider than 16:9 (ultrawide) is filled, with the race display moved
  to the screen edges.

To change them, edit bin/rr6_recomp.toml with a text editor (every line is
explained there), or press F4 in the game and then "Save to config" to keep
the change. F3 shows the frame rate.
If the game runs slowly, lower draw_resolution_scale_x and _y.
To have the file written afresh:  ./ridge-racer-6.sh --new-settings

The game always runs at 60 frames per second, as it did on Xbox 360.

The game's language (English unless chosen; the disc has six), kept from
then on:  ./ridge-racer-6.sh --language german   (or english, japanese,
french, spanish, italian), or user_language in bin/rr6_recomp.toml.

Other options of the start script:  ./ridge-racer-6.sh --help


CONTROLS
--------
Controllers: Xbox and PlayStation controllers work as soon as they are
plugged in or paired. The game shows Xbox button names. On a PlayStation pad:
Cross = A, Circle = B, Square = X, Triangle = Y, L1/R1 = LB/RB,
L2/R2 = LT/RT, Options = Start, Create/Share = Back.

Keyboard (works together with a controller):
  Arrow keys or W A S D   steer, move in menus
  Up or W                 accelerate        Down or S   brake
  Space                   A (confirm)       Backspace or B   B (cancel)
  X, Y                    X, Y              Q, E   LB, RB
  Enter or P              Start (pause)     Tab    Back
The keys are listed in bin/rr6_recomp.toml and can be changed there.


ACHIEVEMENTS
------------
The game's 36 achievements work as they did on the Xbox 360: when you earn
one, a pop-up appears with a sound, and it is remembered.

- The list: press F7 while playing. Without a keyboard: hold Back + Start for
  a second, then press Y. The arrow keys, the D-pad or the stick scroll it;
  Esc or B closes it.
- 15 of the 36 need Xbox Live play, which this version does not have. The
  list shows them apart, and counts your progress against the 21 you can
  earn (565 of the 1000 gamerscore).
- Your own sound: put a file named achievement.wav into this folder (next to
  ridge-racer-6.sh) and the game plays it instead of its own chime.
- Progress is kept with your save data, in ~/.local/share/rr6_recomp.


DOWNLOADABLE CONTENT
--------------------
If you own downloadable content for Ridge Racer 6, you can add it. Put the
content files in the DLC folder here (folders inside it are searched too):
each time the game starts, it adds the files that are new or have changed.
Or, from anywhere:

  ./ridge-racer-6.sh --install-dlc FILE-OR-FOLDER

You need the content files from your own Xbox 360's storage. They are in the
folder Content/0000000000000000/4E4D07D3/00000002 and have long names without
an extension; keep those names. Give one file, or a folder that holds them; the folders inside
it are searched too, and the option can be repeated. A game window opens for a moment and closes again, and the script
says what was added. Only content for Ridge Racer 6 is accepted.

The game looks for added content each time it starts. It is unpacked into
~/.local/share/rr6_recomp/0000000000000000/4E4D07D3/00000002; to remove
content, delete its folder there, and take its file out of the DLC folder if
it is there. Added music appears under "Change BGM" on
the screen before a race, as "Player Disc". This is new: real content has
been added and played on Windows, on Linux only stand-in files made for
testing.


WHERE THINGS ARE KEPT
---------------------
  this folder/game/        the game files copied from your disc image
  this folder/bin/         the program and its settings (rr6_recomp.toml)
  this folder/logs/        the log of the last run
  ~/.local/share/rr6_recomp/   your save data, and the graphics cache

To remove everything, delete this folder and ~/.local/share/rr6_recomp.


KNOWN LIMITATIONS
-----------------
- Hardly tested on real graphics hardware (see the top of this file).
- The game runs through X11 (XWayland on a Wayland desktop). To let it try
  Wayland directly:  ./ridge-racer-6.sh --wayland
- Online play does not work. Leave "Online Battle" alone.
- On ultrawide screens the videos and the loading screen are stretched.
- The first minutes of play can stutter while graphics shaders are built;
  they are kept for the next start.


REPORTING A PROBLEM
-------------------
Run  tools/collect-report.sh  in this folder. It packs the game's log, your
settings and a description of your system (distribution, processor, graphics
card, driver) into bug-report.tar.gz, with your user name and computer name
removed. Send that file with a few words on what happened and where.
For a crash, start the game once more with  ./ridge-racer-6.sh --diagnostic
and make it happen again before collecting the report.

Project page: https://github.com/Sirhalo23/RidgeRacer6-Recompilation


LEGAL
-----
Ridge Racer 6 is the property of Bandai Namco Entertainment. This build is
not made, endorsed or supported by them. It contains no game data, artwork or
sound; the program in bin/ is the game's own program in recompiled form and
is of no use without the game files from your own disc.
Licences of the software this build is made with are in the "licenses" folder.
