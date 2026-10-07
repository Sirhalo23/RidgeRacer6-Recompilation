#!/bin/bash
# usage: stop.sh  -- stops the game. A killed game leaves its guest memory behind in /dev/shm
# (several hundred MB each); remove it, or a few dozen runs use up the memory.
pkill -x rr6_recomp; sleep 1; pkill -9 -x rr6_recomp; sleep 0.3; rm -f /dev/shm/xenia_memory_*; true
