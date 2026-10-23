#!/bin/bash
# Start The Sims 2 on the G5, click through its menu into Pleasantview, leave it running.
#
#   scripts/sims2-run.sh [-a original|patched] [-g file.gdb] [-e VAR=value]... [-w seconds]
#
#   -a   which app: "original" (The Sims 2.app) or "patched" (The Sims 2 (shader model 2).app); default original
#   -g   a gdb command file on the G5 (its "run" line starts the game); the game is run under gdb -batch
#   -e   an environment variable for the game (RDN_FPS=1, RDN_GLTHREAD=0, ...), repeatable
#   -w   seconds to wait after the last click for the neighborhood to load (default 50)
#
# It refuses to start when a Sims or gdb process already runs there (the user may be playing).
# The clicks are for the 1920x1080 screen: the menu is a 1024x768 window in the middle, Pleasantview's
# thumbnail at 774,524 and the play button of its dialog at 916,770 (tools/g5/rdninput.c, on the G5 as
# ~/rdninput). Afterwards: scripts/mac.sh g5 grab, sample, ... and, to stop what this started, kill the
# pids it prints (never killall).
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
mac() { "$root/scripts/mac.sh" g5 "$@"; }
app=original; gdbfile=; wait=50; envs=()
while getopts "a:g:e:w:" o; do
    case $o in
    a) app=$OPTARG ;;
    g) gdbfile=$OPTARG ;;
    e) envs+=("$OPTARG") ;;
    w) wait=$OPTARG ;;
    *) sed -n '2,17p' "$0"; exit 1 ;;
    esac
done
case $app in
original) dir='The Sims 2.app' ;;
patched) dir='The Sims 2 (shader model 2).app' ;;
*) echo "unknown app $app" >&2; exit 1 ;;
esac
running=$(mac ssh 'ps -axc -o pid,command | grep -i -E "Sims|gdb" | grep -v grep || true')
[ -z "$running" ] || { echo "a game or debugger is already running there, not starting:" >&2; echo "$running" >&2; exit 2; }
envstr=
for e in "${envs[@]:-}"; do [ -n "$e" ] && envstr="$envstr $e"; done
if [ -n "$gdbfile" ]; then
    launch="( env$envstr gdb -batch -x $gdbfile ./\"The Sims 2\" > /Users/tiger/sims-run.out 2>&1 & )"
else
    launch="( env$envstr ./\"The Sims 2\" > /dev/null 2>&1 & )"
fi
mac ssh "rm -f /tmp/rdn-fps.log; cd \"/Volumes/Leopard/Users/tiger/Desktop/The Sims 2.localized/$dir/Contents/MacOS\" && $launch; sleep 30; ~/rdninput move 774 524; sleep 1; ~/rdninput click 774 524; sleep 4; ~/rdninput click 916 770; sleep $wait; ps -axc -o pid,ppid,command | grep -i -E 'Sims|gdb' | grep -v grep"
