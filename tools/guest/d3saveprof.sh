#!/bin/sh
# Load a Doom 3 save, let it settle for 60 frames, then profile the game
# with sample(1) for a number of seconds and stop it.
#
#   d3saveprof.sh <save> <seconds> <out file> [environment assignments]
#
# The call graph in <out file> has one tree per thread; with glthread the
# program's own thread is the first and Mesa's the third.
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT
save=$1; secs=$2; out=$3; shift; shift; shift
D=~/Library/Application\ Support/Doom\ 3\ Demo/demo
L="$D/qconsole.log"
printf 'wait 60\necho MARK1\nwait 4000\necho MARK2\nwait 5\nquit\n' > "$D/d3save.cfg"
rm -f "$L"
cd ~/Desktop/Doom\ 3\ Demo
env A=1 RDN_STATS=1 "$@" ./Doom\ 3\ Demo.app/Contents/MacOS/Doom\ 3\ Demo +set r_mode -1 +set r_customWidth 1920 +set r_customHeight 1080 +set r_fullscreen 1 +set r_aspectRatio 1 +set logFile 2 $D3ARGS +loadGame "$save" +exec d3save.cfg > /tmp/d3.out 2>&1 &
i=0; until grep -q MARK1 "$L" 2>/dev/null; do sleep 1; i=$((i+1)); [ $i -gt 240 ] && break; done
sleep 2
~/gl/fences | tail -1
sample "Doom 3 Demo" $secs 1 -file "$out" > /dev/null 2>&1
~/gl/fences | tail -1
killall "Doom 3 Demo" 2>/dev/null; sleep 3
grep "rdn" /tmp/d3.out | tail -12 | cut -c1-200
