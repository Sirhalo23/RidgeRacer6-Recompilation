#!/bin/bash
# usage: shot.sh <display> <name> [wait]
S=${RR6_RIG:?set RR6_RIG to the rig folder}
mkdir -p $S/run/seq; sleep ${3:-8}
DISPLAY=$1 import -window root $S/run/seq/$2.png
