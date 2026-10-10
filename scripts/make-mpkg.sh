#!/bin/bash
# Build RadeonNI.mpkg, the graphical installer for Tiger's Installer.app, from
# the staged package scripts/make-g5-package.sh makes.
#
#   scripts/make-mpkg.sh [stage-dir [out-dir]]
#
# Two packages in one metapackage: "RadeonNI" (required: kext, OpenGL bundle,
# 2D plug-in, microcode; its postflight runs install.sh --accel --hwcursor) and
# "Open Firmware support" (optional, not selected by default; its postflight
# runs of-install.sh --yes, which writes the NVRAM). Panes: Welcome, Read Me,
# License, Select Destination, Customize, Install, Summary; text from
# g5/installer/.
#
# The archives and bills of materials are made by the Tiger guest's own pax
# and mkbom (the host has neither); everything else is assembled here.
# The staged tree must already have the licences (make-dist.sh adds them
# before calling this); the license pane reads LICENSE, LICENSE.radeon and
# LICENSE.mesa from it.
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
stage=${1:-$root/build/RadeonNI-g5}
out=${2:-$root/build}
src=$root/g5/installer
work=$root/build/mpkg-work
version=$(cat "$stage/VERSION")

[ -f "$stage/install.sh" ] || { echo "no staged package in $stage" >&2; exit 1; }
for f in rdnk.elf of-block.txt of-install.sh of-uninstall.sh; do
    [ -s "$stage/$f" ] || { echo "missing from $stage: $f" >&2; exit 1; }
done

rm -rf "$work"
mkdir -p "$work/main/root/Library/RadeonNI/Setup" "$work/of/root/Library/RadeonNI/Setup-OF"
tar -C "$stage" --exclude=./rdnk.elf --exclude=./of-block.txt --exclude=./of-install.sh \
    -cf - . | tar -C "$work/main/root/Library/RadeonNI/Setup" -xf -
for f in rdnk.elf of-block.txt of-install.sh; do
    cp -p "$stage/$f" "$work/of/root/Library/RadeonNI/Setup-OF/"
done

# Archive.pax.gz and Archive.bom, in the guest, as root:wheel (/ and
# /Library as Tiger has them, so that installing changes nothing there).
guest=osx-gpu-mpkg
"$root/scripts/tiger.sh" ssh "echo tiger | sudo -S rm -rf $guest; mkdir $guest" > /dev/null 2>&1
tar -C "$work" --owner=0 --group=0 --numeric-owner -cf - main of |
    "$root/scripts/tiger.sh" ssh "cd $guest && tar -xf -"
"$root/scripts/tiger.sh" ssh "cd $guest && for p in main of; do
    cd \$p/root &&
    echo tiger | sudo -S chown -R root:wheel . &&
    echo tiger | sudo -S chown root:admin . Library &&
    echo tiger | sudo -S chmod 1775 . &&
    echo tiger | sudo -S chmod 775 Library &&
    echo tiger | sudo -S mkbom . ../Archive.bom &&
    echo tiger | sudo -S sh -c 'pax -w -x cpio . | gzip -9 > ../Archive.pax.gz' &&
    echo tiger | sudo -S chmod a+r ../Archive.bom ../Archive.pax.gz &&
    cd ../.. || exit 1
done" > /dev/null 2>&1
"$root/scripts/tiger.sh" ssh "cd $guest && tar -cf - main/Archive.bom main/Archive.pax.gz of/Archive.bom of/Archive.pax.gz" |
    tar -C "$work" -xf -
for p in main of; do
    [ -s "$work/$p/Archive.bom" ] && [ -s "$work/$p/Archive.pax.gz" ] ||
        { echo "the guest made no archive for $p" >&2; exit 1; }
done
"$root/scripts/tiger.sh" ssh "echo tiger | sudo -S rm -rf $guest" > /dev/null 2>&1 || true

mpkg=$work/RadeonNI.mpkg
rm -rf "$mpkg"
mkdir -p "$mpkg/Contents/Packages" "$mpkg/Contents/Resources/English.lproj"
res=$mpkg/Contents/Resources/English.lproj

for t in Welcome ReadMe Conclusion; do
    python3 -I "$src/mkrtf.py" "$src/$t.txt" "$res/$t.rtf"
done
{
    cat "$stage/LICENSE"
    for l in LICENSE.radeon LICENSE.mesa; do
        if [ -f "$stage/$l" ]; then printf '\n\n----------------------------------------\n%s\n----------------------------------------\n\n' "$l"; cat "$stage/$l"; fi
    done
} > "$res/License.txt"
cp "$src/InstallationCheck" "$mpkg/Contents/Resources/InstallationCheck"
cp "$src/InstallationCheck.strings" "$res/InstallationCheck.strings"
chmod 755 "$mpkg/Contents/Resources/InstallationCheck"

python3 -I - "$work" "$src" "$mpkg" "$version" <<'PY'
import os, plistlib, shutil, sys

work, src, mpkg, version = sys.argv[1:]
FORMAT = 0.10000000149011612


def kbytes(d):
    total = 0
    for folder, _, files in os.walk(d):
        for f in files:
            total += os.path.getsize(os.path.join(folder, f))
    return total // 1024 + 1


def read(name):
    return open(os.path.join(src, name), encoding="utf-8").read().strip()


packages = [
    ("main", "RadeonNI", "org.osxgpu.pkg.RadeonNI", "RadeonNI", "desc-main.txt",
     "postflight-main", "required", True),
    ("of", "OpenFirmware", "org.osxgpu.pkg.RadeonNI.OpenFirmware",
     "Open Firmware support", "desc-of.txt", "postflight-of", "unselected", False),
]
plist_list = []
for key, fname, ident, title, desc, post, selection, required in packages:
    pkg = os.path.join(mpkg, "Contents", "Packages", fname + ".pkg")
    res = os.path.join(pkg, "Contents", "Resources")
    os.makedirs(os.path.join(res, "English.lproj"))
    shutil.copy(os.path.join(work, key, "Archive.bom"), os.path.join(pkg, "Contents"))
    shutil.copy(os.path.join(work, key, "Archive.pax.gz"), os.path.join(pkg, "Contents"))
    shutil.copy(os.path.join(src, post), os.path.join(res, "postflight"))
    os.chmod(os.path.join(res, "postflight"), 0o755)
    info = {
        "CFBundleIdentifier": ident,
        "CFBundleShortVersionString": version,
        "IFPkgFlagAllowBackRevision": True,
        "IFPkgFlagAuthorizationAction": "RootAuthorization",
        "IFPkgFlagDefaultLocation": "/",
        "IFPkgFlagFollowLinks": True,
        "IFPkgFlagInstallFat": False,
        "IFPkgFlagInstalledSize": kbytes(os.path.join(work, key, "root")),
        "IFPkgFlagIsRequired": required,
        "IFPkgFlagRelocatable": False,
        "IFPkgFlagRestartAction": "RequiredRestart",
        "IFPkgFlagRootVolumeOnly": True,
        "IFPkgFlagUpdateInstalledLanguages": False,
        "IFPkgFormatVersion": FORMAT,
    }
    with open(os.path.join(pkg, "Contents", "Info.plist"), "wb") as f:
        plistlib.dump(info, f)
    with open(os.path.join(res, "English.lproj", "Description.plist"), "wb") as f:
        plistlib.dump({"IFPkgDescriptionTitle": title,
                       "IFPkgDescriptionDescription": read(desc),
                       "IFPkgDescriptionVersion": version}, f)
    plist_list.append({"IFPkgFlagPackageLocation": fname + ".pkg",
                       "IFPkgFlagPackageSelection": selection})

meta = {
    "CFBundleIdentifier": "org.osxgpu.mpkg.RadeonNI",
    "CFBundleShortVersionString": version,
    "IFMajorVersion": 1,
    "IFMinorVersion": 0,
    "IFPkgFlagComponentDirectory": "./Contents/Packages",
    "IFPkgFlagPackageList": plist_list,
    "IFPkgFormatVersion": FORMAT,
}
with open(os.path.join(mpkg, "Contents", "Info.plist"), "wb") as f:
    plistlib.dump(meta, f)
with open(os.path.join(mpkg, "Contents", "Resources", "English.lproj", "Description.plist"), "wb") as f:
    plistlib.dump({"IFPkgDescriptionTitle": "RadeonNI",
                   "IFPkgDescriptionDescription": "Driver for the AMD Radeon HD 7570",
                   "IFPkgDescriptionVersion": version}, f)
PY

mkdir -p "$out"
rm -rf "$out/RadeonNI.mpkg"
cp -R "$mpkg" "$out/RadeonNI.mpkg"
echo "$out/RadeonNI.mpkg"
