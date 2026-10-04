#!/bin/bash
# Move the HD 7570 (both functions) between host drivers at runtime.
# Nothing persistent: driver_override is lost at reboot, when radeon and
# snd_hda_intel bind again by themselves.
#
#   scripts/card-bind.sh status
#   scripts/card-bind.sh vfio      unbind host drivers, bind vfio-pci
#   scripts/card-bind.sh none      unbind everything, leave the card driverless
#   scripts/card-bind.sh radeon    give the card back to radeon / snd_hda_intel
#
# Only ever touches the two addresses below. Needs sudo.

set -euo pipefail

VGA=${CARD_VGA:-0000:10:00.0}
AUDIO=${CARD_AUDIO:-0000:10:00.1}
EXPECT_ID=0x1002:0x675d

sys=/sys/bus/pci/devices

driver_of() {
    basename "$(readlink "$sys/$1/driver" 2>/dev/null)" 2>/dev/null || true
}

check_identity() {
    local id
    id="$(cat "$sys/$VGA/vendor"):$(cat "$sys/$VGA/device")"
    [ "$id" = "$EXPECT_ID" ] || { echo "$VGA is $id, expected $EXPECT_ID; refusing" >&2; exit 1; }
    [ "$(cat "$sys/$VGA/boot_vga")" = 0 ] || { echo "$VGA is the boot VGA; refusing" >&2; exit 1; }
}

unbind_all() {
    # Release the framebuffer console first if it sits on this card.
    if [ "$(driver_of "$VGA")" = radeon ]; then
        for v in /sys/class/vtconsole/vtcon*; do
            grep -q 'frame buffer' "$v/name" && echo 0 | sudo tee "$v/bind" > /dev/null
        done
    fi
    for d in "$AUDIO" "$VGA"; do
        if [ -e "$sys/$d/driver" ]; then
            echo "$d" | sudo tee "$sys/$d/driver/unbind" > /dev/null
        fi
    done
}

set_override() {
    for d in "$VGA" "$AUDIO"; do
        echo "$1" | sudo tee "$sys/$d/driver_override" > /dev/null
    done
}

probe() {
    for d in "$VGA" "$AUDIO"; do
        echo "$d" | sudo tee /sys/bus/pci/drivers_probe > /dev/null
    done
}

status() {
    for d in "$VGA" "$AUDIO"; do
        echo "$d driver=$(driver_of "$d") override=$(cat "$sys/$d/driver_override")"
    done
}

check_identity
case "${1:-status}" in
status)
    status
    ;;
vfio)
    unbind_all
    sudo modprobe vfio-pci
    set_override vfio-pci
    probe
    status
    ;;
none)
    unbind_all
    # An override naming no real driver keeps radeon from re-binding on probe.
    set_override none
    status
    ;;
radeon)
    unbind_all
    set_override ""
    sudo modprobe radeon
    probe
    status
    ;;
*)
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
