#!/bin/bash
# Build and install the OpenGL driver bundle in the running Tiger guest.
#
#   scripts/gld.sh build      copy gld/ to the guest and build
#   scripts/gld.sh install    copy the bundle to /System/Library/Extensions
#   scripts/gld.sh uninstall  remove it from there
#   scripts/gld.sh log        the bundle's call log
#   scripts/gld.sh clearlog
#
# OpenGL loads the bundle the accelerator names (IOGLBundleName) from
# /System/Library/Extensions, so unlike the kext it has to be installed.
# It is only used while the kext announces its accelerator
# (RDN_ACCEL=1 scripts/kext.sh up).

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
gssh() { "$root/scripts/tiger.sh" ssh "$@"; }
SLE=/System/Library/Extensions
NAME=RadeonNIGLDriver

case "${1:-}" in
build)
    tar -C "$root" -cf - gld | gssh 'rm -rf ~/osx-gpu-gld && mkdir -p ~/osx-gpu-gld && tar -C ~/osx-gpu-gld -xf -'
    gssh "cd ~/osx-gpu-gld/gld && make 2>&1 && file build/$NAME.bundle/Contents/MacOS/$NAME"
    ;;
install)
    gssh "sudo rm -rf $SLE/$NAME.bundle &&
        sudo cp -R ~/osx-gpu-gld/gld/build/$NAME.bundle $SLE/ &&
        sudo chown -R root:wheel $SLE/$NAME.bundle && sudo chmod -R go-w $SLE/$NAME.bundle &&
        sudo sync && ls -ld $SLE/$NAME.bundle"
    ;;
uninstall)
    gssh "sudo rm -rf $SLE/$NAME.bundle && sudo sync && echo removed"
    ;;
log)
    gssh 'cat /tmp/rdngld.log 2>/dev/null || echo "no log"'
    ;;
clearlog)
    gssh 'sudo rm -f /tmp/rdngld.log'
    ;;
*)
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
