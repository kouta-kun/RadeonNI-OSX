#!/bin/bash
# Wait until the guest screen has stopped changing: two screenshots taken
# INTERVAL seconds apart are identical. Prints the screenshot path.
#   scripts/guest-wait.sh [interval-seconds] [max-seconds]
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
interval=${1:-10}
max=${2:-1800}
prev=""
start=$(date +%s)
while :; do
    "$root/scripts/guest-ctl.py" shot > /dev/null 2>&1 || { echo "guest not reachable"; exit 1; }
    cur=$(sha1sum < "$root/build/shot.png")
    [ "$cur" = "$prev" ] && { echo "stable: $root/build/shot.png"; exit 0; }
    prev=$cur
    [ $(( $(date +%s) - start )) -gt "$max" ] && { echo "timeout, still changing"; exit 2; }
    sleep "$interval"
done
