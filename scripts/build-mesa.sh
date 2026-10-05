#!/bin/bash
# Build Mesa's r600 driver on top of this project's winsys, for the Linux
# host: third_party/mesa-<version>/build-x86/src/gallium/targets/rdn/librdngl.so
#
#   scripts/build-mesa.sh           fetch if needed, prepare, configure, build
#
# Mesa is not modified beyond mesa/patches/: this project's winsys, frontend
# and target directories (mesa/) and hw/ are copied into its tree. Python modules
# Mesa's build needs go into a private environment in third_party/pyenv;
# nothing is installed on the host.

set -euo pipefail

MESA=mesa-26.2.4
MESA_SHA256=bce5f7fbebb934373b86c999a064d52fb5065878dc57f287f95346648ec832e9

root=$(cd "$(dirname "$0")/.." && pwd)
tp=$root/third_party
src=$tp/$MESA

mkdir -p "$tp"
cd "$tp"
if [ ! -d "$MESA" ]; then
    [ -f "$MESA.tar.xz" ] || curl -fL -O "https://archive.mesa3d.org/$MESA.tar.xz"
    echo "$MESA_SHA256  $MESA.tar.xz" | sha256sum -c -
    tar xf "$MESA.tar.xz"
fi
if [ ! -x pyenv/bin/python ]; then
    python3 -m venv pyenv
    pyenv/bin/pip -q install mako pyyaml packaging
fi
export PATH=$tp/pyenv/bin:$PATH

cd "$src"
for p in "$root"/mesa/patches/*.patch; do
    if patch -p1 -R --dry-run -s -f < "$p" > /dev/null 2>&1; then
        continue    # already applied
    fi
    patch -p1 < "$p"
done
# Copies, not links: meson resolves ".." through a link to the wrong tree.
# Edit the originals in mesa/ and hw/ and run this script again.
for d in src/gallium/winsys/rdn src/gallium/frontends/rdn src/gallium/targets/rdn; do
    [ -L "$d" ] && rm "$d"
done
rsync -a --delete "$root/mesa/winsys/" src/gallium/winsys/rdn/
rsync -a --delete "$root/mesa/frontend/" src/gallium/frontends/rdn/
rsync -a --delete --exclude hw "$root/mesa/target/" src/gallium/targets/rdn/
rsync -a --delete "$root/hw/" src/gallium/targets/rdn/hw/

if [ ! -f build-x86/build.ninja ]; then
    meson setup build-x86 -Drdn=true \
        -Dgallium-drivers=r600,softpipe -Dvulkan-drivers= \
        -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dllvm=disabled \
        -Dplatforms= -Dgles1=disabled -Dgles2=disabled -Dzstd=disabled \
        -Dvalgrind=disabled -Dlibunwind=disabled -Dvideo-codecs= \
        -Dgallium-va=disabled -Dbuildtype=debugoptimized
fi
ninja -C build-x86 "$@"
ls -l build-x86/src/gallium/targets/rdn/librdngl.so
