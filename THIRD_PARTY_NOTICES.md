# Third-party software and data

| What | Where it is used | Licence | Text |
|---|---|---|---|
| [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) v0.10.0, derived in part from [Xenia](https://xenia.jp) | The recompiler and the runtime the game runs on. Not in this repository; downloaded separately. Its runtime files are part of a release package. | BSD 3-Clause | `package/licenses/ReXGlue-SDK-LICENSE.txt` |
| [SDL 3](https://libsdl.org) | Input and audio, inside the SDK runtime. | zlib | `package/licenses/SDL3-LICENSE.txt` |
| [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) | `gamecontrollerdb.txt`: community controller mappings. | zlib | `package/licenses/SDL_GameControllerDB-LICENSE.txt` |
| [pl_mpeg](https://github.com/phoboslab/pl_mpeg) by Dominic Szablewski | `launcher/pl_mpeg_sofdec.h`, a copy with two small changes described at the top of the file. It reads a still picture from the opening movie of the player's own copy of the game. | MIT | `launcher/pl_mpeg-LICENSE.txt` |

The background and lettering of the banner in `docs/images` are an original
drawing (`docs/images/banner.py`), set in
[Michroma](https://github.com/googlefonts/Michroma-font) and
[Inter](https://rsms.me/inter/), both under the SIL Open Font License. The car
in `banner.png` is fan art made by the repository owner with an AI image
generator; it is not a file from the game.

`assets/achievement.wav`, the sound played with an achievement pop-up, is an
original chime made by `tools/make_chime.py`. It is not a recording of any
console's sound.

The BSD 3-Clause licence in `LICENSE` covers this project's own files only.

Ridge Racer 6 itself is the property of Bandai Namco Entertainment. No file
from the game is in this repository.
