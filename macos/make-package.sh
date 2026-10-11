#!/bin/bash
# Package the current native build as one Finder app. Does not include the ISO.
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ $(uname -s) != Darwin ]]; then
  echo "Packaging requires macOS." >&2
  exit 1
fi
arch=${RR6_ARCH:-$(uname -m)}
case "$arch" in
  arm64) platform=mac-arm64 ;;
  x86_64) platform=mac-amd64 ;;
  *) echo "Unsupported architecture: $arch" >&2; exit 1 ;;
esac
build=${RR6_BUILD_DIR:-"$PWD/out/build/$platform-release/rr6_recomp.app"}
sdk=${REXSDK_DIR:-"$PWD/../rexglue-sdk"}
python=${PYTHON:-python3}
"$python" macos/package.py "$build" "$sdk" "$arch" "${1:-$PWD/dist}"
