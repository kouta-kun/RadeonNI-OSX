#!/bin/sh
# Quake 3 timedemo of the built-in demo "four" at 1920x1080, the benchmark of
# the journal (2026-10-06: 47.8 fps to 148 fps).
#
#   q3timedemo.sh [environment assignments for the game]
#
# Run on the Mac, logged in at the console (ssh will do). The game takes
# over the screen until the demo has played, then quits by itself. Prints
# the game's own line ("N frames, X seconds: Y fps").
#
# Example: q3timedemo.sh RDN_GLTHREAD=0
#
# The saved config of the game may have a mode the display does not offer;
# the command line below sets one.
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT
cd ~/Desktop/Quake\ 3 || exit 1
out=/tmp/q3timedemo.$$.out
env A=1 "$@" ./Quake3.app/Contents/MacOS/Quake3 \
    +set r_mode -1 +set r_customwidth 1920 +set r_customheight 1080 \
    +set r_fullscreen 1 +set timedemo 1 +set nextdemo quit +demo four \
    > "$out" 2>&1 < /dev/null &
pid=$!
# The demo is short; give it two minutes in all.
i=0
while [ $i -lt 120 ] && kill -0 $pid 2> /dev/null; do
    sleep 1
    i=`expr $i + 1`
done
if kill -0 $pid 2> /dev/null; then
    echo "the demo did not finish in 120 s"
    kill $pid
fi
grep -a "frames, .* seconds" "$out" | tail -1
rm -f "$out"
