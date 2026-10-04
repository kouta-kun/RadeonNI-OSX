# Journal

Dated log of experiments: what was tried, what was observed, what was
concluded. Negative results are recorded too. Newest entry last.

## 2026-10-04 — Host inventory; the HD 7570 is not on the PCI bus

**Tried.** Read-only inspection of the host (`scripts/host-inventory.sh`
reproduces it): os-release, kernel cmdline and config, `lspci -nnvv`, `lspci
-tv`, IOMMU groups, dmesg, loaded modules, DMI, installed tools.

**Observed.**

- Arch Linux, kernel 6.18.50-2-lts, MSI X370 KRAIT GAMING (MS-7A33), BIOS 1.L8
  (2024-08-30), Ryzen 5 5600G, 27 GiB RAM, 331 GB free on `/`. Uptime 2 days.
- AMD-Vi is enabled with interrupt remapping; 19 IOMMU groups; default domain
  type "Translated". No kernel parameters were needed for that.
- The only display controller is the 5600G's integrated GPU, `30:00.0`
  `1002:1638` (Cezanne), driver `amdgpu`, `boot_vga=1`, IOMMU group 11.
- No device with a Turks or Redwood ID exists. No `1002:aa90`-style HDMI audio
  function either. `radeon` is not loaded.
- The CPU's x16 slot hangs off root port `00:01.1`. That function does not
  respond to config reads (`setpci -s 00:01.1 0.l` selects nothing); only the
  dummy host bridge `00:01.0` is there. On this platform the GPP bridge is
  hidden when no link trained at boot.
- The chipset's downstream ports with nothing behind them (`20:00.0`,
  `20:02.0`, `20:03.0`, `20:04.0`) all report `PresDet-`.
- The DMI slot table is boilerplate (Intel-style bus addresses) and says
  nothing useful.
- Kernel has `CONFIG_MMIOTRACE=y`, `CONFIG_VFIO_PCI=m`, `CONFIG_DRM_RADEON=m`,
  `CONFIG_VFIO_NOIOMMU` unset. `TURKS_{mc,me,pfp,smc}.bin.zst` are present in
  `/lib/firmware/radeon`.
- QEMU: only `qemu-base 11.1.1-1` (x86 system emulator and `qemu-img`). No
  `qemu-system-ppc`, no `qemu-ppc` user emulator, no PowerPC cross compiler,
  no clang. Arch's repos have `qemu-system-ppc` and `qemu-user-static`
  11.1.1-4 but no `powerpc-linux-gnu-gcc`.
- No display manager or graphical session; the host is used over ssh. Passwordless
  sudo is available.

**Concluded.**

- The card is not installed, not seated, not powered, or dead. Milestone 0
  cannot start until it enumerates. This needs the user physically, and a
  power-off of the host.
- IOMMU grouping constrains which slot is usable: everything behind the X370
  chipset (Ethernet, chipset USB and SATA, all chipset PCIe slots) is one
  group, number 9. A card in a chipset slot could only be passed through
  together with the host's NIC, which is not acceptable. The card has to go in
  the CPU-attached x16 slot (`PCI_E1`, the one nearest the CPU), where it is
  expected to get its own group behind `00:01.1`. To be confirmed once it
  enumerates.
- The premise "the server has the 7570 installed as a secondary GPU" in the
  brief was wrong at the time of inspection; RESEARCH.md §11 updated.

## 2026-10-04 — QEMU 11.1.1 built from source; `mac99` and VFIO checked without the card

**Tried.** `scripts/build-qemu.sh`: QEMU 11.1.1 release tarball (SHA-256
`079ffbff…12482`), targets `ppc-softmmu` and `ppc-linux-user`, `log` trace
backend, built under `third_party/`. Then booted `-M mac99,via=pmu -cpu G4`
with `-nographic` and `auto-boot?=false`.

**Observed.**

- The build takes about two minutes and needs no packages beyond what the
  host already has.
- `vfio-pci` is built into `qemu-system-ppc` on the x86 host, with
  `x-no-mmap`, `romfile` and `x-vga` properties. The four trace events named
  in RESEARCH.md §8 exist under those names.
- OpenBIOS 1.1 (built 2026-06-29) reaches its prompt on serial. `dev
  /pci@f2000000 ls` lists `mac-io@c`, `usb@d`, `QEMU,VGA@e`, `ethernet@f`.
- `hw/ppc/mac_newworld.c` maps the PCI hole at `0x80000000`, as RESEARCH.md
  says.
- The host's OpenSSH 10.5 still offers `diffie-hellman-group1/14-sha1` and
  `ssh-rsa` when asked, so it should be able to talk to Tiger's sshd.

**Concluded.** The harness side of RESEARCH.md §8 holds as far as it can be
checked with no device: cross-architecture VFIO is compiled in and the trace
mechanism is there. Whether a device actually attaches, and what OpenBIOS
does with its BARs, is still open. `scripts/tiger.sh` wraps the guest
lifecycle; only its plumbing has been exercised.

## 2026-10-04 — Card installed: Turks PRO confirmed, memory is GDDR5, VBIOS dumped

**Tried.** After the user installed the card and rebooted: `lspci -nnvv`,
radeon's dmesg, a read-only mmap of BAR2 to read `MC_SEQ_MISC0` and
`CONFIG_MEMSIZE`, and a VBIOS dump through the sysfs `rom` node.

**Observed.**

- `0000:10:00.0` `1002:675d` Turks PRO, subsystem Dell `1028:2b20`, with audio
  function `10:00.1` `1002:aa90`. Behind root port `00:01.1`. The two
  functions are alone in IOMMU group 10. `reset_method` is `bus`.
- The iGPU is still `boot_vga=1` on `amdgpu`; the 7570 is `boot_vga=0`.
- `radeon` auto-loaded about 60 s into boot and bound; `snd_hda_intel` took
  the audio function.
- radeon: "GPU not posted. posting now...". The firmware left the card
  un-POSTed. The ROM holds a single legacy x86 image, no EFI image.
- Memory is GDDR5: the VBIOS board string says `GDDR5 64Mx32`, and
  `MC_SEQ_MISC0` = `0x500026a9` (type nibble 5, the value
  `ni_mc_load_microcode()` tests for). 1024 MB.
- radeon's "RAM width 128bits DDR" line does not distinguish memory types;
  it prints "DDR" for everything.
- Connectors: one DisplayPort, one DVI-I. Both report `disconnected`, no
  EDID.
- Link trained at 2.5 GT/s x4 although card and slot are capable of more.
- VBIOS: 65536 bytes, SHA-256 `591e5d5d…9bf1fb`, part `113-C3340200-101`.
  Details in HARDWARE.md.

**Concluded.**

- The card is Turks, not Redwood: the DCE5 plan stands.
- The brief's assumption of DDR3 is wrong. This is the configuration where
  Linux loads `TURKS_mc.bin`. Whether the memory works after `asic_init`
  without it is now the main open question for milestone 2; Haiku loads no
  microcode on these IDs, so it is not settled either way. The blob cannot be
  in the repository; if it turns out to be required it has to be loaded from
  a file the user supplies.
- A host reboot leaves the card un-POSTed until `radeon` binds. Preventing
  that bind would give a clean cold state, but needs a boot-time change
  (approval required). A secondary bus reset is available and still has to
  be tested as the no-reboot alternative.
- No monitor is detected, so EDID and the modeset part of the reference
  trace are waiting on the user.
- The x4 / 2.5 GT/s link does not matter for modesetting. Noted in case it
  points at a seating or slot problem.

## 2026-10-04 — Monitor EDID; reference trace captured in an x86 guest

**Tried.** With the user's approval: read the EDID through the host's
`radeon`, moved the card to `vfio-pci` (`scripts/card-bind.sh vfio`), and ran
`scripts/x86-trace-guest.sh` until it produced a complete trace.

**Observed.**

- EDID: 256 bytes, both checksums valid, manufacturer `XXX`, name `AAA`,
  2023, digital. Preferred timing 1366x768@59.79 (85.5 MHz); second detailed
  timing 1920x1080@60 (148.5 MHz). The host's radeon chose 1366x768 for
  fbcon.
- Unbinding `radeon` on the host produced two kernel WARNs
  (`irq_domain_remove`, `msi_device_data_release`) and set the W taint. No
  other effect; `vfio-pci` bound normally.
- Guest attempt 1: the initcpio busybox is dynamically linked and has no
  `insmod` or `mount` applets. Fixed by copying its libraries and adding
  `kmod` and `mount` from the host.
- Guest attempt 2: with the card directly on the guest's root bus, radeon
  failed with "Fatal error during GPU init", error -22. The guest kernel had
  logged "Video device with shadowed ROM at [mem 0x000c0000-0x000dffff]" for
  the 7570, so radeon read the emulated VGA's BIOS at 0xc0000 and found no
  ATOM signature. `romfile=` did not help, for the same reason.
- Guest attempt 3: card behind a `pci-bridge`. radeon read the ROM through
  the passed-through ROM BAR, logged "GPU not posted. posting now...", and
  initialised fully. `modetest` then set 1920x1080 and 1366x768.
- Trace: 929 MB raw, of which all but 16 MB is framebuffer aperture writes.
  Details in REFERENCE-TRACE.md.

**Concluded.**

- Milestone 0's criteria are met: ID, memory type, VBIOS, trace.
- A VFIO reset gives the un-POSTed state without rebooting the host. That is
  the recovery method for milestone 2, still to be reproduced outside QEMU.
- Linux does load the MC microcode on this card from cold (6024 words to
  `MC_SEQ_SUP_PGM`). Whether the card works without it remains the open
  question for milestone 2.
- The x86 "shadowed ROM" behaviour is specific to x86 guests and does not
  apply to `mac99`, but it is exactly the failure the pc297 attempt hit:
  taking the VBIOS from the legacy address instead of the ROM BAR.
- After the run the card stays on `vfio-pci`; the monitor shows nothing
  until something drives the card again. A host reboot gives it back to
  `radeon`.
