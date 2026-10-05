#!/bin/sh
# Install RadeonNI.kext on a PowerPC Mac running Mac OS X 10.4, so that it
# loads at boot like any other driver.
#
#   sudo ./install.sh [vbios.rom]
#
# Run it from the unpacked package directory, on the Mac itself. It needs
# the VBIOS image of the card: the kext does not read the card's ROM yet, so
# the image is stored in the kext's Info.plist. Without an argument the
# script looks for vbios.rom next to itself.
#
# Nothing is written to the card. To undo: sudo ./uninstall.sh
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT

set -e

SLE=/System/Library/Extensions
KEXT=RadeonNI.kext
here=$(cd "$(dirname "$0")" && pwd)
vbios=${1:-$here/vbios.rom}

fail() { echo "install.sh: $*" >&2; exit 1; }

[ "$(id -u)" = 0 ] || fail "run as root: sudo ./install.sh [vbios.rom]"
[ -d "$here/$KEXT" ] || fail "$KEXT not found next to this script"
[ -f "$vbios" ] || fail "VBIOS image not found: $vbios"

case "$(sw_vers -productVersion)" in
10.4*) ;;
*) fail "this kext is built for Mac OS X 10.4; this is $(sw_vers -productVersion)" ;;
esac
[ "$(uname -p)" = powerpc ] || fail "this kext is PowerPC only"

# A PCI option ROM starts with 55 AA and states its length in 512-byte units.
sig=$(od -A n -t x1 -N 3 "$vbios" | tr -d ' \n')
case "$sig" in
55aa*) ;;
*) fail "$vbios does not start with the option ROM signature 55 AA" ;;
esac
blocks=$(printf '%d' "0x$(echo "$sig" | cut -c5-6)")
size=$(wc -c < "$vbios" | tr -d ' ')
[ "$size" -ge $((blocks * 512)) ] || fail "$vbios is truncated: $size bytes, header says $((blocks * 512))"
strings "$vbios" | grep -q ATOMBIOS || fail "$vbios has no ATOMBIOS marker"

if ioreg -p IODeviceTree | grep -q 'pci1002,675d'; then
    echo "Found the Radeon HD 7570 (1002:675d)."
else
    echo "Note: no device 1002:675d in the device tree right now."
    echo "      The kext will be installed but will only load when that card is present."
fi

tmp=/tmp/RadeonNI-install.$$
rm -rf "$tmp"
mkdir "$tmp"
cp -R "$here/$KEXT" "$tmp/"

# Put the image into the driver's personality as a base64 <data> property.
perl -MMIME::Base64 -e '
    my ($plist, $rom) = @ARGV;
    local $/;
    open(my $p, "<", $plist) or die "$plist: $!";
    my $text = <$p>;
    close($p);
    open(my $r, "<", $rom) or die "$rom: $!";
    binmode($r);
    my $data = encode_base64(<$r>, "");
    close($r);
    $text =~ s{\s*<key>VBIOS</key>\s*<data>.*?</data>}{}s;
    $text =~ s{(\t*<key>IOProviderClass</key>)}{\t\t\t<key>VBIOS</key>\n\t\t\t<data>$data</data>\n$1}
        or die "IOProviderClass not found in $plist\n";
    open($p, ">", $plist) or die "$plist: $!";
    print $p $text;
    close($p);
' "$tmp/$KEXT/Contents/Info.plist" "$vbios"

chown -R root:wheel "$tmp/$KEXT"
chmod -R go-w "$tmp/$KEXT"

# Check that the system accepts it before touching the Extensions folder.
kextload -t -n "$tmp/$KEXT" || fail "the system rejects the kext; nothing was installed"

rm -rf "$SLE/$KEXT"
cp -R "$tmp/$KEXT" "$SLE/"
chown -R root:wheel "$SLE/$KEXT"
chmod -R go-w "$SLE/$KEXT"
rm -rf "$tmp"

# Make the system rebuild its extension caches at the next boot.
rm -f /System/Library/Extensions.mkext /System/Library/Extensions.kextcache
touch "$SLE"
sync

cat <<'MSG'

Installed /System/Library/Extensions/RadeonNI.kext.
Restart the Mac. The driver starts at boot when it finds the card; its
messages are in the kernel log:  sudo dmesg | grep RadeonNI

If the Mac does not finish booting with the driver installed:
  - hold Shift while it starts (Safe Boot does not load third-party
    drivers), log in and run:  sudo ./uninstall.sh
  - or hold Command-S for single-user mode and type:
        /sbin/fsck -fy && /sbin/mount -uw /
        rm -rf /System/Library/Extensions/RadeonNI.kext
        rm -f /System/Library/Extensions.mkext /System/Library/Extensions.kextcache
        reboot
  - or take the card out: without it the driver is never loaded.
MSG
