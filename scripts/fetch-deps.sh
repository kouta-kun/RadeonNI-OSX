#!/bin/bash
# Fetch the third-party pieces the host build and tests need, into
# third_party/ (git-ignored). Nothing is installed on the host.
#   - PowerPC (big-endian, musl) cross toolchain from Bootlin, for the
#     big-endian tests run under qemu-ppc
#   - Linux radeon driver sources, for reference while porting
# QEMU is handled by scripts/build-qemu.sh.

set -euo pipefail

TC=powerpc-e300c3--musl--stable-2026.08-1
TC_SHA256=65f2bdf7b1557bae9fa929d03546e511b1699cb8287d6776ed59da57299ca747
TC_URL=https://toolchains.bootlin.com/downloads/releases/toolchains/powerpc-e300c3/tarballs/$TC.tar.xz

tp=$(cd "$(dirname "$0")/.." && pwd)/third_party
mkdir -p "$tp"
cd "$tp"

if [ ! -d "$TC" ]; then
    [ -f "$TC.tar.xz" ] || curl -fL -O "$TC_URL"
    echo "$TC_SHA256  $TC.tar.xz" | sha256sum -c -
    tar xf "$TC.tar.xz"
fi
ln -sfn "$TC" ppc-toolchain

if [ ! -d linux ]; then
    git clone --depth 1 --filter=blob:none --sparse https://github.com/torvalds/linux.git linux
    git -C linux sparse-checkout set drivers/gpu/drm/radeon include/drm
fi
echo "linux reference tree at $(git -C linux rev-parse --short HEAD)"
