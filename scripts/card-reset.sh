#!/bin/bash
# Return the HD 7570 to the un-POSTed (cold) state without rebooting the host:
# a secondary bus reset issued through the root port it hangs from. The bus
# behind that port holds only the card's two functions.
#
#   scripts/card-reset.sh
#
# Refuses while radeon or snd_hda_intel own the card. Needs sudo.

set -euo pipefail

VGA=${CARD_VGA:-0000:10:00.0}
AUDIO=${CARD_AUDIO:-0000:10:00.1}
sys=/sys/bus/pci/devices

for d in "$VGA" "$AUDIO"; do
    drv=$(basename "$(readlink "$sys/$d/driver" 2>/dev/null)" 2>/dev/null || true)
    case "$drv" in
    ""|vfio-pci) ;;
    *) echo "$d is owned by $drv; run scripts/card-bind.sh none first" >&2; exit 1 ;;
    esac
done

bridge=$(basename "$(dirname "$(readlink -f "$sys/$VGA")")")
others=$(ls "$sys/$bridge" | grep -E '^[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\.[0-9a-f]$' | grep -v -e "$VGA" -e "$AUDIO" || true)
[ -z "$others" ] || { echo "other devices behind $bridge: $others; refusing" >&2; exit 1; }

echo 1 | sudo tee "$sys/$bridge/reset_subordinate" > /dev/null
echo "bus reset issued through $bridge"
