# Shared by install.sh and uninstall.sh (sourced). Open Firmware variables from
# Linux through powerpc-utils' nvram ("nvram --print-config=NAME",
# "nvram --update-config NAME=VALUE") or nvsetenv. The variables are strings;
# values with spaces are passed as one argument.
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT

NVRAM_CMD=${NVRAM_CMD:-nvram}

fail() { echo "${0##*/}: $*" >&2; exit 1; }

# the old block (an earlier install) comes out; everything else stays
strip_block() {
    perl -0pe 's/ ?\( RadeonNI-OF begin \).*?\( RadeonNI-OF end \)//s'
}

nvram_get() {   # name -> stdout
    case "${NVRAM_CMD##*/}" in
    nvsetenv) "$NVRAM_CMD" "$1" 2>/dev/null | sed "s/^$1[[:space:]=]*//" ;;
    *) "$NVRAM_CMD" --print-config="$1" 2>/dev/null ;;
    esac
}

nvram_set() {   # name value
    case "${NVRAM_CMD##*/}" in
    nvsetenv) "$NVRAM_CMD" "$1" "$2" ;;
    *) "$NVRAM_CMD" --update-config "$1=$2" ;;
    esac || fail "writing $1 with $NVRAM_CMD failed"
}

nvram_load() {  # sets $current and $use
    command -v "$NVRAM_CMD" > /dev/null || fail "$NVRAM_CMD not found (install powerpc-utils), or use --print-only"
    current=$(nvram_get nvramrc)
    use=$(nvram_get 'use-nvramrc?')
}
