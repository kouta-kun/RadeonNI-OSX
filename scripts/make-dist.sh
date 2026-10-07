#!/bin/bash
# Build the archive to hand to other people: build/RadeonNI-<date>-<commit>.zip,
# one folder to unpack on a Mac with a plain Mac OS X 10.4.11 and install
# from, with nothing else needed there.
#
#   scripts/make-dist.sh [--keep-gl]
#
# It builds everything first (scripts/make-g5-package.sh): the kext and the
# 2D plug-in in the running Tiger guest, the OpenGL bundle with Mesa inside
# on the host with the cross toolchain (Mesa cannot be built with Tiger's
# own compiler). --keep-gl takes the OpenGL bundle as last built.
#
# In the folder: the three bundles, the microcode, install.sh and
# uninstall.sh, INSTALL.txt (g5/README.txt), the project's README.md, and
# the licences: LICENSE (this project), LICENSE.radeon (the microcode) and
# LICENSE.mesa with licenses.mesa/ (Mesa, which is inside the OpenGL
# bundle).
#
# Never in it: a VBIOS image. The driver reads the card's ROM, and the image
# belongs to the card's vendor. The script refuses to go on if one turns up.

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
stage=$root/build/RadeonNI-g5

for arg in "$@"; do
    case "$arg" in
    --keep-gl) ;;
    *) echo "unknown option $arg" >&2; exit 1 ;;
    esac
done

"$root/scripts/make-g5-package.sh" "$@" > /dev/null

name=RadeonNI-$(git -C "$root" log -1 --format=%cd-%h --date=format:%Y%m%d)
[ -z "$(git -C "$root" status --porcelain --untracked-files=no)" ] || name=$name-dirty
dist=$root/build/dist/$name
out=$root/build/$name.zip

rm -rf "$root/build/dist"
mkdir -p "$dist"
cp -R "$stage/." "$dist/"
mv "$dist/README.txt" "$dist/INSTALL.txt"
cp "$root/README.md" "$dist/"

# Mesa's own statement of its licences and the texts it points to.
mesa=$(ls -d "$root"/third_party/mesa-*/ | tail -n 1)
cp "$mesa/docs/license.rst" "$dist/LICENSE.mesa"
cp -R "$mesa/licenses" "$dist/licenses.mesa"

if find "$dist" -name '*.rom' | grep -q . ||
    grep -rq '<key>VBIOS</key>' "$dist/RadeonNI.kext/Contents/Info.plist"; then
    echo "a VBIOS image is in $dist; not making an archive of that" >&2
    exit 1
fi
for f in RadeonNI.kext/Contents/MacOS/RadeonNI \
    RadeonNIGA.plugin/Contents/MacOS/RadeonNIGA \
    RadeonNIGLDriver.bundle/Contents/MacOS/RadeonNIGLDriver \
    TURKS_pfp.bin TURKS_me.bin TURKS_mc.bin LICENSE.radeon LICENSE \
    install.sh uninstall.sh INSTALL.txt README.md VERSION; do
    [ -s "$dist/$f" ] || { echo "missing from the archive: $f" >&2; exit 1; }
done

# The host may have no zip; Python writes it, with the files' modes, so
# that the scripts stay executable when Tiger unpacks it.
rm -f "$out"
python3 - "$root/build/dist" "$name" "$out" <<'PY'
import os, sys, zipfile

base, name, out = sys.argv[1:]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for folder, dirs, files in os.walk(os.path.join(base, name)):
        dirs.sort()
        for entry in sorted(dirs) + sorted(files):
            path = os.path.join(folder, entry)
            arc = os.path.relpath(path, base)
            if os.path.isdir(path):
                info = zipfile.ZipInfo(arc + "/")
                info.external_attr = (0o40755 << 16) | 0x10
                z.writestr(info, "")
                continue
            info = zipfile.ZipInfo.from_file(path, arc)
            mode = 0o755 if os.access(path, os.X_OK) else 0o644
            info.external_attr = (0o100000 | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(path, "rb") as f:
                z.writestr(info, f.read())
PY

echo "$out"
python3 -m zipfile -l "$out" | sed 's/^/  /'
