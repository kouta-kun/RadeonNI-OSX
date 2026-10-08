#!/bin/bash
# Build piglit's OpenGL tests for Mac OS X 10.4 on PowerPC (GLUT framework,
# no Waffle), in the toolchain container (scripts/darwin.sh), into
# build/piglit-darwin/.
#
#   scripts/build-piglit.sh [make targets...]
#
# piglit is fetched into third_party/piglit on first use (git-ignored).
# tools/piglit/run.py runs the result on the G5.

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
src=$root/third_party/piglit
out=$root/build/piglit-darwin
sdk=/usr/local/MacOSX10.4u.sdk
tc=/usr/local/powerpc-apple-darwin8
compat=$root/third_party/darwin8-compat

[ -d "$src/.git" ] || git clone --depth 1 https://gitlab.freedesktop.org/mesa/piglit.git "$src"
for p in "$root"/tools/piglit/patches/*.patch; do
    if patch -d "$src" -p1 -R --dry-run -s -f < "$p" > /dev/null 2>&1; then
        continue    # already applied
    fi
    patch -d "$src" -p1 < "$p"
done
mkdir -p "$out"

cat > "$out/toolchain.cmake" <<TC
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR powerpc)
set(CMAKE_C_COMPILER powerpc-apple-darwin8-gcc)
set(CMAKE_CXX_COMPILER powerpc-apple-darwin8-g++)
set(CMAKE_AR powerpc-apple-darwin8-ar CACHE FILEPATH "")
set(CMAKE_RANLIB powerpc-apple-darwin8-ranlib CACHE FILEPATH "")
set(CMAKE_OSX_SYSROOT $sdk)
set(CMAKE_FIND_ROOT_PATH $sdk)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_FRAMEWORK FIRST)
set(CMAKE_C_FLAGS_INIT "-mmacosx-version-min=10.4 -mlongcall -mtune=970 -fno-strict-aliasing -isystem $compat/src/include -std=gnu11")
set(CMAKE_CXX_FLAGS_INIT "-mmacosx-version-min=10.4 -mlongcall -mtune=970 -fno-strict-aliasing -isystem $compat/src/include -include $compat/src/tiger_compat.h")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-mmacosx-version-min=10.4 -static-libgcc -static-libstdc++ -Wl,-dead_strip $compat/tiger_emutls.o $compat/libtigercompat.a")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-mmacosx-version-min=10.4 -static-libgcc -static-libstdc++ $compat/tiger_emutls.o $compat/libtigercompat.a")
TC

cd "$out"
if [ ! -f build.ninja ]; then
    "$root/scripts/darwin.sh" cmake -G Ninja -S "$src" -B "$out" \
        -DCMAKE_TOOLCHAIN_FILE="$out/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
        -DPIGLIT_USE_WAFFLE=OFF -DPIGLIT_BUILD_GL_TESTS=ON \
        -DPIGLIT_BUILD_GLES1_TESTS=OFF -DPIGLIT_BUILD_GLES2_TESTS=OFF \
        -DPIGLIT_BUILD_GLES3_TESTS=OFF -DPIGLIT_BUILD_CL_TESTS=OFF \
        -DPIGLIT_BUILD_VK_TESTS=OFF -DPIGLIT_BUILD_DMA_BUF_TESTS=OFF \
        -DPIGLIT_BUILD_EGL_TESTS=OFF -DPIGLIT_BUILD_GLX_TESTS=OFF
fi
"$root/scripts/darwin.sh" cmake --build "$out" -j"$(nproc)" -- "$@"
