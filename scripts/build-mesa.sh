#!/bin/bash
# Build Mesa's r600 driver on top of this project's winsys, for the Linux
# host: third_party/mesa-<version>/build-x86/src/gallium/targets/rdn/librdngl.so
# (and rdn_gltest next to it).
#
#   scripts/build-mesa.sh [x86]     fetch if needed, prepare, configure, build
#   scripts/build-mesa.sh darwin [targets]
#                                   for Mac OS X 10.4 on PowerPC, in the
#                                   toolchain container (scripts/darwin.sh)
#   scripts/build-mesa.sh ppc       the GL test program for big-endian
#                                   PowerPC Linux, static, to run under
#                                   third_party/qemu/qemu-ppc
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
rsync -a --delete --exclude hw --exclude tests "$root/mesa/target/" src/gallium/targets/rdn/
rsync -a --delete "$root/hw/" src/gallium/targets/rdn/hw/
rsync -a --delete "$root/mesa/tests/" src/gallium/targets/rdn/tests/

common=(-Drdn=true -Dgallium-drivers=r600,softpipe -Dvulkan-drivers=
    -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dllvm=disabled
    -Dplatforms= -Dgles1=disabled -Dgles2=disabled -Dzstd=disabled
    -Dvalgrind=disabled -Dlibunwind=disabled -Dvideo-codecs=
    -Dgallium-va=disabled -Dbuildtype=debugoptimized)

case "${1:-x86}" in
x86)
    [ -f build-x86/build.ninja ] || meson setup build-x86 "${common[@]}"
    ninja -C build-x86
    ls -l build-x86/src/gallium/targets/rdn/librdngl.so
    ;;
ppc)
    # Big-endian PowerPC Linux, static, for qemu-ppc: the same code as the
    # host build in the byte order of the real target.
    tc=$tp/ppc-toolchain/bin
    cat > "$tp/mesa-ppc-cross.ini" <<INI
[binaries]
c = '$tc/powerpc-linux-gcc'
cpp = '$tc/powerpc-linux-g++'
ar = '$tc/powerpc-linux-ar'
strip = '$tc/powerpc-linux-strip'
pkg-config = 'false'
exe_wrapper = '$tp/qemu/qemu-ppc'

[host_machine]
system = 'linux'
cpu_family = 'ppc'
cpu = 'e300c3'
endian = 'big'
INI
    # Mesa declares three pthread functions weak (for old glibc); a static
    # link then leaves them out unless they are asked for by name.
    static="-static -Wl,-u,pthread_mutexattr_init -Wl,-u,pthread_mutexattr_settype -Wl,-u,pthread_mutexattr_destroy"
    [ -f build-ppc/build.ninja ] || meson setup build-ppc "${common[@]}" \
        --cross-file "$tp/mesa-ppc-cross.ini" -Dxmlconfig=disabled \
        -Dshader-cache=disabled -Dc_link_args="$static" -Dcpp_link_args="$static" \
        -Dzlib:default_library=static -Dexpat:default_library=static \
        --wrap-mode=default
    ninja -C build-ppc src/gallium/targets/rdn/rdn_gltest
    ls -l build-ppc/src/gallium/targets/rdn/rdn_gltest
    ;;
darwin)
    # Mac OS X 10.4 on PowerPC, with the toolchain container
    # (scripts/darwin.sh image). Extra arguments are ninja targets.
    # Built for size and with long calls: a PowerPC branch reaches 16 MB,
    # and Mesa's code is larger than that.
    compat=$tp/darwin8-compat
    mkdir -p "$compat"
    rsync -a --delete "$root/mesa/darwin8/" "$compat/src/"
    "$root/scripts/darwin.sh" sh -c "powerpc-apple-darwin8-gcc -O2 -Wall \
        -mmacosx-version-min=10.4 -c -o $compat/tiger_compat.o \
        $root/mesa/darwin8/tiger_compat.c && \
        powerpc-apple-darwin8-ar rcs $compat/libtigercompat.a $compat/tiger_compat.o"
    cat > "$tp/mesa-darwin-cross.ini" <<INI
[binaries]
c = 'powerpc-apple-darwin8-gcc'
cpp = 'powerpc-apple-darwin8-g++'
objc = 'powerpc-apple-darwin8-gcc'
ar = 'powerpc-apple-darwin8-ar'
strip = 'powerpc-apple-darwin8-strip'
ranlib = 'powerpc-apple-darwin8-ranlib'
pkg-config = 'false'

[built-in options]
c_args = ['-mmacosx-version-min=10.4', '-mlongcall', '-isystem', '$compat/src/include']
cpp_args = ['-mmacosx-version-min=10.4', '-mlongcall', '-isystem', '$compat/src/include', '-include', '$compat/src/tiger_compat.h']
objc_args = ['-mmacosx-version-min=10.4', '-mlongcall', '-isystem', '$compat/src/include']
objc_link_args = ['-mmacosx-version-min=10.4', '-static-libgcc', '-Wl,-dead_strip', '$compat/libtigercompat.a']
c_link_args = ['-mmacosx-version-min=10.4', '-static-libgcc', '-Wl,-dead_strip', '$compat/libtigercompat.a']
cpp_link_args = ['-mmacosx-version-min=10.4', '-static-libgcc', '-static-libstdc++', '-Wl,-dead_strip', '$compat/libtigercompat.a']

[host_machine]
system = 'darwin'
cpu_family = 'ppc'
cpu = 'ppc7400'
endian = 'big'
INI
    shift || true
    [ -f build-darwin/build.ninja ] || "$root/scripts/darwin.sh" meson setup build-darwin \
        "${common[@]}" --cross-file "$tp/mesa-darwin-cross.ini" \
        -Dxmlconfig=disabled -Dshader-cache=disabled \
        -Dzlib:default_library=static -Dexpat:default_library=static \
        -Dbuildtype=minsize -Db_ndebug=true \
        --wrap-mode=nodownload
    "$root/scripts/darwin.sh" ninja -C build-darwin "$@"
    ;;
*)
    echo "usage: $0 [x86|ppc|darwin]" >&2
    exit 2
    ;;
esac
