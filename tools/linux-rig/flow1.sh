#!/bin/bash
# usage: flow1.sh <display> <prefix>   boot -> title screen
S=${RR6_RIG:?set RR6_RIG to the rig folder}
cd $S/run; D=$1; P=$2
# wait for the loading (Pac-Man) screen: the first non-white, non-black frame after the logos
for i in $(seq 1 60); do
  sleep 10
  m=$(DISPLAY=$D import -window root -resize 10% png:- 2>/dev/null | convert - -format '%[fx:mean]' info: 2>/dev/null)
  echo "t=$((i*10)) mean=$m" >> flow.log
  [ $i -gt 12 ] && awk -v m="$m" 'BEGIN{exit !(m>0.12 && m<0.6)}' && break
done
sleep 6; ./shot.sh $D ${P}_loading 1
./press.sh $D start >> flow.log; sleep 22
./press.sh $D a >> flow.log; sleep 8
./shot.sh $D ${P}_after_skip 1
sleep 30
./shot.sh $D ${P}_title 1
echo "flow1 done" >> flow.log
