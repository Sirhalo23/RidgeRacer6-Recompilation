#!/bin/bash
# usage: flow4.sh <display> <prefix>   title -> first-run save prompts -> Single Race highlighted
S=${RR6_RIG:?set RR6_RIG to the rig folder}
cd $S/run; D=$1; P=$2
p() { ./press.sh $D $1 >> flow.log; }
waitlog() { for i in $(seq 1 ${2:-60}); do grep -a -q "$1" run.log && return 0; sleep 3; done; echo "timeout waiting for $1" >> flow.log; return 1; }
p start
waitlog 'XContentClose' 60; ./shot.sh $D ${P}_autosave 25
p a; ./shot.sh $D ${P}_save_confirm 18          # autosave ON
p a                                              # save: YES
waitlog 'XContentSetThumbnail' 100; ./shot.sh $D ${P}_saved 25
p a; ./shot.sh $D ${P}_menu2 15                  # OK
p down; sleep 4; p down; ./shot.sh $D ${P}_menu_single 6
echo "flow4 done" >> flow.log
