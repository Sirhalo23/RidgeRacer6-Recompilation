#!/bin/bash
# usage: flow3.sh <display> <prefix>   main menu (Single Race highlighted) -> race start
S=${RR6_RIG:?set RR6_RIG to the rig folder}
cd $S/run; D=$1; P=$2
p() { ./press.sh $D $1 >> flow.log; }
p a; ./shot.sh $D ${P}_course 22
p a; ./shot.sh $D ${P}_class 16
p a; ./shot.sh $D ${P}_machine 18
p a; ./shot.sh $D ${P}_transmission 16
p a; ./shot.sh $D ${P}_confirm 24
p a; echo "race requested $(date +%T)" >> flow.log
# wait until the race scene draws its 2D through the sprite-group routine
for i in $(seq 1 200); do
  sleep 6
  grep -a '\[hud\]' run.log | tail -1 | grep -q 'in sprite group: quads [1-9]' && break
done
echo "race scene after $((i*6))s" >> flow.log
./shot.sh $D ${P}_flyby 4
timeout 120 ./press.sh $D a >> flow.log
./shot.sh $D ${P}_race1 40
./shot.sh $D ${P}_race2 30
./shot.sh $D ${P}_race3 30
./shot.sh $D ${P}_race4 40
echo "flow3 done" >> flow.log
