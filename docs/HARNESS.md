# Harness

The reproducible environment: host configuration, QEMU, patches, and how to
recover the card and the guest. Sections marked **PLANNED** describe intent,
not something that has been run; replace them with the real commands once they
have.

## Host (inspected 2026-10-04)

Re-run `scripts/host-inventory.sh` after any hardware or kernel change and
update this section.

| Item | Value |
|---|---|
| Hostname | `arch-server`, used headless over ssh (no display manager) |
| Distro | Arch Linux (rolling) |
| Kernel | `6.18.50-2-lts`, booted via GRUB |
| Kernel cmdline | `root=UUID=… rw zswap.enabled=0 rootfstype=ext4 loglevel=3 quiet` |
| Initramfs | mkinitcpio, `MODULES=()`, hooks include `kms` |
| Board | MSI X370 KRAIT GAMING (MS-7A33), BIOS 1.L8 (2024-08-30) |
| CPU | Ryzen 5 5600G (6C/12T), AMD-V |
| RAM / disk | 27 GiB / 331 GB free on `/` (NVMe) |
| IOMMU | AMD-Vi on, interrupt remapping on, 19 groups, no cmdline flags needed |
| Kernel config | `MMIOTRACE=y`, `VFIO_PCI=m`, `DRM_RADEON=m`, `VFIO_NOIOMMU` unset |
| Radeon firmware | `TURKS_{mc,me,pfp,smc}.bin.zst` in `/lib/firmware/radeon` |
| sudo | passwordless |

### GPUs

| | Host GPU | Passthrough GPU |
|---|---|---|
| Device | Cezanne iGPU `1002:1638` | Radeon HD 7570 |
| Address | `0000:30:00.0` | **not enumerated** |
| Driver | `amdgpu` | — |
| `boot_vga` | 1 | — |
| IOMMU group | 11 (alone) | — |
| BARs | 256M + 2M 64-bit pref, I/O `e000`, 512K 32-bit | — |

The host GPU is never touched: no unbind, no module unload, no reset.

### Slot constraint

IOMMU group 9 contains the whole X370 chipset: Ethernet `29:00.0`, chipset
USB and SATA, the ASMedia USB controller and every chipset PCIe port. A card in
a chipset slot cannot be isolated. The 7570 must sit in the CPU-attached x16
slot (`PCI_E1`), behind root port `00:01.1`.

### Software state

| Tool | State |
|---|---|
| `qemu-base` | 11.1.1-1 (x86 only) |
| `qemu-system-ppc` | missing; plan is a source build under `third_party/` so it can be patched |
| `qemu-ppc` (user mode) | missing; same source build |
| OpenBIOS | QEMU ships a binary; source build under `third_party/` when patching is needed |
| PowerPC Linux cross compiler | missing, not in Arch repos; to be chosen in milestone 2 |
| gcc / meson / ninja / dtc | present |

## Binding the card to a driver (PLANNED)

No persistent host configuration. Binding is done at runtime through sysfs
`driver_override`, for both functions of the card (VGA and HDMI audio), by a
script in `scripts/`. This avoids kernel parameters, initramfs changes and
blacklists, all of which need the user's approval.

Expected consequence: at every host boot the stock `radeon` module will
auto-bind and initialise the card. That is acceptable for milestone 0 (it is
the reference driver) and must be undone by the script before passthrough.

## Returning the card to the un-POSTed state (PLANNED, unverified)

Required by milestone 2. Candidates, to be tested in this order:

1. Secondary bus reset through the root port (`/sys/bus/pci/devices/<vga>/reset`
   with `reset_method` = `bus`), then check `radeon_card_posted()`'s
   conditions by hand: all `CRTC_CONTROL` enable bits clear and
   `CONFIG_MEMSIZE` zero.
2. If the firmware always POSTs the card at host boot and a bus reset does not
   clear it: suspend-to-RAM of the host is *not* an option without approval.

Record the result in JOURNAL and replace this section.

## QEMU command line (PLANNED)

Baseline for Tiger on `mac99`, before passthrough:

    qemu-system-ppc -M mac99,via=pmu -cpu G4 -m 1024 \
        -drive file=images/tiger.qcow2,format=qcow2 \
        -netdev user,id=n0,hostfwd=tcp:127.0.0.1:2222-:22 -device sungem,netdev=n0 \
        -vnc 127.0.0.1:1 -monitor unix:build/qemu-mon.sock,server,nowait

Passthrough adds:

    -device vfio-pci,host=<addr>,x-no-mmap=on \
    -trace 'vfio_region_*' -trace 'vfio_pci_*_config' -D traces/<name>.log

`x-vga=on` is not used. The emulated VGA stays primary.

## Guest recovery (PLANNED)

- The kext is only ever loaded with `kextload` from a temporary directory; a
  guest reboot always returns to a clean system.
- A `clean-install` qcow2 snapshot is taken once Tiger, updates, Xcode and ssh
  are in place; `qemu-img snapshot -a clean-install` restores it.

## Patches

None yet. Patches to QEMU or OpenBIOS live in `patches/` as `git format-patch`
files against a recorded upstream commit, and are described here.
