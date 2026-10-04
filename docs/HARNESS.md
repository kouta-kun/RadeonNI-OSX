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
| Device | Cezanne iGPU `1002:1638` | Radeon HD 7570 `1002:675d` + audio `1002:aa90` |
| Address | `0000:30:00.0` | `0000:10:00.0`, `0000:10:00.1` (root port `00:01.1`) |
| Driver at boot | `amdgpu` | `radeon` (auto-loads ~60 s after boot), `snd_hda_intel` |
| `boot_vga` | 1 | 0 |
| IOMMU group | 11 (alone) | 10 (the two functions, nothing else) |
| BARs | 256M + 2M 64-bit pref, I/O `e000`, 512K 32-bit | 256M 64-bit pref, 128K 64-bit, I/O `f000`, ROM 128K |
| Reset | — | `bus` |

The host GPU is never touched: no unbind, no module unload, no reset.

### Slot constraint

IOMMU group 9 contains the whole X370 chipset: Ethernet `29:00.0`, chipset
USB and SATA, the ASMedia USB controller and every chipset PCIe port. A card in
a chipset slot cannot be isolated. The 7570 must sit in the CPU-attached x16
slot (`PCI_E1`), behind root port `00:01.1`.

### Software state

| Tool | State |
|---|---|
| `qemu-base` (Arch package) | 11.1.1-1, x86 only; used for `qemu-img` |
| `qemu-system-ppc`, `qemu-ppc` | 11.1.1 built from source by `scripts/build-qemu.sh` into `third_party/qemu/`; not installed on the host |
| OpenBIOS | 1.1 (built 2026-06-29), the binary shipped in the QEMU tarball; source build when patching is needed |
| PowerPC Linux cross compiler | missing, not in Arch repos; to be chosen in milestone 2 |
| gcc / meson / ninja / dtc | present |

Verified on the source build (2026-10-04): `vfio-pci` is available in
`qemu-system-ppc` on this x86 host, with the `x-no-mmap` and `romfile`
properties; the trace events `vfio_region_read`, `vfio_region_write`,
`vfio_pci_read_config` and `vfio_pci_write_config` exist; `mac99` reaches the
OpenBIOS prompt on serial and its PCI bus is `/pci@f2000000` with `mac-io`,
`usb`, `QEMU,VGA` and `ethernet`.

## Binding the card to a driver (PLANNED)

No persistent host configuration. Binding is done at runtime through sysfs
`driver_override`, for both functions of the card (VGA and HDMI audio), by a
script in `scripts/`. This avoids kernel parameters, initramfs changes and
blacklists, all of which need the user's approval.

Expected consequence: at every host boot the stock `radeon` module will
auto-bind and initialise the card. That is acceptable for milestone 0 (it is
the reference driver) and must be undone by the script before passthrough.

## Returning the card to the un-POSTed state (PLANNED, unverified)

Known so far (2026-10-04): the host firmware does not POST the card (iGPU is
primary, the ROM has no EFI image), so after a host boot it stays un-POSTed
until `radeon` binds and posts it.

Required by milestone 2. Candidates, to be tested in this order:

1. Secondary bus reset through the root port (`/sys/bus/pci/devices/<vga>/reset`
   with `reset_method` = `bus`), then check `radeon_card_posted()`'s
   conditions by hand: all `CRTC_CONTROL` enable bits clear and
   `CONFIG_MEMSIZE` zero.
2. If the firmware always POSTs the card at host boot and a bus reset does not
   clear it: suspend-to-RAM of the host is *not* an option without approval.

Record the result in JOURNAL and replace this section.

## QEMU and the Tiger guest

    scripts/build-qemu.sh             # once; builds third_party/qemu/
    scripts/tiger.sh create           # images/tiger.qcow2, 32 GB sparse
    scripts/tiger.sh install media/<tiger-dvd>.iso
    scripts/tiger.sh run
    scripts/tiger.sh passthru <host-pci-addr> [trace-name]
    scripts/tiger.sh ssh [cmd]

The machine is `-M mac99,via=pmu -cpu G4 -m 1024` with `sungem` networking.
Guest screen on VNC `127.0.0.1:5901`, guest ssh on `127.0.0.1:2222`, monitor
socket at `build/qemu-mon.sock`, serial log at `build/guest-serial.log`.
From another machine: `ssh -L 5901:127.0.0.1:5901 arch-server`, then a VNC
client on `localhost:5901`.

`passthru` adds `-device vfio-pci,host=<addr>,x-no-mmap=on` and logs the
`vfio_region_*` and `vfio_pci_*_config` trace events to `traces/<name>.log`.
It refuses to start unless the device is already bound to `vfio-pci`.
`x-vga=on` is not used; the emulated VGA stays primary.

Only the script plumbing has been run (QEMU starts and reaches OpenBIOS with
an empty disk). Booting Tiger, the install procedure and passthrough are
untested until the media and the card are available.

### Installing Tiger (PLANNED, needs the user at the VNC console)

1. `scripts/tiger.sh install media/<dvd>.iso`. In the installer: Disk Utility,
   partition the 32 GB disk as Apple Partition Map with one HFS+ Journaled
   volume, then install. Deselect printer drivers and extra languages.
2. First boot: create the user `tiger` (the scripts assume it; override with
   `TIGER_USER`). System Preferences, Sharing, enable Remote Login. Energy
   Saver: never sleep.
3. Shut down. `scripts/tiger.sh cdrom media/<combo-update>.dmg` to reach
   10.4.11, then the same with the Xcode 2.5 image.
4. Shut down. `scripts/tiger.sh snapshot clean-install`.

The steps after 2 can be driven over ssh once Remote Login is on.

## Guest recovery (PLANNED)

- The kext is only ever loaded with `kextload` from a temporary directory; a
  guest reboot always returns to a clean system.
- A `clean-install` qcow2 snapshot is taken once Tiger, updates, Xcode and ssh
  are in place; `qemu-img snapshot -a clean-install` restores it.

## Patches

None yet. Patches to QEMU or OpenBIOS live in `patches/` as `git format-patch`
files against a recorded upstream commit, and are described here.
