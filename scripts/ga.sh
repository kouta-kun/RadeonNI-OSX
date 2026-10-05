#!/bin/bash
# Build and install the 2D accelerator plug-in in the running Tiger guest.
#
#   scripts/ga.sh build      copy ga/ and hw/rdn_user.h to the guest and build
#   scripts/ga.sh install    copy the plug-in to /System/Library/Extensions
#   scripts/ga.sh uninstall  remove it from there
#
# The system loads the plug-in the framebuffer names (IOCFPlugInTypes) from
# /System/Library/Extensions; the kext names it when it is loaded with
# RDN_GA=1.

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
gssh() { "$root/scripts/tiger.sh" ssh "$@"; }
SLE=/System/Library/Extensions
NAME=RadeonNIGA

case "${1:-}" in
build)
    tar -C "$root" -cf - ga hw/rdn_user.h | gssh 'rm -rf ~/osx-gpu-ga && mkdir -p ~/osx-gpu-ga && tar -C ~/osx-gpu-ga -xf -'
    gssh "cd ~/osx-gpu-ga/ga && make 2>&1 && file build/$NAME.plugin/Contents/MacOS/$NAME"
    ;;
install)
    gssh "sudo rm -rf $SLE/$NAME.plugin &&
        sudo cp -R ~/osx-gpu-ga/ga/build/$NAME.plugin $SLE/ &&
        sudo chown -R root:wheel $SLE/$NAME.plugin && sudo chmod -R go-w $SLE/$NAME.plugin &&
        sudo sync && ls -ld $SLE/$NAME.plugin"
    ;;
uninstall)
    gssh "sudo rm -rf $SLE/$NAME.plugin && sudo sync && echo removed"
    ;;
*)
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
