#!/bin/sh
# Take the Open Firmware block out of nvramrc (what else is there stays),
# put use-nvramrc? back to what it was and remove the client. See of-install.sh.
#
#   sudo ./of-uninstall.sh
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT

set -e

DEST=/Library/RadeonNI/OpenFirmware

[ "$(id -u)" = 0 ] || { echo "of-uninstall.sh: run as root" >&2; exit 1; }

current=$(nvram nvramrc 2>/dev/null | sed -n 's/^nvramrc[[:space:]]//p')
stripped=$(printf '%s' "$current" | perl -0pe 's/ ?\( RadeonNI-OF begin \).*?\( RadeonNI-OF end \)//s')
nvram "nvramrc=$stripped"
if [ -n "$stripped" ]; then
    echo "nvramrc keeps its other content; use-nvramrc? stays as it is."
else
    old=false
    [ ! -f "$DEST/use-nvramrc.orig" ] || old=$(cat "$DEST/use-nvramrc.orig")
    nvram "use-nvramrc?=$old"
fi
rm -rf "$DEST"
rmdir /Library/RadeonNI 2>/dev/null || true
sync
echo "Removed. Restart the Mac; a restart makes sure the NVRAM change is written."
