#!/bin/bash
# Build QEMU (PowerPC system emulator and user-mode emulator) from source into
# third_party/. Nothing is installed on the host. Patches in patches/qemu/ are
# applied in order when the tree is first unpacked.
# Usage: scripts/build-qemu.sh

set -euo pipefail

QEMU_VERSION=11.1.1
QEMU_SHA256=079ffbff8a7111bbc89022107cbabf3bbfd614d5fc9d7cc675991196aca12482

root=$(cd "$(dirname "$0")/.." && pwd)
tp=$root/third_party
src=$tp/qemu-$QEMU_VERSION
tarball=$tp/qemu-$QEMU_VERSION.tar.xz

mkdir -p "$tp"
if [ ! -f "$tarball" ]; then
    curl -fL -o "$tarball" "https://download.qemu.org/qemu-$QEMU_VERSION.tar.xz"
fi
echo "$QEMU_SHA256  $tarball" | sha256sum -c -

if [ ! -d "$src" ]; then
    tar -C "$tp" -xf "$tarball"
    for p in "$root"/patches/qemu/*.patch; do
        [ -e "$p" ] || continue
        patch -d "$src" -p1 < "$p"
    done
fi

mkdir -p "$src/build"
cd "$src/build"
if [ ! -f build.ninja ]; then
    ../configure --target-list=ppc-softmmu,ppc-linux-user \
        --enable-trace-backends=log --enable-vnc --enable-slirp \
        --disable-docs --disable-werror
fi
nice ninja -j"$(nproc)"
ln -sfn "qemu-$QEMU_VERSION/build" "$tp/qemu"
"$tp/qemu/qemu-system-ppc" --version | head -1
