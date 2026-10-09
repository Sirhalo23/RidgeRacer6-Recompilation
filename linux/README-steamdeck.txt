RIDGE RACER 6 - STEAM DECK TEST BUILD @BUILD@
==============================================

An unofficial native Linux version of Ridge Racer 6 (Xbox 360, USA disc) for
the Steam Deck, made by recompiling the game's program. It contains no game
data: you need your own Ridge Racer 6 (USA) disc image (.iso).

This build is experimental. Build 02 was tried once on a Steam Deck: it
started, but the track was drawn black, and the controls were wrong because it
had been started from outside Steam. Builds 03 and later deal with both: the
black track is fixed (from build 04 on in the graphics code itself, which is
our own build of the ReXGlue SDK; checked with software rendering, not yet on
a Deck), and the start script says so when it is not started from Steam. How
fast it runs on a Deck is not known yet. Please report what you see (see
REPORTING A PROBLEM).

New in build 05: downloadable content you own can be added (see DOWNLOADABLE
CONTENT below).


WHAT YOU NEED
-------------
- A Steam Deck (LCD or OLED) with SteamOS 3.6 or newer (Settings > System
  shows the version; update if it is older).
- About 6.5 GB of free space for the game files, plus 7.8 GB for the disc
  image while it is being copied. The disc image can stay on a microSD card
  or a USB drive; the copy does not need it afterwards.
- Your Ridge Racer 6 (USA) disc image. Other regions do not work.


SETTING UP (ONCE, IN DESKTOP MODE)
----------------------------------
1. Switch to Desktop Mode: hold the power button, choose "Switch to Desktop".
2. Get this archive and your disc image onto the Deck (browser download,
   USB drive, microSD card, or over the network).
3. Unpack the archive: in the Dolphin file manager, right-click it and choose
   Extract > "Extract archive here". A good place is your Home folder. (On a
   microSD card it must be one the Deck formatted itself; a card formatted for
   Windows cannot hold programs.)
4. Copy or move your .iso file INTO the unpacked folder (next to
   ridge-racer-6.sh). The start script finds it there by itself.
   (Without this step the script opens a window to choose the file.)
5. Double-click "ridge-racer-6.sh" and choose "Execute" if asked.
   A terminal window shows the game files being copied (about 6 GB, several
   minutes).
6. When the copy is done, the script says that it was started from outside
   Steam and asks what to do. Type a and press Enter: it adds the game to
   Steam and closes. (By hand instead: right-click "ridge-racer-6.sh" in
   Dolphin and choose "Add to Steam", or in the Steam window choose Games >
   "Add a Non-Steam Game to My Library" > Browse, and pick ridge-racer-6.sh.)
7. The .iso in the folder is not needed any more; delete it to get the space
   back.
8. Go back to Gaming Mode ("Return to Gaming Mode" on the desktop). The game
   is in your Library under "Non-Steam", as ridge-racer-6.sh. You can rename
   it there (gear icon > Properties). Start it from there.

ALWAYS START THE GAME FROM STEAM. The Deck's buttons are an Xbox controller
only for programs that Steam starts. Started by a double click in Desktop
Mode, they act as a keyboard and mouse instead: Y confirms in the menus and
nothing accelerates. The start script warns about this; "Start anyway" is for
playing with a keyboard or a controller you have plugged in.


PLAYING
-------
Started from Steam, the Deck's own controls work as an Xbox controller; the
game shows Xbox button names (A, B, X, Y, LB/RB, LT/RT). Start is the Menu
button (right of the screen), Back is the View button (left of the screen).
If the buttons do the wrong things, the game was started from outside Steam
(see above), or its controller layout in Steam is not a gamepad one: with the
game selected, press the controller icon and pick the "Gamepad" template.

To leave the game: hold View + Menu for a second; the game asks whether to
quit; A quits, B goes back. Or press the Steam button and choose "Exit game".

The picture: the Deck's screen is 1280x800 and the game is 16:9, so there are
thin black bars above and below. The game is drawn at its original 1280x720,
which the Deck shows pixel for pixel.

The game always runs at 60 frames per second. If the Deck's frame limit or
refresh rate is set lower in the "..." menu, set it to 60 for this game;
with a lower limit the game will probably run in slow motion.


ACHIEVEMENTS
------------
The game's 36 achievements work as they did on the Xbox 360: when you earn
one, a pop-up appears with a sound, and it is remembered.

- The list: hold View + Menu for a second, then press Y. The D-pad or the
  left stick scrolls it; B closes it.
- 15 of the 36 need Xbox Live play, which this version does not have. The
  list shows them apart, and counts your progress against the 21 you can
  earn (565 of the 1000 gamerscore).
- Your own sound: in Desktop Mode, put a file named achievement.wav into
  this folder (next to ridge-racer-6.sh) and the game plays it instead of
  its own chime.


SETTINGS
--------
Settings are in bin/rr6_recomp.toml, a text file with every line explained
(edit it in Desktop Mode with Kate). With a keyboard attached, F4 in the game
opens a settings window ("Save to config" there keeps a change) and F3 shows
the frame rate.

A sharper picture: set draw_resolution_scale_x and draw_resolution_scale_y
to 2 (the game is then drawn at 2560x1440 and scaled down). This needs four
times the graphics work; whether the Deck manages it at full speed is not
known yet. Edge smoothing at little cost: swap_post_effect = "fxaa".

Docked to a TV or monitor the same settings are used; raise the two scale
values if the picture looks soft there and the speed holds.

Language: the game is in English unless you choose another of the disc's six.
Set user_language in bin/rr6_recomp.toml (1 English, 2 Japanese, 3 German,
4 French, 5 Spanish, 6 Italian), or in Konsole:
./ridge-racer-6.sh --language german


DOWNLOADABLE CONTENT
--------------------
If you own downloadable content for Ridge Racer 6, you can add it. The
simplest way: in Desktop Mode, copy the content files into the DLC folder
here. Each time the game starts, it adds the ones that are new or have
changed, also when started from Game Mode. Or open a terminal (Konsole) in
this folder and run:

  ./ridge-racer-6.sh --install-dlc FILE-OR-FOLDER

You need the content files from your own Xbox 360's storage. They are in the
folder Content/0000000000000000/4E4D07D3/00000002 and have long names without
an extension; keep those names. Give one file, or a folder that holds them (the folders inside
it are searched too). A game window opens
for a moment and closes again, and the script says what was added. To remove
content, delete its folder in
~/.local/share/rr6_recomp/0000000000000000/4E4D07D3/00000002. Added music
appears under "Change BGM" on the screen before a race, as "Player Disc". This
is new: real content has been added and played on Windows, on Linux only
stand-in files made for testing.


WHERE THINGS ARE KEPT
---------------------
  this folder/game/        the game files copied from your disc image
  this folder/bin/         the program and its settings (rr6_recomp.toml)
  this folder/logs/        the log of the last run
  ~/.local/share/rr6_recomp/   your save data, and the graphics cache
                               (that is /home/deck/.local/share/rr6_recomp)

To remove everything: delete this folder and that one, and remove the entry
from your Steam library.


KNOWN LIMITATIONS
-----------------
- Untested on a Steam Deck (see the top of this file).
- Online play does not work. Leave "Online Battle" alone.
- The first minutes of play can stutter while graphics shaders are built;
  they are kept for the next start.
- If the game does not start from Gaming Mode, start it once in Desktop Mode
  from a terminal (Konsole):  ./ridge-racer-6.sh  and read what it says.


REPORTING A PROBLEM
-------------------
In Desktop Mode, double-click tools/collect-report.sh (or run it in Konsole).
It packs the game's log, your settings and a description of the system into
bug-report.tar.gz in this folder, with your user name and the Deck's network
name removed. Send that file with a few words on what happened and where.
For a crash, start the game once with  ./ridge-racer-6.sh --diagnostic  from
Konsole and make it happen again before collecting the report.

Project page: https://github.com/Sirhalo23/RidgeRacer6-Recompilation


LEGAL
-----
Ridge Racer 6 is the property of Bandai Namco Entertainment. This build is
not made, endorsed or supported by them, nor by Valve. It contains no game
data, artwork or sound; the program in bin/ is the game's own program in
recompiled form and is of no use without the game files from your own disc.
Licences of the software this build is made with are in the "licenses" folder.
