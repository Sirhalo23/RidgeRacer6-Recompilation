#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
arch=${RR6_ARCH:-$(uname -m)}
case "$arch" in
  arm64) platform=mac-arm64 ;;
  x86_64) platform=mac-amd64 ;;
  *) echo "Unsupported architecture: $arch" >&2; exit 1 ;;
esac
build="$PWD/out/build/$platform-release/rr6_recomp.app/Contents/MacOS"
if [[ ! -x "$build/rr6_recomp" ]]; then
  echo "Build first: ./build-macos.sh" >&2
  exit 1
fi
mkdir -p logs out/userdata out/DLC
has_dlc_arg=false
for arg in "$@"; do
  case "$arg" in --rr6_dlc_folder|--rr6_dlc_folder=*) has_dlc_arg=true ;; esac
done
if [[ $has_dlc_arg == false ]]; then
  set -- "--rr6_dlc_folder=$PWD/out/DLC" "$@"
fi
exec "$build/rr6_recomp" \
  --game_data_root "$PWD/../game" \
  --user_data_root "$PWD/out/userdata" \
  --gpu_plugin=xenos \
  --hid_mappings_file "$build/gamecontrollerdb.txt" \
  --log_file "$PWD/logs/run-macos.log" "$@"
