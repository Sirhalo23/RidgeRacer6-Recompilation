#!/bin/bash
# usage: p1.sh <display> <key> <log pattern> [shot name] [settle seconds]
# One key press for very slow frames: the key is released as soon as the game's
# own log (--rr6_log_input=true) shows the press, then the script waits for the
# picture to settle and takes a half-size screenshot to run/seq/<name>.png.
# Example: p1.sh :92 Down 'stick 0,-32767' menu1 45
# HOLD=<seconds> keeps the key down a little longer after the game saw it.
S=${RR6_RIG:?}; L=$S/run/run.log
export DISPLAY=$1
off=$(stat -c %s "$L"); xdotool keydown "$2"; seen=0
for i in $(seq 1 900); do
  sleep 0.03
  tail -c +$((off+1)) "$L" | grep -a '\[input\] pad' | grep -q "$3" && { seen=1; break; }
done
sleep "${HOLD:-0}"; xdotool keyup "$2"; echo "$2 seen=$seen after $i"
mkdir -p "$S/run/seq"; sleep "${5:-40}"
import -window root -resize 50% "$S/run/seq/${4:-now}.png"
