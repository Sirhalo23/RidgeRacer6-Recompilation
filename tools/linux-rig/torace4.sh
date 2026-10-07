#!/bin/bash
# usage: torace4.sh <display> <tag>
# From the main menu with "Single Race" highlighted to a running race, taking
# the first course, class and machine. For very slow frames: every press is
# released as soon as the game's log shows it, and the screen gets 40 s to
# settle before the next one. Screenshots: run/seq/<tag>_*.png.
S=${RR6_RIG:?}; L=$S/run/run.log; D=$1; T=$2
export DISPLAY=$D; cd "$S/run"; mkdir -p seq
a() {
  off=$(stat -c %s "$L"); xdotool keydown space
  for i in $(seq 1 900); do
    sleep 0.03
    tail -c +$((off+1)) "$L" | grep -a '\[input\] pad' | grep -q 'buttons 0x1000' && break
  done
  xdotool keyup space; echo "a seen after $i $(date +%T)" >> torace.log
}
s() { sleep "$2"; import -window root -resize 50% "seq/${T}_$1.png"; echo "shot $1 $(date +%T)" >> torace.log; }
a; s course 40; a; s class 40; a; s machine 40; a; s transmission 40; a; s confirm 40
a; echo "race requested $(date +%T)" >> torace.log
sleep 200
for i in 1 2 3 4; do sleep 25; import -window root "seq/${T}_fly$i.png"; done
a; echo "skip pressed $(date +%T)" >> torace.log
for i in 1 2 3 4 5 6; do sleep 20; import -window root "seq/${T}_race$i.png"; done
echo done >> torace.log
