#!/bin/sh
# Take the Open Firmware block out of nvramrc (what else is there stays), put
# use-nvramrc? back to what it was and remove the client. See install.sh.
#
#   sudo ./uninstall.sh [--nvram-cmd CMD]
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT

set -e

STATE=${RADEONNI_STATE:-/var/lib/radeonni-of}
here=$(cd "$(dirname "$0")" && pwd)
. "$here/nvram-lib.sh"

while [ $# -gt 0 ]; do
    case "$1" in
    --nvram-cmd) NVRAM_CMD=$2; shift ;;
    *) fail "unknown option $1" ;;
    esac
    shift
done
[ "$(id -u)" = 0 ] || fail "run as root: sudo ./uninstall.sh"

nvram_load
stripped=$(printf '%s' "$current" | strip_block)
nvram_set nvramrc "$stripped"
if [ -n "$stripped" ]; then
    echo "nvramrc keeps its other content; use-nvramrc? stays as it is."
else
    old=false
    [ ! -f "$STATE/use-nvramrc.orig" ] || old=$(cat "$STATE/use-nvramrc.orig")
    nvram_set 'use-nvramrc?' "$old"
fi

# the client, from the volume it was put on
if [ -f "$STATE/volume" ]; then
    part= mnt=
    . "$STATE/volume"
    m=$mnt tmp=
    if [ -n "$part" ] && ! findmnt -n "$part" > /dev/null 2>&1; then
        tmp=$(mktemp -d)
        if mount -o rw "$part" "$tmp" 2>/dev/null; then m=$tmp; else rmdir "$tmp"; tmp=; m=; fi
    elif [ -n "$part" ]; then
        m=$(findmnt -n -o TARGET "$part" | head -n 1)
    fi
    if [ -n "$m" ] && [ -d "$m/RadeonNI" ]; then
        rm -f "$m/RadeonNI/rdnk.elf"
        rmdir "$m/RadeonNI" 2>/dev/null || true
    else
        echo "Could not reach the volume; remove RadeonNI/rdnk.elf from it by hand."
    fi
    sync
    [ -z "$tmp" ] || { umount "$tmp"; rmdir "$tmp"; }
fi
rm -rf "$STATE"
echo "Removed. Restart the Mac; a restart makes sure the NVRAM change is written."
