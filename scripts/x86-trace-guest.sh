#!/bin/bash
# Reference trace: let the stock Linux radeon driver initialise the card
# inside a throwaway x86 KVM guest, with every MMIO and config access of the
# passed-through card logged by QEMU (x-no-mmap=on + vfio trace events).
#
#   scripts/x86-trace-guest.sh [trace-name]
#
# The guest is the host's own kernel plus a small initramfs built here from
# the host's radeon module, firmware, busybox, kmod, mount, modetest and setpci. The card
# must already be on vfio-pci (scripts/card-bind.sh vfio).
#
# The card sits behind a PCI bridge in the guest. Directly on the root bus the
# x86 kernel marks every VGA device the BIOS enabled as having its ROM shadowed
# at 0xc0000, and radeon then reads the emulated VGA's BIOS instead of its own.
#
# Output: traces/<name>.log (QEMU trace), traces/<name>.serial (guest console).
# Phases are delimited in the trace by config writes to the Interrupt Line
# register (0x3c) of the VGA function: 0xa1, 0xa2, ... (see guest-init below).

set -euo pipefail

VGA=${CARD_VGA:-0000:10:00.0}
AUDIO=${CARD_AUDIO:-0000:10:00.1}
name=${1:-ref-$(date +%Y%m%d-%H%M%S)}

root=$(cd "$(dirname "$0")/.." && pwd)
work=$root/build/x86-trace
kver=$(uname -r)
kernel=/boot/vmlinuz-linux-lts
# TRACE_ROMFILE=path makes QEMU serve a VBIOS file instead of the card's ROM.
romopt=${TRACE_ROMFILE:+,romfile=$TRACE_ROMFILE}

for d in "$VGA" "$AUDIO"; do
    drv=$(basename "$(readlink "/sys/bus/pci/devices/$d/driver" 2>/dev/null)" 2>/dev/null || true)
    [ "$drv" = vfio-pci ] || { echo "$d is on '${drv:-nothing}', not vfio-pci" >&2; exit 1; }
done

rm -rf "$work/rootfs"
mkdir -p "$work/rootfs"/{bin,proc,sys,dev,modules,usr/lib/firmware/radeon} "$root/traces"
r=$work/rootfs
# Arch layout: /lib, /lib64 and /usr/lib64 all point at /usr/lib.
ln -s usr/lib "$r/lib"
ln -s usr/lib "$r/lib64"
ln -s lib "$r/usr/lib64"

cp /usr/lib/initcpio/busybox "$r/bin/busybox"

# Dynamic tools from the host, with their libraries.
for tool in /usr/lib/initcpio/busybox /usr/bin/kmod /usr/bin/mount /usr/bin/modetest /usr/bin/setpci; do
    [ "$(basename "$tool")" = busybox ] || cp "$tool" "$r/bin/"
    { ldd "$tool" || true; } | awk '$3 ~ /^\// {print $3} $1 ~ /^\// {print $1}' | while read -r lib; do
        cp -L "$lib" "$r/usr/lib/"
    done
done

# The initcpio busybox has no insmod applet; kmod provides it.
ln -s kmod "$r/bin/insmod"

# radeon and its module dependencies, decompressed, in load order.
: > "$r/modules/order"
modprobe --show-depends radeon | awk '$1 == "insmod" {print $2}' | while read -r ko; do
    out=$(basename "${ko%.zst}")
    zstd -dqf "$ko" -o "$r/modules/$out"
    echo "$out" >> "$r/modules/order"
done

cp /lib/firmware/radeon/{TURKS_mc,TURKS_me,TURKS_pfp,TURKS_smc,BTC_rlc,SUMO_uvd}.bin.zst \
    "$r/usr/lib/firmware/radeon/"

cat > "$r/init" <<'GUEST'
#!/bin/busybox sh
/bin/busybox --install -s /bin
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev

DEV=01:01.0
mark() {
    echo "=== MARK $1 $2"
    setpci -s $DEV 3c.b=$1
}

mark a1 "before radeon load (init + first modeset by fbcon)"
while read -r m; do insmod /modules/$m || echo "insmod $m failed"; done < /modules/order
sleep 2
mark a2 "radeon loaded"
dmesg | grep -iE 'radeon|drm|fb'
cat /sys/class/graphics/fb0/virtual_size

ls -l /dev/dri/ /sys/class/drm/
modetest -M radeon -c 2>&1 | head -40
conn=$(modetest -M radeon -c | awk '$3 == "connected" {print $1; exit}')
echo "connected connector id: $conn"
if [ -n "$conn" ]; then
    mark a3 "modeset 1920x1080 (modetest)"
    (sleep 4) | modetest -M radeon -s "$conn:1920x1080-60"
    mark a4 "modeset 1366x768 (modetest)"
    (sleep 4) | modetest -M radeon -s "$conn:1366x768"
fi
mark a5 "fb blank"
echo 1 > /sys/class/graphics/fb0/blank
sleep 2
mark a6 "fb unblank"
echo 0 > /sys/class/graphics/fb0/blank
sleep 2
mark a7 "end"
dmesg | tail -20
poweroff -f
GUEST
chmod +x "$r/init"

(cd "$r" && find . | cpio -o -H newc --quiet | zstd -q -3 -f -o "$work/initramfs.img")

log=$root/traces/$name.log
serial=$root/traces/$name.serial
sudo rm -f "$log" "$serial"

sudo timeout 600 qemu-system-x86_64 \
    -enable-kvm -M pc -cpu host -m 2048 -smp 2 \
    -nodefaults -vga std -display none -no-reboot \
    -kernel "$kernel" -initrd "$work/initramfs.img" \
    -append "console=ttyS0 loglevel=7 panic=-1 rdinit=/init" \
    -serial "file:$serial" \
    -device pci-bridge,id=br1,chassis_nr=1,addr=05.0 \
    -device "vfio-pci,host=$VGA,bus=br1,addr=01.0,x-no-mmap=on$romopt" \
    -device "vfio-pci,host=$AUDIO,bus=br1,addr=02.0" \
    -trace vfio_region_read -trace vfio_region_write \
    -trace vfio_pci_read_config -trace vfio_pci_write_config \
    -msg timestamp=on -D "$log" || echo "qemu exit: $?"

sudo chown "$(id -u):$(id -g)" "$log" "$serial"
ls -l "$log" "$serial"
