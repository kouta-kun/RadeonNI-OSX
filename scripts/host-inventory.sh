#!/bin/bash
# Read-only host inventory. Prints everything docs/HARNESS.md records about the
# host, so the document can be re-checked after a hardware or kernel change.
# Usage: scripts/host-inventory.sh   (uses sudo for dmesg and PCI capabilities)

set -u

section() { printf '\n=== %s ===\n' "$1"; }

section "System"
. /etc/os-release && echo "$PRETTY_NAME"
uname -srvm
echo "cmdline: $(cat /proc/cmdline)"
sudo -n dmidecode -t baseboard 2>/dev/null | grep -E 'Manufacturer|Product Name'
sudo -n dmidecode -t bios 2>/dev/null | grep -E 'Version|Release Date'
lscpu | grep -E 'Model name|Nombre del modelo'

section "GPUs"
for dev in /sys/bus/pci/devices/*; do
    class=$(cat "$dev/class")
    case "$class" in 0x03*) ;; *) continue ;; esac
    addr=$(basename "$dev")
    sudo -n lspci -nnvv -s "$addr" 2>/dev/null || lspci -nnvv -s "$addr"
    echo "    driver:      $(basename "$(readlink "$dev/driver" 2>/dev/null)" 2>/dev/null)"
    echo "    boot_vga:    $(cat "$dev/boot_vga" 2>/dev/null)"
    echo "    iommu_group: $(basename "$(readlink "$dev/iommu_group" 2>/dev/null)" 2>/dev/null)"
done

section "Radeon candidates (1002:6759 / 1002:675d = Turks)"
lspci -Dnn -d 1002: | grep -Ei 'vga|display|audio' || echo "none"

section "PCI tree"
lspci -tv

section "IOMMU"
echo "groups: $(ls /sys/kernel/iommu_groups 2>/dev/null | wc -l)"
sudo -n dmesg 2>/dev/null | grep -E 'AMD-Vi|DMAR|iommu: Default' | head -10
for g in /sys/kernel/iommu_groups/*; do
    for d in "$g"/devices/*; do
        echo "group $(basename "$g"): $(lspci -nns "$(basename "$d")")"
    done
done | sort -V

section "Kernel config"
zgrep -E 'CONFIG_MMIOTRACE=|CONFIG_VFIO_PCI=|CONFIG_DRM_RADEON=|CONFIG_VFIO_NOIOMMU' /proc/config.gz

section "Loaded GPU/VFIO modules"
lsmod | grep -E '^(radeon|amdgpu|vfio|vfio_pci|kvm)' || true

section "Tools"
for t in qemu-system-ppc qemu-ppc qemu-img gcc git meson ninja; do
    printf '%-18s %s\n' "$t" "$(command -v "$t" || echo MISSING)"
done
pacman -Q qemu-base 2>/dev/null
ls /lib/firmware/radeon 2>/dev/null | grep -i -E 'turks|btc' | tr '\n' ' '; echo
