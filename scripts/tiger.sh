#!/bin/bash
# Tiger guest harness for qemu-system-ppc -M mac99.
#
#   scripts/tiger.sh create              create images/tiger.qcow2 (32 GB, sparse)
#   scripts/tiger.sh install <dvd>       boot the install DVD image
#   scripts/tiger.sh cdrom <image>       boot the disk with an image in the drive
#   scripts/tiger.sh run [qemu args...]  boot the disk
#   scripts/tiger.sh passthru <addr> [trace-name]
#                                        boot with the host PCI device <addr>
#                                        (e.g. 0000:01:00.0) on vfio-pci; with
#                                        TIGER_TRACE=1 every access is traced
#                                        to traces/<trace-name>.log (slow)
#   scripts/tiger.sh snapshot <name>     save a qcow2 snapshot (guest off)
#   scripts/tiger.sh restore <name>      revert to a snapshot (guest off)
#   scripts/tiger.sh ssh [cmd...]        ssh into the guest
#
# Guest screen: VNC on 127.0.0.1:5901 (tunnel with ssh -L 5901:127.0.0.1:5901).
# Guest ssh:    127.0.0.1:2222.  QEMU monitor: build/qemu-mon.sock.
# QMP for scripts/guest-ctl.py: build/qemu-qmp.sock. A USB tablet gives the
# guest an absolute pointer so clicks can be scripted.
# Environment:  TIGER_MEM (MB, default 1024), TIGER_DISK, TIGER_USER,
#               TIGER_BOOTARGS (kernel boot arguments, e.g. debug=0x100).

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
qemu=$root/third_party/qemu/qemu-system-ppc
disk=${TIGER_DISK:-$root/images/tiger.qcow2}
mem=${TIGER_MEM:-1024}
user=${TIGER_USER:-tiger}

base_args() {
    mkdir -p "$root/build"
    args=(
        -M mac99,via=pmu -cpu G4 -m "$mem"
        -drive "file=$disk,format=qcow2,media=disk"
        -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:2222-:22"
        -device sungem,netdev=n0
        -vnc 127.0.0.1:1
        -monitor "unix:$root/build/qemu-mon.sock,server,nowait"
        -qmp "unix:$root/build/qemu-qmp.sock,server,nowait"
        -device usb-tablet
        -serial "file:$root/build/guest-serial.log"
    )
    # TIGER_BOOTARGS="debug=0x100" makes a kernel panic print its text and
    # backtrace on the screen instead of the restart dialog.
    if [ -n "${TIGER_BOOTARGS:-}" ]; then
        args+=(-prom-env "boot-args=$TIGER_BOOTARGS")
    fi
}

need_qemu() {
    [ -x "$qemu" ] || { echo "run scripts/build-qemu.sh first" >&2; exit 1; }
}

cmd=${1:-}
shift || true
case "$cmd" in
create)
    mkdir -p "$(dirname "$disk")"
    [ -e "$disk" ] && { echo "$disk already exists" >&2; exit 1; }
    qemu-img create -f qcow2 "$disk" 32G
    ;;
install|cdrom)
    need_qemu
    image=${1:?image path required}
    base_args
    boot=c
    [ "$cmd" = install ] && boot=d
    exec "$qemu" "${args[@]}" -drive "file=$image,format=raw,media=cdrom" -boot "$boot"
    ;;
run)
    need_qemu
    base_args
    exec "$qemu" "${args[@]}" -boot c "$@"
    ;;
passthru)
    need_qemu
    addr=${1:?host PCI address required}
    name=${2:-passthru-$(date +%Y%m%d-%H%M%S)}
    drv=$(basename "$(readlink "/sys/bus/pci/devices/$addr/driver" 2>/dev/null)" 2>/dev/null || true)
    [ "$drv" = vfio-pci ] || { echo "$addr is bound to '${drv:-nothing}', not vfio-pci" >&2; exit 1; }
    base_args
    # Tracing traps every access to the card, including each framebuffer
    # write, which makes drawing very slow. It is off unless TIGER_TRACE=1.
    if [ "${TIGER_TRACE:-0}" = 1 ]; then
        mkdir -p "$root/traces"
        exec "$qemu" "${args[@]}" -boot c \
            -device "vfio-pci,host=$addr,x-no-mmap=on" \
            -trace 'vfio_region_*' -trace 'vfio_pci_*_config' \
            -D "$root/traces/$name.log"
    fi
    exec "$qemu" "${args[@]}" -boot c -device "vfio-pci,host=$addr"
    ;;
snapshot)
    qemu-img snapshot -c "${1:?snapshot name required}" "$disk"
    ;;
restore)
    qemu-img snapshot -a "${1:?snapshot name required}" "$disk"
    ;;
ssh)
    # Tiger ships an old OpenSSH; re-enable the algorithms it speaks.
    exec ssh -p 2222 -i "$root/private/ssh/tiger_rsa" -o IdentitiesOnly=yes \
        -o KexAlgorithms=+diffie-hellman-group14-sha1,diffie-hellman-group1-sha1 \
        -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
        -o ConnectTimeout=10 -o ServerAliveInterval=5 -o ServerAliveCountMax=3 \
        "$user@127.0.0.1" "$@"
    ;;
*)
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
