#!/bin/bash
# Assembles the two Linux packages from the current build:
#
#   ../dist/RidgeRacer6-Linux-TestBuild-NN.tar.gz       desktop Linux
#   ../dist/RidgeRacer6-SteamDeck-TestBuild-NN.tar.gz   Steam Deck
#
# usage: linux/make-package.sh <NN> [output folder]
#
# The two hold the same program. The Steam Deck package has its own README and
# a marker file that makes the start script choose the Deck's settings. Next
# to them goes the game binary with its symbols (kept private, like the
# Windows linker map: it turns a crash address into a function name).
# Run build-linux.sh first. No game data goes in, and the script refuses to
# pack anything that looks like it.
set -e
cd "$(dirname "$(readlink -f "$0")")/.."
[ -n "$1" ] || { sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
NUMBER="$1"
OUT="$(mkdir -p "${2:-../dist}" && cd "${2:-../dist}" && pwd)"
SDK="${REXSDK:-$PWD/../sdk/linux-amd64}"
BUILD="${RR6_BUILD_DIR:-out/build/linux-release}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Optional local artwork, extracted from the player's disc during packaging.
if [ -n "${RR6_ICON_XEX:-}" ]; then
  "${PYTHON:-python3}" tools/disc_icon.py "$RR6_ICON_XEX" \
    "${RR6_ICON_REXGLUE:-$SDK/bin/rexglue}" "$WORK/local-icons"
fi

for f in "$BUILD/rr6_recomp" "$SDK/lib/librexruntime.so" "$SDK/lib/librexgpu-xenos.so" launcher/rr6-extract \
         gamecontrollerdb.txt assets/achievement.wav linux/ridge-racer-6.sh linux/collect-report.sh linux/README.txt linux/README-steamdeck.txt; do
  [ -f "$f" ] || { echo "Missing: $f (run build-linux.sh first)"; exit 1; }
done

source_note="$(git rev-parse --short HEAD 2>/dev/null || true)"
made="$(date -u '+%Y-%m-%d')"
# Which SDK the two runtime libraries come from. Builds of our fork
# (github.com/Sirhalo23/rexglue-sdk) are numbered x.y.z.100 and up.
sdk_version="$(sed -nE 's/^#define REXGLUE_VERSION_STRING "([^"]+)".*/\1/p' "$SDK/include/rex/version.h" 2>/dev/null)"
sdk_source="github.com/rexglue/rexglue-sdk"
case "$sdk_version" in
  *.*.*.[1-9][0-9][0-9]*) sdk_source="our fork, github.com/Sirhalo23/rexglue-sdk" ;;
esac

assemble() {  # assemble <folder name> <readme> <kind text> [deck]
  local dir="$WORK/$1"
  mkdir -p "$dir/bin" "$dir/tools" "$dir/licenses" "$dir/DLC"
  # Empty but for its note: players put their own content files there.
  sed 's/\\/\//g' package/DLC/PUT-CONTENT-FILES-HERE.txt > "$dir/DLC/PUT-CONTENT-FILES-HERE.txt"
  install -m 755 linux/ridge-racer-6.sh "$dir/ridge-racer-6.sh"
  install -m 755 linux/collect-report.sh "$dir/tools/collect-report.sh"
  # The build number goes into the title; the line under it follows its length.
  sed "s/@BUILD@/$NUMBER/g" "$2" |
    awk 'NR == 1 { print; n = length($0); next } NR == 2 { line = ""; for (i = 0; i < n; i++) line = line "="; print line; next } { print }' > "$dir/README.txt"
  if [ -n "${RR6_ICON_XEX:-}" ]; then
    mkdir -p "$dir/icons"
    install -m 644 "$WORK/local-icons/rr6.png" "$dir/icons/rr6.png"
    install -m 755 linux/install-desktop.py "$dir/tools/install-desktop.py"
    printf '\nLocal disc-derived icon: icons/rr6.png\nTo add a desktop-menu shortcut, run: python3 tools/install-desktop.py\nRe-run it after moving the extracted package. Steam custom icons can use the same PNG.\n' >> "$dir/README.txt"
  fi
  install -m 755 "$BUILD/rr6_recomp" "$dir/bin/rr6_recomp"
  strip "$dir/bin/rr6_recomp"
  install -m 755 "$SDK/lib/librexruntime.so" "$SDK/lib/librexgpu-xenos.so" launcher/rr6-extract "$dir/bin/"
  install -m 644 gamecontrollerdb.txt "$dir/bin/"
  mkdir -p "$dir/bin/sounds"
  install -m 644 assets/achievement.wav "$dir/bin/sounds/achievement.wav"
  install -m 644 package/licenses/ReXGlue-SDK-LICENSE.txt package/licenses/SDL3-LICENSE.txt \
    package/licenses/SDL_GameControllerDB-LICENSE.txt "$dir/licenses/"
  {
    echo "Ridge Racer 6 - $3 test build $NUMBER"
    echo "made: $made${source_note:+  source: $source_note}"
    echo "runtime: ReXGlue SDK ${sdk_version:-unknown} ($sdk_source)"
  } > "$dir/BUILD.txt"
  if [ -n "$4" ]; then
    echo "This file tells ridge-racer-6.sh that this is the Steam Deck package: it then writes the Deck's settings on the first start. Delete it to have settings chosen for the screen instead." > "$dir/steam-deck"
  fi
  # Nothing from the game may be in a package.
  local bad
  bad="$(find "$dir" -type f \( -iname '*.xex' -o -iname '*.iso' -o -iname '*.sfd' -o -iname '*.dat' -o -iname '*.bin' -o -iname '*.xpso' \) -o -type f -size +60M)"
  [ -z "$bad" ] || { echo "Refusing to pack, this looks like game data: $bad"; exit 1; }
  # Content packages have no extension; they start with LIVE, PIRS or "CON ".
  while IFS= read -r -d '' f; do
    case "$(head -c 4 "$f")" in
      LIVE|PIRS|"CON ") echo "Refusing to pack a content package: $f"; exit 1 ;;
    esac
  done < <(find "$dir" -type f -print0)
  tar -C "$WORK" --sort=name --owner=0 --group=0 --numeric-owner -czf "$OUT/$1.tar.gz" "$1"
  echo "Made: $OUT/$1.tar.gz ($(du -h "$OUT/$1.tar.gz" | cut -f1))"
}

assemble "RidgeRacer6-Linux-TestBuild-$NUMBER" linux/README.txt "Linux"
assemble "RidgeRacer6-SteamDeck-TestBuild-$NUMBER" linux/README-steamdeck.txt "Steam Deck" deck

xz -9 -T0 -c "$BUILD/rr6_recomp" > "$OUT/RidgeRacer6-Linux-TestBuild-$NUMBER.symbols.xz"
echo "Made: $OUT/RidgeRacer6-Linux-TestBuild-$NUMBER.symbols.xz (private: the game binary with symbols)"
( cd "$OUT" && sha256sum "RidgeRacer6-Linux-TestBuild-$NUMBER.tar.gz" "RidgeRacer6-SteamDeck-TestBuild-$NUMBER.tar.gz" )
