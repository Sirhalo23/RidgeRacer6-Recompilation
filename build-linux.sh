#!/bin/bash
# Builds the Linux version: the game (out/build/linux-release/rr6_recomp) and
# the disc-image tool (launcher/rr6-extract). Run it from anywhere.
#
# Needs: clang 18 or newer, CMake 3.25 or newer, Ninja, g++ (for the small
# tool), the ReXGlue SDK's Linux package in ../sdk/linux-amd64 (or where
# REXSDK points), and for the first build the game files in ../game.
# CXX and CC choose another compiler; RR6_BUILD_DIR another build folder.
set -e
cd "$(dirname "$(readlink -f "$0")")"
SDK="${REXSDK:-$PWD/../sdk/linux-amd64}"
BUILD="${RR6_BUILD_DIR:-out/build/linux-release}"

if [ -z "${CXX:-}" ]; then
  for candidate in clang++-21 clang++-20 clang++-19 clang++-18 clang++; do
    if command -v "$candidate" >/dev/null 2>&1; then CXX="$candidate"; break; fi
  done
fi
[ -n "${CXX:-}" ] || { echo "No clang++ found. Install clang 18 or newer, or set CXX."; exit 1; }
CC="${CC:-${CXX/clang++/clang}}"
for tool in cmake ninja g++; do
  command -v "$tool" >/dev/null 2>&1 || { echo "$tool is not installed."; exit 1; }
done
[ -f "$SDK/lib/cmake/rexglue/rexglueConfig.cmake" ] || {
  echo "The ReXGlue SDK's Linux package was not found in $SDK."
  echo "Unpack it there, or set REXSDK to its linux-amd64 folder."
  exit 1
}

# First build after a fresh checkout: turn the game's executable into C++
# (generated/default). Later builds redo this by themselves when the manifest
# changes.
if [ ! -f generated/default/sources.cmake ]; then
  [ -f ../game/default.xex ] || {
    echo "The game files are not in ../game yet. Copy them out of your disc image first:"
    echo "  python3 tools/extract_xiso.py /path/to/your.iso extract ../game"
    exit 1
  }
  echo "Recompiling the game's executable into C++ (first build only)..."
  "$SDK/bin/rexglue" codegen rr6_recomp_manifest.toml
fi

echo "Compiler: $("$CXX" --version | head -1)"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" -DCMAKE_PREFIX_PATH="$SDK"
cmake --build "$BUILD"

# The disc-image tool is linked statically so that it runs on any system.
g++ -std=c++17 -O2 -s -static -pthread launcher/rr6_extract.cpp launcher/disc_image.cpp -o launcher/rr6-extract ||
  g++ -std=c++17 -O2 -s -pthread launcher/rr6_extract.cpp launcher/disc_image.cpp -o launcher/rr6-extract

echo
echo "Built: $BUILD/rr6_recomp and launcher/rr6-extract"
echo "A package for other people: linux/make-package.sh <number>"
