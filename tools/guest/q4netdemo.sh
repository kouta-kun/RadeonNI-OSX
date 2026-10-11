#!/bin/sh
# Quake 4 network timedemo (id's client demo id_demo001, from the game's
# own Benchmarking folder), at 1920x1080.
#
#   q4netdemo.sh [environment assignments for the game]
#
# Run on the Mac, logged in at the console (ssh will do). The game takes
# over the screen. The command is playNetTimeDemo; the game does not quit
# by itself, so this waits for the result line in the console log, prints
# it and quits the game it started. Stands in for Doom 3 saves (same
# engine family) when they are not there.
#
# Example: q4netdemo.sh RDN_GLTHREAD=0
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT
D=~/Library/Application\ Support/Quake4/q4base
L="$D/qconsole.log"
rm -f "$L"
# Neither "+playNetTimeDemo" nor "+exec file" on the command line does
# anything in this build (the log shows no trace of them and the game goes
# to its menu, 2026-10-11). The game does exec autoexec.cfg at start, so the
# command goes there, after a wait; the file is removed again at the end so
# that normal play is not affected.
trap 'rm -f "$D/autoexec.cfg"' 0 1 2 15
printf 'wait 300\nplayNetTimeDemo id_demo001\n' > "$D/autoexec.cfg"
cd ~/Desktop/Quake\ 4 || exit 1
env A=1 "$@" "./Quake 4.app/Contents/MacOS/Quake 4" \
    +set r_mode -1 +set r_customWidth 1920 +set r_customHeight 1080 \
    +set r_fullscreen 1 +set logFile 2 \
    > /tmp/q4netdemo.out 2>&1 < /dev/null &
pid=$!
i=0
while [ $i -lt 240 ] && kill -0 $pid 2> /dev/null; do
    sleep 2
    i=`expr $i + 2`
    # The game prints "2811 frames in 61593 ms: 45.64 fps" and stops the demo.
    if grep -a -q "frames in [0-9]* ms" "$L" 2> /dev/null; then
        sleep 2
        break
    fi
done
grep -a "frames in [0-9]* ms" "$L" | tail -1
kill $pid 2> /dev/null
