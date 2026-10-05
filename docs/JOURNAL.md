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

## 2026-10-04 — AtomBIOS interpreter ported; ASIC_Init replays the Linux trace on x86 and big-endian PowerPC

**Tried.**

- `scripts/card-state.py` after the trace guest had exited.
- `scripts/fetch-deps.sh`: Bootlin `powerpc-e300c3` musl toolchain
  (2026.08-1) and a sparse checkout of the Linux radeon sources (commit
  `7704c4c5bb12`) into `third_party/`.
- Copied `atom.c`, `atom.h`, `atom-bits.h`, `atom-names.h`, `atom-types.h`,
  `atombios.h` and `ObjectID.h` (all MIT-headed) into `hw/atom/` and adapted
  them to the OS layer `hw/rdn_os.h` through `hw/atom/atom_port.h`. Register
  callbacks are in `hw/rdn_atom.c`.
- `tests/atom_replay.c`: runs `ASIC_Init` from the VBIOS dump against a mock
  card driven by the reference trace. Each access must be the next one in the
  trace; reads return what the real card returned. Built for x86 and for
  PowerPC, the latter run under `qemu-ppc`.

**Observed.**

- When QEMU exits, the card is left un-POSTed: all CRTCs off,
  `CONFIG_MEMSIZE` 0, `MC_SEQ_SUP_CNTL` 0. `MC_SEQ_MISC0` also reads 0 in
  that state, so the memory type cannot be read from it before `asic_init`.
- First replay attempt matched 1492 accesses and then diverged on an I/O
  port write: the interpreter asked for register 0x2a44 through the I/O BAR,
  and Linux reaches registers beyond the 256-byte BAR through its
  index/data pair at 0/4. With that reproduced in `rdn_atom.c`:
- `make test`: `PASS: asic_init matches trace entries 53..2488 (2436
  accesses), digest 63c4ddd8`, identical on x86 and on PowerPC.
- The 53 accesses before it are Linux's GPU reset check and POST check
  (reads) plus BIOS scratch register setup at 0x172c–0x173c. What follows
  entry 2488 is `evergreen_init_golden_registers` and later the MC
  microcode.
- Turks goes through `evergreen_init()` in Linux, not `ni.c`'s Cayman path;
  only the microcode loader comes from `ni.c`.

**Concluded.**

- The interpreter is correct for the largest table this VBIOS has, and is
  endian-clean: no difference between hosts across 2436 accesses with
  data-dependent control flow.
- The design goal "what is validated without hardware is what runs on
  hardware" holds so far: the same `hw/` objects go into the test and will go
  into the tool and the kext.
- A sysfs reset of the card from the host is still untested, because it
  needs a POSTed card to be meaningful. The bridge `00:01.1` exposes
  `reset_subordinate`, which is the candidate.

## 2026-10-04 — First cold POST of the real card with our code; VRAM works without MC microcode

**Tried.** `rdn_card_post()` (scratch registers, reset check, `ASIC_Init`)
added to the library and to the replay test, then `tools/rdn_tool.c`, a
sysfs front end, run against the driverless card:

    scripts/card-bind.sh none
    sudo build/x86/rdn_tool status
    sudo build/x86/rdn_tool -t traces/ours-post-1.txt post
    sudo build/x86/rdn_tool vramtest
    scripts/card-reset.sh
    sudo build/x86/rdn_tool -n -t traces/ours-post-2-noio.txt post
    sudo build/x86/rdn_tool vramtest

**Observed.**

- Replay test from the very first access: `PASS: post matches trace entries
  0..2488 (2489 accesses), digest 6b51b16f` on x86 and PowerPC.
- First run of the tool read only 4096 bytes of VBIOS: sysfs returns one page
  per read. The ATOM header parsed fine, the tables behind it were zeros. It
  was caught before posting. The tool now loops and checks the image length
  against the size byte in the ROM header.
- Cold POST on the real card: returned 0. Afterwards `CONFIG_MEMSIZE` =
  0x400 (the register is in megabytes: 1024), `MC_SEQ_MISC0` = `0x500026a9`
  (GDDR5), all `CRTC_CONTROL` = `0x00400310`, `MC_SEQ_SUP_CNTL` = 0.
- Our access log against Linux's first 2489 accesses: the same sequence and
  the same written values. The only difference is 170 extra reads of 0x60c
  and 0x61c, polling loops that spin more often when the registers are not
  trapped by QEMU.
- VRAM test through the 256 MB aperture, no MC microcode loaded: 16373 words
  spread over the aperture, written and read back twice (pattern and
  complement), 0 mismatches.
- `reset_subordinate` on root port `00:01.1` puts the card back to
  un-POSTed (`CONFIG_MEMSIZE` 0, `MC_SEQ_MISC0` 0). Wrapped as
  `scripts/card-reset.sh`. POST, reset, POST, reset, POST all behaved the
  same.
- POST with `-n` (no I/O BAR; AtomBIOS indirect I/O through MMIO) also
  succeeds and passes the VRAM test.

**Concluded.**

- Cold POST without x86 code works on this card with our library, in the
  Linux order.
- The MC microcode is not needed for the memory to be usable through the
  aperture: `ASIC_Init` trains the GDDR5 itself. The "no microcode in this
  phase" decision stands. Limits of the evidence: only the first 256 MB of
  the 1024 MB are reachable through the aperture, the test is sparse, and
  nothing has scanned out of that memory yet.
- The I/O BAR is optional, which removes one dependency on OpenBIOS and on
  the G5's Open Firmware.
- The reset method for milestone 2 is settled: `scripts/card-reset.sh`.
- After these runs the card is left posted and driverless, with nothing on
  screen. Next is DDC/EDID and the DCE5 modeset.

## 2026-10-04 — Tiger install media; installer driven through QMP

**Tried.** The user supplied `/srv/files/tiger/MacOSX.4.iso` (SHA-256
`d537b2b9…bf54f8`) and `xcode_2.5_8m2558_developerdvd.dmg` (SHA-256
`c59d5a5c…d9ab2e`). Booted the DVD with `scripts/tiger.sh install` and drove
the installer with `scripts/guest-ctl.py` (QMP screenshots, absolute pointer
through `usb-tablet`, key events).

**Observed.**

- The ISO carries an Apple Partition Map and boots on `mac99,via=pmu -cpu
  G4` to the language chooser in about a minute. Build strings found in the
  image: `8I128` and `8E5`; the exact 10.4.x version is to be read from the
  installed system.
- `qemu-img` cannot open the Xcode `.dmg` ("sector count ... larger than
  max"). `7z x -tdmg` extracts its pieces; concatenating them in order
  (`0.ddm`, partition map, ATAPI driver, `3.hfs`, `4.free`) gives a raw
  image with a partition map, `images/xcode25.img`.
- `usb-tablet` works as an absolute pointer in the Tiger installer, so
  clicks can be scripted at screenshot coordinates.
- Partitioned from the installer's Terminal:
  `diskutil partitionDisk disk0 1 APMFormat "Journaled HFS+" Tiger 31G`.
- Accepted the licence prompt and started an Easy Install on `Tiger`.

**Concluded.** The whole installation can be driven without a VNC client.
No 10.4.11 Combo Update was supplied; whether it is needed depends on the
version this DVD installs and on what Xcode 2.5 requires.

**Licence check.** `r600.c`, `r600_reg.h`, `radeon.h` and `nid.h`, which the
bring-up port drew on, carry the same MIT permission notice as the files
checked earlier and no GPL text.

## 2026-10-04 — Tiger 10.4.6 installed; ssh into the guest; 10.4.11 update started

**Tried.** Let the Easy Install finish, booted from disk, went through the
setup assistant with `guest-ctl.py`, enabled sshd, installed a key.

**Observed.**

- The copy took about 25 minutes under TCG. The installer restarts by
  itself and, because QEMU was started with `-boot d`, comes back up on the
  DVD. QEMU has to be stopped and restarted with `scripts/tiger.sh run`.
- A helper command of mine that ran `pkill -f guest-wait.sh` killed its own
  shell (the pattern matched its own command line). The second QEMU that was
  then started against the same disk was refused by the qcow2 write lock, so
  nothing was damaged. The QMP socket takes one client at a time; while a
  watcher holds it, `guest-ctl.py` cannot connect. The installer QEMU was
  ended with SIGTERM while it sat at the DVD's language screen.
- Snapshot `installed-raw` taken before the first boot.
- First boot: keyboard identification (click OK, press `z`, press `/`),
  region, no transfer, default keyboard, blank Apple ID, Command-Q on the
  registration form then Skip, account `tiger` / password `tiger`, default
  time zone and date.
- `sw_vers`: 10.4.6, build 8I128, Darwin 8.6.0, xnu-792.6.70. OpenSSH
  3.8.1p1.
- sshd: `sudo launchctl load -w /System/Library/LaunchDaemons/ssh.plist`.
  Sleep disabled with `pmset`. Key fetched in the guest with `curl` from a
  one-minute web server on the host's loopback (`10.0.2.2` from the guest).
  Tiger's sshd needs an RSA key; it is in `private/ssh/` (git-ignored).
  `scripts/tiger.sh ssh` works. `tiger` has passwordless sudo. Tiger's sudo
  has no `-n` option.
- Apple's Software Update server still serves the 10.4.11 Combined update
  for PowerPC (186 MB) and a Java update. `sudo softwareupdate -i
  MacOSXUpdCombo10.4.11PPC-10.4.11` started in the guest.

**Concluded.** No Combo Update file is needed from the user as long as
Apple's server keeps answering. The guest is reachable over ssh, so the rest
of the setup does not need the GUI.

## 2026-10-04 — 10.4.11 and Xcode 2.5 in the guest; host kernel oops on first Tiger passthrough

**Tried.** Updated the guest to 10.4.11, installed the Xcode 2.5 pieces
needed for kexts, snapshotted, then `scripts/card-bind.sh vfio`,
`scripts/card-reset.sh` and `sudo scripts/tiger.sh passthru 0000:10:00.0`.

**Observed (guest).**

- `softwareupdate -i` in the guest lists the 10.4.11 Combined update but
  never opens a connection to download it. The package URL is in Apple's
  catalog (`http://swscan.apple.com/content/catalogs/index-1.sucatalog`);
  downloaded on the host from `swcdn.apple.com` (`.tar`, 195368960 bytes,
  SHA-256 `6689b28d…b6d1aa`), copied in over ssh, installed with
  `installer -pkg`.
- Apple's `installer` hangs under QEMU on larger packages: all files are
  written, the log says "Assembling receipt", then the process and its
  `runner` helper sit idle indefinitely. Seen with the Combo update
  (559 MB), `DevSDK.pkg` and `MacOSX10.4.Universal.pkg`; small packages
  finish. Cause unknown. Killing it leaves a stale helper registration and
  the next `installer` dies with "Couldn't register server on this host"
  until the guest is restarted.
- After a restart: 10.4.11, build 8S165, xnu-792.24.17. Snapshot
  `tiger-10.4.11`.
- The real meta-package is `/Volumes/Xcode Tools/Packages/XcodeTools.mpkg`
  (the one at the top of the disc is not usable from the command line).
  Its sub-packages are relocatable: on their own they install to `/`, and
  the meta-package places `DeveloperTools`, `DeveloperToolsCLI`, `gcc4.0`
  and `MacOSX10.4.Universal` under `/Developer`.
- What was done: `DevToolsSystem`, `DeveloperToolsCLI`, `gcc4.0`, `DevSDK`
  and `BSDSDK` with `installer` into `/`; then the four `/Developer`
  packages unpacked directly with
  `cd /Developer && gzip -dc <pkg>/Contents/Archive.pax.gz | sudo pax -r -pe`.
  The `DeveloperTools` postflight only rewrites library paths when the
  folder is not `/Developer`, so it was not run.
- Result: `xcodebuild` runs (DevToolsCore-798.0), gcc 4.0.1 build 5370
  compiles and runs a PPC binary, `Kernel.framework` headers include
  `IOKit/graphics/IOFramebuffer.h` and `IOKit/pci/IOPCIDevice.h`, the
  Kernel Extension project template is present. Snapshot `clean-install`.

**Observed (host).**

- QEMU for the passthrough run never started the guest. The host kernel
  oopsed inside it: "BUG: unable to handle page fault", in
  `native_queued_spin_lock_slowpath`, called from `__pm_runtime_resume` <-
  `irq_chip_pm_get` <- `request_threaded_irq` <-
  `vfio_pci_set_intx_trigger`, i.e. while vfio-pci was setting up the card's
  legacy INTx interrupt. About four minutes later a second oops hit an
  unrelated process (`nmbd`, in `anon_vma_fork`).
- Since then anything that walks `/proc` for all processes (`ps`, `pgrep`)
  blocks and cannot be killed. Plain commands, sudo, dmesg and the
  filesystem still work. The kernel is tainted `D` and `W`.
- A copy of the kernel log is in `build/host-oops-dmesg.txt`.

**Concluded.**

- The host kernel is damaged and has to be rebooted before any more
  hardware work. Rebooting is the user's call.
- Hypothesis, not verified: the stale interrupt state comes from unbinding
  the host's `radeon` earlier in this boot, which logged WARNs in
  `irq_domain_remove` and `msi_device_data_release`. The x86 trace guest ran
  after that unbind without an oops, but it used MSI ("Failed to enable MSI"
  was printed once); `mac99` has no MSI, so this was the first INTx request.
- If the hypothesis is right, the fix is to keep `radeon` from ever binding
  to the card, which needs a boot-time change on the host (approval
  required). If it is wrong, INTx passthrough of this card is broken on its
  own and the guest should be started without the interrupt.
- Milestone 1's passthrough test has not produced any result yet.

## 2026-10-04 — radeon blacklisted on the host

The user chose the blacklist. Wrote `/etc/modprobe.d/osx-gpu.conf`
(`blacklist radeon`). `lsinitcpio` shows the initramfs contains `amdgpu.ko`
but not `radeon.ko`, so no initramfs rebuild is involved. `modprobe -c`
lists the blacklist. Takes effect at the next host boot, which is pending.
To verify after boot: `radeon` not loaded, `0000:10:00.0` without a driver,
card un-POSTed.

## 2026-10-04 — After the host reboot: Tiger boots with the card passed through

**Tried.** After the user rebooted the host: checked the blacklist took
effect, `scripts/card-bind.sh vfio`, then
`sudo scripts/tiger.sh passthru 0000:10:00.0 tiger-passthru-1`.

**Observed.**

- After boot: `radeon` not loaded, `10:00.0` without a driver, card
  un-POSTed, kernel taint 0. `snd_hda_intel` had the audio function and
  logged "GPU sound probed, but not operational"; unbinding it gave no
  warning.
- No oops this time. QEMU started, vfio reset the card, the kernel stayed
  untainted through boot and shutdown of the guest.
- OpenBIOS's config writes to the card, in full: command 0; interrupt line
  0x1b; BAR0 sized and set to `0x9000000c`; BAR2 sized and set to
  `0xa0000004`; command 3. It never touched the upper halves of the 64-bit
  BARs, BAR4 (I/O) or the expansion ROM register.
- QEMU `info pci`: BAR0 64-bit prefetchable at `0x90000000`–`0x9fffffff`,
  BAR2 64-bit at `0xa0000000`–`0xa001ffff`, BAR4 I/O not mapped, ROM not
  mapped, IRQ 27.
- Tiger booted to the desktop on the emulated VGA. In the device tree the
  card is `pci1002,675d@10`, class `IOPCIDevice`, registered and matched,
  with `assigned-addresses` for BAR0 (256 MB at `0x90000000`) and BAR2
  (128 KB at `0xa0000000`), `IODeviceMemory` listing the same two ranges,
  `compatible` = `pci1028,2b20`, `pci1002,675d`, `pciclass,030000`, and
  `AAPL,ndrv-dev`. `reg` lists only config space, BAR0 and BAR2.
- No driver attached to it (IONDRVSupport did not create a framebuffer).
- `ioreg -l` for the whole tree fails in the guest with "can't obtain
  properties"; `ioreg -p IODeviceTree -n <name>` works.
- QEMU's `xp` monitor command refuses to read the BAR addresses ("Cannot
  access memory"), so register access from the guest side is not yet
  demonstrated.
- `scripts/card-state.py` reported "posted" after the guest exited. Wrong:
  the idle card was in D3hot under `vfio-pci` and every register read
  all-ones. The script now reports that case as unreadable.

**Concluded.**

- The oops hypothesis is supported: same command, same card, no `radeon`
  bind/unbind in this boot, no oops. One clean run is not proof.
- OpenBIOS handles the 256 MB 64-bit BAR without patching, placing it below
  4 GB. RESEARCH.md's worry about large and 64-bit BARs did not materialise.
- OpenBIOS does not assign the I/O BAR or the expansion ROM. The I/O BAR is
  not needed (POST works through MMIO). The ROM matters: under `mac99` the
  kext cannot read the VBIOS from the ROM BAR as things stand. Options are
  the load-from-file fallback already in the design, QEMU's `romfile=` with
  an OpenBIOS patch that assigns the ROM BAR, or mapping the ROM from the
  kext. To decide when the kext needs it.
- Milestone 1's criterion is met on "IOPCIDevice with BARs assigned". The
  "accessible" half needs kernel code in the guest and will be shown by the
  first probe kext.

## 2026-10-04 — Decision: VBIOS from a file under QEMU

The user chose the load-from-file route over patching OpenBIOS to assign the
ROM BAR. Reading the VBIOS from the expansion ROM is deferred to the real G5
and listed in PLAN.md under "Deferred to the real G5".

## 2026-10-04 — Probe kext reads the card's registers from inside Tiger; QEMU patched

**Tried.** `kext/RadeonNI`: an `IOService` matching `IOPCIMatch 0x675d1002`
that maps BAR2 with `mapDeviceMemoryWithRegister` and logs `CRTC_CONTROL`,
`CONFIG_MEMSIZE` and `MC_SEQ_MISC0`. Built in the guest with a plain
Makefile (`scripts/kext.sh build`), loaded with `kextload` from `/tmp`.

**Observed.**

- Build problems, in order: `-mkernel` is not accepted by Apple's PowerPC
  gcc 4.0.1 (use `-static` only); `OSBundleLibraries` versions of 8.0.0 for
  `com.apple.kernel.*` are rejected on 10.4.11, 6.0 is accepted (Apple's own
  ATI kexts ask for 1.0.0b1); with `-lkmodc++` before the objects on the
  link line, `kld` fails with undefined `.constructors_used` /
  `.destructors_used`, so the libraries go after the objects.
- First load: the kext matched, mapped BAR2 at physical `0xa0000000`, and
  read 0 from every register. The host trace showed no access at all.
- Cause: in QEMU 11.1.1 the 32-bit `mac99` main PCI bus aliases only
  `0x80000000`-`0x8fffffff` of PCI memory into the CPU's address space
  (`hw/pci-host/uninorth.c`; the 0x70000000-sized hole RESEARCH.md quoted is
  the U3/G5 variant). OpenBIOS's own `ranges` property says the same 256 MB,
  yet it assigned the card's BARs at `0x90000000` and `0xa0000000`.
- Patch `patches/qemu/0001-uninorth-widen-mac99-pci-hole.patch`: the alias
  now covers `0x80000000`-`0xefffffff`. OpenBIOS is unchanged.
- Second load, patched QEMU: `CRTC0..5_CONTROL` = `00400110`,
  `CONFIG_MEMSIZE` = 0, `MC_SEQ_MISC0` = 0, the values of the un-POSTed card.
  The host trace shows the same eight reads at `region2+0x6e70` ... with the
  same values. Unload works. Host kernel untainted throughout.

**Concluded.**

- Milestone 1's criterion is met: `IOPCIDevice` in `ioreg`, BARs assigned,
  and BAR2 accessible from kernel code in Tiger, byte order included.
  BAR0 (the aperture) has the same mapping path but has not been touched
  from the guest yet.
- The device tree still advertises a 256 MB memory range for the bus, and
  the bridge's address-select register still describes 256 MB. Tiger mapped
  the BAR anyway. If something later depends on those, OpenBIOS and the
  bridge register need the matching change.
- The in-guest build loop works and takes seconds.

## 2026-10-04 — EDID over DDC with our code

**Tried.** `hw/rdn_i2c.c`: I2C line lookup from the VBIOS `GPIO_I2C_Info`
table (read with byte accessors, not through the packed struct), pin
handling ported from Linux `radeon_i2c.c`, and a bit-banged I2C master
written from the I2C specification (Linux's `i2c-algo-bit` is GPL and was
not used). `tests/i2c_edid.c` attaches a simulated EDID EEPROM to each line.
Then on the card: `card-bind.sh none`, `card-reset.sh`, `rdn_tool post`,
`rdn_tool edid`.

**Observed.**

- The reference trace shows Linux bit-banging DDC on the DVI connector
  through registers 0x6460-0x646c (22656 accesses to 0x6468 for one probe);
  the DisplayPort connector uses the AUX channel at 0x62a0.
- The VBIOS lists 8 I2C lines, ids 0x90-0x97, all marked hardware-capable.
- Simulated test: 256-byte EDID read correctly on all 8 lines, a line with
  no device fails with -ENXIO, pins are released afterwards. Same output on
  x86 and PowerPC.
- Real card, cold-POSTed by our code: line 3 (id 0x93, registers 0x6460)
  returns a 256-byte EDID, preferred mode 1366x768. `cmp` against
  `private/monitor-edid.bin`, which the Linux driver read: identical.
- The other lines: -ENXIO (no acknowledge) on three, -ETIMEDOUT (clock never
  rises) on four. The four that time out have no pull-up because nothing is
  connected there.

**Concluded.** DDC works with our code on the real hardware. The DVI
connector's DDC line is id 0x93, matching the registers Linux reported for
it. A probe of an unconnected line costs up to 50 ms in the clock-stretch
timeout.

## 2026-10-04 — Modeset: AtomBIOS call log, port, replay test, first run on the card

**Tried.**

- `scripts/x86-trace-guest.sh` now puts a kprobe on `atom_execute_table`
  and logs every table call with its first eight parameter words
  (`traces/ref-radeon-3.*`; decoded list in
  `traces/ref-radeon-3.atomcalls.txt`).
- `hw/rdn_mode.c` (EDID preferred timing, HDMI detection) and
  `hw/rdn_modeset.c` (PLL info and divider computation, AdjustDisplayPll,
  SelectCRTC_Source, SetPixelClock v6, DTD timing, scanout surface, LUT,
  DIG encoder v4, UNIPHY transmitter v4), ported from the Linux sources named
  in the file. Parameter blocks are built byte by byte.
- `tests/modeset_replay.c`, then `rdn_tool modeset` on the cold-POSTed card
  with a test pattern drawn through the aperture.

**Observed.**

- Linux's cold modeset to 1366x768, in table calls: AdjustDisplayPll
  (`031e2166 00000020`), SelectCRTC_Source (`00030300`),
  UpdateCRTC_DoubleBufferRegisters lock, EnableCRTCMemReq/EnableCRTC off,
  EnableSpreadSpectrumOnPPLL off, SetPixelClock (`00002166 020c004c
  00031e00`: fb 76, post 12, ref 2, PLL 1, UNIPHY, HDMI),
  SetCRTC_UsingDTDTiming, SetCRTC_OverScan, EnableScaler,
  DIG1TransmitterControl disable, EnableCRTC/MemReq on, BlankCRTC off,
  unlock, DIGxEncoderControl setup and panel mode, DIG1TransmitterControl
  enable. Linux drives this monitor in HDMI mode because its EDID has an HDMI
  vendor block.
- Our PLL computation gives the same dividers (fb 76.0, ref 2, post 12).
- After BIOS POST the card's framebuffer address range starts at
  `0xF00000000` (`MC_VM_FB_LOCATION` = `0x0f3f0f00`, `HDP_NONSURFACE_BASE` =
  `0x0f000000`). Linux moves it to 0; we leave it and program the scanout
  address accordingly.
- Linux pads the pitch of a 1366-wide buffer to 1408 pixels; we do the same.
- Replay test: all 620 register accesses of `rdn_modeset()` occur in order
  in Linux's trace of the same modeset (5 exempt: scanout address and the
  read of `MC_VM_FB_LOCATION`), identical on x86 and PowerPC. The rule is
  weaker than in `atom_replay`: extra accesses on Linux's side are skipped.
- On the card: `modeset returned 0`. Readback: `CRTC0_CONTROL` =
  `0x00410311` (the value Linux leaves), `H_TOTAL` = 0x6ff, the CRTC position
  and frame counters advance, viewport 1366x768, pitch 1408, surface high
  address 0xf.
- Compared with the readback taken while Linux drove the same mode, 70 of
  1408 display registers differ. Expected ones: scanout address, counters.
  Not ported: the HDMI block at 0x7028-0x7124 (infoframes, audio), line
  buffer and watermark setup at 0x6b00-0x6bd0, hot-plug and AUX state.

**Concluded.** The CRTC is running the right timing with our code. Whether
the monitor shows the pattern is for the user to say. If HDMI signalling
without infoframes is a problem for the monitor, DVI signalling is the
obvious thing to try next; the unported watermark setup is the other
candidate.

## 2026-10-04 — "Input not supported": the display engine clock was never started

**Observed.** With the first modeset the user's monitor reported "input not
supported": it saw a signal it could not use.

**Found.** Linux's AtomBIOS call log shows two calls at driver start that the
first port left out: `DIG1TransmitterControl` with action INIT for each
connector (`07020002 00000004` for the DVI-I one), and `SetPixelClock`
`0000d2f0 00000000 00000002`, which starts the display engine PLL (DCPLL) at
540 MHz (`atombios_crtc_set_disp_eng_pll`). The pixel PLL's output is
derived relative to that clock.

**Tried.** `rdn_display_init()`: transmitter INIT for the DVI-I connector and
the DCPLL at `ulDefaultDispEngineClkFreq` from FirmwareInfo (0 on this card,
so the DCE5 default of 540 MHz). The replay test now also checks it against
the driver-start phase of the trace: 699 accesses in order, same on both
architectures. `rdn_tool modeset` calls it; `-d` forces DVI signalling.

**Observed after the change.** Registers 0x6530 and 0x6590, which differed
from the Linux readback before, now match it (`00005200`, `00010f9f`).
CRTC still running.

**Concluded.** Probably the cause; the user's answer decides. Lesson for the
replay tests: they prove that what we do is what Linux did, not that we do
everything Linux did. The AtomBIOS call log is the checklist for omissions.

## 2026-10-04 — Milestone 2 confirmed by the user

After the display engine clock fix, with the card reset to un-POSTed and
then driven only by `rdn_tool post` and `rdn_tool modeset`, the user reports
on the monitor: the white edge line, the black margin, the eight colour bars
and the black box with the resolution text. Nothing looks wrong, except that
white looks slightly yellow, which the user attributes to the monitor and
asked to ignore for now.

Milestone 2's criterion is met. The display engine clock was the cause of
"input not supported".

Open, low priority: the yellowish white. If it turns out not to be the
monitor, candidates are HDMI signalling without an AVI infoframe (the
monitor may assume a different colour encoding or range) and the unported
parts of the colour pipeline. `rdn_tool -d modeset` (DVI signalling) is a
quick way to tell.

## 2026-10-04 — The kext brings the card up from inside Tiger; pattern confirmed by the user

**Tried.** The test pattern moved into `hw/rdn_pattern.c`. `kext/RadeonNI`
now links the whole `hw/` library, supplies the OS layer for IOKit, takes the
VBIOS from a `VBIOS` data property that `scripts/kext.sh load` injects into
the personality from `private/vbios.rom`, and in `start()` runs POST, EDID,
display init and modeset, with the pattern drawn through BAR0. Loaded in a
passthrough guest whose card QEMU had just reset to un-POSTed.

**Observed.**

- Apple gcc 4.0.1 compiles the library unchanged. Kernel.framework has no
  `<stddef.h>` or `<stdbool.h>`; `kext/RadeonNI/compat/` provides them. The
  kernel exports everything else the kext needs, including `vsnprintf`.
- First load, kernel log: "GPU not posted. posting now...", `CONFIG_MEMSIZE`
  1024 MB, EDID 256 bytes with 1366x768 preferred, display engine clock,
  PLL fb 76.0 ref 2 post 12, "modeset returned 0, CRTC0_CONTROL 00410311".
- The user confirms the monitor shows the same pattern as with the Linux
  tool. The colour bars being in the right order means the pixel byte order
  is right: the kext writes big-endian XRGB words and the card swaps them
  (`GRPH_SWAP_CONTROL` = 2).
- Host trace: the kext's first 1754 register writes (the POST) equal those
  of the x86 tool's MMIO-only run except two writes to register 0x3c that
  differ in one bit, a read-modify-write of a value the card returned
  differently. 2.08 million aperture writes for the pattern.
- Host kernel untainted.

**Concluded.** The design goal holds end to end: the code validated in
userspace on x86 is the code that runs in the big-endian Tiger kernel, and it
worked on the first load. Milestone 3's "single fixed mode" step is reached
in the sense that the kext drives the display; it is not yet an
`IOFramebuffer`, so Tiger does not know the screen exists.

## 2026-10-04 — IOFramebuffer: Tiger draws its desktop on the 7570

**Tried.** `RadeonNI` is now an `IOFramebuffer` subclass with one display
mode (the EDID's preferred timing, 32 bpp, XRGB): hardware bring-up in
`start()` before `IOFramebuffer::start()`, aperture range at the start of
BAR0, pixel and timing information, DDC block from the EDID read at start,
one connection. Loaded with `kextload` into the running guest.

**Observed.**

- "framebuffer started"; the service registers under the card's node, but
  nothing attaches: no `IODisplayConnect`, no user client, and the pattern
  stays on screen. The window server does not pick up a framebuffer that
  appears after it has started.
- After `sudo killall WindowServer` (the login session restarts):
  `display0` (`IODisplayConnect`) with `AppleDisplay` and an
  `IOFramebufferUserClient` appear under `RadeonNI`.
- `system_profiler SPDisplaysDataType` lists a second display on
  `pci1002,675d`: 1366 x 768, 32-bit colour, alongside the QEMU VGA at
  800 x 600 as main display. It reports 4 MB of VRAM, which is the size of
  the aperture range we expose (one surface).
- The user sees the screen blank and then the Mac OS X screen being drawn,
  very slowly.
- The slowness is the harness: with `x-no-mmap=on` QEMU traps every
  framebuffer write. `scripts/tiger.sh passthru` now only traces with
  `TIGER_TRACE=1`.
- Host kernel untainted.

**Concluded.**

- RESEARCH.md's open question is answered: an `IOFramebuffer` loaded with
  `kextload` after boot is used only after the window server restarts.
- Tiger accepts the framebuffer and extends the desktop onto it at the
  native resolution. Still missing for milestone 3: more than one mode,
  resolution and depth switching, the cursor, gamma.

## 2026-10-04 — Mode list, depths and the first resolution change from System Preferences

**Tried.**

- Library: `rdn_edid_detailed_mode()`, 8/16/32 bpp surfaces, `rdn_lut_set()`,
  and blanking of a running CRTC before a mode change. The replay test now
  also covers Linux's direct switch from 1366x768 to 1920x1080 (2051
  accesses in order; PLL fb 88.0 ref 2 post 8, as Linux).
- Kext: modes from the EDID's detailed timings (1366x768 and 1920x1080 for
  this monitor), depth indices 0/1/2 for 8-bit indexed, 16-bit 1555 and
  32-bit, colour table through `setCLUTWithEntries`, gamma through
  `setGammaTable`, both written to the hardware table.
- In an untraced passthrough guest: load, restart the window server, open
  the Displays pane, "Gather Windows", pick 1920 x 1080 for the 7570.

**Observed.**

- Without tracing the kext loads in 15 s and the desktop draws at normal
  speed.
- The Displays pane for the 7570 lists exactly "1366 x 768" and
  "1920 x 1080", colours "Millions".
- First attempt: the driver set 1920x1080, the user saw the picture
  correctly for a moment, then corruption. The window server had crashed
  with SIGSEGV in `vecCopyBytesFDV` <- `CGXReleaseDisplayDeviceSurface`,
  at an address 0x5a0000 past the display's base. `getApertureRange` was
  returning only as much memory as the current mode needs (4.1 MB for
  1366x768), and the window server kept its original mapping after the
  switch to a mode that needs 7.9 MB. loginwindow did not bring the window
  server back; the guest was restarted.
- Fix: the aperture range (and `getVRAMRange`) is now a fixed size, enough
  for the largest mode at 32 bpp (8 MB here).
- Second attempt: mode 2 set, `system_profiler` reports 1920 x 1080, the
  same window server process is still running, no new crash report.
- With a second display present, the absolute pointer from QEMU's USB
  tablet lands off target on the main screen, as if scaled about its centre
  by about 1.17 horizontally and 1.15 vertically. Clicks are sent
  pre-corrected for that. Sometimes the mapping is exact; not understood.
- Host kernel untainted.

**Concluded.** Mode switching works from System Preferences once the
framebuffer memory range does not depend on the mode. What the monitor shows
at 1920x1080 is for the user to confirm.

## 2026-10-04 — 1080p confirmed; all six mode and depth combinations switched

- The user confirms the desktop at 1920x1080 "looking perfect", with the
  display settings window on it.
- `tools/guest/cgmode.c`, built in the guest, lists and switches modes
  through Quartz Display Services and can move the cursor to a display.
  Quartz lists six modes for the 7570: 1366x768 and 1920x1080, each at 8, 16
  and 32 bpp.
- Switched through all six in turn (1366x768 at 32/16/8, 1920x1080 at
  8/16/32): every `CGDisplaySwitchToMode` returned 0, every `rdn_modeset`
  returned 0, the window server process survived throughout, host kernel
  untainted. When a switch changes resolution and depth together the window
  server goes through an intermediate 32 bpp mode.
- What each of these looked like on the monitor, and whether the cursor is
  visible there, is for the user to say.

## 2026-10-04 — Milestone 3 and phase 1 confirmed by the user

The user reports on the mode and depth cycle: the monitor took time to
re-sync at each step, so not every state was seen for long, but everything
seen looked good and "input not supported" never appeared. The mouse cursor
is visible on the 7570's screen.

With the earlier confirmations (desktop at 1366x768, then at 1920x1080 after
a change made in System Preferences), milestone 3's criterion is met, and
with it the goal of phase 1: Tiger in QEMU shows its desktop at native
resolution on a monitor connected to the real card.

Not individually confirmed by eye: each of the 8 and 16 bpp states. The
cursor is the software one drawn by IOGraphics.

## 2026-10-04 — The window server restart cannot be avoided from a running session

**Tried.** In a freshly booted guest with the kext loaded and the window
server not restarted: `tools/guest/fbprobe.c`, which calls
`IOServiceRequestProbe(fb, kIOFBUserRequestProbe)` on every `IOFramebuffer`
(what "Detect Displays" does).

**Observed.** Both framebuffers return success. Nothing attaches to
`RadeonNI`, and the window server's log shows no new display. `scripts/
kext.sh activate` (restart the window server, wait for an
`IODisplayConnect` under `RadeonNI`) takes 14 s; the screen came back at
1920x1080, the resolution chosen earlier, so Tiger remembers the setting per
display. Tiger's `/etc/rc` runs `/etc/rc.local`, if it exists, at line 294,
after `SystemStarter` and before `loginwindow` is started from `/etc/ttys`.

**Concluded.** The window server enumerates framebuffers once, when it
starts; a re-probe only re-examines displays on framebuffers it already
has. One experiment, so "cannot" means "not by this route". The restart is
now one command. The way to not need it is to have the kext loaded before
the window server starts, which is what an installed driver does anyway;
in the harness that could be an `/etc/rc.local` that runs `kextload` from a
directory outside `/System/Library/Extensions`. That touches the "always
loaded by hand" decision and is the user's call. Not tried.

## 2026-10-04 — Installed in /System/Library/Extensions: loads at boot, no window server restart

**Decision.** The user dropped the rule "always loaded by hand from a
temporary directory, never installed in /System/Library/Extensions" (it
came from the original brief; the user wants the driver to run as a real
one does).

**Tried.** Snapshot `pre-kext-install` with the guest off. `scripts/kext.sh
install`: copy the bundle with the VBIOS-carrying Info.plist to
`/System/Library/Extensions/RadeonNI.kext`, root:wheel, validate with
`kextload -t -n`, delete `Extensions.mkext` and `Extensions.kextcache`,
touch the Extensions folder. Guest restart.

**Observed.** 39 s after boot: the kext is loaded (index 54), the log shows
a cold POST ("GPU not posted. posting now..."), modes, "framebuffer
started", and then, with no action from outside, the window server setting
mode 2 (1920x1080, the resolution saved earlier). `IODisplayConnect`,
`AppleDisplay` and the user client are attached. `system_profiler`: 1920 x
1080, 32-bit. Host kernel untainted.

**Concluded.** Installed like any driver, the kext is matched and started
before the window server, which then uses the screen by itself. This is the
normal way to run it from now on; `load` + `activate` remains as the quick
loop. Recovery if an installed kext breaks boot: start the guest without the
card (`scripts/tiger.sh run`), where the kext matches nothing and is never
loaded, then `scripts/kext.sh uninstall`; or restore a snapshot.

## 2026-10-04 — Install package for the real Mac; QEMU work stays on the temporary kext

**Decision (user).** For development under QEMU, keep loading the freshly
built kext from a temporary directory so that it can never be stale.
Installing into `/System/Library/Extensions` is for the real G5, through a
script the user runs there.

**Tried.** `g5/install.sh`, `g5/uninstall.sh`, `g5/README.txt` and
`scripts/make-g5-package.sh`, which packs them with the guest-built kext
into `build/RadeonNI-g5.tar.gz`. `install.sh` checks for root, 10.4 and
PowerPC, validates the VBIOS image (55 AA, length, ATOMBIOS marker), injects
it into the Info.plist with perl's MIME::Base64, validates the kext with
`kextload -t -n` before touching the Extensions folder, installs, and
prints the three ways to recover a Mac that does not boot. Rehearsed in the
guest: removed the earlier install, ran the package's `install.sh`,
restarted; then `uninstall.sh`, restarted.

**Observed.** After install and restart: kext loaded, "GPU not posted.
posting now...", framebuffer started, the window server set 1920x1080 by
itself. After uninstall and restart: kext not loaded, not installed.
`scripts/kext.sh install` was removed again; `up` refuses to run while an
installed copy is present.

**Concluded.** The install path works on Tiger 10.4.11. Nothing here has
run on a real Mac: Open Firmware's BAR assignment, the interplay with the
GeForce 6600 LE, and cold POST on the G5 are all untested.

## 2026-10-04 — Phase 2 (acceleration) investigated and planned

**Asked.** The user wants Quartz Extreme, Core Image, OpenGL 2.0 and
whatever else is possible, milestone by milestone, and proposed building
Mesa for Mac OS X with an interface to the card.

**Looked at.** In the running guest, read-only: the extension list, the
OpenGL framework's bundles, `nm` of `GLEngine`, `GLRendererFloat` and
`ATIRadeon9700GLDriver`, the 9700 kext's personality, strings of
OpenGL.framework and CoreGraphics. On the web: Mesa `r600` on big-endian,
cross toolchains for Tiger, VMsvga2's GLD directory.

**Observed.** Two plug-in levels (`gli*` engine, `gld*` driver, 17 and 62
functions); a public dispatch table header; `gldInitDispatch` and
`gldUpdateDispatch` in every driver; no accelerated device in the guest.
Details in RESEARCH.md section 10.

**Concluded.** Mesa is the right 3D driver but cannot be a plain `gld*`
driver. The plan is a `gld*` bundle that gives the dispatch table to Mesa,
on top of a kernel half ported from Linux into `hw/`. The takeover is a
hypothesis; milestone A0 tests it before any hardware work. Milestones A0
to A7, risks and the user's decisions (ABI learning by observation and
disassembly reading, microcode handled like the VBIOS) are in PLAN.md.
Nothing was run on the card.

## 2026-10-05 — A0: Tiger's OpenGL loads our bundle; glClear replaced

**Tried.** (1) `tools/guest/glprobe.c`: list renderers, draw off-screen,
read a pixel back. (2) An accelerator service created by the framebuffer
when the personality has `Accelerator` true (`RDN_ACCEL=1 scripts/kext.sh
load`): `IOGLBundleName` on itself, `IOAccelTypes` path on the framebuffer,
as VMsvga2 does. (3) `gld/RadeonNIGLDriver.bundle`, installed in the guest's
`/System/Library/Extensions`: forwards all 62 `gld*` calls to
`GLRendererFloat`, logging them. (4) Changes on the way through: renderer
ID, `GL_RENDERER` string, `glClear` replaced.

**Observed.**
- Baseline without our kext: renderers 0x20200 and 0x20400, both software.
  The guest's installed OpenGL.framework has no headers; the 10.4u SDK does.
- With the accelerator published and the window server restarted, `glprobe`
  loads our bundle. The window server itself did not load it, asked for no
  user client, and kept running.
- With the renderer info's ID changed, CGL lists 0x00021a00 in place of
  0x20400, on both displays.
- With the pixel format's ID changed too, `CGLCreateContext` fails with
  10002 before calling the bundle. Left unchanged, the context is created
  through our bundle (asked for as 0x20400) and `GL_RENDERER` is our string.
- Replacing entry 10 of the table passed to `gldInitDispatch` had no effect:
  that table is the driver's own, inside the engine context, not the
  application's. A search of the engine context for the address of the
  application's `disp` (printed by `glprobe`) found it at `table - 0x18`.
- Replacing `disp->clear` through that pointer: the program clears to green
  and reads back 255 0 255 255, magenta, as our replacement paints. The
  entry was still ours after drawing and after a `gldUpdateDispatch`.

**Concluded.** The plug-in route works as far as A0 asked: our bundle is
loaded by the system's OpenGL and can replace a GL entry point for an
application. The hypothesis in the plan was wrong in one detail
(`gldInitDispatch` does not hand over the public table) and is not fully
proven: one entry of 686, one short off-screen program. Open points are in
GLD-INTERFACE.md. The guest is left with the kext loaded with the
accelerator and the bundle installed.
