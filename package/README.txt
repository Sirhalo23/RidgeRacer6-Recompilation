RIDGE RACER 6 - PC TEST BUILD @BUILD@
================================

An unofficial test build of Ridge Racer 6 (Xbox 360) running natively on
Windows. It contains NO game data: you supply your own disc image of the game.
Please do not pass this build on to anyone who does not own the game.


WHAT YOU NEED
-------------
- Your own Ridge Racer 6 disc image (.iso), USA version. Other regional
  versions are refused: this build only matches the USA executable.
- Windows 10 or 11, 64-bit.
- A graphics card with DirectX 12 support.
- About 6.5 GB of free disk space for the extracted game files.
- An Xbox or PlayStation controller, or a keyboard.
- The Microsoft Visual C++ 2015-2022 runtime (x64). Most gaming PCs already
  have it; the launcher tells you if it is missing.


HOW TO START
------------
1. Extract this whole zip to a normal folder (for example C:\Games\RR6).
   Do not run it from inside the zip.
2. Double-click   RR6 Launcher.exe
3. Press "Choose disc image..." and pick your Ridge Racer 6 .iso, wherever it
   is on your PC. The launcher copies the game files out of it into a "game"
   folder here. That takes a few minutes and happens only once. Your .iso is
   not changed and is not needed again afterwards.
4. Choose your settings (the defaults suit most PCs) and press Play.

You can press Stop while the files are being copied; choosing the .iso again
later carries on from where it stopped.

If Windows shows "Windows protected your PC", choose More info > Run anyway.
The build is not code-signed. (To avoid being asked for each program, unblock
the zip before extracting it: right-click the zip, Properties, tick "Unblock",
OK.) If the launcher will not open at all, copy your .iso into the
PUT-ISO-HERE folder and use "Play without the launcher.bat" instead.

Some antivirus programs flag bin\rexruntime.dll under a general name such
as "Yogi" or "Generic". That file is the runtime of the ReXGlue SDK, the
toolkit this port is made with. From test build 07 on it is built from our
own copy of the SDK's source (github.com/Sirhalo23/rexglue-sdk), which fewer
scanners flag than the SDK's own download. The project's page on GitHub has
the current results and says how to check the file ("If your antivirus flags
rexruntime.dll"). If the game does not start and the file is gone from the
bin folder, your antivirus has removed it.


THE LAUNCHER
------------
The picture at the top of the launcher is not part of this package. On the
first start it is a plain drawn banner; once your game files have been copied
out of your .iso, the launcher shows a still from the game's own opening movie
instead (read from your files, on your PC). To use a picture of your own, put
it next to RR6 Launcher.exe as launcher-art.png, launcher-art.jpg or
launcher-art.bmp.


SETTINGS
--------
The launcher's Display tab:
- Screen: full screen or a window.
- Picture shape: on a screen wider than 16:9 (ultrawide), "Fill my screen"
  widens the 3D view to use all of it. "Original 16:9" keeps bars at the sides.
- HUD position on wide screens: at the screen edges, or where 16:9 put it.
- Sharpness: how large the game is drawn internally. Automatic matches your
  screen. Lower it if the game runs slowly.
- Edge smoothing, Texture detail: optional image-quality extras.
- Language: English, Japanese, German, French, Spanish or Italian. The disc
  has all six; the game's text and menus follow this choice.

The game always runs at 60 frames per second, as it did on Xbox 360.
While playing: F4 opens more settings; press "Save to config" there to keep a
change for the next start. F3 shows the frame rate. Settings changed in F4
or in the file by hand are kept when you press Save or Play in the launcher.
The launcher stores its choices in bin\rr6_recomp.toml.

To leave the game, press Esc: it asks "Quit Ridge Racer 6?". Enter quits,
Esc again goes back to the game. On a controller, hold Back + Start for a
second (Create/Share + Options on a PlayStation pad), then A quits and B goes
back. Alt+F4 still closes the game at once.


CONTROLS
--------
Controllers: Xbox and PlayStation controllers work as soon as they are
plugged in or paired. The game shows Xbox button names. On a PlayStation pad:
Cross = A, Circle = B, Square = X, Triangle = Y, L1/R1 = LB/RB, L2/R2 = LT/RT,
Options = Start, Create/Share = Back.

Keyboard (can be changed on the launcher's Controls tab):
  Arrow keys or W A S D   steer / move in menus; Up or W also accelerates,
                          Down or S also brakes
  Space                   A  (confirm)
  Backspace or B          B  (cancel)
  X, Y                    X, Y
  Q, E                    LB, RB
  Enter or P              Start (pause)
  Tab                     Back
  Numpad 8 2 4 6          D-pad
  I K J L                 right stick
Keys do not respond while Shift, Ctrl or Alt is held down.
The keyboard is on/off: steering is all-or-nothing, so a controller is the
better way to play.


ACHIEVEMENTS
------------
The game's 36 achievements work as they did on the Xbox 360: when you earn
one, a pop-up appears with a sound, and it is remembered.

- The list while playing: press F7. Or press Esc and then Y; on a controller,
  hold Back + Start for a second and then press Y. The arrow keys, the D-pad
  or the stick scroll it; Esc or B closes it.
- The list without starting the game: the launcher's Achievements page.
- 15 of the 36 need Xbox Live play, which this version does not have. The
  lists show them apart, and count your progress against the 21 you can earn
  (565 of the 1000 gamerscore).
- Your own sound: put a file named achievement.wav next to RR6 Launcher.exe
  and the game plays it instead of its own chime. The launcher's Achievements
  page has a button to hear it.
- Progress is kept with your save data (Documents\rr6_recomp).


DOWNLOADABLE CONTENT
--------------------
If you own downloadable content for Ridge Racer 6, you can add it to the
game: put the files in the DLC folder (next to RR6 Launcher.exe), or use the
launcher's DLC page.

- You need the content files from your own Xbox 360's storage. They are in
  the folder Content\0000000000000000\4E4D07D3\00000002 and have long names
  without an extension; keep those names. A PC cannot read an Xbox 360 drive directly; a reader
  program for the console's disk format is needed to copy them off.
- The DLC folder: copy the files into it (folders inside it are searched
  too). Each time the game starts, it adds the files that are new or have
  changed; that takes a few seconds the first time.
- Or, on the DLC page, press "Add content files..." and choose them, or
  "Add a folder..." for a folder that holds them. The folders inside it are
  searched too, so the copied Content folder itself will do. A game window
  opens for a moment and closes again; the launcher then says what was added.
- Only content for Ridge Racer 6 is accepted. A save or another game's file
  chosen by itself is refused with a reason; in a folder, anything that is
  not Ridge Racer 6 content is passed over.
- The game looks for added content each time it starts. To remove content,
  delete its folder ("Open content folder" on the DLC page), and take its
  file out of the DLC folder if it is there.
- Added music appears under "Change BGM" on the screen before a race, as
  "Player Disc".
- This is new. Five of the downloadable music tracks have been added and
  played this way; the downloadable car designs have not been tried. If your
  content does not show up in the game, please report it.


WHAT TO TEST
------------
This build has only been played for short sessions on ONE PC (NVIDIA
RTX 3060 Ti, 3440x1440). Everything else is unknown, so all of this is useful:

- Does it start and reach the menus on your PC? (AMD and Intel graphics are
  completely untested.)
- Other tracks, cars and game modes.
- The intro and ending videos.
- Whether your progress is still there after closing and restarting.
- Longer sessions: stutter, slowdowns, crashes, sound problems.
- Ultrawide screens: anything stretched, cut off or in the wrong place.
- PlayStation controllers and keyboard play.
- Anything that looks wrong on screen. Screenshots help a lot.
- Achievements: does the pop-up appear, with its sound, when you earn one?
  The quickest is "360!": spin the car a full turn in a drift during a race.

Known: online play and Xbox Live features do not work.


REPORTING A PROBLEM
-------------------
1. In the launcher, open the Troubleshooting tab, tick "Record a detailed log
   the next time I play", and press Play.
2. Play until the problem happens, then close the game (or let it crash).
3. The launcher saves   bug-report.zip   in this folder and shows it to you.
   Send that file with:
   - what you were doing (track, car, mode),
   - what went wrong,
   - a screenshot if it is a visual problem.

If the game crashes without the box ticked, a smaller bug-report.zip is still
made. bug-report.zip contains the game's log, the exit code, your settings,
Windows' crash record for this game, and your PC's Windows version, CPU,
memory and graphics card and driver. Nothing else is collected and nothing is
sent automatically. Your Windows user name is taken out of the file paths in
it, so it can be attached to a public bug report.


A NOTE ON SAFETY
----------------
- Get this build only from the place you were pointed to. If a SHA-256 value
  was published with the download, you can compare it: in PowerShell,
  Get-FileHash followed by the path of the zip.
- Use your own disc image and your own save data. The launcher checks that the
  image is the right game, but the game trusts everything else in it and in
  save files, just as it did on the console.
- The programs are not code-signed, so Windows and antivirus tools may warn
  about them.
- The launcher has no network features, and nothing is sent anywhere. The
  game's online modes do not work on PC, and what they attempt has not been
  tested: leave "Online Battle" alone.


WHERE THINGS ARE
----------------
RR6 Launcher.exe     start here
game\                game files copied out of your .iso (created by the launcher)
PUT-ISO-HERE\        only for "Play without the launcher.bat": your .iso goes here
bin\                 the game program and its settings file (rr6_recomp.toml)
logs\                log files
gamecontrollerdb.txt extra controller definitions (community database)
Documents\rr6_recomp (in your user folder)   save data and shader cache

To remove everything: delete this folder and Documents\rr6_recomp.
