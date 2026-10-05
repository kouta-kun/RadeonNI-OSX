#!/bin/bash
# Keep a fatal PCIe error of the HD 7570 from resetting the host.
#
#   scripts/card-quiet.sh apply     stop the card from reporting errors and
#                                   the root port from forwarding them
#   scripts/card-quiet.sh status    show the relevant bits
#
# Why: on 2026-10-05 the host reset with "an uncorrected error caused a data
# fabric sync flood event" during GPU tests (docs/JOURNAL.md). Approved by
# the user the same day. Nothing here is persistent: a host reboot undoes
# all of it, and a reset of the card (scripts/card-reset.sh, QEMU starting
# with the card attached) undoes the card's half, so apply it again after
# either. Needs sudo. Only touches the card's two functions and the root
# port they hang from.
#
# What it sets:
#   card, both functions: PCIe Device Control, the four error reporting
#     enables off; AER uncorrectable and correctable masks all ones
#   root port: Bridge Control, SERR forwarding off

set -euo pipefail

VGA=${CARD_VGA:-0000:10:00.0}
AUDIO=${CARD_AUDIO:-0000:10:00.1}
EXPECT_ID=0x1002:0x675d
sys=/sys/bus/pci/devices

id="$(cat $sys/$VGA/vendor):$(cat $sys/$VGA/device)"
[ "$id" = "$EXPECT_ID" ] || { echo "$VGA is $id, not $EXPECT_ID" >&2; exit 1; }
PORT=$(basename "$(dirname "$(readlink -f $sys/$VGA)")")

show() {
    for d in "$PORT" "$VGA" "$AUDIO"; do
        echo "== $d"
        sudo lspci -s "${d#0000:}" -vvv | grep -E "BridgeCtl|DevCtl:|DevSta:|UEMsk|CEMsk|UESta|CESta" | sed 's/^\s*/   /'
    done
}

case "${1:-}" in
apply)
    for d in "$VGA" "$AUDIO"; do
        # Device Control bits 0-3: correctable, non-fatal, fatal, unsupported request.
        sudo setpci -s "${d#0000:}" CAP_EXP+8.w=0000:000f
        if sudo setpci -s "${d#0000:}" ECAP_AER+8.l > /dev/null 2>&1; then
            sudo setpci -s "${d#0000:}" ECAP_AER+8.l=ffffffff
            sudo setpci -s "${d#0000:}" ECAP_AER+14.l=ffffffff
        fi
    done
    # Bridge Control bit 1: forward errors from the card as system errors.
    sudo setpci -s "${PORT#0000:}" BRIDGE_CONTROL.w=0000:0002
    show
    ;;
status)
    show
    ;;
*)
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
