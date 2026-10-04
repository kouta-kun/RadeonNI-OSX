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
| PowerPC Linux cross compiler | Bootlin `powerpc-e300c3` musl 2026.08-1 in `third_party/ppc-toolchain/`, fetched by `scripts/fetch-deps.sh`; static binaries run under `third_party/qemu/qemu-ppc` |
| Linux radeon sources | sparse checkout in `third_party/linux/`, same script; reference for porting |
| gcc / meson / ninja / dtc | present |

Verified on the source build (2026-10-04): `vfio-pci` is available in
`qemu-system-ppc` on this x86 host, with the `x-no-mmap` and `romfile`
properties; the trace events `vfio_region_read`, `vfio_region_write`,
`vfio_pci_read_config` and `vfio_pci_write_config` exist; `mac99` reaches the
OpenBIOS prompt on serial and its PCI bus is `/pci@f2000000` with `mac-io`,
`usb`, `QEMU,VGA` and `ethernet`.

## Binding the card to a driver

    scripts/card-bind.sh status
    scripts/card-bind.sh vfio      # for QEMU passthrough
    scripts/card-bind.sh none      # driverless, for the milestone 2 tool
    scripts/card-bind.sh radeon    # back to the stock drivers

No persistent host configuration: the script uses sysfs `driver_override` on
both functions, releases fbcon first, and refuses to act on anything that is
not `1002:675d` or that is the boot VGA. `vfio`, `none` and `status` have been run; `radeon` has not.

At every host boot the stock `radeon` auto-binds about 60 s in, POSTs the
card and puts fbcon on it. Unbinding it logs two kernel WARNs
(`irq_domain_remove`, `msi_device_data_release`) and taints the kernel with
W; nothing else was affected.

## Returning the card to the un-POSTed state

Verified 2026-10-04: starting a QEMU guest with the card on `vfio-pci` resets
it to the un-POSTed state (all `CRTC_CONTROL` enables clear, `CONFIG_MEMSIZE`
zero, Linux says "GPU not posted"), regardless of what initialised it before.
This worked twice in a row.

Also known: the host firmware does not POST the card (iGPU is primary, the
ROM has no EFI image), so it is un-POSTed after a host boot until `radeon`
binds.

Without QEMU (verified 2026-10-04, three times):

    scripts/card-bind.sh none      # once after boot; unbinds radeon
    scripts/card-reset.sh          # secondary bus reset through 00:01.1

`sudo scripts/card-state.py` or `sudo build/x86/rdn_tool status` reports the
POST state without changing it.

## Driving the card from userspace (milestone 2)

    scripts/card-bind.sh none
    sudo build/x86/rdn_tool status
    sudo build/x86/rdn_tool [-t traces/<name>.txt] [-n] post
    sudo build/x86/rdn_tool vramtest

`-t` logs every register access in the compact trace format, `-n` avoids the
I/O BAR, `-b <file>` takes the VBIOS from a file instead of the expansion
ROM. The tool refuses to run while a kernel driver owns the card or if the
device is not `1002:675d`.

## Reference trace guest

    scripts/card-bind.sh vfio
    scripts/x86-trace-guest.sh <name>
    scripts/trace-split.py traces/<name>.log

Boots the host kernel in a KVM guest with an initramfs holding `radeon` and
its firmware, and logs every access to the card. Takes about three minutes
and writes roughly 1 GB to `traces/`. The card must be behind a bridge in an
x86 guest: on the root bus the kernel treats its ROM as shadowed at 0xc0000
and radeon reads the wrong BIOS. See REFERENCE-TRACE.md.

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
