#!/bin/bash
# usage: press.sh <display> <button>
# Holds the keyboard key for a pad button until the game's own input log shows
# that it saw the press, then releases and waits until it saw the release.
# Needs the game started with --rr6_log_input=true.
S=${RR6_RIG:?set RR6_RIG to the rig folder}
L=$S/run/run.log
case "$2" in
  a) key=space;  pat='buttons 0x1000' ;;
  b) key=b;      pat='buttons 0x2000' ;;
  x) key=x;      pat='buttons 0x4000' ;;
  y) key=y;      pat='buttons 0x8000' ;;
  start) key=Return; pat='buttons 0x0010' ;;
  left) key=Left;   pat='stick -32767,0' ;;
  right) key=Right; pat='stick 32767,0' ;;
  up) key=Up;       pat='stick 0,32767' ;;
  down) key=Down;   pat='stick 0,-32767' ;;
  lb) key=q; pat='buttons 0x0100' ;;
  rb) key=e; pat='buttons 0x0200' ;;
  *) echo "unknown button $2"; exit 2 ;;
esac
export DISPLAY=$1
n0=$(grep -a -c '\[input\] pad' $L)
xdotool keydown $key
seen=0
for i in $(seq 1 300); do
  sleep 0.1
  if grep -a '\[input\] pad' $L | tail -n +$((n0+1)) | grep -q "$pat"; then seen=1; break; fi
done
xdotool keyup $key
n1=$(grep -a -c '\[input\] pad' $L)
for i in $(seq 1 300); do
  sleep 0.1
  [ "$(grep -a -c '\[input\] pad' $L)" -gt "$n1" ] && break
  grep -a '\[input\] pad' $L | tail -1 | grep -q "$pat" || break
done
[ $seen = 1 ] && echo "pressed $2 (${i})" || echo "NOT SEEN: $2"
