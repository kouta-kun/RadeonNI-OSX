#!/bin/sh
# Build build/radeonni-of-linux-<date>-<commit>.tar.gz: the Open Firmware client
# and nvramrc block with a Linux installer (g5/linux/), separate from the
# Mac OS X package. Needs the PowerPC toolchain (scripts/fetch-deps.sh).
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
name=radeonni-of-linux-$(date +%Y%m%d)-$(git -C "$root" rev-parse --short HEAD)
stage=$root/build/$name
rm -rf "$stage"; mkdir -p "$stage"
make -C "$root/of" build/rdnk.elf > /dev/null
"$root/third_party/ppc-toolchain/bin/powerpc-linux-strip" -o "$stage/rdnk.elf" "$root/of/build/rdnk.elf"
python3 -I "$root/scripts/of-hook.py" block --dir '\RadeonNI' --dev '@OFDEV@' > "$stage/of-block.tmpl"
cp "$root/g5/linux/install.sh" "$root/g5/linux/uninstall.sh" "$root/g5/linux/nvram-lib.sh" \
   "$root/g5/linux/README.txt" "$root/LICENSE" "$stage/"
chmod +x "$stage/install.sh" "$stage/uninstall.sh"
git -C "$root" describe --tags --always --dirty > "$stage/VERSION"
tar -C "$root/build" -czf "$root/build/$name.tar.gz" "$name"
echo "$root/build/$name.tar.gz"
