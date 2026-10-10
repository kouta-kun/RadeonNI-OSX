#!/bin/sh
# Put the Radeon HD 7570 on Open Firmware's console on a Power Mac G5
# (PowerMac11,2, Open Firmware 5.2.7f1), and make Tiger boot with the card
# already running.
#
#   sudo ./of-install.sh [--yes]
#
# What it does:
#   1. copies rdnk.elf (the Open Firmware client) to
#      /Library/RadeonNI/OpenFirmware on the boot volume;
#   2. appends a block, between the comments "( RadeonNI-OF begin )" and
#      "( RadeonNI-OF end )", to the nvramrc variable, and sets use-nvramrc?
#      to true. What nvramrc held before is kept, and the old text and the old
#      value of use-nvramrc? are saved in /Library/RadeonNI/OpenFirmware.
#
# The block patches two Open Firmware words in RAM, after checking that they
# are what Open Firmware 5.2.7f1 has (on any other version it does nothing).
# At the first Open Firmware prompt (Cmd-Opt-O-F at the chime) it runs the
# client, so the console is on the 7570's monitor; in a normal boot it runs
# the client before Tiger, which brings the card up and starts Tiger. If the
# file cannot be loaded, or the client fails, the boot goes on as if nothing
# was installed.
#
# The block finds the file with hd:,\... (the boot volume, whatever its
# partition number). If the NVRAM is lost (a reset with Cmd-Opt-P-R, a dead
# battery), the block is gone: run this script again from Tiger.
# RadeonNI.kext (install.sh) checks for the block and says so if it is missing.
#
# Nothing is written to the card. To undo: sudo ./of-uninstall.sh
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT

set -e

DEST=/Library/RadeonNI/OpenFirmware
here=$(cd "$(dirname "$0")" && pwd)

fail() { echo "of-install.sh: $*" >&2; exit 1; }

yes=0
for arg in "$@"; do
    case "$arg" in
    --yes) yes=1 ;;
    *) fail "unknown option $arg" ;;
    esac
done

[ "$(id -u)" = 0 ] || fail "run as root: sudo ./of-install.sh"
case "$(sw_vers -productVersion)" in
10.4*) ;;
*) fail "this is for Mac OS X 10.4; this is $(sw_vers -productVersion)" ;;
esac
[ "$(uname -p)" = powerpc ] || fail "PowerPC Macs only"
[ -f "$here/rdnk.elf" ] || fail "rdnk.elf not found next to this script"
[ -f "$here/of-block.txt" ] || fail "of-block.txt not found next to this script"

rom=$(system_profiler SPHardwareDataType 2>/dev/null | sed -n 's/.*Boot ROM Version: *//p')
echo "Boot ROM version: ${rom:-unknown}"
case "$rom" in
5.2.7f1*) ;;
*) echo "This is not the Open Firmware this was written for (5.2.7f1). The block checks"
   echo "before patching and will do nothing, so the Mac boots as before." ;;
esac

block=$(cat "$here/of-block.txt")
current=$(nvram nvramrc 2>/dev/null | sed -n 's/^nvramrc[[:space:]]//p')
use=$(nvram 'use-nvramrc?' 2>/dev/null | sed -n 's/^use-nvramrc?[[:space:]]//p')

# the old block (an earlier install) comes out; everything else stays
stripped=$(printf '%s' "$current" | perl -0pe 's/ ?\( RadeonNI-OF begin \).*?\( RadeonNI-OF end \)//s')
new="$stripped $block"
[ -n "$stripped" ] || new="$block"
[ ${#new} -lt 4000 ] || fail "nvramrc would be ${#new} characters; too long to be safe"

echo "nvramrc now:        ${current:-(empty)}"
echo "use-nvramrc? now:   ${use:-unknown}"
if [ -n "$stripped" ] && [ "$use" != true ]; then
    echo "WARNING: nvramrc has text of its own and use-nvramrc? is off, so that text is not"
    echo "running today. Turning use-nvramrc? on makes it run at every boot."
fi
echo "This will write nvramrc (${#new} characters) and set use-nvramrc? to true in the"
echo "Mac's NVRAM, and copy rdnk.elf to $DEST."
if [ "$yes" = 0 ]; then
    printf 'Go ahead? [y/N] '
    read answer
    case "$answer" in y|Y|yes) ;; *) echo "Nothing changed."; exit 1 ;; esac
fi

mkdir -p "$DEST"
if [ ! -f "$DEST/nvramrc.orig" ]; then
    printf '%s\n' "$stripped" > "$DEST/nvramrc.orig"
    printf '%s\n' "${use:-false}" > "$DEST/use-nvramrc.orig"
fi
cp "$here/rdnk.elf" "$DEST/rdnk.elf"
chown -R root:wheel /Library/RadeonNI
chmod 644 "$DEST/rdnk.elf"

nvram "nvramrc=$new"
nvram 'use-nvramrc?=true'
back=$(nvram nvramrc | sed -n 's/^nvramrc[[:space:]]//p')
[ "$back" = "$new" ] || fail "nvramrc reads back differently from what was written; run of-uninstall.sh"
sync

echo "Done. The block is in place; restart the Mac. Cmd-Opt-O-F at the chime gives the"
echo "Open Firmware console on the 7570."
echo "If the NVRAM is ever reset, run this script again."
