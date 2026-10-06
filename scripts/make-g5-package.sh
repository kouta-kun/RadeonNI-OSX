#!/bin/bash
# Build the package that installs the driver on a real Mac:
# build/RadeonNI-g5.tar.gz, holding the kext and the 2D plug-in
# (RadeonNIGA.plugin) as built in the running Tiger guest, the OpenGL driver
# bundle with Mesa inside as cross-built on the host
# (scripts/build-mesa.sh darwin), plus g5/install.sh, g5/uninstall.sh and
# g5/README.txt.
#
#   scripts/make-g5-package.sh [--with-vbios]
#
# The command processor's microcode (AMD's, redistributable in binary form)
# is always packed, with its licence LICENSE.radeon;
# scripts/fetch-firmware.sh gets it if firmware/ does not have it.
# --with-vbios also packs private/vbios.rom, so that install.sh needs no
# argument. The VBIOS belongs to the card's vendor: a package built that
# way is for your own machine, not for publishing.

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
stage=$root/build/RadeonNI-g5
out=$root/build/RadeonNI-g5.tar.gz

"$root/scripts/kext.sh" build > /dev/null

rm -rf "$stage"
mkdir -p "$stage"
"$root/scripts/tiger.sh" ssh 'tar -C ~/osx-gpu/kext/RadeonNI/build -cf - RadeonNI.kext' | tar -C "$stage" -xf -
[ -f "$stage/RadeonNI.kext/Contents/MacOS/RadeonNI" ] || { echo "no kext came back from the guest" >&2; exit 1; }
if grep -q '<key>VBIOS</key>' "$stage/RadeonNI.kext/Contents/Info.plist"; then
    echo "the built kext already carries a VBIOS; refusing to package it" >&2
    exit 1
fi

# The two user-space halves install.sh --accel puts next to the kext.
"$root/scripts/ga.sh" build > /dev/null
"$root/scripts/tiger.sh" ssh 'tar -C ~/osx-gpu-ga/ga/build -cf - RadeonNIGA.plugin' | tar -C "$stage" -xf -
[ -f "$stage/RadeonNIGA.plugin/Contents/MacOS/RadeonNIGA" ] || { echo "no plug-in came back from the guest" >&2; exit 1; }

"$root/scripts/build-mesa.sh" darwin src/gallium/targets/rdn/RadeonNIGLDriver.dylib > /dev/null
gl=$(ls "$root"/third_party/mesa-*/build-darwin/src/gallium/targets/rdn/RadeonNIGLDriver.dylib | tail -n 1)
[ -s "$gl" ] || { echo "the OpenGL bundle was not built" >&2; exit 1; }
mkdir -p "$stage/RadeonNIGLDriver.bundle/Contents/MacOS"
cp "$root/gld/Info.plist" "$stage/RadeonNIGLDriver.bundle/Contents/Info.plist"
cp "$gl" "$stage/RadeonNIGLDriver.bundle/Contents/MacOS/RadeonNIGLDriver"
chmod 755 "$stage/RadeonNIGLDriver.bundle/Contents/MacOS/RadeonNIGLDriver"

cp "$root/g5/install.sh" "$root/g5/uninstall.sh" "$root/g5/README.txt" "$root/LICENSE" "$stage/"
chmod +x "$stage/install.sh" "$stage/uninstall.sh"
git -C "$root" describe --tags --always --dirty > "$stage/VERSION"

"$root/scripts/fetch-firmware.sh"
cp "$root/firmware/TURKS_pfp.bin" "$root/firmware/TURKS_me.bin" "$root/firmware/TURKS_mc.bin" "$root/firmware/LICENSE.radeon" "$stage/"

for arg in "$@"; do
    case "$arg" in
    --with-vbios) cp "$root/private/vbios.rom" "$stage/vbios.rom" ;;
    --with-firmware) ;; # always packed now
    *) echo "unknown option $arg" >&2; exit 1 ;;
    esac
done

tar -C "$root/build" -czf "$out" RadeonNI-g5
echo "$out"
tar -tzf "$out" | sed 's/^/  /'
