#!/bin/bash
# scripts/sims2-cpu.sh [-a app] [-e VAR=val]... : main-thread CPU ms per frame of The Sims 2 in Pleasantview.
# Starts the game, waits for the neighborhood to settle, samples 10 s, reads RDN_FPS's lines of the same time.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
mac() { "$root/scripts/mac.sh" g5 "$@"; }
args=(); app=patched
while getopts "a:e:" o; do case $o in a) app=$OPTARG ;; e) args+=(-e "$OPTARG") ;; esac; done
"$root/scripts/sims2-run.sh" -a "$app" -e RDN_FPS=1 "${args[@]}" -w 55 >/dev/null
pid=$(mac ssh 'ps -axc -o pid,command | grep "The Sims 2" | grep -v grep | cut -c1-6' | tr -d ' ')
mac ssh "sample $pid 10 >/dev/null 2>&1; tail -n 12 /tmp/rdn-fps.log" > /tmp/sims2-cpu.fps
mac get "/tmp/The Sims 2_$pid.sample.txt" /tmp/sims2-cpu.sample >/dev/null
mac ssh "kill $pid"
python3 - <<PY
import re, subprocess
out = subprocess.run(["python3", "$root/scripts/sample-profile.py", "/tmp/sims2-cpu.sample"], capture_output=True, text=True).stdout
blk = out.split("== thread 1 ")[0]
total = int(re.search(r"(\d+) samples", blk).group(1))
idle = sum(int(m.group(1)) for m in re.finditer(r"^\s+(\d+)\s+[\d.]+%\s+(?:mach_msg_trap|semaphore_\w+|mach_wait_until)\b", blk, re.M))
fps = [float(x) for x in re.findall(r"fps ([\d.]+)", open("/tmp/sims2-cpu.fps").read())[-10:]]
f = sum(fps) / len(fps)
busy = 1 - idle / total
print("busy %.0f%%  fps %.1f  main-thread ms per frame %.1f" % (100 * busy, f, 1000 * busy / f))
PY
