#!/bin/sh
# Remove RadeonNI.kext from a Mac. Takes effect at the next restart.
#
#   sudo ./uninstall.sh
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT

set -e

SLE=/System/Library/Extensions

[ "$(id -u)" = 0 ] || { echo "run as root: sudo ./uninstall.sh" >&2; exit 1; }

if [ -d "$SLE/RadeonNI.kext" ]; then
    rm -rf "$SLE/RadeonNI.kext"
    rm -f /System/Library/Extensions.mkext /System/Library/Extensions.kextcache
    touch "$SLE"
    sync
    echo "Removed $SLE/RadeonNI.kext. Restart the Mac to stop the running driver."
else
    echo "RadeonNI.kext is not installed."
fi
