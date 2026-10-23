#!/bin/bash
# Frame rate of The Sims 2 in Pleasantview, measured without anyone touching the G5.
#
#   scripts/sims2-bench.sh [-a original|patched] [-e VAR=value]... [-n runs] [-s seconds]
#
# Each run starts the game (scripts/sims2-run.sh: menu clicks into the neighborhood), waits for the
# frame counter, averages RDN_FPS's lines of the last -s seconds (default 40), and stops the game by
# its pid. The camera is whatever the game does by itself (it drifts when idle), so compare runs of
# the same kind. Refuses to start while a Sims or gdb process runs there.
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
mac() { "$root/scripts/mac.sh" g5 "$@"; }
app=original; runs=1; secs=40; args=()
while getopts "a:e:n:s:" o; do
    case $o in
    a) app=$OPTARG ;;
    e) args+=(-e "$OPTARG") ;;
    n) runs=$OPTARG ;;
    s) secs=$OPTARG ;;
    *) sed -n '2,12p' "$0"; exit 1 ;;
    esac
done
for ((i = 1; i <= runs; i++)); do
    "$root/scripts/sims2-run.sh" -a "$app" -e RDN_FPS=1 "${args[@]:-}" -w 45 > /dev/null
    sleep 30
    pid=$(mac ssh 'ps -axc -o pid,command | grep "The Sims 2" | grep -v grep | head -1 | awk "{print \$1}"')
    log=$(mac ssh 'cat /tmp/rdn-fps.log')
    [ -z "$pid" ] || mac ssh "kill $pid"
    echo "$log" | python3 -I -c '
import re, sys
rows = [(int(m.group(2)), float(m.group(1))) for m in (re.search(r"fps ([\d.]+) .* at (\d+) s", l) for l in sys.stdin) if m]
end = rows[-1][0]
w = [f for t, f in rows if t > end - '"$secs"']
print("run '"$i"': %d s logged; last '"$secs"' s: avg %.1f fps (min %.1f, max %.1f, %d samples)" % (end, sum(w) / len(w), min(w), max(w), len(w)))
'
    sleep 6
done
