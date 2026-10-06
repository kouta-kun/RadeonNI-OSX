#!/bin/bash
# Put the command processor's microcode and its licence into firmware/
# (git-ignored): TURKS_pfp.bin, TURKS_me.bin and LICENSE.radeon.
#
#   scripts/fetch-firmware.sh
#
# Files already there are kept. Otherwise they come from the host's
# linux-firmware package (/lib/firmware/radeon, possibly compressed), and
# failing that from the linux-firmware repository on kernel.org. Each file
# is checked against the checksum below, which is of the upstream file.
#
# The microcode is AMD's, redistributable in binary form under
# LICENSE.radeon, which has to travel with it. It is never committed here.

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
dir=$root/firmware
upstream=https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain

sums="65d98665384252ddd627c365dbfd2f238b0538570d9e1dad6d4382bb93723089  TURKS_pfp.bin
37fa5fb7cdb13df94c9a64d7ba752b63119f063f64326b67ec54656cd184363b  TURKS_me.bin
fc6223d4bfe9f2f9e2eddc44b9fe5721d0caf49f01cb08d602906add686d8c6f  LICENSE.radeon"

good() {
    [ -s "$dir/$1" ] &&
        [ "$(sha256sum < "$dir/$1" | cut -d' ' -f1)" = "$(echo "$sums" | awk -v f="$1" '$2 == f { print $1 }')" ]
}

# local <name>: from the host's own copy, whatever its compression.
local_copy() {
    local name=$1 src
    case "$name" in
    LICENSE.radeon)
        for src in /usr/share/licenses/linux-firmware-radeon/LICENSE.radeon \
                   /usr/share/doc/linux-firmware/LICENSE.radeon \
                   /usr/share/doc/firmware-amd-graphics/copyright; do
            [ -f "$src" ] && cat "$src" > "$dir/$name" && return 0
        done
        return 1 ;;
    esac
    src=/lib/firmware/radeon/$name
    if [ -f "$src" ]; then cat "$src" > "$dir/$name"
    elif [ -f "$src.zst" ]; then zstd -dcf "$src.zst" > "$dir/$name"
    elif [ -f "$src.xz" ]; then xz -dc "$src.xz" > "$dir/$name"
    else return 1
    fi
}

remote_copy() {
    local name=$1 path
    case "$name" in
    LICENSE.radeon) path=LICENSES/LICENSE.radeon ;;
    *) path=radeon/$name ;;
    esac
    curl -fsSL -o "$dir/$name" "$upstream/$path"
}

mkdir -p "$dir"
for name in TURKS_pfp.bin TURKS_me.bin LICENSE.radeon; do
    if good "$name"; then
        continue
    fi
    if local_copy "$name" 2> /dev/null && good "$name"; then
        echo "firmware/$name: from this host"
    elif remote_copy "$name" && good "$name"; then
        echo "firmware/$name: from kernel.org"
    else
        rm -f "$dir/$name"
        echo "could not get $name with the expected checksum" >&2
        exit 1
    fi
done
