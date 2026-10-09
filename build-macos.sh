#!/bin/bash
# Build the SDK tools first, generate the guest sources, then build the game.
set -euo pipefail
cd "$(dirname "$0")"

if [[ $(uname -s) != Darwin ]]; then
  echo "This script requires macOS." >&2
  exit 1
fi

arch=${RR6_ARCH:-$(uname -m)}
case "$arch" in
  arm64) platform=mac-arm64 ;;
  x86_64) platform=mac-amd64 ;;
  *) echo "Unsupported architecture: $arch" >&2; exit 1 ;;
esac
preset="$platform-release"
sdk=${REXSDK_DIR:-"$PWD/../rexglue-sdk"}
cmake=${CMAKE:-cmake}
python=${PYTHON:-python3}
jobs=${RR6_BUILD_JOBS:-8}
deployment_target=${RR6_MACOS_DEPLOYMENT_TARGET:-14.0}

if [[ ! -f ../game/default.xex ]]; then
  if [[ $# != 1 ]]; then
    echo "Usage: $0 /path/to/Ridge-Racer-6-USA.iso" >&2
    echo "Or extract your disc into ../game before building." >&2
    exit 1
  fi
  "$python" tools/extract_xiso.py "$1" extract ../game
fi
expected=39d3c0004ec62aeb0fe3e7e1889cf25d98fbd27987b6bc6b5ff30a56ffba6c00
actual=$(shasum -a 256 ../game/default.xex | awk '{print $1}')
if [[ "$actual" != "$expected" ]]; then
  echo "default.xex does not match the supported USA executable." >&2
  exit 1
fi
if [[ ! -f "$sdk/CMakeLists.txt" ]]; then
  echo "Set REXSDK_DIR to a ReXGlue SDK source checkout; see BUILDING.md for macOS requirements." >&2
  exit 1
fi

# Run separately: sources.cmake does not exist at the first configure.
"$cmake" --preset "$preset" -DREXSDK_DIR="$sdk" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
  -DCPM_SOURCE_CACHE="$PWD/out/dependencies" \
  -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_USE_VULKAN=ON
"$cmake" --build --preset "$preset" --target rr6_recomp_codegen --parallel "$jobs"
"$cmake" --preset "$preset" -DREXSDK_DIR="$sdk" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target"
"$cmake" --build --preset "$preset" --target rr6_recomp --parallel "$jobs"

build="out/build/$preset/rr6_recomp.app/Contents/MacOS"
if [[ ! -f "$build/rr6_recomp.toml" ]]; then
  cp config/rr6_recomp.macos.toml "$build/rr6_recomp.toml"
fi
echo "Built out/build/$preset/rr6_recomp.app. Start with ./run-macos.sh"
