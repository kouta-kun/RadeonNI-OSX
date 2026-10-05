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
    # The VBIOS is not in the repository or in the built kext: it is added to
    # the personality here, as a data property, from the host's dump.
    vbios=$root/private/vbios.rom
    [ -f "$vbios" ] || { echo "missing $vbios" >&2; exit 1; }
    python3 - "$root/kext/RadeonNI/Info.plist" "$vbios" > "$root/build/kext-Info.plist" <<'PY'
import base64, sys
plist = open(sys.argv[1]).read()
data = base64.b64encode(open(sys.argv[2], "rb").read()).decode()
marker = "\t\t\t<key>IOProviderClass</key>"
assert marker in plist
sys.stdout.write(plist.replace(marker, "\t\t\t<key>VBIOS</key>\n\t\t\t<data>" + data + "</data>\n" + marker))
PY
    gssh 'sudo rm -rf /tmp/rdnkext && sudo mkdir /tmp/rdnkext &&
        sudo cp -R ~/osx-gpu/kext/RadeonNI/build/RadeonNI.kext /tmp/rdnkext/ && cat > /tmp/rdn-Info.plist &&
        sudo cp /tmp/rdn-Info.plist /tmp/rdnkext/RadeonNI.kext/Contents/Info.plist &&
        sudo chown -R root:wheel /tmp/rdnkext && sudo chmod -R go-w /tmp/rdnkext &&
        sudo sync && sudo kextload -t /tmp/rdnkext/RadeonNI.kext; kextstat | grep -i osxgpu' < "$root/build/kext-Info.plist"
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
