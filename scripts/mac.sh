#!/bin/bash
# Run things on the two Macs this project tests on, without knowing where
# they are: the Power Mac G5 with the card in it, and the Tiger guest
# under QEMU.
#
#   scripts/mac.sh <g5|guest> ssh [command...]   a shell, or one command
#   scripts/mac.sh <g5|guest> sudo <command>     the command as root (sh -c)
#   scripts/mac.sh <g5|guest> put <local> <remote>
#   scripts/mac.sh <g5|guest> get <remote> <local>
#   scripts/mac.sh <g5|guest> grab <out.png> [scale]
#                                    the card's screen as the card holds it
#                                    (~/gl/rdnuc grab), as a PNG; scale 0.5
#                                    halves it
#   scripts/mac.sh <g5|guest> wait [seconds]     until it answers (after a
#                                                restart); default 240
#   scripts/mac.sh <g5|guest> addr               where it is: host and port
#
# The guest is always 127.0.0.1 port 2222 (scripts/tiger.sh forwards it).
# The G5 gets its address from the network and it changes. This script
# keeps the last one that worked in private/g5.addr, and when that one no
# longer answers it looks for the G5 again: among the machines that
# announce ssh on the network, the Apple network cards the host has seen,
# and the addresses it had before. The G5 is whichever of them lets our
# key in and says it is a PowerMac11,2. G5_HOST in the environment skips
# all of that.
#
# The key (private/ssh/tiger_rsa) is not in the repository. In a git
# worktree it is taken from the main checkout, so this works from there
# too.
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT

set -uo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
# The main checkout: where private/ is, also when this is a worktree.
main=$(git -C "$root" rev-parse --path-format=absolute --git-common-dir 2>/dev/null) || main=
main=${main%/.git}
[ -n "$main" ] && [ -d "$main/private" ] || main=$root
key=$main/private/ssh/tiger_rsa
cache=$main/private/g5.addr
user=${TIGER_USER:-tiger}
password=${TIGER_PASSWORD:-tiger}
G5_MODEL=PowerMac11,2
# The G5's two Ethernet ports, as the host's neighbour table shows them.
G5_MAC_PREFIX=00:14:51:64:97
G5_OLD_ADDRS="192.168.1.127 192.168.1.128"

usage() { sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-1}"; }
die() { echo "mac.sh: $*" >&2; exit 1; }

[ -f "$key" ] || die "no key at $key"

# Tiger ships an old OpenSSH; re-enable the algorithms it speaks.
ssh_opts=(-i "$key" -o IdentitiesOnly=yes
    -o KexAlgorithms=+diffie-hellman-group14-sha1,diffie-hellman-group1-sha1
    -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR
    -o ServerAliveInterval=5 -o ServerAliveCountMax=3)

# True if the machine at $1 lets the key in and is the G5.
is_g5() {
    [ "$(ssh "${ssh_opts[@]}" -o BatchMode=yes -o ConnectTimeout=3 "$user@$1" \
        'sysctl -n hw.model' 2>/dev/null)" = "$G5_MODEL" ]
}

# Look for the G5 on the network; prints its address and remembers it.
find_g5() {
    local a seen=" "
    for a in \
        $(ip neigh 2>/dev/null | awk -v m="$G5_MAC_PREFIX" 'index($0, m) { print $1 }') \
        $(timeout 6 avahi-browse -t -r -p _ssh._tcp 2>/dev/null |
            awk -F';' '$1 == "=" && $3 == "IPv4" { print $8 }') \
        $G5_OLD_ADDRS; do
        case "$seen" in *" $a "*) continue ;; esac
        seen="$seen$a "
        if is_g5 "$a"; then
            echo "$a" > "$cache"
            echo "$a"
            return 0
        fi
    done
    return 1
}

target=${1:-}; verb=${2:-}
[ -n "$target" ] && [ -n "$verb" ] || usage
shift 2
case "$target" in
guest) host=127.0.0.1; port=${TIGER_SSH_PORT:-2222} ;;
g5)
    port=22
    host=${G5_HOST:-$(cat "$cache" 2>/dev/null || true)}
    ;;
-h|--help|help) usage 0 ;;
*) usage ;;
esac

# ssh to the target. If the G5 is not where it was, find it and try again:
# ssh itself exits with 255 when it could not connect.
rssh() {
    local r
    if [ "$target" = g5 ] && [ -z "$host" ]; then
        host=$(find_g5) || die "the G5 does not answer anywhere I looked; is it on? (G5_HOST=address overrides)"
    fi
    ssh "${ssh_opts[@]}" -o ConnectTimeout=10 -p "$port" "$user@$host" "$@"
    r=$?
    if [ $r = 255 ] && [ "$target" = g5 ] && [ -z "${G5_HOST:-}" ] && ! is_g5 "$host"; then
        host=$(find_g5) || die "the G5 does not answer anywhere I looked; is it on? (G5_HOST=address overrides)"
        echo "mac.sh: the G5 is at $host now" >&2
        ssh "${ssh_opts[@]}" -o ConnectTimeout=10 -p "$port" "$user@$host" "$@"
        r=$?
    fi
    return $r
}

# One word for the remote shell, whatever is in it.
squote() {
    local q=${1//\'/\'\\\'\'}
    printf "'%s'" "$q"
}

# A remote path as one word; a leading ~/ stays the home directory.
rquote() {
    case "$1" in
    "~/"*) printf '~/%s' "$(squote "${1#\~/}")" ;;
    *) squote "$1" ;;
    esac
}

case "$verb" in
ssh)
    rssh "$@"
    ;;
sudo)
    [ $# -gt 0 ] || usage
    # Both Macs let this user be root without a password today; the
    # password is offered on standard input in case one asks.
    printf '%s\n' "$password" | rssh "sudo -S -p '' sh -c $(squote "$*")"
    ;;
put)
    [ $# = 2 ] || usage
    [ -f "$1" ] || die "no file $1"
    rssh "cat > $(rquote "$2")" < "$1"
    ;;
get)
    [ $# = 2 ] || usage
    rssh "cat $(rquote "$1")" > "$2.part.$$" && mv "$2.part.$$" "$2" ||
        { rm -f "$2.part.$$"; die "could not get $1"; }
    ;;
grab)
    [ $# -ge 1 ] || usage
    command -v pnmtopng > /dev/null || die "pnmtopng (netpbm) is not installed on this host"
    tmp=$(mktemp)
    trap 'rm -f "$tmp"' EXIT
    rssh 'f=/tmp/macgrab.$$.ppm; ~/gl/rdnuc grab $f > /dev/null 2>&1 && cat $f; r=$?; rm -f $f; exit $r' > "$tmp" ||
        die "no picture: is the kext's accelerator running there, and ~/gl/rdnuc built?"
    if [ -n "${2:-}" ]; then
        pnmscale "$2" "$tmp" 2>/dev/null | pnmtopng > "$1"
    else
        pnmtopng < "$tmp" > "$1"
    fi
    echo "$1"
    ;;
wait)
    limit=${1:-240}
    start=$(date +%s)
    answers() {
        if [ "$target" = g5 ] && [ -z "${G5_HOST:-}" ]; then
            # It may come back with another address.
            { [ -n "$host" ] && is_g5 "$host"; } || host=$(find_g5)
        else
            ssh "${ssh_opts[@]}" -o BatchMode=yes -o ConnectTimeout=5 -p "$port" \
                "$user@$host" true 2>/dev/null
        fi
    }
    until answers; do
        [ $(($(date +%s) - start)) -lt "$limit" ] || die "$target did not answer within $limit s"
        sleep 4
    done
    echo "$target answers after $(($(date +%s) - start)) s"
    ;;
addr)
    if [ "$target" = g5 ] && [ -z "${G5_HOST:-}" ]; then
        { [ -n "$host" ] && is_g5 "$host"; } || host=$(find_g5) || die "the G5 does not answer anywhere I looked"
    fi
    echo "$host $port"
    ;;
*)
    usage
    ;;
esac
