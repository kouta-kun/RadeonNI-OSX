#!/bin/bash
# Build and load the kext in the running Tiger guest.
#
#   scripts/kext.sh build      copy hw/ and kext/ to the guest and build
#   scripts/kext.sh load       copy the bundle to a root-owned temporary
#                              directory and kextload it from there
#   scripts/kext.sh activate   restart the guest's window server so that it
#                              uses the new screen (ends the login session)
#   scripts/kext.sh up         build, load and activate
#   scripts/kext.sh unload
#   scripts/kext.sh log        kernel log lines from the driver
#
#   scripts/kext.sh install    install into /System/Library/Extensions so
#                              that it loads at boot like a real driver
#   scripts/kext.sh uninstall
#
# load/activate is the quick loop while developing; install is how the
# driver is meant to run.

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
gssh() { "$root/scripts/tiger.sh" ssh "$@"; }
ID=org.osxgpu.driver.RadeonNI
SLE=/System/Library/Extensions

# The VBIOS is not in the repository or in the built kext: it is added to
# the personality here, as a data property, from the host's dump.
make_plist() {
    local vbios=$root/private/vbios.rom
    [ -f "$vbios" ] || { echo "missing $vbios" >&2; exit 1; }
    mkdir -p "$root/build"
    python3 - "$root/kext/RadeonNI/Info.plist" "$vbios" > "$root/build/kext-Info.plist" <<'PY'
import base64, sys
plist = open(sys.argv[1]).read()
data = base64.b64encode(open(sys.argv[2], "rb").read()).decode()
marker = "\t\t\t<key>IOProviderClass</key>"
assert marker in plist
sys.stdout.write(plist.replace(marker, "\t\t\t<key>VBIOS</key>\n\t\t\t<data>" + data + "</data>\n" + marker))
PY
}

case "${1:-}" in
build)
    tar -C "$root" -cf - hw kext | gssh 'rm -rf ~/osx-gpu && mkdir -p ~/osx-gpu && tar -C ~/osx-gpu -xf -'
    gssh 'cd ~/osx-gpu/kext/RadeonNI && make 2>&1 && ls -l build/RadeonNI.kext/Contents/MacOS/ && file build/RadeonNI.kext/Contents/MacOS/RadeonNI'
    ;;
load)
    make_plist
    gssh 'sudo rm -rf /tmp/rdnkext && sudo mkdir /tmp/rdnkext &&
        sudo cp -R ~/osx-gpu/kext/RadeonNI/build/RadeonNI.kext /tmp/rdnkext/ && cat > /tmp/rdn-Info.plist &&
        sudo cp /tmp/rdn-Info.plist /tmp/rdnkext/RadeonNI.kext/Contents/Info.plist &&
        sudo chown -R root:wheel /tmp/rdnkext && sudo chmod -R go-w /tmp/rdnkext &&
        sudo sync && sudo kextload -t /tmp/rdnkext/RadeonNI.kext; kextstat | grep -i osxgpu' < "$root/build/kext-Info.plist"
    ;;
install)
    # Install like a real driver: the system loads it at boot when it finds
    # the card, before the window server starts. Takes effect at the next
    # guest boot. If it ever stops the guest from booting, boot without the
    # card (scripts/tiger.sh run): the kext then matches nothing and is not
    # loaded, and "scripts/kext.sh uninstall" removes it.
    make_plist
    gssh "sudo rm -rf $SLE/RadeonNI.kext &&
        sudo cp -R ~/osx-gpu/kext/RadeonNI/build/RadeonNI.kext $SLE/ && cat > /tmp/rdn-Info.plist &&
        sudo cp /tmp/rdn-Info.plist $SLE/RadeonNI.kext/Contents/Info.plist &&
        sudo chown -R root:wheel $SLE/RadeonNI.kext && sudo chmod -R go-w $SLE/RadeonNI.kext &&
        sudo kextload -t -n $SLE/RadeonNI.kext &&
        sudo rm -f /System/Library/Extensions.mkext /System/Library/Extensions.kextcache &&
        sudo touch $SLE && sudo sync && ls -ld $SLE/RadeonNI.kext" < "$root/build/kext-Info.plist"
    ;;
uninstall)
    gssh "sudo rm -rf $SLE/RadeonNI.kext &&
        sudo rm -f /System/Library/Extensions.mkext /System/Library/Extensions.kextcache &&
        sudo touch $SLE && sudo sync && echo removed"
    ;;
activate)
    # Tiger's window server only looks for framebuffers when it starts, so a
    # kext loaded afterwards drives the card but gets no desktop until the
    # window server is restarted. That ends the login session; the guest
    # logs in again by itself.
    gssh 'sudo killall WindowServer; n=0
        until ioreg -p IOService -w0 | grep -A2 "RadeonNI " | grep -q IODisplayConnect; do
            n=$((n+1)); [ $n -gt 30 ] && { echo "the window server did not attach"; exit 1; }
            sleep 2
        done
        sleep 4
        system_profiler SPDisplaysDataType 2>/dev/null | grep -A12 "pci1002" | grep -E "Resolution|Depth"'
    ;;
up)
    "$0" build && "$0" load && "$0" activate
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
