# Cross toolchain for Mac OS X 10.4 on PowerPC, with what Mesa's build needs.
#
# The base image is VariantXYZ/gcc-powerpc-apple-darwin8: GCC 14.2, cctools
# and the 10.4u SDK under /usr/local, target powerpc-apple-darwin8. This
# adds meson, ninja, cmake and the Python modules Mesa's and piglit's build
# scripts import.
# Built and used by scripts/darwin.sh; nothing is installed on the host.
FROM ghcr.io/variantxyz/gcc-powerpc-apple-darwin8:build-gcc-14.2-MacOSXSDK10.4u

RUN apt-get update -q && \
    apt-get install -y --no-install-recommends \
        python3-venv python3-pip ninja-build pkg-config bison flex rsync cmake \
    && apt-get clean -y && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/py && \
    /opt/py/bin/pip install --no-cache-dir meson==1.8.2 mako pyyaml packaging numpy six

ENV PATH=/opt/py/bin:/usr/local/bin:$PATH
WORKDIR /work

# meson recognises Apple's linker by the "PROJECT:ld64" line of `ld -v`,
# which this cctools build does not print. Add it in front of the real one.
RUN real=$(readlink -f /usr/local/powerpc-apple-darwin8/bin/ld) && \
    rm /usr/local/powerpc-apple-darwin8/bin/ld && \
    printf '%s\n' '#!/bin/sh' \
        'for a in "$@"; do [ "$a" = -v ] && echo "@(#)PROGRAM:ld  PROJECT:ld64-253.9" >&2; done' \
        "exec $real \"\$@\"" > /usr/local/powerpc-apple-darwin8/bin/ld && \
    chmod +x /usr/local/powerpc-apple-darwin8/bin/ld

# Nothing of GCC's runtime exists on a Tiger machine. With the dynamic
# libatomic out of the way, -latomic links the static one.
RUN rm -f /usr/local/powerpc-apple-darwin8/lib/libatomic*.dylib
