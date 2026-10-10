#!/bin/sh
# Install the Radeon HD 7570 Open Firmware support from Linux on a Power Mac G5
# (PowerMac11,2): the console on the card's monitor at the "ok" prompt
# (Cmd-Opt-O-F), and the card POSTed at 1920x1080 before the boot loader runs.
# This is the Linux counterpart of the Mac OS X package's of-install.sh; the
# client and the nvramrc block are the same (docs/OPEN-FIRMWARE.md).
#
#   sudo ./install.sh --partition /dev/sda2 [--yes]
#   sudo ./install.sh --mountpoint /boot/efi-like/dir --of-device hd:2, [--yes]
#
# What it does:
#   1. copies rdnk.elf to RadeonNI/ on a volume Open Firmware can read: the
#      HFS or HFS+ boot partition (Apple_Bootstrap) of yaboot or GRUB. Open
#      Firmware cannot read ext4, btrfs or xfs;
#   2. appends a block, between the comments "( RadeonNI-OF begin )" and
#      "( RadeonNI-OF end )", to the nvramrc variable, and sets use-nvramrc?
#      to true. What nvramrc held before is kept and saved in
#      /var/lib/radeonni-of.
#
# The block patches two Open Firmware words in RAM, after checking that they
# hold what Open Firmware 5.2.7f1 (the PowerMac11,2) has; elsewhere it does
# nothing. If the file cannot be loaded, or the client fails, the boot goes on
# as if nothing was installed. Nothing is written to the card or to flash.
# If the NVRAM is lost (Cmd-Opt-P-R, a dead battery) the block is gone: run
# this script again. To undo: sudo ./uninstall.sh
#
# Options:
#   --partition DEV    the HFS/HFS+ boot partition (mounted here if it is not)
#   --mountpoint DIR   use this directory as the volume (needs --of-device)
#   --of-device STR    the Open Firmware device of that volume, with the
#                      trailing comma, e.g. hd:2, (default: hd:N, where N is
#                      DEV's number in the Apple partition map; "hd" is the
#                      internal disk that Open Firmware boots from)
#   --nvram-cmd CMD    how to write NVRAM: powerpc-utils' nvram (default,
#                      "nvram"), or "nvsetenv"
#   --print-only       write nothing; print the nvramrc text and what would be
#                      copied, for pasting at the Open Firmware prompt by hand
#   --yes              do not ask
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT

set -e

STATE=${RADEONNI_STATE:-/var/lib/radeonni-of}
here=$(cd "$(dirname "$0")" && pwd)
. "$here/nvram-lib.sh"

part= mnt= ofdev= yes=0 printonly=0
while [ $# -gt 0 ]; do
    case "$1" in
    --partition) part=$2; shift ;;
    --mountpoint) mnt=$2; shift ;;
    --of-device) ofdev=$2; shift ;;
    --nvram-cmd) NVRAM_CMD=$2; shift ;;
    --print-only) printonly=1 ;;
    --yes) yes=1 ;;
    *) fail "unknown option $1" ;;
    esac
    shift
done

[ "$(uname -s)" = Linux ] || fail "this is for Linux; on Mac OS X use of-install.sh"
case "$(uname -m)" in ppc|ppc64|ppc64le) ;; *) fail "PowerPC Macs only" ;; esac
[ -f "$here/rdnk.elf" ] || fail "rdnk.elf not found next to this script"
[ -f "$here/of-block.tmpl" ] || fail "of-block.tmpl not found next to this script"
[ "$printonly" = 1 ] || [ "$(id -u)" = 0 ] || fail "run as root: sudo ./install.sh"

if [ -r /proc/device-tree/model ]; then
    model=$(tr -d '\0' < /proc/device-tree/model)
    echo "Machine: $model"
    case "$model" in
    PowerMac11,2) ;;
    *) echo "This was written for the PowerMac11,2. The block checks Open Firmware before"
       echo "patching and does nothing on another, so the Mac boots as before." ;;
    esac
fi

# --- the volume --------------------------------------------------------------
mounted_here=
if [ -n "$part" ]; then
    [ -b "$part" ] || fail "$part is not a block device"
    name=${part##*/}
    if [ -z "$ofdev" ]; then
        n=$(cat "/sys/class/block/$name/partition" 2>/dev/null) || fail "cannot tell $part's partition number; give --of-device"
        disk=$(readlink -f "/sys/class/block/$name/..")
        disk=/dev/${disk##*/}
        label=$(blkid -p -o value -s PTTYPE "$disk" 2>/dev/null || true)
        [ "$label" = mac ] || fail "$disk has a '${label:-unknown}' partition table; Open Firmware here boots from an Apple partition map. Give --of-device if you know the right device"
        ofdev="hd:$n,"
    fi
    mnt=$(findmnt -n -o TARGET "$part" 2>/dev/null | head -n 1 || true)
    if [ -z "$mnt" ]; then
        mnt=$(mktemp -d)
        mount -o rw "$part" "$mnt" || { rmdir "$mnt"; fail "cannot mount $part read-write"; }
        mounted_here=$mnt
    fi
    case "$(blkid -p -o value -s TYPE "$part" 2>/dev/null)" in
    hfs|hfsplus) ;;
    *) [ -z "$mounted_here" ] || umount "$mnt"; fail "$part is not HFS or HFS+: Open Firmware cannot read it" ;;
    esac
else
    [ -n "$mnt" ] && [ -n "$ofdev" ] || fail "give --partition DEV, or --mountpoint DIR with --of-device"
fi
cleanup() { [ -z "$mounted_here" ] || { umount "$mounted_here" 2>/dev/null; rmdir "$mounted_here" 2>/dev/null; }; }
trap cleanup EXIT
case "$ofdev" in *,) ;; *) fail "--of-device must end with a comma, e.g. hd:2," ;; esac
[ -d "$mnt" ] || fail "$mnt is not a directory"

# --- the nvramrc text ----------------------------------------------------------
block=$(sed "s|@OFDEV@|$ofdev|g" "$here/of-block.tmpl")
nvram_load                        # sets current, use
stripped=$(printf '%s' "$current" | strip_block)
new="$stripped $block"
[ -n "$stripped" ] || new="$block"
[ ${#new} -lt 4000 ] || fail "nvramrc would be ${#new} characters; too long to be safe"

echo "Volume:             $mnt   (Open Firmware: ${ofdev}\\RadeonNI\\rdnk.elf)"
echo "nvramrc now:        ${current:-(empty)}"
echo "use-nvramrc? now:   ${use:-unknown}"

if [ "$printonly" = 1 ]; then
    echo
    echo "Copy rdnk.elf to RadeonNI/ on that volume, then set nvramrc to:"
    echo
    printf '%s\n' "$new"
    exit 0
fi

if [ -n "$stripped" ] && [ "$use" != true ]; then
    echo "WARNING: nvramrc has text of its own and use-nvramrc? is off, so that text is not"
    echo "running today. Turning use-nvramrc? on makes it run at every boot."
fi
echo "This will write nvramrc (${#new} characters) and set use-nvramrc? to true in the"
echo "Mac's NVRAM, and copy rdnk.elf to $mnt/RadeonNI."
if [ "$yes" = 0 ]; then
    printf 'Go ahead? [y/N] '
    read answer
    case "$answer" in y|Y|yes) ;; *) echo "Nothing changed."; exit 1 ;; esac
fi

mkdir -p "$STATE"
if [ ! -f "$STATE/nvramrc.orig" ]; then
    printf '%s\n' "$stripped" > "$STATE/nvramrc.orig"
    printf '%s\n' "${use:-false}" > "$STATE/use-nvramrc.orig"
fi
mkdir -p "$mnt/RadeonNI"
cp "$here/rdnk.elf" "$mnt/RadeonNI/rdnk.elf"
cmp -s "$here/rdnk.elf" "$mnt/RadeonNI/rdnk.elf" || fail "the copy on $mnt differs from rdnk.elf"
sync
# remember where the file is, for uninstall.sh
{ echo "part=$part"; echo "mnt=$mnt"; echo "ofdev=$ofdev"; } > "$STATE/volume"

nvram_set nvramrc "$new"
nvram_set 'use-nvramrc?' true
nvram_load
[ "$current" = "$new" ] || fail "nvramrc reads back differently from what was written; run uninstall.sh"
sync

echo "Done. The block is in place; restart the Mac. Cmd-Opt-O-F at the chime gives the"
echo "Open Firmware console on the 7570 (the wired keyboard must be plugged in before"
echo "power-up). If the NVRAM is ever reset, run this script again."
