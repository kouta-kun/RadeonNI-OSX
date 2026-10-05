#!/bin/bash
# Run a command with the cross toolchain for Mac OS X 10.4 on PowerPC
# (powerpc-apple-darwin8-gcc/g++ 14.2, cctools, the 10.4u SDK), inside a
# container, with the repository mounted at the same path as on the host.
#
#   scripts/darwin.sh image            build the container image (once)
#   scripts/darwin.sh <command...>     run a command in it, as the caller's
#                                      user, in the current directory
#
# The compiler runs confined to the container and sees only the repository.
# Example: scripts/darwin.sh powerpc-apple-darwin8-g++ -std=c++17 -o hello hello.cpp

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
image=osx-gpu-darwin8

if [ "${1:-}" = image ]; then
    exec docker build -t "$image" "$root/scripts/darwin-toolchain"
fi
[ $# -gt 0 ] || { sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
docker image inspect "$image" > /dev/null 2>&1 || {
    echo "run 'scripts/darwin.sh image' first" >&2; exit 1; }
exec docker run --rm --network none -u "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$root:$root" -w "$PWD" "$image" "$@"
