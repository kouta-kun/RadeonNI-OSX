#!/bin/bash
# Build the package that installs the driver on a real Mac:
# build/RadeonNI-g5.tar.gz, holding the kext as built in the running Tiger
# guest plus g5/install.sh, g5/uninstall.sh and g5/README.txt.
#
#   scripts/make-g5-package.sh [--with-vbios] [--with-firmware]
#
# --with-vbios also packs private/vbios.rom, so that install.sh needs no
# argument. --with-firmware also packs the command processor's microcode
# from firmware/, which install.sh --accel needs. Both belong to the card's
# vendor: a package built that way is for your own machine, not for
# publishing.

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

cp "$root/g5/install.sh" "$root/g5/uninstall.sh" "$root/g5/README.txt" "$root/LICENSE" "$stage/"
chmod +x "$stage/install.sh" "$stage/uninstall.sh"
git -C "$root" describe --tags --always --dirty > "$stage/VERSION"

for arg in "$@"; do
    case "$arg" in
    --with-vbios) cp "$root/private/vbios.rom" "$stage/vbios.rom" ;;
    --with-firmware) cp "$root/firmware/TURKS_pfp.bin" "$root/firmware/TURKS_me.bin" "$stage/" ;;
    *) echo "unknown option $arg" >&2; exit 1 ;;
    esac
done

tar -C "$root/build" -czf "$out" RadeonNI-g5
echo "$out"
tar -tzf "$out" | sed 's/^/  /'
