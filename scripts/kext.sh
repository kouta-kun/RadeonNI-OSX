#!/bin/bash
# Build and load the kext in the running Tiger guest.
#
#   scripts/kext.sh build      copy hw/ and kext/ to the guest and build
#   scripts/kext.sh load       copy the bundle to a root-owned temporary
#                              directory and kextload it from there
#   scripts/kext.sh unload
#   scripts/kext.sh log        kernel log lines from the driver
#
# The kext is never installed in /System/Library/Extensions: a guest reboot
# always removes it.

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
gssh() { "$root/scripts/tiger.sh" ssh "$@"; }
ID=org.osxgpu.driver.RadeonNI

case "${1:-}" in
build)
    tar -C "$root" -cf - hw kext | gssh 'rm -rf ~/osx-gpu && mkdir -p ~/osx-gpu && tar -C ~/osx-gpu -xf -'
    gssh 'cd ~/osx-gpu/kext/RadeonNI && make 2>&1 && ls -l build/RadeonNI.kext/Contents/MacOS/ && file build/RadeonNI.kext/Contents/MacOS/RadeonNI'
    ;;
load)
    gssh 'sudo rm -rf /tmp/rdnkext && sudo mkdir /tmp/rdnkext &&
        sudo cp -R ~/osx-gpu/kext/RadeonNI/build/RadeonNI.kext /tmp/rdnkext/ &&
        sudo chown -R root:wheel /tmp/rdnkext && sudo chmod -R go-w /tmp/rdnkext &&
        sudo sync && sudo kextload -t /tmp/rdnkext/RadeonNI.kext; kextstat | grep -i osxgpu'
    ;;
unload)
    gssh "sudo kextunload -b $ID; kextstat | grep -ci osxgpu || true"
    ;;
log)
    gssh 'sudo dmesg | grep RadeonNI | tail -40'
    ;;
*)
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
