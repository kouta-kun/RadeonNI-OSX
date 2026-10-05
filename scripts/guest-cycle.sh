#!/bin/bash
# Shut the Tiger guest down and wait until its QEMU is gone, so that it can
# be started again (scripts/tiger.sh passthru, in the background) with a
# fresh kernel: a loaded framebuffer kext cannot be unloaded while the
# window server uses its screen.
#
#   scripts/guest-cycle.sh down    shut down and wait
#   scripts/guest-cycle.sh ready   wait until the guest answers ssh and its
#                                  Finder runs, then mask the card's PCIe
#                                  error escalation again (QEMU reset it)
#                                  and, with TIGER_EVDEV set, attach that
#                                  host keyboard/pointer (tiger.sh evdev)

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)

case "${1:-}" in
down)
    timeout 60 "$root/scripts/tiger.sh" ssh 'sudo shutdown -h now' > /dev/null 2>&1 || true
    while pgrep -x qemu-system-ppc > /dev/null; do sleep 2; done
    echo "guest is down"
    ;;
ready)
    until pgrep -x qemu-system-ppc > /dev/null &&
          timeout 20 "$root/scripts/tiger.sh" ssh 'ps axc | grep -q Finder' 2> /dev/null; do
        sleep 4
    done
    "$root/scripts/card-quiet.sh" apply > /dev/null
    echo "guest is up; card error escalation masked"
    if [ -n "${TIGER_EVDEV:-}" ]; then
        "$root/scripts/tiger.sh" evdev && echo "host input $TIGER_EVDEV attached"
    fi
    ;;
*)
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
