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
#   scripts/kext.sh uninstall  remove a copy installed in the guest's
#                              /System/Library/Extensions (see below)
#
# With RDN_ACCEL=1, load (and so up) also makes the kext start the 3D
# engine (the microcode comes from firmware/ on the host) and announce its
# accelerator service, which names the OpenGL driver bundle and serves user
# clients. RDN_SELFTEST=1 adds a drawing self-test on the screen at start.
# RDN_AGPSHIM=n adds the registry objects Tiger's window server looks for
# before it considers Quartz Extreme (see RadeonNIAccel.h): 1 the ancestor,
# 2 the registered shim, 3 both.
# RDN_SURFACES=1 makes the accelerator hand out surface user clients, which
# for now only accept and log what they are asked.
# RDN_GA=1 names the 2D accelerator plug-in (scripts/ga.sh) on the
# framebuffer.
# RDN_ACCELCAPS=n publishes an AccelCaps property of that value on the
# accelerator (experiments with the window server).
# Apply scripts/card-quiet.sh first.
#
# Under QEMU the kext is always loaded from the temporary directory, freshly
# built, so that what runs is never a stale installed copy. Installing into
# /System/Library/Extensions is for the real Mac and is done by the package
# from scripts/make-g5-package.sh; rehearsing that package in the guest is
# the only reason an installed copy should ever be there.

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
import base64, os, sys
plist = open(sys.argv[1]).read()
data = base64.b64encode(open(sys.argv[2], "rb").read()).decode()
marker = "\t\t\t<key>IOProviderClass</key>"
assert marker in plist
extra = "\t\t\t<key>VBIOS</key>\n\t\t\t<data>" + data + "</data>\n"
if os.environ.get("RDN_ACCEL") == "1":
    extra += "\t\t\t<key>Accelerator</key>\n\t\t\t<true/>\n"
    # The command processor's microcode, like the VBIOS: from the host's
    # git-ignored firmware/ into the personality, never into the repository.
    root = os.path.dirname(os.path.dirname(os.path.abspath(sys.argv[1])))
    root = os.path.dirname(root)
    for key, name in (("FW_PFP", "TURKS_pfp.bin"), ("FW_ME", "TURKS_me.bin")):
        blob = open(os.path.join(root, "firmware", name), "rb").read()
        extra += "\t\t\t<key>%s</key>\n\t\t\t<data>%s</data>\n" % (
            key, base64.b64encode(blob).decode())
    if os.environ.get("RDN_ACCELCAPS"):
        extra += "\t\t\t<key>AccelCaps</key>\n\t\t\t<integer>%d</integer>\n" % int(
            os.environ["RDN_ACCELCAPS"], 0)
    if os.environ.get("RDN_AGPSHIM"):
        extra += "\t\t\t<key>AGPShim</key>\n\t\t\t<integer>%d</integer>\n" % int(
            os.environ["RDN_AGPSHIM"])
    if os.environ.get("RDN_SURFACES") == "1":
        extra += "\t\t\t<key>Surfaces</key>\n\t\t\t<true/>\n"
    if os.environ.get("RDN_GA") == "1":
        extra += "\t\t\t<key>GAPlugin</key>\n\t\t\t<true/>\n"
    if os.environ.get("RDN_SELFTEST") == "1":
        extra += "\t\t\t<key>AccelSelfTest</key>\n\t\t\t<true/>\n"
sys.stdout.write(plist.replace(marker, extra + marker))
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
    if gssh "test -d $SLE/RadeonNI.kext"; then
        echo "an installed RadeonNI.kext is in the guest; run 'scripts/kext.sh uninstall' and restart the guest first" >&2
        exit 1
    fi
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
