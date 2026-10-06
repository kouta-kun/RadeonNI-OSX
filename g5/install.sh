#!/bin/sh
# Install RadeonNI.kext on a PowerPC Mac running Mac OS X 10.4, so that it
# loads at boot like any other driver.
#
#   sudo ./install.sh [--accel] [--hwcursor] [vbios.rom]
#
# Run it from the unpacked package directory, on the Mac itself. It needs
# the VBIOS image of the card: the kext does not read the card's ROM yet, so
# the image is stored in the kext's Info.plist. Without an argument the
# script looks for vbios.rom next to itself.
#
# Without options the driver is a plain framebuffer. --accel also starts
# the card's 3D engine at boot, for OpenGL and Quartz Extreme; that needs
# the microcode files TURKS_pfp.bin and TURKS_me.bin next to this script.
# It also installs RadeonNIGLDriver.bundle (OpenGL) and RadeonNIGA.plugin
# from this package into /System/Library/Extensions; they do nothing
# without the accelerator. --hwcursor (with --accel) uses the card's
# hardware cursor. Run the script again without options to go back.
#
# Nothing is written to the card. To undo: sudo ./uninstall.sh
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT

set -e

SLE=/System/Library/Extensions
KEXT=RadeonNI.kext
USERLAND="RadeonNIGLDriver.bundle RadeonNIGA.plugin"
here=$(cd "$(dirname "$0")" && pwd)

fail() { echo "install.sh: $*" >&2; exit 1; }

accel=0
hwcursor=0
vbios=$here/vbios.rom
for arg in "$@"; do
    case "$arg" in
    --accel) accel=1 ;;
    --hwcursor) hwcursor=1 ;;
    -*) fail "unknown option $arg" ;;
    *) vbios=$arg ;;
    esac
done
[ "$hwcursor" = 0 ] || [ "$accel" = 1 ] || fail "--hwcursor needs --accel"

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

if [ "$accel" = 1 ]; then
    for f in TURKS_pfp.bin TURKS_me.bin; do
        [ -s "$here/$f" ] || fail "--accel needs the microcode file $f next to this script"
    done
    for b in $USERLAND; do
        [ -d "$here/$b" ] || [ -d "$SLE/$b" ] ||
            fail "--accel needs $b, which is neither in this package nor installed"
    done
fi

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
    my ($plist, $rom, $accel, $hwcursor, $dir) = @ARGV;
    local $/;
    open(my $p, "<", $plist) or die "$plist: $!";
    my $text = <$p>;
    close($p);
    open(my $r, "<", $rom) or die "$rom: $!";
    binmode($r);
    my $data = encode_base64(<$r>, "");
    close($r);
    $text =~ s{\s*<key>VBIOS</key>\s*<data>.*?</data>}{}s;
    my $extra = "\t\t\t<key>VBIOS</key>\n\t\t\t<data>$data</data>\n";
    if ($accel) {
        # The keys scripts/kext.sh adds for Quartz Extreme under QEMU.
        $extra .= "\t\t\t<key>Accelerator</key>\n\t\t\t<true/>\n";
        for my $fw (["FW_PFP", "TURKS_pfp.bin"], ["FW_ME", "TURKS_me.bin"]) {
            open(my $f, "<", "$dir/$fw->[1]") or die "$fw->[1]: $!";
            binmode($f);
            my $blob = encode_base64(<$f>, "");
            close($f);
            $extra .= "\t\t\t<key>$fw->[0]</key>\n\t\t\t<data>$blob</data>\n";
        }
        $extra .= "\t\t\t<key>AccelCaps</key>\n\t\t\t<integer>3</integer>\n";
        $extra .= "\t\t\t<key>AGPShim</key>\n\t\t\t<integer>3</integer>\n";
        $extra .= "\t\t\t<key>Surfaces</key>\n\t\t\t<true/>\n";
        $extra .= "\t\t\t<key>GAPlugin</key>\n\t\t\t<true/>\n";
        $extra .= "\t\t\t<key>HWCursor</key>\n\t\t\t<true/>\n" if $hwcursor;
    }
    $text =~ s{(\t*<key>IOProviderClass</key>)}{$extra$1}
        or die "IOProviderClass not found in $plist\n";
    open($p, ">", $plist) or die "$plist: $!";
    print $p $text;
    close($p);
' "$tmp/$KEXT/Contents/Info.plist" "$vbios" "$accel" "$hwcursor" "$here"

chown -R root:wheel "$tmp/$KEXT"
chmod -R go-w "$tmp/$KEXT"

# Check that the system accepts it before touching the Extensions folder.
kextload -t -n "$tmp/$KEXT" || fail "the system rejects the kext; nothing was installed"

rm -rf "$SLE/$KEXT"
cp -R "$tmp/$KEXT" "$SLE/"
chown -R root:wheel "$SLE/$KEXT"
chmod -R go-w "$SLE/$KEXT"
rm -rf "$tmp"

# The OpenGL bundle and the 2D plug-in: copied beside the old one and
# renamed into place, so that a running window server keeps its copy.
if [ "$accel" = 1 ]; then
    for b in $USERLAND; do
        [ -d "$here/$b" ] || continue
        rm -rf "$SLE/$b.new" "$SLE/$b.old"
        cp -R "$here/$b" "$SLE/$b.new"
        chown -R root:wheel "$SLE/$b.new"
        chmod -R go-w "$SLE/$b.new"
        [ ! -d "$SLE/$b" ] || mv "$SLE/$b" "$SLE/$b.old"
        mv "$SLE/$b.new" "$SLE/$b"
        rm -rf "$SLE/$b.old"
        echo "Installed $SLE/$b."
    done
fi

# Make the system rebuild its extension caches at the next boot.
rm -f /System/Library/Extensions.mkext /System/Library/Extensions.kextcache
touch "$SLE"
sync

if [ "$accel" = 1 ]; then
    echo
    echo "Acceleration is ON: the 3D engine starts at boot."
    echo "To go back to the plain framebuffer: sudo ./install.sh"
fi

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
