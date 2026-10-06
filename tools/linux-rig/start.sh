#!/bin/bash
# usage: start.sh <display> <tomlfile> [extra game args...]
# Starts the Linux check build under Xvfb + software Vulkan in the background.
S=${RR6_RIG:?set RR6_RIG to the rig folder}
D=$1; T=$2; shift 2
B=$S/rr6-recomp/out/build/linux-check
cp "$T" $B/rr6_recomp.toml
rm -f $S/run/run.log
cd $B
export DISPLAY=$D ALSA_CONFIG_PATH=${RR6_ALSA:-$S/run/asound-pulse.conf} LD_LIBRARY_PATH=$S/sdk/linux-amd64/lib:. XDG_RUNTIME_DIR=/tmp/xdg-rr6
mkdir -p $XDG_RUNTIME_DIR; chmod 700 $XDG_RUNTIME_DIR
setsid nohup ./rr6_recomp --game_data_root $S/game --gpu_plugin=xenos --log_file $S/run/run.log --log_flush_interval 1 --log_max_file_size_mb 300 "$@" > $S/run/out.txt 2>&1 < /dev/null &
echo started pid $!
