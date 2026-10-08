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

## 2026-10-05 — A1: command processor and first 3D drawing, from Linux userspace

**Tried.** Ported to `hw/`: `evergreen_gpu_init()` for Turks (`rdn_gpu.c`),
CP microcode load, ring start, ring test, fences and indirect buffers
(`rdn_cp.c`), and a drawing self-test built from Linux 3.9's blit code
(`rdn_selftest.c`). `tests/accel_replay.c` checks the bring-up against the
existing Linux trace (phase a1 already contains Linux's CP start). Then on
the card: `rdn_tool post`, `modeset`, `accel`. New `rdn_tool grab` saves the
scanout surface as an image, read back through the aperture.

Memory model as planned: ring, indirect buffer, shaders, vertices and
texture all in video memory, written through the aperture. No GART, no
write-back, no interrupt, no MC microcode. Video memory stays at GPU address
0xF00000000 where ASIC_Init puts it.

**Observed.**
- Replay: 2938 register accesses in Linux's order, identical on x86 and
  big-endian PowerPC. The first run failed on `W 28c58`: registers beyond
  the 128 KB BAR have to go through the index/data pair at offsets 0 and 4,
  as the trace shows Linux doing. `rdn_rreg`/`rdn_wreg` now do that.
- On the card: ring test passes in 1 us, fences complete.
- First drawing attempts gave results that lagged one run behind and
  readbacks that disagreed with themselves. Cause: the card caches host
  accesses to video memory; Linux writes `HDP_MEM_COHERENCY_FLUSH_CNTL`.
  With a flush before every ring commit and after every fence wait the
  results are stable.
- With a linear render target only every other group of four pixel columns
  was written. Setting the non-display tiling order bit in
  `CB_COLOR0_ATTRIB` fixes it; `ARRAY_LINEAR_GENERAL` does not.
- Final state, read back from the framebuffer (`build/grab-a1.png`): the
  textured square and the textured triangle are complete, in the right
  places, with the right colours in all four texture quadrants, on top of
  the test pattern.

**Concluded.** The command processor and the 3D pipeline (vertex fetch,
both shader stages, texturing, rasterisation, colour write) work on this
card under our code with everything in video memory. What the monitor
shows is for the user to confirm; the readback says it is right.

## 2026-10-05 — A2: Mesa version settled on the newest release (26.2.4)

**Decision (user).** Use the newest Mesa. The user has seen endianness
problems with this GPU on big-endian PowerPC Linux "until recently", so the
fixes are in current releases.

**What happened.** I had started on Mesa 21.3.9, chosen for an easier
build (the old C shader backend, fewer dependencies), without doing the
old-versus-current comparison the plan called for. It built on the host
after two small fixes. Nothing version-specific had been written on top of
it, so it was dropped.

**Observed with 26.2.4.** Configures and builds on the host unmodified with
`-Dgallium-drivers=r600,softpipe` and no window system (meson 1.12, GCC 16,
mako and pyyaml in a private Python environment under `third_party/`).
Differences that matter here: the off-screen (OSMesa) frontend no longer
exists, so the project needs its own small frontend; the surface layout
code from libdrm is now inside Mesa's radeon winsys; `r600` needs C++17.

**Concluded.** Mesa 26.2.4 is the base. The C++17 requirement lands on the
Tiger cross-compile in A3.

## 2026-10-05 — A2 (x86): Mesa 26.2.4's r600 renders on the card through our code

**Tried.** `mesa/winsys/`: a winsys for r600 over a small device interface
(`rdn_device.h`): buffers are ranges of video memory with fixed GPU
addresses, a command stream is copied into video memory and run as one
indirect buffer, fences are the hardware library's. The driver is told it
has GPU virtual addressing, a mode Mesa already has (`RADEON_VA`), so it
writes buffer addresses into the stream itself and nothing is patched or
parsed. Mesa's own surface layout sources are built unchanged over two
stand-in headers. `mesa/target/`: the device for Linux, the hardware
library in the same process. `mesa/frontend/`: Mesa 25.0's OSMesa
frontend, which current Mesa dropped, adapted. `hw/rdn_mem.c`: the video
memory allocator. `scripts/build-mesa.sh` builds `librdngl.so`;
`mesa/tests/rdn_gltest.c` draws with fixed-function GL.

**Observed.**
- Mesa needed one change in r600: it called libdrm's `drmGetVersion()` on
  the winsys's file descriptor; it now takes the version from the winsys.
  Plus the build hooks. Both are `mesa/patches/0001-osx-gpu.patch`.
- `GL_RENDERER: AMD TURKS`, `GL_VERSION: 4.6 (Compatibility Profile) Mesa
  26.2.4`, no GL error.
- The picture, read back from the off-screen buffer and, after copying 90
  frames to the scanout surface, from the framebuffer (`rdn_tool grab`):
  grey background, a smoothly shaded red-green-blue triangle in front of a
  yellow one drawn after it (so the depth test works), and a magenta and
  white checkerboard texture on a square, all rotated as asked.
- 90 frames in 7 s including start-up, with each frame read back and copied
  to the screen by the CPU.

**Concluded.** The whole path works on little-endian: GL state tracker,
shader compilation, r600, our winsys, our command processor code, the card.
Not yet done for A2: the same on big-endian under `qemu-ppc`. What the
monitor showed is for the user to confirm; the framebuffer readback is
right.

## 2026-10-05 — Host reset by an uncorrected hardware error during the big-endian tests

**What happened.** Around 00:50 the host reset without any kernel message.
The previous boot's log ends at 00:49:19 in the middle of normal activity;
the next boot starts at 00:52:37 and reports `x86/amd: Previous system
reset reason [0x08000800]: an uncorrected error caused a data fabric sync
flood event`. Files written in the last seconds (freshly linked
`rdn_tool`, Mesa objects) were left empty. The repository itself is intact
(`git fsck` clean).

**What was running.** Userspace tests against the card with no kernel
driver bound: at 00:49:07 the x86 Mesa test, at 00:49:08 the big-endian
Mesa test under `qemu-ppc` (which returned in a second, reported no error
and read back the previous run's picture, i.e. drew nothing). After that,
not in the surviving log: a rebuild, `rdn_tool modeset`, and the first run
of the drawing self-test with a byte-swapped indirect buffer (`-S`).

**Not known.** Which of those caused it, and how. Candidates: the card
raising a fatal PCIe error after the command processor was fed something
it could not digest (the big-endian Mesa stream, or the swapped indirect
buffer); a modeset on a GPU already in a bad state. Nothing was running on
the card at the time of earlier, harmless hangs, so there is no comparison.

**State afterwards.** Card un-POSTed and driverless, as after any reboot
(the audio function is back on `snd_hda_intel`). Tiger guest not running.
No hardware test has been run since; the user decides how to go on.

**Also learned.** In the big-endian Mesa run the readback matched the
previous x86 run pixel for pixel, including a background colour the
big-endian run was told to change: on big-endian nothing is drawn yet.

## 2026-10-05 — A2 (big-endian): the PowerPC build renders the same picture as x86

**Decision (user).** After the host reset: mask PCIe error escalation for
the card at runtime and continue. `scripts/card-quiet.sh apply` turns off
the card's error reporting enables and AER masks and the root port's SERR
forwarding; nothing persistent. The server had come back by itself.

**Tried, one test at a time, syncing before each.**
1. The drawing self-test on x86 with the indirect buffer stored big-endian
   and the swap flag in its address, the ring unchanged: the command
   processor hangs (`GRBM_STATUS 0xA0003828`, fence not reached). Starting
   the accelerator again recovers it.
2. Ring swapped (`BUF_SWAP_32BIT`, big-endian ring words), indirect buffer
   little-endian without the flag: ring test passes, the buffer hangs it.
3. Both swapped, which is what Linux does on big-endian: works.
4. After making one setting govern both (`rdn_accel.swapped`,
   `RDN_BIG_ENDIAN` by default, `rdn_tool -R` for the other order): the
   self-test draws correctly in both orders on x86 and in both orders in
   the big-endian build under `qemu-ppc`.
5. Mesa big-endian under `qemu-ppc -cpu 7447a`: now draws, but only
   clears; no geometry, and a scissored clear came out black.
6. Cause in Mesa: `r600_set_constant_buffer()` byte-swaps constants only
   when they arrive as a user buffer, and r600 asks the state tracker for
   real buffers (`prefer_real_buffer_in_constbuf0`). With that request
   turned off on big-endian, geometry and colours are right.

**Observed at the end.** The same scene (`-a 75 -b 402000`) rendered by
the x86 build and by the big-endian build under `qemu-ppc`, both on the
card: all 262144 pixels identical, and `glReadPixels` agrees at six probe
points. The big-endian build reports GL 3.2 where x86 reports 4.6. No host
reset during any of this; whether the masking or the absence of the bad
mix is why, is not known.

**Concluded.**
- The swap flag of an indirect buffer and the ring's swap setting must
  agree; a mismatch hangs the command processor. The earlier big-endian
  Mesa run was such a mismatch, and is the likeliest trigger of the host
  reset, not proven.
- Mesa 26.2.4's r600 needs one fix for big-endian fixed-function drawing
  (constants). It is in `mesa/patches/0001-osx-gpu.patch`; whether it is
  also wrong on big-endian Linux was not checked, and it is worth
  reporting upstream once confirmed there.
- The frontend's buffer format `OSMESA_BGRA` is a 32-bit ARGB word in host
  byte order, which is what Mac OS X uses for its surfaces.
- Under `qemu-ppc` the test needs `-cpu 7447a` and a static link that
  names three pthread functions Mesa declares weak.
A2 is complete by readback. Nothing on the monitor has been confirmed by
the user since phase 1.

## 2026-10-05 — A3 (part): Mesa's OpenGL runs in Tiger on the card

**Tried.**
- Cross toolchain for Tiger with C++17: the published container image of
  GCC 14.2 for `powerpc-apple-darwin8` with the 10.4u SDK, plus meson and
  Mesa's Python modules (`scripts/darwin-toolchain/Dockerfile`,
  `scripts/darwin.sh`). The compiler runs in the container, without
  network, seeing only the repository.
- `scripts/build-mesa.sh darwin`: Mesa 26.2.4 for Tiger. What Tiger's C
  library lacks is in `mesa/darwin8/` (`clock_gettime`, `posix_memalign`,
  `strnlen`, `strndup`, `getline`, `open_memstream`, `pthread_setname_np`,
  `sysconf(_SC_PHYS_PAGES)`, `static_assert`, and newer names for old
  things), reaching Mesa through wrapper headers and a static library. No
  further change to Mesa's sources.
- Kext: `RadeonNIAccel` starts the 3D engine on the framebuffer's card
  (microcode injected into the personality like the VBIOS), can draw the
  self-test at start, and serves `RadeonNIUserClient` (`hw/rdn_user.h`):
  map the aperture, allocate and free video memory, submit commands, wait
  for fences. Allocations are freed when the client goes.
- `tools/guest/rdnuc.c` for the user client; `mesa/target/rdn_device_darwin.c`
  as Mesa's device.

**Observed.**
- A C++17 program with threads and thread-local storage, built in the
  container, runs on 10.4.11 and depends only on libSystem.
- Meson needed three accommodations: the linker has to print a
  `PROJECT:ld64` line, an Objective-C compiler has to be named, and the
  compat declarations must not be force-included for C (they clash with
  meson's own function probes).
- The linked test program exceeded what a PowerPC branch reaches (16 MB).
  Built for size, with `-mlongcall` and dead-code stripping, its text is
  15 MB and it links. GCC's libatomic has to be linked statically.
- Kext log in the guest: "ring test succeeded", "3D engine up; 222 MB of
  video memory for clients", "drawing self-test on 1366x768 returned 0".
  `rdnuc probe` reads all eight self-test pixels right from user space, in
  the screen's native byte order; `rdnuc alloc` passes.
- `rdn_gltest` in Tiger: `GL_RENDERER: AMD TURKS (DRM 2.51.0 / 8.11.0)`,
  GL 3.2, no error, in 1.4 s. Its picture is identical, all 262144 pixels,
  to the x86 Linux run of the same scene. Sixty animated frames copied to
  the screen took 17 s; the screen grabbed from video memory
  (`build/guest-screen.png`) shows the test pattern, the kext's self-test
  shapes and the GL scene on top.
- No host reset. Error escalation was masked (`card-quiet.sh`) throughout,
  re-applied after QEMU reset the card at guest start.

**Concluded.** Every layer below Apple's OpenGL now works in Tiger on the
real card: kext engine, user client, Mesa's r600 built for Tiger. What A3
still needs is the top: the `gld*` bundle handing CGL contexts to Mesa
instead of to Apple's software renderer. The user has not yet looked at
the monitor for any of phase 2.

## 2026-10-05 — A3: a CGL program in Tiger renders through Mesa on the card

**Tried.** The driver bundle built on the host with Mesa inside
(`mesa/target/meson.build`, `scripts/gld.sh install-mesa`): the `gld*`
pass-through to Apple's software renderer stays, and at `gldInitDispatch`
the application's GL table is filled with generated entry points
(`gld/gen_dispatch.py`, from the SDK's `gliDispatch.h` at build time) that
make the context's Mesa context current and call Mesa. Off-screen
drawables only: Mesa renders on the card and copies into the buffer CGL
was given, on flush.

**Observed.**
- 621 of the table's entries get a Mesa function.
- First run: the picture was right, then a crash in `CGLDestroyContext`.
  The running system's table is 684 entries, the SDK header's 686; the last
  two writes landed on the CGL context's private fields. With the glue
  limited to 684 the program exits cleanly.
- `tools/guest/glprobe.c`, an ordinary CGL program linked against
  OpenGL.framework, unmodified: `GL_VENDOR: Mesa`, `GL_RENDERER: AMD TURKS
  (DRM 2.51.0 / 8.11.0)`, `GL_VERSION: 3.2 (Compatibility Profile) Mesa
  26.2.4`; cleared to green, `glReadPixels` gives 0 255 0 255, and the
  off-screen buffer itself holds `ff00ff00` ARGB words.

**Concluded.** The route planned for phase 2 works end to end for an
off-screen context: Apple's OpenGL framework, our bundle, Mesa's r600, the
kext, the card. Open: windowed and full-screen drawables (A3's criterion
and A4), the 63 Apple-only entry points, Apple-specific extension names
that programs look for, more than one thread, and whether the engine ever
puts its own entries back.

## 2026-10-05 — A4 (first form): a windowed OpenGL program in Tiger on Mesa

**Tried.** Experiments with the guest-built logging bundle, each a loop
over single bits or a counter: which bits of the renderer info and pixel
format records CGL reads as which capability; why a request for an
accelerated renderer was refused; where the renderer ID has to match; what
a window drawable's record holds; which entries of the driver's internal
table the engine calls per frame (`tools/guest/glwin.c` with drawing steps
left out one at a time). Then the Mesa-backed bundle extended to windows.

**Observed.** Details in GLD-INTERFACE.md. In short: the ID must agree in
`gldGetVersion`, renderer info and pixel format; bit 8 of word 2 means
accelerated in both records; a window's record carries the address of the
buffer the software renderer presents; entry 24 of the driver table
presents.

With the bundle claiming its own accelerated renderer, taking over the
dispatch table for windows as for off-screen contexts, and finishing
Mesa's frame into the window's buffer just before entry 24 runs:
`glwin`, an unmodified GLUT program, reports `GL_RENDERER: AMD TURKS`, GL
3.2, and its window on the Tiger desktop shows the white frame and the
shaded triangle turning (`build/shot-glwin-mesa.png`, a screenshot of the
emulated display). 141 frames in 10 s under emulation, each read back
from the card and copied by the CPU.

**Not right yet.** `malloc: Deallocation of a pointer not malloced` once
per run. The row length of a window's buffer is assumed. The copy per
frame; the proper path is a surface the kext presents. One Mesa context
current per process. Nothing on the 7570's own monitor confirmed by the
user.

## 2026-10-05 — Apple's Chess renders through Mesa on the card

**Tried.** `open -a Chess` in the guest with the Mesa-backed bundle
installed: the first real application, using NSOpenGL in a window.

**Observed.**
- First attempt: Chess drew a frame and crashed in the copy into its
  window's buffer (`osmesa_read_buffer` → `memcpy`). The row length of a
  window's buffer is not its width: word 27 of the drawable record holds
  the row length in pixels (the width rounded up to 16) and the bytes per
  pixel. The engine also changes the record in place when the window is
  resized, so the bundle now reads it before every use instead of copying
  it at attach time.
- After that: Chess stays up and shows its board, textured and lit, with
  all pieces and their reflections (`build/shot-chess-mesa.png`, a
  screenshot of the emulated display; the rendering is done by the 7570).
  `glwin` at 333x251 and `glprobe` still pass.

**Concluded.** An unmodified Apple OpenGL application runs on the driver.
Still open from the previous entry: the stray `malloc` warning, the copy
per frame, threads, and the user's own look at the 7570's monitor.

## 2026-10-05 — Stray free fixed; full screen looked at; a window left on the 7570

- The `malloc` warning: a pixel format answer is a chain of records linked
  through word 0. Only the first carried our renderer ID, so CGL gave the
  second to Apple's float renderer to destroy, which freed the middle of
  our block. Every record now carries the ID; the warning is gone.
- Full screen (A3's criterion as first written): the software renderer
  returns no format when `kCGLPFAFullScreen` (54) is in the attribute
  list, which CGL does pass on. Flag bits alone do not help. It needs the
  bundle to answer that request and to handle the attach itself; not done.
  Windows work, which matters more.
- For the user's check: `glwin` left running for twelve hours with its
  window at (200,200) on the 7570's own screen, 1920x1080. A grab of that
  screen from video memory shows the Tiger desktop and the window with
  Mesa's picture in it (the triangle is smeared in the grab only, because
  reading the screen back takes longer than a frame).

## 2026-10-05 — Windowed OpenGL confirmed on the monitor by the user

- The user first saw a screensaver on the 7570. Screensaver and display
  sleep are now off in the guest (`defaults -currentHost write
  com.apple.screensaver idleTime -int 0`, `pmset -a displaysleep 0 sleep
  0`); the guest was restarted and `glwin` started again on that screen.
- The user then reported: the triangle turning steadily, the white frame,
  the dark blue background. That confirms A4 in its first form on the
  monitor.
- Asked whether this is really hardware accelerated. Measured with
  `tools/guest/fences.c`: the card completed 304 command buffers in 5 s
  while `glwin` ran, 0 while it was stopped (`killall -STOP`), 300 after it
  resumed. The drawing is the GPU's; putting the frame into the window is
  a read back and a CPU copy, and the window server composites in software.
- Tagged `working-cpu-copy`. The user chose trial and error in the guest
  for Quartz Extreme over capturing the window server on the real G5.

## 2026-10-05 — Quartz Extreme by trial and error: the window server's gates

**Tried**, each a guest restart or a window server restart: `AccelCaps` on
the accelerator; the whole aperture as VRAM; the stock configuration's AGP
requirement changed to PCI (then restored); the window server's own check
functions called from `gdb` in a test process and in the window server;
registry shim objects to satisfy the AGP checks; a pixel format record made
by the bundle; `CGXGLAccelForDisplayDevice` called from the debugger.

**Observed.** All in `docs/QUARTZ-EXTREME.md`. Highlights:
- The window server never contacted kext or bundle until asked by hand.
- Three guest kernel panics while building the shim (host unaffected):
  `IOPCIDevice::attach` and a registered PCI nub's generic calls
  dereference a missing bridge. Panic text is readable with
  `TIGER_BOOTARGS=debug=0x100`.
- Apple's software renderer returns no pixel format inside the window
  server, whatever is asked.
- With the gates met, the window server creates its context on our
  renderer and `qe` reports "Quartz Extreme in use" for the 7570's display,
  but nothing is composited with it, and after a restart it does not try.
- The reason it does not try: a display only gets the flags the update
  path needs if `IOPSAllocateBlitEngine` succeeds, that is, if the
  framebuffer has a 2D accelerator (GA) plug-in.

**Concluded.** Next is a GA plug-in for the framebuffer (public interface,
`IOGraphicsInterface.h`; VMsvga2 has an MIT one to learn from). After
that, whatever the window server asks of its context and of the
`IOAccelSurface` user client.

## 2026-10-05 — Quartz Extreme composites the desktop through Mesa on the card

**Tried.** A 2D accelerator (GA) plug-in (`ga/`), then a surface user
client that only logs (`RadeonNISurface.cpp`), then everything the window
server asked of the bundle, one obstacle at a time.

**Observed.**
- With the GA plug-in the window server tries by itself: surface client,
  `setIDMode(1, 0x24)`, `setShape` with the changed region, context,
  `gldAttachDrawable(type 0x50)`.
- Forwarding that attach to Apple's software renderer hangs the window
  server for good: backtrace `gldAttachDrawable` -> `glsAssignDrawable` ->
  `CGSGetSurfaceBounds` -> `CGSNewConnection` -> `mach_msg`, a connection
  to itself. Guest restarted.
- Attaching the screen ourselves instead: the window server keeps
  running, `qe` says "in use", the GPU runs command buffers, and the
  screen (read back with `rdnuc grab`) shows windows with shadows, but
  upside down, then (rows the other way) in the bottom left corner
  whatever their position, then in the right place with rubbish around
  them.
- Each of those had one cause: Mesa's off-screen front end stores the
  bottom row first; the drawable is not the screen but the surface's
  region, whose origin is GL's origin; the region is not a box and the
  window server leaves the rest of the box undrawn.
- I took the first grab's desktop picture for the right way up and the
  later ones for flipped. It was the other way round: the picture's dark
  part belongs at the bottom, and the window's backing store read in the
  debugger has the same rows as the screen now shows.
- A window moved in eight steps: about 190 command buffers in 5 s, no
  trails. Exposé dims the desktop. A windowed GL program (A4's) still
  runs next to it, 45 frames in 5 s on the emulated display.

**Concluded.** A5 works by readback; the user has not looked yet. What the
window server does is in `docs/QUARTZ-EXTREME.md`. Next: draw on the
screen's surface directly instead of copying, and see what the cursor
does.

## 2026-10-05 — Quartz Extreme without the copy: Mesa draws on the screen's surface

Host rebooted by the user in between (unrelated); card bound to `vfio-pci`
and error masking applied again before the guest started.

**Tried.** The window server's context bound to the screen's surface
itself (imported by the winsys as a linear render target at the scanout's
offset and pitch), with window coordinates shifted to the corner of the
surface's region.

**Observed.** Worked on the first run. `rdnuc grab` shows the same
pictures as with the copy: desktop, window with shadow at 100,78, eight
moves without trails (304 command buffers in 5 s, 8 AppleScript moves in
5 s), Exposé dimming and back. The window server's CPU time after all of
that: 4.3 s, against 19 s at the same point with the copy.

**Concluded.** r600 renders to the linear scanout surface as it is; no
change to Mesa was needed beyond the winsys import. The copying path
stays behind `/tmp/rdngld.copy`. The cursor is the open question, and it
needs the user's eyes.

## 2026-10-05 — User on the monitor: Quartz Extreme works, Exposé flickered; fixed by copying on flush

**Observed by the user.** Desktop and windows fine, a Finder window
dragged from the emulated display onto the 7570's shows no corruption,
Exposé works but flickers (recording, 60 fps). The cursor could not be
tested: VNC only reaches the emulated display.

**Analysis.** Frames of the recording show a window at its old and its new
place at once: updates were visible while being built, because Mesa drew
on the scanout surface.

**Tried.** Mesa draws on its own screen-sized surface; at `glFlush` the
GPU copies the region's rectangles to the scanout
(`resource_copy_region`).

**Observed.** Readback correct after eight window moves (337 command
buffers in 5 s). Not yet seen by the user.

## 2026-10-05 — Quartz Extreme confirmed on the monitor by the user

**Observed by the user**, with the GPU copying each finished update to the
screen: Exposé "works great now", no flicker in it; it looks a bit slow.
The cursor, moved across the 7570's display, leaves no marks but seems to
flicker a little (the user is not sure it is not the display).

**Concluded.** A5's criterion is met. Open after it: speed, and the
cursor, which `IOFramebuffer` draws with the CPU into the surface the GPU
copies over; a hardware cursor is the fix to try.

## 2026-10-05 — Hardware cursor

**Tried.** `hw/rdn_cursor.c` (after Linux's `radeon_cursor.c`: 64x64 ARGB
overlay of CRTC 0, picture at 31 MB in video memory) and, in the kext,
`getAttribute(kIOHardwareCursorAttribute)`, `setCursorImage` (through
`convertCursorImage` with a 32-bit descriptor) and `setCursorState`,
behind `RDN_HWCURSOR=1`.

**Observed.** `IOFramebuffer` takes it: the first picture converts to
32x32 and the calls succeed; Quartz Extreme still comes up. The overlay
is not in the scanout surface, so it cannot be read back.

**Not known until the user looks.** Whether the picture is right: I
assumed the converted pixels are packed at the cursor's width and that
their colours are already multiplied by alpha.

## 2026-10-05 — Hardware cursor confirmed on the monitor by the user

`tools/guest/curmove.c` moved the pointer over the 7570's display
(mouse-moved events, 60 a second). **The user:** the arrow looks fine, it
moves with no corruption and leaves nothing behind; some blur they put
down to the TV. So both assumptions held: `convertCursorImage` with a
32-bit descriptor gives rows packed at the cursor's width, colours
already multiplied by alpha.

## 2026-10-05 — Chess with Quartz Extreme on: drawn by Apple's software renderer, not by the card

**Tried.** `open /Applications/Chess.app` with the Quartz Extreme set-up
loaded (`RDN_SURFACES=1` and the rest), as an existing OpenGL program to
test with.

**Observed.** Chess shows its board correctly (on the emulated display,
where its window opens; neither AppleScript nor System Events could move
it). But the GPU ran no command buffers for it and the bundle's log has
no "entries of table ... are Mesa's" line: `rdn_mesa_dispatch` refused,
because the record passed to `gldAttachDrawable(type 0x50)` is not the one
`read_record` knows. It begins `00007d53 00000027 02ca5de0 00000002
00000003 00000002`: connection, window and a third word, the same shape
as the window server's own record (`0, 0, surface ID`), with no size or
base address in the places they were before. Read again later with the
debugger it is unchanged. So every GL call stayed with Apple's software
renderer.

Before the Quartz Extreme set-up the same Chess rendered through Mesa
(entry above); `glwin` did so even with Quartz Extreme on, earlier today.

**Concluded.** With surfaces on offer, at least some applications' window
drawables are described by connection, window and surface instead of by a
buffer, and the bundle does not handle that: such programs work, but in
software. This is the unfinished half of A4 (windows as surfaces of their
own). Not known: what decides between the two kinds of record (`glwin`
is GLUT, Chess is Cocoa and asks for a multisampled format).

## 2026-10-05 — The 7570 as main display, then as the only display

Asked for by the user, who settled "emulated VGA stays primary" earlier
and wanted both tried.

**Main display.** `cgmode main 1` (new: `CGConfigureDisplayOrigin` puts
the display at the origin, kept across logins). Menu bar, Dock and
desktop icons moved to the 7570's display and are composited by Quartz
Extreme (readback).

**Only display.** `TIGER_NOVGA=1 scripts/tiger.sh passthru ...` adds
`-vga none`. OpenBIOS complains on the serial log (`NULL ihandle`,
`slw_update_keymap` failing) and boots anyway; Tiger reaches ssh in the
usual time with WindowServer and loginwindow running and no screen.
`scripts/guest-cycle.sh ready` cannot be used (it waits for the Finder,
which only starts once there is a display); wait for ssh instead. After
`scripts/kext.sh up` the card is display 0, main, 1366x768 (the start-up
mode), Quartz Extreme in use, Finder and Dock up; `cgmode set 0 1920 1080
32` switches it. Readback shows the complete desktop.

**Costs seen.** No picture on VNC and no emulated screen to read a panic
from. A Dock process of the display-less session stayed behind next to
the new one.



## 2026-10-05 — Chess: three causes found, two fixed, not yet re-tested as a whole

1. Chess asks for a multisampled format; Apple's side answers with a
   buffer twice the window's size. The bundle now takes
   kCGLPFASampleBuffers/Samples out of the request.
2. Chess loads textures and builds display lists before its window
   exists. OpenGL fills a new context's dispatch table itself when the
   context is complete, with no driver call, so those calls stayed with
   Apple's engine. `gld/rdn_hook.c` hooks CGLSetCurrentContext (symbol
   pointers of every image) and the bundle gives the table to Mesa then,
   bound to a placeholder until a drawable exists.
3. With that, the pieces were garbage. `tools/guest/listwin.c` and
   `rdn_gltest -L mode`: display lists with some vertex formats draw
   nothing, in Tiger only (x86 and big-endian Linux builds are right).
   Cause: GCC 14.2 for powerpc-apple-darwin8 miscompiles
   `src/mesa/vbo/vbo_save_api.c` at -Os. With that file at -O1 both tests
   pass (`scripts/build-mesa.sh` now does this). A whole build at -O1
   still failed the windowed test and its test program does not link
   (branch range); not understood.

Not done: Chess itself has not been run on the fixed build. Off-screen
contexts on our renderer read back nothing since the Quartz Extreme
changes (`glprobe draw 0x21a00`, `prelist`); the record is of another
form. The guest was started with the emulated display again.
\n

## 2026-10-05 — The display-list failure narrowed down; Chess correct on the card in one configuration

Asked by the user whether the "compiler bug" is certain. It was not.

**Narrowing.** All with `rdn_gltest -L 1/5/15` in the guest, the rest of
Mesa at -Os, one file (`src/mesa/vbo/vbo_save_api.c`) varied by pragma:
- as is: fail. `optimize("Os")` (control: the pragma alone): fail.
  With no-trapping-math and no-math-errno added: fail.
- `optimize("O1")`, `("O2")`, `("no-strict-aliasing")`: pass.
- no-strict-aliasing over one range of functions at a time: only
  `compile_vertex_list()` (lines 506 to 998 of that file) matters.
So: this compiler's strict-aliasing-based optimisation at -Os produces
wrong code for that one function. The x86 and big-endian Linux builds
compile the same function at -O2 with strict aliasing on and are right.
Whether the function breaks the aliasing rules (Mesa's fault) or GCC 14.2
for powerpc-apple-darwin8 is wrong has not been decided; I read the
function and found no obvious violation. Not looked at: the generated
code.

**Done about it.** `-fno-strict-aliasing` for the whole Tiger build
(`scripts/build-mesa.sh`; an existing build directory needs `meson
configure -Dc_args=... -Dcpp_args=...`, the cross file is only read at
the first set-up). With it both list tests pass in all seven modes. The
earlier "whole build at -O1 still fails" was never a clean observation
and is withdrawn.

**Chess.** Started from an ssh session with `RDN_GLD_EARLY=1`, it is
drawn completely by the card: textured board, all pieces, reflections
(readback). Two more things were needed:
- With Quartz Extreme compositing, a window's buffer holds its top row
  first; before, the bottom row. The bundle now asks
  `CGDisplayUsesOpenGLAcceleration`.
- Started normally (`open`), with the early takeover on, the record the
  software renderer is supposed to fill stays as it came in (`connection,
  window, surface, 2`, no buffer) and the window stays white; without the
  early takeover the record is filled, but then what Chess set up before
  its window existed is lost (untextured board, no pieces). So the early
  takeover is off unless `RDN_GLD_EARLY=1`, and a normally started Chess
  is still wrong. Why the record is not filled in that one combination is
  not known.

## 2026-10-05 — Application windows as surfaces: the protocol found, a GLUT window shown by the card

**Step 1 (which form of window record a launch gets).** Nine controlled
launches of Chess (warm, in the background, disk busy, from ssh, first
after a window server restart, with and without the early takeover): all
got the filled, buffer form. The unfilled form seen earlier could not be
reproduced. Dropped as a line of attack.

**Step 3 (how a hardware driver takes a window).** By symbol lists and by
reading `glcBindSurface` in OpenGL.framework:
- Apple's hardware GL bundles import no window server functions at all.
  The software renderer imports `CGSBindSurface`, `CGSGetSurfaceBounds`,
  `CGSLockWindowBits`, `CGSFlushSurface`.
- `gldInitializeLibrary`'s first argument is an array with one word per
  display (0x3207 here; a Mach port by its look), its fifth is
  `glcBindSurface(connection, window, surface, mode, display word)`,
  which calls `CGSBindSurface(connection, window, surface, 2, mode,
  display ID)`.
- The record of a window's `gldAttachDrawable` starts connection, window,
  surface; the surface ID comes from the same allocator as
  `IOAccelCreateAccelID`.
- Calling the callback with mode 0x24 makes the window server open a
  surface client in the kext, `setIDMode(surface ID, 0x24)`, and
  `setShape(0, ...)` with the window's GL area on the screen (3,44
  540x488 for Chess); it then leaves that area alone, and at times asks
  `readLock`/`readLockOptions(2)` and `control(1, ...)`, which the kext
  refuses.
- With no software renderer behind the context the engine's driver table
  is never called; a swap reaches the driver as `glSwapAPPLE` in the
  program's dispatch table.

**Built on it** (`/tmp/rdngld.surface` or `RDN_GLD_SURFACE=1`): the bundle
binds the surface itself, does not forward the attach, gives Mesa a
drawable of the surface's size (`CGSGetSurfaceBounds`) and, at
`glSwapAPPLE`, has the GPU copy it to where the kext says the window is
(`OSMesaMakeCurrentSurface`).

**Observed.** `glwin` (GLUT, animating) is shown that way: 4241 presents
in about ten seconds, readback shows its triangle in the window. Chess,
which draws one frame and waits, stays white: its swap arrives with the
right place and is bound, but the window server asks to read the surface
(`readLock`) and, refused, paints the window's white backing there.

**Also found.** The kext's table of surface shapes was never emptied
(eight entries); after a few launches new surfaces got no shape. Fixed
(forgotten on close, 32 entries).

**Next.** The kext must know where a surface's pixels are so that it can
answer `readLock`: the drawable has to be a linear buffer in video memory
that the bundle registers with the kext.

## 2026-10-05 — Chess, started normally, drawn and shown by the card

**What the window server does with a program's surface** (GL trace of the
window server, `/tmp/rdngld.trace`): in its own picture it draws an
untextured white rectangle over the surface's area (texturing off, colour
ffffffff, no blending) and flushes that to the screen with everything
else. Putting the surface there is left to the driver. It also read-locks
the surface now and then (`readLockOptions(2)`), and unlocks.

**Built.**
- The bundle keeps each surface's finished picture in a linear buffer in
  video memory (64-pixel row multiple) that it allocates and registers
  with the kext (`RDN_UC_SURFACE_BUFFER`); the frame is copied there and
  to the screen at every `glSwapAPPLE`.
- The kext answers read locks by mapping that part of the aperture,
  read-only, into the asking task, and lists the registered surfaces
  (`RDN_UC_SURFACE_LIST`).
- In the window server, after each `glFlush`, the bundle copies every
  listed surface's picture over its visible rectangles
  (`OSMesaShowStore`).
- The drawable's size comes from `CGSGetSurfaceBounds` at once; the kext
  learns the place a moment later, and the first swap waits for it (up to
  half a second).

**Observed (readback, 7570 as the only display).** `open
/Applications/Chess.app`: the board with its textures, all pieces and
reflections, in its window, right way up. It survives hiding and showing
the program. A Finder window moved over it covers it correctly. A second
GL window (`glwin`) runs beside it. Quitting leaves nothing behind; no
crash log, no panic.

**Wrong.** Where a window's shadow falls on a surface there is a white
band: the window server takes the shadow's area out of the surface's
shape and draws its white rectangle plus shadow there. Translucent things
over a surface need the surface inside the window server's own
compositing (as a texture), which this is not.

**Not seen by the user yet.** Still opt-in: `/tmp/rdngld.surface` in the
guest (or `RDN_GLD_SURFACE=1`).

## 2026-10-05 — Dragging a surface window: white, flicker and trails, and the fix

**Seen by the user** (and in their recording): Chess as a surface is
complete and the right way up; dragged, its window flickers, stays white
at some positions and leaves copies of the board behind.

**Cause.** The surface was laid over the window server's finished frame
after its flush, at the shape the kext had been told. While the window
server drags a window that shape is one step behind its drawing, so the
board went to the old place (trail) and the new place kept the window
server's white rectangle; and two copies to the screen per frame flicker.
A window that moves itself (`tools/guest/surfmove.c`) does not show it:
there the shape arrives first.

**Fix.** The window server's placeholder is recognised as it is drawn: a
quad with texturing off and white, whose texture coordinates are the part
of the surface it stands for. The bundle watches those calls in the
window server (`gen_dispatch.py`, `WATCHED`) and copies that part of the
surface's picture over the quad in the window server's own drawing buffer
at that moment (`OSMesaDrawStore`). What the window server draws
afterwards (windows in front, shadows, the Dock, translucent panels) lands
on top, and it all reaches the screen in the window server's one flush. A
swap now stores the picture and calls `CGSFlushSurface(connection,
window, surface, NULL)`, which has the window server draw the area again;
writing to the screen from the program is kept only behind
`RDN_GLD_DIRECT_SWAP=1`.

**Observed (readback).** Chess dragged slowly and fast with
`tools/guest/drag.c` (mouse events posted inside the guest; QMP's
absolute positions land in the wrong place without the emulated display):
right at a held mid-drag position and after release, no white, no copies
left behind. The speech panel and the Dock are blended over the board;
the white box and the white shadow band of the earlier entry are gone.

**Not measured.** Every frame of a program now goes through the window
server, which is slower than writing to the screen.

## 2026-10-05 — Two surface windows: the wrong picture in one; how the window server means it to work

**Seen by the user.** Dragging and making moves in Chess work, no flicker
or trails. While `glwin` ran next to Chess, its window showed a part of
the Chess board.

**Cause.** A placeholder quad says which part of a surface it stands for,
not which surface. The bundle took the first registered surface the quad
fits in. Now: of those it fits, the one whose corner is nearest to where
that surface was drawn last or to where the kext has it. Chess and
`glwin` together are both right (readback).

**How the window server means it to work** (asked by the user; from
`CGXNextSurface` in CoreGraphics, read to understand the interface):
1. It read-locks the surface and looks at the answer
   (`IOAccelSurfaceInformation`).
2. No address in the answer: the overlay case. It looks at the flags
   (bit 1, key colour), the pixel format (1 to 4) and `typeDependent[0]`
   and paints a colour for hardware to show the surface through.
3. An address, and the surface's accelerator is the display's own:
   `CGXGLCreateSurfaceTextureReference`, which sets a private parameter
   on its GL context, `cglsSetInteger(ctx, 997, eight words)` with
   `GL_TEXTURE_RECTANGLE` among them (the public sibling is
   `kCGLCPSurfaceTexture`, 228: surface ID, target, internal format),
   checks for a GL error, and then draws the surface as an ordinary
   textured quad. The driver is told which surface a texture is; nothing
   is guessed.
4. An address, another accelerator: `CGXGLCreateSurfaceTexture`, a copy.
With us it ends up drawing the quad untextured and white. Which step
fails is not known: no `gldSetInteger` reaches the bundle in the window
server, and two attempts to watch `cglsSetInteger` in the debugger were
inconclusive (the first left the window server stopped for some seconds).
A suspect: the check for a GL error right after, since Mesa leaves
`GL_INVALID_ENUM` behind for Apple's own enums (`/var/log/windowserver.log`
is full of "GL error 0500"; that log exists and is worth reading).

**So.** Matching by position is a stopgap. The real fix is to make step 3
work, so that the window server names the surface.

## 2026-10-05 — Apple-only parameters accepted; the GL errors were not why surfaces are drawn white

**Tried.** The bundle now accepts and drops three Apple-only parameters
before Mesa sees them (`gen_dispatch.py`, `APPLE_ONLY`):
`GL_UNPACK_CLIENT_STORAGE_APPLE` (0x85B2, `glPixelStore`),
`GL_TEXTURE_STORAGE_HINT_APPLE` (0x85BC, `glTexParameter`) and
`GL_TRANSFORM_HINT_APPLE` (0x85B1, `glHint`). Standard constants need
nothing: their values are the same everywhere.

**Observed.** `/var/log/windowserver.log`: no "GL error 0500" line after
the window server's restart, where there had been over a thousand; so
those three were all of them. But the window server still draws a
program's surface as an untextured white quad, and still no
`gldSetInteger` reaches the bundle.

**Concluded.** The suspect of the previous entry is cleared: a left-over
GL error is not what keeps the window server from binding the surface as
a texture. Why it does not is still open; the placeholder detection
stays.

## 2026-10-05 — Why the window server drew surfaces white, and surfaces as its textures

**Found in the debugger** (breakpoints inside `CGXNextSurface`, the window
server stopped only for the attach): the read lock succeeds, the surface's
accelerator is the display's, `CGXGLCreateSurfaceTextureReference` is
called and returns nothing. That routine starts by switching on the read
lock's `pixelFormat`: 3, 4, 5, 6 and two YUV codes, anything else fails.
The numbers are the surface colour depth codes of
`IOAccelSurfaceConnect.h` (4: `kIOAccelSurfaceModeColorDepth8888`). The
kext returned 32. The white quad is what the window server draws for a
surface it could not make a texture for.

**With 4:** the window server makes a rectangle texture (`glGenTextures`,
`glBindTexture`, two filters, anisotropy), sets its private context
parameter 997, and draws the surface as a textured quad. The parameter
never reaches the bundle: no `gldSetInteger`, no driver table entry;
Apple's engine keeps it. The texture has no image for Mesa and shows
white.

**Built.** The kext remembers which surface is read-locked
(`RDN_UC_SURFACE_LOCKED`). In the window server the bundle watches for a
rectangle texture whose anisotropy is set before it has an image while a
surface is locked, and makes that surface's buffer in video memory the
texture's image (`OSMesaTexStore`, Mesa's `st_context_teximage`; no
copy, no alpha). The window server then names the surface itself, by
making the texture while it holds the lock; nothing is matched by size or
place.

**Observed (readback).** Chess started normally: complete, and the log
says its surface became a texture. Chess and `glwin` together, Chess
dragged: each window has its own picture; two textures bound; the
placeholder path was not used once; `/var/log/windowserver.log` has no GL
error.

**Left in.** The placeholder detection, as a fallback for a surface that
gets no texture. **Not tried:** a surface window that changes size (its
buffer moves; the texture would have to follow).

## 2026-10-05 — A real keyboard and pointer for the guest: evdev, not USB passthrough

**Tried (by the user).** A Logitech Unifying receiver (`046d:c52b`, K400
Plus) passed to the guest with `usb-host`.

**Observed.** The guest logged `AppleUSBOHCI: Found a transaction which
hasn't moved in 5 seconds on bus 13, timing out!` every 13 s, 40 times,
from 41 s after the receiver appeared on the host until 2 s before it was
unplugged.

**Likely cause (from the source, not captured).** `mac99` has only an OHCI
controller, and QEMU's OHCI allows one pending asynchronous packet for the
whole controller (`hw/usb/hcd-ohci.c`, "We only allow one active packet
per controller"). `usb-host` submits interrupt-IN polls without a timeout,
so an idle device's poll holds the one slot and every other transfer
waits. The emulated devices answer "no data" at once and never do this.
`-trace usb_ohci_td_too_many_pending` would confirm it.

**Tried.** `input-linux` instead: QEMU reads the host's event device and
feeds its emulated USB keyboard and mouse. Added to the running guest with
`object-add` over QMP.

**Observed (user).** Keys and pointer work. Every click threw the pointer
into the top left corner: QEMU sends button events to the first pointer
device in its list, which was the absolute tablet, and the tablet reports
the click at its own position, (0,0). `mouse_set` to the relative mouse
fixed it (confirmed by the user). The guest's HID drivers put the tablet
first again when they start, so this is done after boot.

**Changed.** `TIGER_EVDEV=<node>` for `scripts/tiger.sh` (the device from
boot), `scripts/tiger.sh evdev [node|off]` (attach to the running guest
and make the relative mouse the one for buttons; `guest-cycle.sh ready`
runs it when `TIGER_EVDEV` is set). The subcommand was run on the live
guest, twice. Starting a guest with `TIGER_EVDEV` set has not been run.

**Not tried.** `usb-host` on an added UHCI controller (Tiger has
`AppleUSBUHCI.kext`); scripted clicks from `guest-ctl.py` with the relative
mouse first.

**Seen by the user afterwards (surfaces as textures):** resizing the Chess
window stutters a little, with nothing wrong in the picture. So the
window server's texture does follow a surface whose buffer moves; how has
not been looked at.

## 2026-10-05 — Full-screen OpenGL: Sauerbraten on the card

**Asked by the user**, who copied Sauerbraten (PowerPC, SDL 1.2) into the
guest and found it "a bit wonky" in a window.

**Observed first.** Full screen it stopped with "Failed creating OpenGL
pixel format": its attribute list is `54 (full screen), 8: 32, 12: 16, 5,
84: 1, 4`, the software renderer has no format for that, and the record
the bundle then made itself was the window server's (no depth, no
full-screen flag).

**Built** (with `/tmp/rdngld.surface`):
- For a program, the bundle's own pixel format record is the software
  renderer's window record (words 3, 5 and 8 as it has them, depth mode
  0x1000, stencil mode 0x80) with the full-screen flag (bit 1 of word 2)
  added; the renderer info has that flag too.
- `gldAttachDrawable(ctx, 0x36, record)`: the bundle attaches the whole
  screen itself, as for the window server's context; Mesa draws
  off-screen and each `glSwapAPPLE` copies the picture to the screen. The
  record is not used (its fifth word is 0x6eb, 1771; not understood).

**Observed (readback).** Sauerbraten starts full screen at 1366x768 and
shows a level: textured, lit walls, items, the crosshair, the status
numbers and text. Its own counter says 6 frames a second. Lines of its
console text overlap each other at the top left.

**Not known.** Why it is that slow (the emulated CPU, or the driver: a
`glFinish` and a full-screen copy per frame), what is wrong with the
console text, what "wonky" was in the window, and whether leaving full
screen gives the screen back cleanly.

## 2026-10-05 — More programs from the user: Sauerbraten's sky, Quake 3, Tux Racer

- **Sauerbraten, seen by the user full screen:** more or less right, but
  the sky is a hall of mirrors once the view moves (right in the first
  frame) and the weapon is not drawn. Reproduced by readback with
  `curmove` turning the view. The game clears only depth; in its first
  frame it draws the sky's polygons to depth alone (colour mask off) and
  then the sky box with `glDepthFunc(GL_GEQUAL)`; that pair appears once
  in a 40 s trace. What later frames do has not been read yet.
  `R600_DEBUG=nohyperz` changes nothing. Not compared with Apple's
  software renderer yet.
- Apple's table calls four functions by other names than Mesa
  (`enable_vertex_attrib_ARB` and so on); mapped in `gen_dispatch.py`
  (629 of 686 entries now). No visible change in Sauerbraten.
- I killed the user's running game with my own test runs. Ask before
  restarting a program the user may be in.
- **Quake 3 (Carbon, the user's copy on the guest's desktop):** full
  screen it stops with "Could not initialize OpenGL" after
  `gldGetRendererInfo`, before any pixel format request reaches the
  bundle. With `+set r_fullscreen 0` it gets a context and a surface,
  reports Mesa's strings and shows its CD key screen in its window
  (readback); the picture fills only the upper two thirds of the window.
- **Tux Racer:** the user says it hangs after "press any key". Not looked
  at.

## 2026-10-05 — Quake 3 full screen: it was the display's mode list

Under the debugger Quake 3 never reaches `CGLChoosePixelFormat` full
screen: it asks about the renderers, then fails in `GLimp_Init`. Its
default is 640x480, and the display offers only what the kext takes from
the EDID's detailed timings: 1366x768 and 1920x1080. Started with
`+set r_fullscreen 1 +set r_mode -1 +set r_customwidth 1366 +set
r_customheight 768` it goes full screen on the card (attributes 54, 84:
mask, 5, 8: 16, 12: 16 ...; the bundle's full-screen attach) and shows
its CD key screen complete; the user confirmed on the monitor. A small
test program shows the pixel format requests themselves were never the
problem.

**To do.** The kext should offer the usual resolutions (640x480, 800x600,
1024x768, 1280x720 ...), from the EDID's established and standard
timings or generated. Not known: whether Quake's key input works full
screen (it did not in a window, the user says).

## 2026-10-05 — Quake 3 in a map: floors and ceilings black; narrowed to client arrays with two texture units

**Seen by the user** full screen in a map: very dark, floors and ceilings
pure black, walls about right.

**Narrowed with Quake's own switches** (readback of q3dm1, and the user's
eyes): right with `r_lightmap 1` (lightmaps alone), `r_vertexLight 1`
(textures alone), `r_ext_multitexture 0` (two passes) and with
`r_primitives 3` (the same multitextured draws, vertex by vertex with
`glMultiTexCoord`). Wrong with the default: multitexturing from client
arrays (`glDrawElements`, `GL_UNSIGNED_INT`, separate coordinate arrays
with stride 0, `glLockArraysEXT`, the second unit's array left enabled).
Mipmapping and compiled vertex arrays make no difference.

**Not reproduced.** `tools/guest/mtex.c` draws that way in every
combination I could think of (two coordinate sets told apart by a
half-black texture, lock, stale enabled array, contents changed between
draws) and is right each time. So the cause is something else Quake does
in that path. Next: dump the arrays of one wrong draw from Quake itself.

**Workaround for the user:** `+set r_primitives 3`.


## 2026-10-05 — A disk image for the real G5, with the kext installed

The user wants to start on the real G5 by writing the guest's disk to a
hard disk.

**Checked first.** The guest's disk has an Apple Partition Map with one
HFS+ volume (`ER`/`PM` blocks read with `qemu-io`). The system is 10.4.11
(8S165) and has the Late 2005 G5's drivers:
`PowerMac11_2_PlatformPlugin` and `PowerMac11_2_ThermalProfile` inside
`AppleMacRISC4PE.kext`, `AppleK2SATA`, `AppleSMU`, the NVIDIA kexts for
the 6600 LE. So the image should boot there; that has not been tried.

**Done.** Snapshot `before-g5-image` of `images/tiger.qcow2` (guest off,
accelerated state, no kext installed). `qemu-img convert` of that state
into `images/tiger-g5.qcow2` (no snapshots inside). Booted the copy with
the card passed through, unpacked `build/RadeonNI-g5` (built with
`--with-vbios` from `working-quartz-extreme-25-g88b9000`) into
`~/RadeonNI-g5` and ran `sudo ./install.sh`. After a restart the kext
was loaded at boot by itself, posted the card and set 1366x768
(`dmesg`: "framebuffer started"); by the log only, nobody looked at the
monitor. The working disk never had the kext installed.

The installed kext is phase 1's: its personality has the VBIOS and no
`Accelerator` key, no microcode. The GL bundle and the GA plug-in are in
that image's `/System/Library/Extensions` and stay inert that way.

**Side effect.** The copy was booted twice under `mac99`, so its caches
are the emulated G4's; the G5 has to rebuild them on its first boot.

To write it: `sudo qemu-img convert -p -O raw images/tiger-g5.qcow2
/dev/sdX`.

## 2026-10-05 — The G5 runs with the card; an installer option for acceleration

**Reported by the user:** the G5 runs with the Radeon (the image with the
phase 1 kext). No log or details yet. The card is no longer in the host.

**Done.** `g5/install.sh --accel [--hwcursor]` puts the same keys into
the installed kext's personality that `scripts/kext.sh` adds under QEMU
for Quartz Extreme (`Accelerator`, `FW_PFP`, `FW_ME`, `AccelCaps` 3,
`AGPShim` 3, `Surfaces`, `GAPlugin`, optionally `HWCursor`);
`scripts/make-g5-package.sh --with-firmware` packs the two microcode
files. Checked in a guest without the card, started with `-snapshot` so
that nothing stays on its disk: the installer accepts the kext, and the
resulting `Info.plist` parses to exactly the personality `kext.sh`
generates with `RDN_ACCEL=1 RDN_ACCELCAPS=3 RDN_AGPSHIM=3 RDN_GA=1
RDN_SURFACES=1 RDN_HWCURSOR=1`.

**Not tested anywhere:** the accelerator starting at boot from an
installed kext (under QEMU it was always loaded after boot, followed by
a window server restart), and all of it on the G5.

## 2026-10-05 — First facts from the real G5 (phase 1 kext)

The G5 (`PowerMac11,2`, 192.168.1.128, ssh as in the guest: user `tiger`,
key `private/ssh/tiger_rsa`) runs the image with the installed phase 1
kext. Read over ssh:

- `dmesg`: device 1002:675d, command 0004 on entry, VBIOS from the
  personality, "card is not posted on entry", posted, CONFIG_MEMSIZE
  1024 MB, one mode 1920x1080 at 148500 kHz (another monitor than the
  host's), "framebuffer started".
- Device tree: the card is `pci1028,2b20` (named after its subsystem ID,
  so `ioreg -n pci1002,675d` finds nothing) in SLOT-2. Open Firmware
  assigned everything: I/O (0x20), BAR0 256 MB at 0x90000000, BAR2 128 KB
  at 0x80140000 and **the ROM (0x30) at 0x80120000, 128 KB**. So the ROM
  path of "Deferred to the real G5" can be tried.
- System Profiler: the 7570's display is the main one, 1920x1080, 32
  bit, Quartz Extreme not supported (as expected without the
  accelerator). Its listing showed no other display.

The package with `install.sh --accel` and the microcode is unpacked in
the G5's `~/RadeonNI-g5` (the earlier one moved to `~/RadeonNI-g5.old`);
not installed yet, the user runs it.

## 2026-10-05 — The G5 boots with the accelerator; Quartz Extreme in use there

The user ran `sudo ./install.sh --accel` on the G5 and restarted. Read
over ssh 45 s after boot: the installed personality has `Accelerator`,
the kext is loaded, and `~/gl/qe` says "display 0 (1366x768 at 0,0):
Quartz Extreme in use". So the accelerator starting at boot from an
installed kext works, and on the real machine. Not yet seen: what the
user sees on the monitor, the kernel log, any OpenGL program.

Host: `/etc/ssh/sshd_config.d/10-tiger.conf` (asked for by the user)
adds `diffie-hellman-group14-sha1` and `ssh-rsa` so that Tiger's OpenSSH
4.5 can connect to the host; negotiation from the G5 checked.

## 2026-10-05 — G5: Quake 3 "could not initialize OpenGL" was the missing surface switch; surfaces are the default now

**Seen by the user on the G5:** Quake 3 full screen fails again.
**Reproduced over ssh:** `CGLSetFullScreen -> 10005 (invalid drawable)`,
then its fallback to 640x480, which the display does not offer. `/tmp`
is emptied at boot, so `/tmp/rdngld.surface` was gone; with
`RDN_GLD_SURFACE=1` in the environment it went full screen at 1366x768
(the user saw it; I ended it with `killall`).

**Changed.** `app_surfaces()` in the bundle is on by default; off with
`RDN_GLD_NOSURFACE` or `/tmp/rdngld.nosurface`. Rebuilt
(`scripts/build-mesa.sh darwin`), copied into the G5's
`/System/Library/Extensions` (the G5's previous binary, identical to the
host's previous build, is `~/RadeonNIGLDriver.prev` there). Quake 3 then
goes full screen with no file and no variable. The QEMU guest's disk
still has the old bundle: `scripts/gld.sh install-mesa` when it runs
next.

## 2026-10-05 — G5: a new monitor refuses the mode; diagnostics added, not yet run

**Seen by the user on the G5:** a new monitor shows "the resolution you
entered isn't supported". They add that the earlier 1366x768 monitor
reported 1368x768 on its own display.

**Read over ssh** (the G5 is still 192.168.1.128; 192.168.1.158 is the
host): the boot at 18:50 read this monitor's EDID and found one detailed
timing, 1920x1080 at 148500 kHz (blanking 280/45, sync 88+44 and 4+5,
the CEA 1080p60 timing), and set it: PLL fb 88.0 ref 2 post 8, mode set
returned 0. So the mode is not hardcoded. What is fixed is the output:
`rdn_modeset.c` always drives UNIPHY link A single link, and chooses
HDMI or DVI signalling from the EDID's vendor block. The log does not
say which it chose, and the raw EDID is not visible from user space
(`AppleDisplay` shows vendor `unkn`, product 0x717, no `IODisplayEDID`:
IODisplay is not getting our EDID either).

**Changed, built in the guest, packaged, not installed:** the kext
publishes the EDID as the `EDID` property of `RadeonNI`, logs "digital
or analog input, HDMI or DVI signalling", and after each mode set logs
CRTC 0's timing registers (0x6e00 to 0x6e18) as read back.
`build/RadeonNI-g5.tar.gz` has it. Installing it on the G5 and
restarting was not permitted to me in this session; the user does it.

**Open:** why the monitor refuses the mode; why IODisplay has no EDID.

## 2026-10-05 — G5: the refused mode; HDMI signalling now carries the AVI infoframe

The user allowed the install. With the diagnostic kext on the G5:

- The monitor is a Xiaomi G24i (EDID name "Mi Monitor", vendor XMI,
  48 to 180 Hz) on the same DVI-to-HDMI adapter as before. Its EDID has
  an HDMI vendor block and an HDMI Forum block (600 MHz, SCDC), so the
  kext chose HDMI signalling.
- The preferred timing is CEA 1080p60 except that the EDID marks vertical
  sync as negative (flag byte 0x1a). The kext follows the EDID.
- CRTC 0 read back: H_TOTAL 0x897, blanking 0x00c00840, sync 0x002c0000.
  The horizontal timing is exactly the mode's, so the "1368x768" the
  earlier TV showed was its own rounding. (The three vertical values in
  that log line were read from the wrong offsets and mean nothing; the
  line is removed again.)

**Conclusion:** the timing is right; what was missing is the known gap
"HDMI without infoframes". The TV tolerated that, this monitor
apparently does not.

**Changed.** `rdn_modeset()` with HDMI signalling now does the video half
of Linux's `radeon_audio_hdmi_mode_set()` and `evergreen_hdmi_enable()`:
AV mute, general control packets, no deep colour, the AVI infoframe (RGB,
underscan, the CEA format number only if timing and polarities match a
CEA format, so 0 for this monitor's mode), unmute, send it every frame.
No audio: no clock regeneration, audio packets or audio infoframe.
`modeset_replay` passes on both architectures: our words for 0x7030,
0x7040, 0x7048, 0x7058 and 0x7084-0x7090 are the ones Linux wrote for
1366x768; 0x7044 is exempt (Linux also enables the audio infoframe).
The kext also takes the boot argument `rdn_dvi=1` (DVI signalling), and
keeps the `EDID` property and the signalling log line.

**On the G5:** installed with `install.sh --accel --hwcursor`, restarted.
Log: "HDMI signalling", "HDMI AVI infoframe, format 0", mode set 0,
3D engine up; `~/gl/qe`: Quartz Extreme in use at 1920x1080. The package
before the diagnostics is `~/RadeonNI-g5.prev2` there.

**Not known:** whether the monitor shows the picture now. The user has to
look. If not: `sudo nvram boot-args="rdn_dvi=1"` on the G5 and restart.
Still open: IODisplay does not get our EDID (`AppleDisplay` vendor
`unkn`).

## 2026-10-06 — Quake 3's black floors: `bool` has four bytes on Tiger PowerPC, Mesa's vertex format assumed one

Done on the real G5 over ssh (the card is there), Quake 3 full screen on
q3dm1, pictures by `~/gl/rdnuc grab`.

**The G5 shows the same bug**, darker than under QEMU (walls too), with
the same pattern: right with `r_primitives 3` and with
`r_ext_multitexture 0`. `r_primitives 3` had been saved in the config by
my earlier runs and travelled with the disk image; the default is 0
(restored).

**It is not a race.** With `/tmp/rdngld.trace` the same draws come out
right. `RDN_SYNC` (new, in the winsys: bit 0 waits for every command
buffer, bit 1 gives every draw its own) changes nothing in any
combination. So what the trace changed was not the timing.

**Bisected by call.** `RDN_GLD_TRACE_ONLY=name,name` (new) limits the
trace to the calls named. Tracing `glTexCoordPointer` alone, or
`glColorPointer` alone, makes the picture right; `glDrawElements`, the
lock calls and `glVertexPointer` do not. So a log call before Mesa's
`_mesa_TexCoordPointer` matters: what it leaves on the stack.

**Cause.** In the disassembly `update_array()` stores 16 bits of a local
and compares 32. The local is `union gl_vertex_format_user`
(`src/mesa/main/glthread.h`): a struct of `GLenum16 Type; bool Bgra;` and
four bit-fields, overlaid with `uint32_t All`, and
`_mesa_update_array_format()` returns early when `All` is unchanged. On
32-bit Darwin PowerPC `bool` has four bytes, so the struct has twelve,
and `All` is `Type` plus two bytes of padding nobody writes. When the
stack's leftovers there happened to equal the array's, a pointer call
with the same type and another size was taken for "no change": Quake's
`glTexCoordPointer(2, GL_FLOAT, 0, ...)` on the second unit kept the
default size 4, so the lightmap coordinates were fetched with stride 16
from an array of stride 8. Immediate mode never comes here, and my
`mtex` test had other leftovers. This is Mesa's assumption, not a
compiler bug.

**Fix.** `mesa/patches/0002-vertex-format-bool-size.patch`: `uint8_t` for
those fields and a static assertion that the union has four bytes.
Rebuilt, installed on the G5 (`~/RadeonNIGLDriver.prev3` is the binary
from before today): Quake 3 with its default settings is lit correctly,
untraced (grab). Not yet seen by the user.

**Looked for more of the kind:** a scan of the built parts of Mesa for
unions containing `bool` finds nothing else that overlays one with an
integer. That is a heuristic; other code may assume one byte in other
ways (the display list breakage at `-Os` with strict aliasing has not
been looked at in this light). `-mone-byte-bool` for the whole build
would need libstdc++ rebuilt the same way.

**Open.**
- `r_primitives -1` (strips with `glArrayElement`) draws no level at
  all: the screen keeps Quake's texture table from the end of loading.
- Sauerbraten's sky and weapon should be looked at again with this fix.
- The QEMU guest's installed bundle is older than both of today's
  changes (`scripts/gld.sh install-mesa` when it runs next).

## 2026-10-06 — The G5 package carries the OpenGL bundle and the 2D plug-in

`scripts/make-g5-package.sh` now also packs `RadeonNIGA.plugin` (built in
the guest, like the kext) and `RadeonNIGLDriver.bundle` (the host's Tiger
cross-build of Mesa with `gld/Info.plist`). `install.sh --accel` installs
both into `/System/Library/Extensions` (copy beside, rename into place)
and no longer requires them to be there already; `uninstall.sh` removes
them. Package built with `--with-vbios --with-firmware`; the installer's
new part has not run anywhere yet (the G5 is disconnected, and the guest
rehearsal was not done).

## 2026-10-06 — ROM comparison in the kext (not run); microcode ships in the package

**ROM, step one.** With the boot argument `rdn_romtest=1` the kext reads
the expansion ROM BAR (0x30), maps it, turns on address decoding, logs the
first bytes, the image's length and checksum and how many bytes differ
from the injected VBIOS, then restores the BAR. It does this before
`rdn_card_init()` and again after the card is up, and uses nothing of
what it read. Off by default: a ROM that does not answer could be a
machine check at boot. Builds in the guest; cannot run there (OpenBIOS
assigns no ROM address). To run on the G5: install the package,
`sudo nvram boot-args="rdn_romtest=1"`, restart,
`sudo dmesg | grep "ROM test"`. If the Mac does not boot with it: reset
the NVRAM (Command-Option-P-R) or boot Safe.

**Microcode.** User's decision: it goes into the package, not into the
repository. `scripts/fetch-firmware.sh` fills `firmware/` with
`TURKS_pfp.bin`, `TURKS_me.bin` and `LICENSE.radeon` from the host's
linux-firmware or, failing that, from kernel.org's linux-firmware
repository, each checked against a SHA-256 in the script (the host's and
kernel.org's files are identical; both sources tried). 
`make-g5-package.sh` runs it and always packs the three files;
`--with-firmware` is accepted and ignored.

## 2026-10-06 — Why Quake 3 was slow on the G5: three driver causes, and the card runs at its boot clocks

Asked by the user: Quake 3 `timedemo` on demo `four` gives 47.5 fps at
1920x1080 where a GeForce 6600 LE gives 148. All of it on the real G5 over
ssh; `sample Quake3` for profiles, `RDN_STATS=1` (new, in
`mesa/target/rdn_device_darwin.c`) for the number and length of fence
waits. Run with `+set r_mode -1 +set r_customwidth 1920 +set
r_customheight 1080`: the saved config has `r_mode 3` (640x480), which the
display does not offer ("Could not initialize OpenGL").

| Step | fps |
|---|---|
| Baseline | 47.8 |
| GART size reported non-zero | 55.8 |
| Engine clock 100 -> 650 MHz, core voltage 0.9 -> 1.0 V | 65.0 |
| Swap flushes instead of waiting | 83.7 |
| Destroyed buffers freed later instead of waited for | 96.4 |

1. **A command buffer per draw.** The winsys reported `gart_size_kb = 0`.
   `radeon_cs_memory_below_limit()` compares what a command buffer
   references with 70 % of that, so r600 flushed before every draw: four
   kernel calls each (fence poll, free, alloc, submit), 35 % of the main
   thread. Now the VRAM size is reported.
2. **The card runs at its boot clocks.** FirmwareInfo's defaults, which
   ASIC_Init sets, are engine 100 MHz, memory 150 MHz, 0.9 V. The
   PowerPlay table's performance state is 650 / 800 MHz at 1.0 V. Nothing
   in `hw/` ever left the boot state (Linux leaves it through DPM, which
   needs the SMC microcode; its older profile method caps at the
   defaults). After fix 1 one wait per frame remained, 7.3 ms on average,
   and spinning instead of sleeping (`RDN_FENCE_SPIN`) did not shorten it:
   the GPU really took that long.
   New: `hw/rdn_pm.c` (PowerPlay parsing; voltage, engine clock and memory
   clock through the VBIOS's SetVoltage, SetEngineClock and
   SetMemoryClock; the temperature sensor), `tests/pm_states` (x86 and
   PowerPC agree), the user client method `RDN_UC_POWER`, and
   `rdnuc power [performance|boot [mask]]`. Nothing switches by itself:
   the kext boots as before.
   On the G5: `rdnuc power` reads 99.99 / 150.00 MHz, 34 C.
   `rdnuc power performance 3` (voltage and engine clock): 649.96 MHz
   read back, 44.5 C after several demo runs.
   `rdnuc power performance 4` (memory clock): the table returns success
   and the clock stays at 150 MHz. Not understood. Linux loads the MC
   microcode on GDDR5 cards and switches the memory clock through the
   SMC; we do neither.
3. **Swap waited for the GPU.** `rdn_mesa_present` called `glFinish`, so
   CPU and GPU never worked at the same time. For a program that has the
   whole screen it is `glFlush` now (`RDN_GLD_SWAP_FINISH=1` for the old
   way), and the winsys keeps at most four command buffers pending.
   Stills of q3dm1 are the same both ways (readback).
4. **Destroying a buffer waited for the GPU** (`rdn_buffer_destroy`, 9 %
   of the frame, from the constant buffer of the copy to the screen). The
   memory now goes on a list and is freed when its fence has passed.

What is left in the profile at 96 fps: Quake's own interpreted game code
(about a third; "Compiled VMs not supported on this platform", the same
with any card), Mesa's draw path, the `memcpy` of vertices into video
memory (5 %). Write-combining, GART and client storage were not needed
for any of this and were not tried.

**Then the user reported black and white lines on the monitor.** The
screen read back from video memory at that moment is a correct desktop
(`rdnuc grab`), so it is the scanout or the signal, not the picture. When
it started is not known; the engine clock had been at 650 MHz for seven
minutes and the memory clock attempt had run. Suspects: the display
watermarks, which Linux computes from the clocks
(`evergreen_program_watermarks`) and we never touch, or something the
memory clock table changed before it gave up. I put voltage and engine
clock back (`rdnuc power boot 3`, 99.99 MHz read back); whether the
picture came back is the user's to say. The fps of steps 3 and 4 were
measured at 650 MHz.

**Open.** The lines. The memory clock. Making the performance state the
default at start (not before the lines are understood). The QEMU guest's
bundle is older than all of this. `power` can be called by any local
user.

## 2026-10-06 — The lines: what the user saw, what was ruled out, a register peek for the next time

**The user:** vertical stripes, white and black alternating, still, the
whole screen, nothing of the desktop to make out. Still there after
`rdnuc power boot 3`, after a mode set from ssh (`cgmode set 0 1920 1080
16`, then 32) and after switching the monitor off and on. But during that
mode switch, and again when the G5 restarted, they saw the desktop and
its fade to black correctly. Their reading: not a driver thing.

**Ruled out from ssh while the stripes were on the monitor:** no Quake
process; the screen in video memory was the desktop with the menu bar
clock advancing (two grabs); Quartz's gamma table was a clean ramp
(`tools/guest/gamma.c`, new); the kext that was replaced this morning
already had yesterday's HDMI changes, so the new kext boots the display
the same way.

**Not explained.** Identical rows and no picture look like a display
controller that repeats its line buffer, but the desktop came back by
itself during a mode switch and at logout, the two moments when the
window server fades with the gamma table and hides the cursor. Which of
this morning's steps started it is not known either: nobody was looking
at the monitor between the restart at 09:05 and the report at about
09:12.

**For the next time:** `RDN_UC_REG_READ` in the kext and `rdnuc reg off
[n]` read registers of the register BAR. After the restart (09:21):
CRTC control 0x6e70 = 0x00410311 (read requests on), cursor 64x64 at
aperture offset 0x01f00000. A Quake 3 run ended with `killall`, at the
boot clocks, changes none of the display, cursor, PLL or memory arbiter
registers I dump except the cursor's position.

**Also measured:** at the boot clocks, with the swap and buffer changes,
the demo runs at 80.8 fps. So most of the gain is the driver fixes; the
engine clock is worth 80.8 -> 96.4.

## 2026-10-06 — Engine clock raised with the user watching: the display starves while the GPU works

After the restart the desktop was fine at the boot clocks, and a demo run
looked right. `rdnuc power performance 3` again (649.96 MHz read back);
the register dump before and after differs only in the engine PLL (0x600,
0x608) and the memory arbiter's timing (0x2774, 0x2778, 0x27b0), which
the VBIOS recomputes. No display or cursor register changed.

**The user, on the monitor at 650 MHz:** "it flickers a bit", with a
video (60 fps, a Finder window dragged through VNC, which was turned on
at their request: `kickstart ... -setvnclegacy`, password `tiger`).
Frame by frame: in single frames a band of the screen, the full width
and from some row down, shows fine vertical stripes instead of the
picture, and thin horizontal streaks run from the window's right edge.
The next frame is right again. The band is not confined to the window,
so it is not a texture or a half-drawn update: rows of the scanout
itself are wrong for a frame.

**Reading.** The display controller does not get its rows from memory in
time while the GPU is busy, and repeats what its line buffer holds. The
priority marks are zero (PRIORITY_A_CNT 0x6b18 and PRIORITY_B_CNT 0x6b1c
read 0): we never program the display watermarks, which Linux computes
from the mode and both clocks (`evergreen_program_watermarks`). At
100 MHz the GPU asks for little memory and the display keeps up; at
650 MHz on a 150 MHz memory clock it does not. This morning's stripes
over the whole screen look like the same failure not recovering, which
is not shown.

Back to the boot clocks (`rdnuc power boot 3`). The engine clock stays
there until the watermarks are programmed; the memory clock is the other
half.

## 2026-10-06 — Watermarks ported; at 650 MHz the user sees no flicker

**The "freeze" the user reported** in between was Apple's VNC server
alone: `AppleVNCServer` at 60 % of a CPU in a loop around `syslog`,
`syslogd` at 100 %, the window server idle and well, ssh fine.
`/var/log/windowserver.log` at the moment VNC was turned on:
"MPHWCopyRegion: surface copy fails (-536870201) ... disabling" and
"CGXAccessDisplayDeviceSurface: Copy screen to surface failed" (the same
lines are there from 2026-10-05 13:09). `kickstart -restart -agent` got
it back. Our accelerator does not do the copy the window server wants
for reading the screen; not looked at further.

**Ported.** `hw/rdn_watermark.c`: Linux's `evergreen_bandwidth_update()`
for CRTC 0 with one display: half a line buffer pair, one DMIF buffer,
the two latency watermarks and the two priority marks, in Linux's 20.12
fixed point. Called by `rdn_modeset()` after the scanout address, where
Linux calls it, and by `rdn_pm_set()` after a clock change with the
clocks read back. `struct rdn_card` now carries the clocks and the mode
they are computed from.

**Checked against the trace.** `modeset_replay` passes on x86 and
PowerPC with the watermark writes in it: for 1920x1080 the latency
words 0x39de444e and 0x39de4f9c and the marks 0x1f and 0x3a, for
1366x768 0x51df5792, 0x51df5c46, 0x36 and 0x3c, as Linux wrote them
(Linux computed set A from 650/800 MHz and set B from 100/150 MHz, its
power management's two states; the test names those clocks). Our own
clocks give 0x4f9c and 0x3a for both sets, at 100 MHz and at 650 MHz
alike: the 150 MHz memory clock is the smallest term either way.

**On the G5** (kext installed, restarted): 0xbf4 = 0x39de4f9c, 0x6b18 =
0x6b1c = 0x3a, 0xca0 = 0x11. `rdnuc power performance 3`: 649.96 MHz.
**The user, dragging a window: "It looks fine now!"** Then the demo:
96.0 fps, 45 C, desktop back afterwards by the registers (not asked of
the user).

**Open.** Whether to switch to the performance state at start. The
memory clock. What made the whole screen stripes this morning stay after
the clock was back at 100 MHz (zero marks were the state then too, so
"the same failure, stuck" is still only a reading).

## 2026-10-06 — Performance state at start; write combining for video memory: 108 fps

**At start.** The user wants the switch automatic. `startEngine()` now
goes to the performance state (voltage and engine clock) once the 3D
engine is up; `rdn_bootclocks=1` as a boot argument keeps the boot state.
G5 restarted: 649.96 MHz and marks 0x3a 27 s after boot without anyone
asking.

**Profile at 96 fps** (`sample`, 5 s of the demo): Quake's interpreted
game code 42 %, its sound mixing 10 %, drawing through Mesa 26 % of which
copying vertices into video memory 8 %, the swap 6 %. Fence waits: 0.4 s
of 13. So Quake no longer waits for the GPU at all, and a faster memory
clock would not show in this number.

**Write combining.** The user client's memory can be mapped with cache
bits chosen by the caller (`kIOMapUserOptionsMask` covers them), so no
kext change: `RDN_APERTURE_CACHE=default|wc|inhibit` in
`rdn_device_darwin.c`. Demo: default 95.7, inhibit 95.5, wc 108.6 fps.
The default for device memory is uncached and guarded; write combining
on PowerPC is uncached without the guard bit. It is the default now
(`=inhibit` goes back). With the kext above: 108.4 fps, 43.5 C.

| Step | fps |
|---|---|
| This morning | 47.8 |
| Driver fixes, boot clocks | 80.8 |
| Engine clock 650 MHz | 96.0 |
| Write combining | 108.4 |

**Not checked:** a still picture with write combining (only that the
demo runs and the number); the QEMU guest.

## 2026-10-06 — Memory controller microcode on the G5: the memory clock goes to 800 MHz

**Ported.** `hw/rdn_mc.c`: Linux's `ni_mc_load_microcode()` for Turks
(reset the sequencer, 29 I/O register pairs, the 6024 words of
`TURKS_mc.bin`, start, wait for training). `tests/mc_replay` is strict:
from the read of MC_SEQ_MISC0 on, each of our 6090 accesses is the next
entry of the reference trace (entries 2407 to 8496 of phase a1), on x86
and PowerPC. The kext runs it after POST and before the EDID and the
first mode set, only with the boot argument `rdn_mc=1` and `FW_MC` in the
personality, which `install.sh --accel` adds when `TURKS_mc.bin` is in
the package (it is now; `fetch-firmware.sh` knows its checksum).

**Installer.** My comment in `install.sh`'s perl program had an
apostrophe, which ended the shell's quote: the first install on the G5
failed with a perl syntax error and changed nothing. The rehearsal on the
host had extracted the program with a regular expression and missed it.

**Before, on the G5:** MC_SEQ_SUP_CNTL 0, MC_SEQ_MISC0 0x500026a9,
MC_IO_PAD_CNTL_D0 0x1000078c: what Linux read before its load.

**With `rdn_mc=1`** (user's go-ahead; restart): "memory controller
microcode: 0", MC_SEQ_SUP_CNTL 0xb1800001 (running), 1024 MB, desktop and
Quartz Extreme as before, `rdnuc alloc` passes, engine clock raised by
itself as before.

**Then `rdnuc power performance 4`:** memory 800.00 MHz read back. This
is the table that returned success and did nothing this morning: it needs
the sequencer's microcode. Changed with it: MPLL mode 0x61c 0x3d10 ->
0x1910, the arbiter's timing 0x2774/0x2778, and our watermarks,
recomputed to latency 17486 ns and mark 0x1f, Linux's own values for its
high state. `rdnuc alloc` passes, Quartz Extreme in use.

**Quake 3 demo:** 112.2 fps (108.4 before), fence waits 72 ms of 11 s,
47 C. A still of q3dm1 and the desktop read back right. As expected the
demo gains little: it was not waiting for the GPU. What the memory clock
does for work that is limited by the GPU has not been measured.

**Not known:** what the monitor shows (the user's to say). Not done: the
memory clock in the automatic switch; `rdn_mc` without a boot argument.

## 2026-10-06 — "No signal" at 800 MHz was the DVI plug

The user reported the monitor going between "no signal" and off after the
memory clock switch. From ssh: CRTC running (frame counter advancing 60 a
second), no display register changed, video memory test passing. A mode
set from ssh did not help; reseating the DVI plug did ("it had slightly
disconnected"). So not the clock. Worth remembering for this morning's
stripes over the whole screen, which no register or readback explained
either; that is a guess, nobody touched the plug then.

## 2026-10-06 — Full performance state by default; where Quake 3's CPU time goes now

**The user:** the desktop at 650 / 800 MHz "feels good"; make it the
default. Done: with `FW_MC` in the personality the kext loads the memory
controller's microcode at every start (`rdn_mc=0` skips it), and the
automatic switch includes the memory clock when the sequencer runs
(`rdn_mclk=0` leaves it, `rdn_bootclocks=1` keeps the whole boot state).
`boot-args` on the G5 is empty again. After a restart, nobody asking:
649.96 / 800.00 MHz, marks 0x1f, video memory test passes, demo 111.3
fps. Under QEMU nothing changes: `kext.sh` does not inject `FW_MC`.

**Profile** (`sample Quake3 5` in the middle of the demo, main thread,
3820 samples, time in each function itself):

| Where | Share |
|---|---|
| Quake's game code, interpreted ("Compiled VMs not supported on this platform") | 39.6 % |
| Mesa and our glue | 17.4 % |
| Quake's renderer (`RB_*`, `R_*`) | 13.2 % |
| Quake's sound mixing | 8.6 % |
| `memcpy` (vertices into video memory, the command buffer) | 5.0 % |
| Kernel calls (alloc, free, submit, fence) | 2.8 % |
| Everything else (C library, collision, network) | 13.5 % |

By call: `glDrawElements` and below 18.4 % (of it `u_vbuf_draw_vbo`
9.4 %, `r600_draw_vbo` 5.6 %, `st_prepare_draw` 4.3 %,
`_mesa_update_state` 2.7 %), the swap 3.5 %. Inside Mesa nothing stands
out: the largest single functions are `vbo_get_minmax_index_mapped`
1.2 %, `restGPRx` 0.9 % (register restore routines of a size-optimised
build), `__emutls_get_address` 0.8 % (thread-local storage is emulated on
Tiger).

So the driver's whole share is about a quarter of the frame; were it
free, the demo would run at about 148 fps. With `+set s_initsound 0`:
127.0 fps.

**The game binary.** `Quake3.app` looks for native `ui`, `cgame` and
`qagame` bundles (`vm_* 0`), finds none and interprets the `.qvm` files.
The folder also has `Quake3 10.2.app` and `Quake3 10.2 G4.app`, older
builds; id's own PowerPC builds could compile the VM. Which binary the
user's 148 fps on the GeForce 6600 LE came from is not known to me.

## 2026-10-06 — The GeForce 6600 LE in the same G5: 127.6 fps, and the same profile next to ours

The user put the 6600 LE back (Apple's `GeForce` 4.1.8 and `NVDAResman`
loaded; ours stays out without its card) and asked whether the numbers
match. Same script, same binary, same config, 1920x1080 at 60 Hz, the
desktop at 16 bpp: **127.6 fps** (GL 1.5 NVIDIA-1.4.18), 139.3 without
sound. Not the 148 the user remembered; with us the same runs gave 111.3
to 112.2 and 127.0.

How much of our glue: the bundle's stubs 1.0 % of the main thread, the
winsys 4.0 % with what it calls (the flush 2.6 %: the copy of the command
buffer and the kernel calls; adding buffers 0.8 %), the copy to the
screen at swap 1.2 %.

Both profiles split the same way (`sample`, main thread, by what the
time was spent under), in milliseconds of a frame:

| | Radeon, our driver (8.98 ms) | GeForce, Apple's (7.84 ms) |
|---|---|---|
| Quake's game code, interpreted | 3.75 | 3.96 |
| OpenGL driver | 2.80 | 1.48 |
| Quake's renderer | 1.13 | 1.06 |
| Quake's sound | 1.10 | 1.11 |
| Other | 0.20 | 0.23 |

The game's own parts agree to a tenth of a millisecond or two, so the
split holds, and the whole difference between the cards is the driver's
CPU time: 2.8 ms against 1.5. Of Apple's 1.5 ms, 0.6 is in
`mach_msg_trap`.

**Built, not installed** (the user: install nothing by yourself):
Mesa for Tiger at -O2, `RDN_MESA_OPT=2 scripts/build-mesa.sh darwin`,
into `build-darwin-O2`; 20.6 MB against 18.8 MB. Never run.

## 2026-10-06 — The 6600 LE again with the desktop at 32 bpp

The user switched the desktop to 32 bpp. Demo: 121.7 and 122.8 fps
(127.6 at 16 bpp), 132.4 without sound. Profile, same split, 8.18 ms a
frame: game code 4.20, sound 1.34, OpenGL driver 1.30, renderer 1.04,
other 0.28. The profile samples five seconds of an eleven second demo,
not the same five each time, so the parts move by a few tenths between
runs; the driver's share is 1.3 to 1.5 ms in both.

## 2026-10-06 — The GPU's time per frame measured; Mesa at -O2 is worth 2 %

Radeon back in the G5 (650 / 800 MHz by itself after the restart).

**How long the GPU works on a frame** (the user asked what makes me sure
the driver and not the GPU is the limit). Demo `four`, fence waits from
`RDN_STATS=1`, with `RDN_FENCE_SPIN=20000` so that the 1 ms sleep of the
kext's poll does not round the waits up:

| Run | fps | Waiting for the GPU |
|---|---|---|
| Normal | 112.0 | 72 ms in all |
| `RDN_GLD_SWAP_FINISH=1` (wait at each swap), spinning | 99.4 | 1455 ms: 1.15 ms a frame |
| `RDN_SYNC=1` (wait for every command buffer), spinning | 90.9 | 2598 ms: 2.06 ms a frame |
| The same two, sleeping | 95.2, 86.5 | 2018 and 3339 ms |

So the GPU needs between one and two milliseconds for a frame the CPU
takes nine to produce. Quake 3 here is limited by the CPU.

**Mesa at -O2** (`build-darwin-O2`), installed on the G5 at the user's
request, the size-optimised one kept as `~/RadeonNIGLDriver.Os`: 114.3
and 114.2 fps against 112.0, 129.7 without sound against 127.0. A still
of q3dm1 is right, and Apple's Chess (display lists, the thing this
compiler's optimiser broke before at -Os with strict aliasing) shows its
board and all pieces (readback). The package still builds the
size-optimised bundle.

## 2026-10-06 — -O2 is the default Tiger build of Mesa

User's decision. `scripts/build-mesa.sh darwin` builds at -O2 into
`build-darwin`, which the package and `gld.sh install-mesa` take the
bundle from; `RDN_MESA_OPT=s` builds for size into `build-darwin-Os`.
The rebuilt bundle is byte for byte the one tried on the G5. Package
rebuilt; not installed anywhere by me (the G5 already runs this bundle).
The user also decided not to change Mesa itself for now.

## 2026-10-06 — Doom 3 demo: starts, then "out of video memory" loading a level

The user installed the Doom 3 demo on the G5 (`~/Desktop/Doom 3 Demo`).
It needs `+set r_mode -1 +set r_customWidth 1920 +set r_customHeight 1080`
like Quake 3 (our display offers one mode). Its `demo00.pk4` has no
`.demo` recording, so `timedemo demo1` has nothing to play; `recordDemo`
is the way. With our driver it reaches its menu and console.

**Loading a level** (`~/Desktop/doom3.err`): over a hundred "rdn: out of
video memory (131072 to 704512 bytes asked, 143 MB in use)", then r600's
"failed to create temporary texture to hold untiled copy", a bus error.

**Two limits, either of which gives that message:**
- The user client tracked at most 4096 allocations per client, and the
  winsys asks the kext for every buffer by itself. 143 MB over 4096 is
  35 KB a buffer, which is about what a level's textures are, so this is
  probably the one that was hit. Now 65536.
- Clients only ever got the 222 MB of the 256 MB aperture; the card has
  1024 MB. The other 768 MB cannot be mapped by the CPU, but r600 never
  maps tiled textures and render targets (it fills them through a
  staging copy and marks them RADEON_FLAG_NO_CPU_ACCESS). New: a second
  allocator in the kext for the memory beyond the aperture
  (`RDN_UC_ALLOC_HIDDEN`, `RDN_UC_HIDDEN_INFO`), `alloc_hidden` in the
  device interface, and the winsys puts buffers with that flag there,
  falling back to the aperture. `RDN_NO_HIDDEN_VRAM=1` in a program's
  environment turns it off.

Built (kext, Tiger bundle, host library, package); the tests pass.
**Not installed, not run:** the user asked me not to install by myself.
Which of the two limits Doom hit is a guess until it runs.

## 2026-10-06 — Doom 3 demo loads its levels now; no benchmark out of it yet

Kext and bundle with the memory beyond the aperture and 65536
allocations installed on the G5 (the user's go-ahead), restarted: 650 /
800 MHz by itself, desktop and a Quake 3 still right by readback, Quake's
demo 114.3 fps as before. **Doom 3 loads `demo_mars_city1` and
`demo_mc_underground`**, no "out of video memory", and shows the opening
scene (grab). Which of the two limits it had hit was not separated.

**A benchmark from the demo build:**
- `wait`, `recordDemo`, `stopRecording`, `setviewpos`, `god`, `echo` and
  `exec` all work from the command line, so I recorded a demo without
  anyone playing: a tour of the level made from the entity positions in
  its `.map` file. I made it far too long (2400 frames; recording ran at
  a few frames a second) and the user had me stop it.
- Playback does not work in this build: `timeDemoQuit bench` says
  "couldn't open demos/bench.demo" ("restricted demo mode" reads no
  loose files), and with the recording in a pak of its own the game
  quits two seconds after starting. So no recorded demo, of any length.
- The tour run live instead (`tools/guest/d3tour.sh`, `d3tour.cfg`: five
  places, four directions, ten frames each, timed between two `echo`
  marks in `qconsole.log`): 2.0 and 2.5 fps. That is not the driver:
  `sample` during it has 47 % of the main thread in the game's
  declaration parser, 31 % reading and inflating files, 14 % in particle
  set-up, 3 % in its renderer and 0.5 % under OpenGL calls; fence waits
  2 ms in a whole run. Each jump of the camera makes the game load that
  place's effects, and ten frames a view never gets past the loading.

**Open.** Whether ordinary play is slow too (the user's to say). A tour
that visits every place once before the timed pass. Doom 3 has not been
looked at for correctness beyond one grab.

## 2026-10-06 — Doom 3 in normal play: half the frame is the driver, and a quarter of that is ours

The user: Doom 3 is slow but no slideshow, slower with dynamic lights and
skinned models. Profile of `demo_mars_city1`'s opening scene
(`sample`, main thread): OpenGL driver 47.1 %, Doom's renderer 23.5 %,
game logic 6.9 %, other 21.9 %. Under GL: `glDrawElements` 24.2 %,
`glBufferData` 6.1 %, swap and flush 5.7 %. By kind: kernel calls 9.5 %
(`dev_alloc` 3.5 %, `dev_free` 5.4 %, fence polls 0.5 %: every buffer
`glBufferData` replaces and every buffer r600's upload path asks for is a
call into the kext, and another to free it), `memcpy` 7.6 %,
`__emutls_get_address` 1.4 %, `rdn_cs_add_buffer` 1.3 %.

**Done on our side, Mesa untouched (user's choice of all four):**
1. A cache in the winsys for video memory a buffer lets go of: kept with
   its fence and handed to the next request of the same size once the GPU
   has finished with it; back to the kext after 128 command buffers
   unused, or above 32 MB (aperture) and 128 MB (beyond). It replaces the
   list of destroyed buffers. A fence known to be reached answers for all
   earlier ones without a kernel call.
2. Command buffers come from the same cache, in steps of 64 KB.
3. `rdn_cs_lookup` has a small hash in front of its linear search.
4. `mesa/darwin8/tiger_emutls.c`: our own `__emutls_get_address`, linked
   ahead of libgcc's, with a short cut for the first thread that uses it
   (two globals instead of `pthread_getspecific`). `nm` shows ours in the
   bundle.

Builds for Tiger, the host and big-endian Linux; the tests pass.
**Not run anywhere:** none of this can be tested without the card.
My estimate to the user before starting: 10 to 15 % more frames in Doom 3,
3 to 4 % in Quake 3.

## 2026-10-06 — The four winsys and TLS changes on the G5

Installed at the user's request (bundle only; the kext is unchanged and
the G5 was not restarted, so the window server still runs the previous
bundle until it next starts). The previous one is
`~/RadeonNIGLDriver.prev5`.

**Quake 3**, demo `four`: 115.3 and 115.6 fps (114.3 before), 130.5
without sound (129.7). A still of q3dm1 is right. One per cent, not the
three to four I had estimated.

**Doom 3**, the same opening scene profiled again:

| Main thread | Before | After |
|---|---|---|
| OpenGL driver | 47.1 % | 38.6 % |
| of it kernel calls (`mach_msg_trap`) | 9.5 % | 0.7 % |
| of it `glBufferData` | 6.1 % | 2.3 % |
| of it swap and flush | 5.7 % | 3.2 % |
| of it `__emutls_get_address` | 1.4 % | 1.0 % |
| Doom's renderer | 23.5 % | 22.6 % |
| Other, idle included | 21.9 % | 32.0 % |

The scene ran at Doom's 60 frames a second before and after (606 command
buffers in 5 s both times, two a frame), so the saving shows as idle
time, not as frames; my "10 to 15 % more frames" cannot be read off this
scene. The heavier scenes the user finds slow have not been measured.
No "out of video memory"; the screen grabbed during the scene is right.
Apple's Chess: board and pieces right. `rdnuc alloc` passes.

**Not seen by the user** (away): all of the above is by readback.

## 2026-10-06 — Doom 3's slow scenes measured; glthread: the second processor does Mesa's work

**The user's slow scenes.** Sampled while they played: OpenGL driver
55.1 % of the main thread, Doom's renderer 23.8 %, game logic 18.4 %.
`glDrawElements` 36.9 %, and under it Mesa's per-draw work
(`st_prepare_draw`, `r600_draw_vbo`, `_mesa_update_state`); copies about
5 %, kernel calls 1.2 %. Doom draws every surface once for every light
that touches it.

**A benchmark that works on any card:** the user's save games `bench`
and `bench2` (both in `demo_mc_underground`), loaded from the command
line, 60 frames to settle and 300 timed between two `echo` marks
(`tools/guest/d3save.sh`).

| | bench | bench2 | Quake 3 `four` |
|---|---|---|---|
| Before today's last changes | 16.0 | 17.3 | 115.3 |
| Built with `-mtune=970` (kept) | 16.1 to 16.3 | 17.6 | 115.3 |
| `RDN_GLTHREAD=1` | 18.8 | 20.2 | 143.1 |
| and the TLS short cut for more than one thread | 21.3 | 22.1 | 143.8 |

Fence waits stay at 1 to 2 ms a run: the GPU is idle most of the time.

**glthread** (user's go-ahead for touching Mesa this once). Mesa's own
answer to programs limited by the CPU: the program's thread only records
its OpenGL calls, a second thread runs them. The G5 has two processors
and the games use one.
- `mesa/patches/0003-r600-glthread.patch`: r600 declares
  `map_unsynchronized_thread_safe` (Mesa refuses glthread without) and a
  map with `PIPE_MAP_THREAD_SAFE` takes its transfer from the pool of
  the other thread. r600 does not run with glthread on Linux either.
- Our front end: `RDN_GLTHREAD=1` in a program's environment calls
  `_mesa_glthread_init()`; every entry point that touches the context
  from the program's thread waits for the second thread first; the copy
  to the screen finds its context through `st->frontend_context`, not
  through the calling thread; and `set_background_context`, which
  glthread calls in its thread before anything else and which we did not
  have (the first try hung in a call to address 0).
- `tiger_emutls.c`: the short cut now serves four threads, not one; the
  worker had been going through `pthread_once` and `pthread_getspecific`
  for every lookup.

Quake 3 with it: 143.8 fps, 163.3 without sound (the GeForce 6600 LE:
122 to 128, and 132 to 139); a still of q3dm1 is right. Doom 3 with it:
a profile shows the program's thread waiting 45 % of the time for room
in glthread's queue and the worker waiting 30 %: the worker is the
bottleneck now and the hand-over between the two is not free.

**Not done:** glthread is off unless the variable is set, and has run
only in these two games, by readback and by the numbers. The window
server and windowed programs have not been tried with it.

## 2026-10-06 — glthread for programs started from the Finder

The user saw Quake 3 and Doom 3 look right with glthread and asked for a
way to have it without a terminal. The front end now also reads
`/Library/Application Support/RadeonNI/glthread`: a program whose
process name is a line of it (or any program, with a line `*`) gets
glthread; `RDN_GLTHREAD=0` or `=1` in the environment overrides the
file; no file, no glthread. On the G5 the file names `Quake3` and
`Doom 3 Demo`. Checked there: Quake 3 144.0 fps from the list with
nothing in the environment, 115.5 with `RDN_GLTHREAD=0`; Doom 3 `bench`
21.5 fps from the list. The installer does not create the file; the
package's README says how.

## 2026-10-06 — The GeForce 6600 LE on the same benchmarks

Same G5, same scripts, 1920x1080 at 32 bpp, Apple's `GeForce` 4.1.8.

| | Radeon, no glthread | Radeon, glthread | GeForce 6600 LE |
|---|---|---|---|
| Doom 3 `bench` | 16.3 | 21.3 to 21.5 | 26.2 |
| Doom 3 `bench2` | 17.6 | 22.1 | 18.7 |
| Quake 3 `four` | 115.5 | 144.0 | 127.8 |
| Quake 3 `four`, no sound | 130.5 | 163.3 | 139.1 |

Each GeForce figure is two runs that agree to the decimal. This
morning's Quake 3 on the Radeon was 47.8.

Profiles of the two saves on the GeForce (`sample`, main thread): Apple's
OpenGL driver running 39 % (`bench`) and 27 % (`bench2`), waiting in the
kernel under OpenGL calls 7 % and 5 %, the rest Doom's own code, which is
most of `bench2`. So the GeForce is not waiting for its GPU in these
scenes either; both cards are limited by the processor, and the
difference between them is how much of it the driver takes.

## 2026-10-06 — Doom 3 quality settings on the GeForce 6600 LE

The demo's config is at the game's defaults: compressed textures
(`image_useCompression 1`, `image_useNormalCompression 2`), anisotropy
1, no antialiasing, shadows on, 1920x1080. `tools/guest/d3quality.sh`
runs both saves at three settings (through `D3ARGS` of `d3save.sh`) and
puts the user's config back after each run.

| GeForce 6600 LE | bench | bench2 |
|---|---|---|
| As configured | 26.1 | 18.7 |
| Ultra (no texture compression, anisotropy 8) | 23.1 | 16.0 |
| Ultra and `r_multiSamples 4` | 5.0 | 3.6 |

With antialiasing the GeForce is limited by its GPU at last. The Radeon
has not run these yet (the GeForce is in the machine).

## 2026-10-06 — Doom 3 quality settings on the Radeon: no change in speed, and no antialiasing

Radeon back in, glthread on from the list, `d3quality.sh`:

| | bench | bench2 |
|---|---|---|
| As configured | 21.5 | 22.2 |
| Ultra | 21.5 | 22.1 |
| Ultra and `r_multiSamples 4` | 21.3 | 22.1 |

The same to a few tenths at all three: limited by the processor, with
the GPU's work not showing at all. But the third line is not antialiased:
our bundle's pixel formats carry no sample buffers
(`gld/RadeonNIGLDriver.c`, the comment at `kCGLPFASampleBuffers`) and the
front end's visual has one sample, so a program that asks for 4 samples
runs without them and is not told. The game does not report it. So the
GeForce's 5.0 and 3.6 fps with antialiasing have no Radeon number to
stand against: ours does less work there. Multisampling is a known gap
now. Anisotropic filtering and uncompressed textures do go through Mesa.
No out-of-memory line at ultra; 50 C afterwards.

## 2026-10-06 — Multisampling

Asked for by the user after the antialiased runs turned out not to be.

**Front end.** `OSMesaSetSamples(n)`: visuals of contexts and buffers
made afterwards have n samples as far as the screen supports the format
with them (at most 8); `validate` makes the colour and depth textures
`PIPE_TEXTURE_2D` with that many samples; every copy out of them
(`osmesa_copy`: to the screen, to a surface's buffer) is a
`pipe->blit`, which resolves. What is imported (the scanout, a surface's
buffer) stays single-sampled.

**Bundle.** The request for samples is still kept from Apple's software
renderer, but remembered, handed to Mesa (`rdn_mesa_samples`, not in
the window server; `RDN_GLD_NO_MSAA` turns it off) and written into the
record that goes back. Without that CGL does not accept the format: Doom
asked for 4, then 2, then none. **Pixel format record word 9 is sample
buffers in the high half and samples in the low** (0x00010004 for four),
read off the software renderer's own answer to such a request
(`tools/guest/pfsamples.c`, `RDN_GLD_KEEP_SAMPLES=1` lets the request
through to it, `RDN_GLD_LOG` dumps the records). With it `pfsamples`
gets "sample buffers 1, samples 4" from our renderer and Doom asks once.

**On the G5** (Radeon, glthread on): Doom 3 `bench` with
`r_multiSamples 4` runs and a grabbed frame is right. That the samples
are there is shown by the GPU's time, not by a picture: fence waits with
`RDN_SYNC=1` and spinning, over the same run, 3417 ms without and
4722 ms with. Two enlarged crops of a ceiling light did not show a
difference I could point at (its edge is a texture's).

| Radeon | bench | bench2 |
|---|---|---|
| No antialiasing | 21.5 | 22.2 |
| 4 samples | 20.8 | 21.6 |
| 8 samples | 20.7 | 21.7 |

**Ultra and 4 samples together run out of video memory** ("out of video
memory ... 915 MB in use", the game quits; my script then waited for
marks that never came and left the game's config at ultra with
antialiasing, restored by hand from the copy the script makes). Read as
capacity, not a leak, but not taken apart: uncompressed textures plus
multisampled buffers against 990 MB shared with the window server, and
nothing here can move a texture out of video memory to make room, which
is what Apple's and Linux's drivers do. r600 resolves to our linear
scanout through a temporary texture each frame.

The GeForce 6600 LE has no figure for 4 samples with the default
textures yet (only with ultra: 5.0 and 3.6).

## 2026-10-06 — Where Doom 3's video memory goes

`RDN_STATS=1` now also accounts for every buffer in the winsys and
prints, at exit and the first time memory runs out, what was allocated
at the moment the most was: by region and by size.

| Doom 3 `bench`, most allocated | In the aperture | Beyond it |
|---|---|---|
| Default textures | 13 MB | 226 MB |
| Ultra (uncompressed) | 121 MB in 1679 buffers | 712 MB in 3280 buffers |

Ultra, beyond the aperture: 771 buffers of 256 to 512 KB (225 MB), 190
of 1 to 2 MB (222 MB), 678 of 64 to 128 KB, 331 of 128 to 256 KB, and so
on down: the game's textures, uncompressed, with their mipmaps. Lost to
the kext's 4 KB granularity: 3 MB in each region. In the cache at that
moment: 28 MB and nothing. So it is not our overhead and not a leak: the
region beyond the aperture is 768 MB and ultra fills 712 of it; four
samples at 1920x1080 want about 33 MB of colour, 33 MB of depth and the
resolve's temporary on top, and the aperture's 222 MB is shared with the
window server.

The user asked for eviction and GART. Proposed order, waiting for their
word: GART first (page table and enable registers ported into `hw/` and
replayed against the trace, pinned pages with bus addresses from the
kext, a third kind of allocation), then eviction to GART memory on top,
which needs r600 to take a buffer's new address (a second small patch to
Mesa).

## 2026-10-06 — GART: the GPU reads system memory on the G5

Asked for by the user together with eviction; the r600 patch for the
second step is allowed. Step one is done.

**`hw/rdn_gart.c`**, after Linux's `evergreen_pcie_gart_enable()`,
`_disable()`, `_tlb_flush()` and the page entries of `rs600.c`: a table
of 64-bit little-endian entries in video memory, one a 4 KB page, for
1 GB of GPU address space at 0x40000000 (where Linux has its own). Linux
moves video memory to address 0 first, with the displays stopped; we
leave it at 0xF00000000 and only set the system aperture registers
(0x2034, 0x2038, 0x203c, all zero until now) to that range, so that it
is not looked up in the table. `tests/gart_replay`: the 17 other
register accesses are found in order in phase a1 with Linux's values
(0x1400 = 0x1c203, the seven TLB controls 0x16801b, range 0x40000 to
0x7ffff, context 0x11, the flush), the five that hold our addresses are
exempt, and all 262144 table entries are checked; x86 and PowerPC agree.

**Kext**, only with the boot argument `rdn_gart=1`: bus mastering on,
two pinned pages from `IOBufferMemoryDescriptor` with their bus
addresses from `getPhysicalSegment()` (through the G5's I/O mapper), the
2 MB table from the aperture's allocator, `rdn_gart_enable()`, and a
test: `rdn_ib_selftest()` writes a three-word command buffer into one of
the pages, binds it at GART page 0 and has the command processor run it
from there. If it fails the GART is switched off again.

**On the G5** (restart with `rdn_gart=1`): "GART of 1024 MB enabled",
"dummy page at bus 0x19c000, test page at bus 0x17c000: the GPU ran a
command buffer from system memory". Registers read back as written.
Desktop with Quartz Extreme, `rdnuc alloc`, Quake 3 and Doom 3 `bench`
run as before with it on.

**Not done:** nothing uses it yet. Next: memory in system RAM for
programs (a third kind of allocation through the kext, mapped into the
program and bound in the table), then moving textures there when video
memory is full, with r600 taking the new address.

## 2026-10-06 — Programs' buffers in system memory behind the GART; ultra with 4 samples runs

User's go-ahead for the next step after the GART came up.

**Kext.** `RDN_UC_GART_BIND(address, size)`: a piece of the calling
program's own memory is wired (`IOMemoryDescriptor::withAddress` on its
task, `prepare`), its pages' bus addresses go into the table at a place
from an allocator over the GART's range, and the offset comes back.
`RDN_UC_GART_UNBIND`, `RDN_UC_GART_INFO`; everything a client bound is
unbound when it goes. The memory is the program's, not the kernel's: a
32-bit kernel has little address space to spend.

**Device layer** (`rdn_device_darwin.c`): chunks of 16 MB from
`vm_allocate`, bound with one call each and divided with `rdn_mem`
(`gart_alloc`, `gart_free`, `gart_cpu`). `RDN_NO_GART=1` in a program's
environment turns it off.

**Winsys:** a third place for a buffer besides the aperture and the
memory beyond it. What r600 asks for in the GTT domain alone (vertex and
constant uploads, staging copies) goes there: ordinary cached memory for
the CPU, fetched by the GPU over the bus. And when video memory is full,
a new buffer of any kind goes there instead of the allocation failing.
That is not eviction (nothing is ever moved out of video memory), which
would need r600 to follow a buffer to a new address: sampler views and
surfaces hold addresses they computed. Not attempted.

**My mistake on the way:** the Tiger bundle did not link `hw/rdn_mem.c`,
which only the Linux device had used. The library linked anyway
(undefined symbols are looked up at load on Darwin), the window server
could not load it and crashed in a loop after the restart, and the
monitor showed the kext's colour bars until I had fixed and reinstalled
the bundle and restarted once more. `nm -u` on the bundle for our own
symbols is the check I had skipped.

**On the G5** (`rdn_gart=1`): desktop with Quartz Extreme through the new
bundle, right by readback. Quake 3: 146.8 fps with, 144.3 with
`RDN_NO_GART=1`; a still is right; 2 to 6 MB of it in system memory.
Doom 3 `bench`: 20.7 both ways, 9 to 12 MB in system memory.

**Ultra with 4 samples**, the run that ran out of memory before:

| | bench | bench2 |
|---|---|---|
| Radeon | 20.9 | 21.7 |
| GeForce 6600 LE | 5.0 | 3.6 |

No "out of video memory". At the most: 706 MB beyond the aperture, 185
to 190 MB in it, 7 to 18 MB in system memory, and no buffer had to
overflow: with the upload and staging buffers out of the aperture it
just fits. So the overflow path has not run yet.

**Open.** The overflow path under real pressure. Real eviction. Command
buffers and client storage for the window server in GART memory. The
GART is on only with the boot argument.

## 2026-10-06 — glthread's hand-over: spinning does nothing, larger batches help Doom 3

The user: 20 fps is too low; anything short of Mesa's internals? The
glthread profile had the program's thread waiting 45 % of the time for
room in the queue while the worker waited 30 %, and glthread hands work
over in batches of 8 KB, so a Doom 3 frame is several hundred hand-overs.
Two things tried, both with the user's go-ahead.

**1. A condition wait that spins before it sleeps** (in
`mesa/darwin8`, through our `<pthread.h>` wrapper; no Mesa change):
nothing. Doom 3 20.8 and 21.7 fps without, 20.4 and 21.3 with a budget
of 200 us, 20.2 and 21.5 with 1000 us; Quake 3 the same. So the cost is
not the trip through the kernel to wake a thread. Taken out again.

**2. Larger batches** (`MARSHAL_MAX_CMD_BUFFER_SIZE`, one constant in
glthread.h; `mesa/patches/0004`):

| Batch | bench | bench2 |
|---|---|---|
| 8 KB (Mesa's) | 20.8 | 21.7 |
| 32 KB | 23.4 | 25.6 |
| 64 KB | 23.3 | 26.5 |
| 256 KB | 21.6 | 24.8 |

64 KB is in. Quake 3 does not care (146.8 fps, 167.5 without sound). A
Doom 3 frame and a Quake 3 still read back right with it.

Where the two cards stand on the same scenes, default settings:

| | bench | bench2 | Quake 3 |
|---|---|---|---|
| Radeon | 23.3 | 26.5 | 146.8 |
| GeForce 6600 LE | 26.2 | 18.7 | 127.8 |

Why the larger batch helps when spinning does not is not explained;
fewer hand-overs with the worker's caches staying warm on its own
processor is a guess.

## 2026-10-06 — Doom 3 was drawing the slow way: the extension list was wrong (`bool` again); and four smaller things

**The user:** find another CPU-side optimisation in our driver without
touching Mesa.

**Where the time was** (`sample` of Doom 3 on the save `bench`, 10 s,
glthread on, read with `scripts/sample-profile.py`):

- The program's thread waited 36 % of the time, 30 % of it in
  `_mesa_glthread_finish` under `glBufferData`. glthread records a call
  with its data only if both fit in one batch; a larger one it runs at
  once in the program's thread, after waiting for the other thread to
  finish everything before it. Doom 3 uploads the vertices of its
  animated models that way early in every frame, so once a frame the
  program stopped until the previous frame was drawn. (This is why the
  larger batches of the last entry helped, which that entry could not
  explain: fewer uploads were over the limit.)
- Mesa's thread was busy 87 %. Of that, ours: `__emutls_get_address`
  with what it called about 8 % (`pthread_self` and `pthread_equal`
  through C library stubs, a stack frame, out-of-line register saves);
  the copy of the command buffer at each flush 3.7 % (`__bigcopy`); the
  list of buffers in a command buffer 4 % (a search that fell back to
  walking the list, two reference counts per buffer per flush).

**Four changes in our code.**
1. The entry points for `glBufferData` and `glBufferSubData`
   (`gld/gen_dispatch.py`) hand data larger than the limit over in
   pieces: `glBufferData` without data, then `glBufferSubData` for each
   piece. The front end says what the limit is
   (`OSMesaAsyncDataLimit()`); `RDN_GLD_NO_SPLIT=1` leaves the data
   whole. If such a call is in error, pieces before the error are
   written; whole, nothing would be.
2. `mesa/darwin8/tiger_emutls.c` recognises a thread by its stack
   pointer lying in the stack noted when the thread first came: no call,
   no stack frame, 23 instructions for the first thread and 29 for the
   second. The C library of 10.4 reports 512 KB for the first thread's
   stack; the real size is the process's limit (8 MB), checked on the G5
   with `vmmap`.
3. The winsys finds a buffer in a command buffer's list through a table
   with open addressing that is exact, and a buffer notes the number of
   the last command buffer that used it instead of holding a reference
   to a fence.
4. Tried and taken out: r600 writing its commands straight into the
   command buffer in video memory, so that a flush copies nothing.
   `tools/guest/apcopy.c` shows why it gains nothing: the aperture takes
   720 MB a second whether the stores are 4, 8 or 16 bytes or a
   `memcpy` (1400 to 2000 into ordinary memory; 52 to 208 with the
   kernel's default mapping). The time only moved from the copy into
   the functions that write. Doom 3 sends about 1.1 MB of commands a
   frame on the old path, 1.5 ms of copying; only a command buffer in
   system memory would save that, and the kext takes none there.

Found on the way: `rdn_swap()` in `gld/rdn_mesa.c` had lost the braces
of its loop when the full-screen case went in (661cac2, 2026-10-05), so
the case of a window that is a surface ran once, after the loop, on the
entry past the end of the table. A windowed program's swap has gone to
Apple's engine since then. Braces back. Not tested with a windowed
program yet.

**The extension list.** With those in, the profile had Mesa's thread 95 %
busy drawing, and Doom's own log said why there was so much to draw:
`X..GL_ARB_vertex_program not found`, `R_ARB2_Init: Not available`,
`using ARB renderSystem`. That is the path Doom 3 keeps for cards
without fragment programs: several passes for every light on every
surface. Mesa has the extension. `_mesa_extension_supported()` reads the
context's flags (`struct gl_extensions`, one byte each) through a
`const bool *`, and `bool` has four bytes here, so flag n is looked for
at byte 4n. `tools/guest/glext.c` on the G5: 220 extensions in the list,
14 of them not the context's (`GL_EXT_depth_bounds_test`, which Doom 3
was "using"), and 87 missing, among them `GL_ARB_vertex_program`,
`GL_ARB_vertex_shader`, `GL_EXT_stencil_two_side`,
`GL_ATI_separate_stencil`, `GL_EXT_texture_compression_s3tc`,
`GL_ARB_texture_rectangle`, `GL_ARB_shadow`. Mesa itself asks each flag
by name and works; only what programs are told was wrong, since the
first day.

Our front end now makes the list as Mesa means to (same order) and puts
it where `glGetString()` looks (`osmesa_extension_string()`); Mesa is not
changed. `glGetStringi()` and `GL_NUM_EXTENSIONS` still go Mesa's way.
`RDN_EXTENSIONS=mesa` gives a program Mesa's list. The window server
keeps Mesa's list unless `/Library/Application
Support/RadeonNI/true-extensions` exists: the desktop has only been seen
with that one, and I cannot see it.

**Results** on the G5, 1920x1080, default settings, frames a second
(`d3save.sh`, `td.sh`):

| | bench | bench2 | Quake 3 `four` |
|---|---|---|---|
| Before this entry | 23.3 | 26.5 | 146.8 |
| Changes 2 and 3 (`RDN_GLD_NO_SPLIT=1`, Mesa's list) | 25.7 | 29.0 | |
| and the split (Mesa's list: `RDN_EXTENSIONS=mesa`) | 27.8 to 28.1 | 28.9 to 29.0 | 149.4 |
| and the true list: Doom 3 on its ARB2 path | 47.7 to 49.2 | 50.7 to 50.9 | 148.5 to 149.9 |
| The same without the split | 41.9 | 50.5 | |
| The same without glthread (`RDN_GLTHREAD=0`) | 28.1 | 26.4 | |
| The same with commands written in place (taken out) | 47.8 against 48.1 | 50.7 against 50.4 | |

Doom's log with the true list: `R_ARB2_Init: Available`, `using ARB2
renderSystem`, and `EXT_depth_bounds_test not found`, which is right. It
sends half the commands for the same frames (191 MB against 455 MB for a
run). The GeForce 6600 LE with Apple's driver, which always had the
right list: 26.2 and 18.7.

**Checked by readback** with the installed bundle
(`RadeonNIGLDriver.dylib` d331b142, the one before it kept as
`~/RadeonNIGLDriver.prev9`): a frame of `bench` on the ARB2 path shows
the scene with the bump mapping and highlights the old path did not
have; a still of q3dm1 is as before; `glwin` in its window (the swap
goes our way again) and Apple's Chess with board and pieces are right on
the desktop; `listwin`'s pixels are right. `make test`: 18 PASS.
`nm -u` on the bundle names nothing of ours.

**Where Doom 3 stands now** (the same profile, ARB2 path): Mesa's thread
is busy 94 % of the time and is the limit; the program's waits 26 %,
nearly all of it in `glGetError`, which Doom calls once a frame and
which glthread answers only after the other thread has caught up. Of
Mesa's thread, still ours: the copy of the command buffer 3.2 %,
`__emutls_get_address` 2.5 %, the list of buffers 2 %. Not ours but not
Mesa's either: Tiger's mutexes about 4 % (`__spin_lock`,
`pthread_mutex_lock`), the compiler's out-of-line register saves 5.7 %
(`saveGPR`, `restGPRx`: GCC does that on Darwin at any optimisation
level).

**Not done, not known:**
- The window server has not run this bundle (it starts with it at the
  next restart or login) and by choice keeps Mesa's list.
- Other programs that look at the list (Dashboard, Core Image, iTunes'
  visualiser, QuickTime) now see texture rectangles and vertex programs
  where they saw none. None has been tried.
- The screen saver shows a black screen and draws nothing with the card
  (0 command buffers in 5 s), with the bundle from before this entry and
  with this one, with either list. Found because it covered the desktop
  during these tests; not looked into. Synthetic mouse movement did not
  end it; `killall ScreenSaverEngine` did.
- The fix in Mesa itself is one line (`const GLboolean *` in
  `_mesa_extension_supported()`) and would also put `glGetStringi()`
  right. Not made: the user's rule for this work was no change to Mesa.
- The user has seen none of this.

## 2026-10-06 — The extension list fixed in Mesa; glthread for every program; an audit for more of the kind

**The user** saw Doom 3 on its ARB2 path: "looks even better, some
lightning that was funky before is correct now." Their decisions: make
it the default; the optimisations, glthread included, for all
applications; the proper fix in Mesa, through a patch; and a quick check
for other places where `bool` having four bytes, or anything like it,
could cause problems.

**The fix in Mesa:** `mesa/patches/0005-extension-flags-bool-size.patch`,
one line in `_mesa_extension_supported()` (`const GLboolean *`). The
front end's own list, `RDN_EXTENSIONS` and the `true-extensions` file of
the last entry are gone. On the G5 `glext` gets 293 extensions from Mesa
itself; `glGetStringi()` and `GL_NUM_EXTENSIONS` are right with it. The
window server will get the true list too when it next starts.

**glthread by default** (`osmesa_want_glthread()`): every program except
the window server, which finishes every update before it goes on and
has the desktop to lose. `RDN_GLTHREAD` in the environment as before;
the list file now takes `name` (on), `-name` (off), `*` and `-*`. What
went with it:
- A context that stops being a thread's current one is finished first
  (`osmesa_sync_switch()`).
- Where a context's picture is copied to memory the program or the
  engine reads (an off-screen drawable, a window that is no surface),
  `glFlush` is `glFinish` (`rdn_flush_waits`): with glthread the copy
  would otherwise be made some time after the call returned.

**The audit** (the Mesa that goes into the bundle: `src/mesa`,
`src/compiler/{glsl,nir}`, `src/util`, gallium's auxiliary code, r600).
Looked for, by pattern and where possible by having the Tiger compiler
check sizes:
- Casts to `bool *`: seven, and only the one in `extensions.c` reads
  something that is not a `bool`.
- Fields `glGet*` reads by offset with a fixed size (`get.c`): 480
  checked with `_Static_assert` under the Tiger compiler. Four context
  fields are `bool` and are read as one byte, so their queries always
  answer false here: `GL_BLEND_ADVANCED_COHERENT_KHR`,
  `GL_PRIMITIVE_RESTART_FOR_PATCHES_SUPPORTED`,
  `GL_SUBGROUP_QUAD_ALL_STAGES_KHR`,
  `GL_SPARSE_TEXTURE_FULL_ARRAY_CUBE_MIPMAPS_ARB` (and four mesh shader
  capabilities this card lacks). OpenGL 4 questions that no program for
  Tiger asks; not patched. One more that is wrong on any big-endian
  machine, not only here: `GL_CONTEXT_ROBUST_ACCESS` reads a one-byte
  field as two.
- Unions that lay a `bool` over other fields: the vertex format in
  `glthread.h` (patch 0002) is the only one that reads the fields as one
  integer. `nir_const_value`, `ir_constant_data` and
  `pipe_query_result` are read by the member that was written.
- Structs of small fields under one integer (the pattern of 0002): all
  others are made of `unsigned` bit-fields.
- `bool` arrays allocated, cleared or copied by a byte count: none.
- The other tables of extension flags (`st_extensions.c`, the overrides,
  `check_extra()`) use `GLboolean`.
- Not checked: code we do not build (other drivers, Vulkan, the video
  state), and layouts that differ here without anything reading across
  them (`bool` bit-fields make some structures larger).

**On the G5** with the bundle f4d22db0 (the one before as
`~/RadeonNIGLDriver.prev10`), by readback: Doom 3 48.6 and 50.6 fps,
ARB2; Quake 3 149.7; frames of both right. `glwin` gets glthread without
being named anywhere (607 frames a second in its window against 543
without). Chess, TuxRacer's first screen and `listwin`'s pixels are
right. A window copied through memory (`RDN_GLD_NOSURFACE=1`) is right
with glthread; a grab of it shows the turning triangle bent, with
glthread or without, because the grab takes longer than a frame.
Dashboard opens and shows its widgets; no crash log.

**Not done:** the window server has not started with any of this; the
user has to be there for that. The screen saver's black screen is as it
was.

## 2026-10-06 — The window server with the true extension list

With the user's go-ahead, `sudo killall WindowServer` on the G5 (nothing
open there but the Finder). Back in ten seconds, logged in by itself,
the same process afterwards (no restart loop), `qe`: Quartz Extreme in
use, no crash log, nothing but its start-up lines in
`windowserver.log`. By readback: desktop, menu bar, Dock and Finder
windows with their shadows are right; Apple's Chess and `glwin`, both
with glthread, are composited right over them; the picture is right
again after a mouse drag. The window server itself runs without
glthread, as intended.

Left on the screen by the system, not by us: the Keyboard Setup
Assistant, which Tiger starts at login when it cannot tell what keyboard
is attached (there is none). A `drag` I started inside a Finder window
selected its icons; nothing was moved.

**Not seen by the user yet;** Exposé, Dashboard's effects and anything
moving are not covered by a grab.

## 2026-10-06 — The GART is on by default

**The user** looked at the desktop with the true extension list (dragging
windows, Exposé, Dashboard): "it all looks good." Then: enable GART by
default.

The kext starts the GART unless `rdn_gart=0` is a boot argument (it was
the other way round); the package's README says how to leave it off and
what to do if a Mac does not come up with it. Nothing else changed: the
self-test, and switching it off again if that fails, are as they were.
That failure path has never run on any machine.

**On the G5**, with the user's go-ahead for the restart: package
`working-quartz-extreme-84-g293a6b0` installed as before (`--accel
--hwcursor`; the kext before it kept as `~/RadeonNI.kext.prev`),
`boot-args` deleted (it held `rdn_gart=1`), restarted. Back in 70
seconds. Kernel log: "GART of 1024 MB enabled at GPU address
0x40000000", "the GPU ran a command buffer from system memory". Clocks
649.96 / 800.00 MHz by themselves. Desktop right by readback, Quartz
Extreme in use. Doom 3 48.8 and 51.1 fps with 9 MB of its buffers in
system memory, Quake 3 149.7.

**Not done:** the QEMU guest has never run the GART (no card there now);
`TIGER_BOOTARGS=rdn_gart=0` would leave it off.

## 2026-10-06 — The VBIOS is read from the card's ROM; the G5 runs without the file

**The user** asked whether the build reads the VBIOS at run time (it did
not: the image in the personality was the only source, and `rdn_romtest`
had never run), then to find out on the G5 whether it can be read and to
make the changes if so.

**What the G5 said without anything being loaded:** the device tree's
`assigned-addresses` has an entry for register 0x30 (128 KB at
0x80120000, next to the register BAR at 0x80140000); `BUS_CNTL` (0x5420)
reads 0 on the running card, so the ROM is not switched off
(`BIOS_ROM_DIS` is what Linux's `ni_read_disabled_bios()` works around);
the kernel exports `ml_probe_read()` to kexts linked against the 6.0
libraries, as it does in the guest. That function reads a word at a
physical address and returns false where a plain read would be a machine
check; the header that declares it is not in Kernel.framework for
PowerPC, so the kext declares it itself.

**A probe instead of `rdn_romtest`** (`kext/RomProbe`, built in the guest
like the driver, tried there first: with no card it only loads, finds
nothing and unloads). `rdn_romtest=1` would have needed a restart and
read the ROM unguarded, on a Mac with no keyboard to recover with. The
probe is loaded by hand on the running G5, finds the card's
`IOPCIDevice`, turns on the ROM's decoding, reads the first word with
`ml_probe_read()`, then the whole BAR through a mapping and again word by
word, puts the BAR back and publishes the bytes in the registry. On the
G5: command 0006, BAR 80120000, first word answered `55aa80e9`, image of
65536 bytes with checksum 0, no difference between the two ways of
reading; the 163840 reads took about two seconds. Read out with `ioreg` and
compared on the host: the first 64 KB are `private/vbios.rom` byte for
byte (SHA-256 `591e5d5d...`), one x86 image with the last-image flag,
and the second 64 KB of the BAR are zeros. The screen did not change and
the driver was not touched.

**The kext** (`RadeonNI::loadBios()`): the ROM first (`biosFromRom()`),
then the personality's image (`biosFromPersonality()`, the old code).
The ROM read needs an address in the BAR and an `IODeviceMemory` for it,
reads every word with `ml_probe_read()`, and wants the signature, a
length that fits the BAR and a checksum of zero; any failure is one log
line with the reason and the fallback. An image the AtomBIOS parser
rejects is also given up for the next source. When the ROM is used and a
file is there too, the log says whether they are the same.
`rdn_rom=0` as a boot argument leaves the ROM alone; `rdn_romtest` and
`compareRom()` are gone.

**The installer** (`g5/install.sh`): the VBIOS file is optional. With one
(argument, or `vbios.rom` beside the script) it goes into the kext as
before, now as the fallback. Without one the script looks the card up in
the device tree by its `compatible` list (the node is `pci1028,2b20`
here, which the old `grep pci1002,675d` does not find) and refuses to
install if Open Firmware gave register 0x30 no address. Rehearsed in the
guest (no card: "absent", a note, no failure) without and with a file and
with a missing one; the kext was removed again with `kext.sh uninstall`.

**On the G5**, four restarts, each back on ssh in under a minute, each
with `install.sh --accel --hwcursor` from the installed package with
only the kext, the installer and the README replaced:
1. With the file inside: "VBIOS, 65536 bytes from the card's ROM", "the
   personality's VBIOS is the same as the ROM", then "card is not posted
   on entry" and everything as before. So the cold card's ROM answers and
   is the same image.
2. Without the file (no `VBIOS` key in the installed `Info.plist`): the
   same lines minus the comparison; POST, 1920x1080, 3D engine, GART
   self-test, 649.96 / 800 MHz, hardware cursor, Quartz Extreme in use.
3. `boot-args="rdn_rom=0"` and the file inside: "the card's ROM is left
   alone (rdn_rom=0)", "VBIOS, 65536 bytes from the personality",
   Quartz Extreme in use.
4. Boot arguments deleted, without the file again: as 2. The desktop is
   right by readback (`rdnuc grab`).

**How the G5 is left:** kext `efb70d8a...` from commit b553466, no VBIOS
file in it, no boot arguments. `~/RadeonNI-g5` is the package it was
installed from (no `vbios.rom`); the one before is `~/RadeonNI-g5.prev5`
(with `vbios.rom`), and the last kext that carried the VBIOS is
`~/RadeonNI.kext.before-rom`.

**Not done, not seen:**
- The failure paths of the ROM read (no answer, a machine check, a wrong
  signature or checksum, no address) have never run anywhere: the ROM
  answered every time. Under QEMU "no address" is what will happen; the
  card is not in the host now, so that was not run either.
- The user has not looked at the G5's screen; nothing about the picture
  should differ, and the grab does not.
- `build/RadeonNI-g5.tar.gz` on the host was not rebuilt
  (`scripts/make-g5-package.sh`, now without `--with-vbios` for a package
  that carries no VBIOS).
- Other cards and other Macs: a card whose ROM holds an Open Firmware or
  EFI image first, or a Mac that assigns the ROM no address, would take
  the fallback and need a file.

## 2026-10-06 — An archive to hand to other people (`scripts/make-dist.sh`)

**The user** wants to distribute the project so that someone with a
compatible card can install it on a scratch 10.4.11: a script that builds
everything and makes a zip with the installer, a copy of `README.md` and
the licences.

**The script:** `scripts/make-dist.sh` runs `make-g5-package.sh` (kext and
2D plug-in built in the guest, the OpenGL bundle cross-built on the host:
Mesa 26 cannot be built with Tiger's gcc 4.0.1, so "in the guest" holds
for two of the three), then adds `README.md`, renames the package's
`README.txt` to `INSTALL.txt`, adds Mesa's `docs/license.rst` as
`LICENSE.mesa` with its `licenses/` directory, checks that every piece is
there and that no VBIOS is, and writes
`build/RadeonNI-<date>-<commit>.zip` with Python (the host has no `zip`),
keeping the executable bits. `make-g5-package.sh` got `--keep-gl` (pack
the bundle as last built), which `make-dist.sh` passes on.

**What a scratch install has:** checked on the G5 against the installer
receipts (`lsbom`): every command `install.sh` and `uninstall.sh` call,
perl's `MIME::Base64` included, belongs to BaseSystem, BSD or Essentials,
none to the developer tools. The OpenGL bundle links libz, IOKit and
libSystem, the plug-in CoreFoundation, IOKit and libSystem. Not checked on
a Mac that really has no Xcode: both Tiger systems here have it.

**The package's text** (`g5/README.txt`) said it had never run on a real
Mac and had no acceleration; rewritten to what is true now (one G5, one
card, one monitor), with `--accel --hwcursor` as the install to use and
Remote Login as the way back in.

**On the G5:** the zip (5.1 MB, about 22 MB unpacked) unpacked with `unzip` and
with `ditto -x -k` (what a double-click does), identical both ways,
scripts executable, the three binaries byte-identical to the installed
ones (the guest rebuilt the kext to the same bytes). Installed from the
unpacked folder with `sudo sh ./install.sh --accel --hwcursor`, restarted:
VBIOS from the card's ROM, Quartz Extreme in use.

**Not done:** run from the worktree, so only with `--keep-gl`; the path
that rebuilds Mesa is `make-g5-package.sh`'s old one, unchanged. No other
Mac, no other card, no system without Xcode. The compatibility list the
README mentions does not exist: the kext matches `1002:675d` only.

## 2026-10-07 — DisplayPort: what bringing it up needs (research, no code)

**The user** moved the G5's monitor from the DVI-I connector to the
DisplayPort one and asked how to bring that up. The G5 is now
192.168.1.127.

**On the G5** (read over ssh, nothing changed there): booted 08:24, VBIOS
from the ROM, POST, then "no EDID on the DVI connector (-6)" and the kext
stops. No accelerator service, so `rdnuc reg` cannot read the hot-plug
registers; `qe` reports one 1024x768 display without Quartz Extreme.

**From the VBIOS** (`docs/HARDWARE.md`, "DisplayPort connector"):
UNIPHY2 link A, AUX instance 2 behind I2C id 0x92, HPD4, no external DP
clock (DCPLL is the reference), `DIGxEncoderControl` and the transmitter
table at 1.4.

**From Linux** (`atombios_dp.c`, `radeon_dp_auxch.c`, `atombios_encoders.c`,
`atombios_crtc.c`, all MIT-headed), what a DisplayPort monitor needs beyond
what `hw/rdn_modeset.c` does for DVI:
1. Topology from the connector in use instead of constants: transmitter
   UNIPHY2 (`ucTransmitterSel` 2), digital encoder 4, HPD4, connector id
   0x13 in the transmitter's INIT.
2. Detection: `DPEncoderService` GET_SINK_TYPE with 0x92 tells a
   DisplayPort sink from a passive DVI/HDMI adapter; HPD4 sense.
3. AUX: on DCE5 Linux uses the registers directly (0x62a0 block, at most
   16 bytes a transaction, pad switched to AUX with bit 16 of 0x6450),
   not the VBIOS table. DPCD read of 15 bytes at 0; EDID as I2C over AUX
   (address 0x50, with defer and retry handling, which Linux has in
   `drm_dp_helper.c`, not in radeon).
4. Link choice: lowest rate, then fewest lanes, that carries the mode at
   24 bpp. 1920x1080 at 148.5 MHz: 4 lanes at 1.62 Gbit/s.
5. Clock: PLL id DCPLL instead of PPLL1; `AdjustDisplayPll` gets the link
   clock and coherent mode; encoder mode DP (0), lane count and link rate
   in `DIGxEncoderControl`, DCPLL as the transmitter's reference clock,
   link clock as its "pixel clock".
6. After the transmitter's ENABLE: link training (sink power D0,
   downspread, lane count, rate; pattern 1 with voltage swing loop,
   pattern 2 or 3 with equalisation loop; each step is a
   `DIGxEncoderControl` action plus a DPCD write, voltage and
   pre-emphasis through the transmitter's SETUP_VSEMPH), then
   `DP_VIDEO_ON`. No HDMI infoframe.

With a passive DisplayPort to DVI/HDMI adapter instead, only 1 and 2
apply, with DDC as plain I2C on the 0x6450 pads.

**Not possible as before:** validating against the reference trace. It has
no AUX traffic and no training, and the card is in the G5. A new trace
needs the card in the host with the monitor on DisplayPort.

## 2026-10-07 — DisplayPort written in `hw/`, not yet run on the card

**The user** asked for DisplayPort without a new Linux trace; the monitor
is on a real DisplayPort cable.

**Written** (commit b2128aa):
- `hw/rdn_dp.c`: the AUX transaction through the card's registers, DPCD
  read and write with retries, the sink's capabilities, the EDID as I2C
  over AUX, lane count and rate, link training. Ported from Linux's
  `radeon_dp_auxch.c` and `atombios_dp.c`.
- `hw/rdn_modeset.c`: the topology constants became a table of two
  outputs (DVI-I, DisplayPort) and `card->output`. With a DisplayPort sink:
  encoder mode DP, the DCPLL as PLL id and as the transmitter's reference
  clock, the link clock in `AdjustDisplayPll` and the transmitter calls,
  digital encoder 4, `DP_VIDEO_OFF` before the transmitter is disabled,
  training and `DP_VIDEO_ON` after it is enabled. `rdn_display_init()`
  now initialises both transmitters, DisplayPort's first, as Linux does:
  its parameters (`13 00 82 07 04`) are in the reference trace's list of
  AtomBIOS calls and ours are the same.
- `rdn_output_detect()`: DVI-I first, then DisplayPort; the kext and
  `rdn_tool modeset` use it. On DisplayPort it logs what `DPEncoderService`
  says the sink is and tries AUX whatever the answer.

**Differences from Linux, chosen:** no spread spectrum on the link clock
(the VBIOS lists 0.38 % for DisplayPort; the sink is told there is none);
5.4 Gbit/s never chosen; the sink is not sent to D3 before a mode set;
8 bits a colour whatever the EDID says.

**Checked without the card:** `make test` passes as before for DVI (same
digests; the display init phase still matches with the second transmitter
INIT). New `tests/dp_link`: a model of the AUX registers with a sink
behind it (lost first transaction, every third deferred, short I2C reads,
training that needs one adjustment per phase); same output on x86 and
under `qemu-ppc`. The model is my reading of Linux's code, so it proves
the logic and the byte order, not the registers.

**Not run:** anything on the card. The kext builds in the guest
(`build/RadeonNI-dp.kext.tar` on the host, built from b2128aa). Copying
it to the G5 and installing it was refused by the session's permission
rules and is left to the user.

**What I expect can go wrong on the first run**, in order: the AUX
channel not answering (pad or hot-plug setup), `SetPixelClock` with the
DCPLL doing something to the display engine clock, training failing at
4 lanes of 1.62 Gbit/s. The kernel log has a line for each.

## 2026-10-07 — DisplayPort works on the G5, first run

**Installed** on the G5 (the user allowed it): the kext from b2128aa
(`5f1bc7a6...`) into `~/RadeonNI-g5`, `install.sh --accel --hwcursor`,
restart. The kext before it is `~/RadeonNI.kext.before-dp` there.

**Kernel log of that boot:**
- "DVI-I: no EDID (-6)", then "the BIOS reports sink type 0x13"
  (DisplayPort).
- DPCD on the first try, hot-plug high: `12 14 c4 81 01 00 01 80 02 02 06
  00 00 00 81`. DisplayPort 1.2, up to 5.4 Gbit/s, 4 lanes, enhanced
  framing and training pattern 3, downspread capable.
- EDID 256 bytes over AUX, one mode: 1920x1080 at 148.5 MHz.
- "PLL 270000 kHz, fb 80.0 ref 2 post 4": what `AdjustDisplayPll`
  returned for the link clock, sent to `SetPixelClock` with the DCPLL.
- "DisplayPort link 4 lanes at 162000 kHz: trained, voltage 0,
  pre-emphasis 0, status 77 77 81": both phases passed with the first
  drive setting, no adjustment asked for.
- Then as on DVI: 3D engine, GART self-test, 649.96 / 800 MHz, hardware
  cursor.

**After boot:** Quartz Extreme in use at 1920x1080, the registry property
`Output` is "DisplayPort", the GPU completes command buffers, `rdnuc
grab` shows the desktop. **The user** looked at the monitor: "It looks
about correct".

**Not done, not seen:**
- Only this monitor, this mode, 4 lanes at 1.62 Gbit/s. 2.7 Gbit/s, fewer
  lanes, and a sink that asks for more voltage or pre-emphasis have run
  only in `tests/dp_link`.
- Unplugging and replugging, monitor sleep and wake: the link is trained
  once per mode set and nothing watches the hot-plug line, so a monitor
  that loses the link stays dark until the next mode set.
- A mode change from System Preferences on DisplayPort (the EDID has one
  mode), other depths.
- What `SetPixelClock` with the DCPLL does to the display engine clock:
  the picture and the watermarks look right, nothing was measured.
- The link's state cannot be read from user space; the log line at mode
  set is all there is.

## 2026-10-07 — Is Quartz Extreme a stub? An audit, and a profile of the window server

**The user** remembered reading that Quartz Extreme was only a stub and
accelerated nothing, and asked for a check and a plan for real Quartz
Extreme and Core Image.

**Where the statement is:** `README.md`, "A 2D accelerator plugin that
enables Quartz Extreme. Currently a CPU-only stub." It is about
`ga/RadeonNIGA.plugin`, whose fill and copy use the CPU and which exists
to pass the window server's first gate. Nothing says it of the
compositing.

**On the G5** (kext and bundle as installed, DisplayPort, 1920x1080):
- Idle desktop, fence counter read every 5 s for 65 s: no command buffer
  except 2 at 08:58:00, when the menu bar clock changed (the kext logged
  `setShape` with bounds 1772,0 98x22 for it).
- A 700x500 Finder window dragged back and forth for 10 s (`drag`, 40 ms
  a step) with `sample WindowServer 12 1`: 259 command buffers in 10 s.
  Of the window server's main thread, 92.4 % is waiting for messages in
  its server loop, 4.7 % `CGXUpdateDisplay`, of which 3.1 % is the
  compositing through GL (`CGGLAccelComposite`: Mesa's immediate mode
  path, binding textures, texture environment). `glTexSubImage2D` 0.25 %,
  `glFlush` with the copy to the screen 0.15 %. No `glFinish` and no fence
  wait anywhere: the window server calls `glFlush` (from
  `CGXGLAccelFinish`) and never comes through `rdn_mesa_present()`.
  2.3 % was `CGXSetDisplayTransferByFormula`, taken to be the fade after
  the screen saver was killed for the test.
- The surface client's methods in the logs since 2026-10-05: `setShape`
  22687, `control` 1820, `flush` 512, read locks 214, `setIDMode` 170;
  `setScale`, `setShapeBacking`, write locks and `read` never; no
  private method asked for. `windowserver.log`: "Accel caps: 00000003".

**So:** the compositing is done by the card and costs the window server
little. What uses the CPU around it: window contents (Quartz 2D, as on
any Tiger Mac), the copy of changed window tiles into textures (small
in this test), the 2D plug-in if it is called at all (not known).

**Wrong in my first reading:** that every update waits for the GPU. The
code that waits is not reached by the window server. A switch for it
(`/tmp/rdngld.wsflush`) was written and reverted unused.

**Core Image** has not been run. By reading: a pbuffer, or any drawable
that is not off-screen, a window, a surface or the whole screen, leaves
the Mesa context bound to a 16x16 dummy; Mesa contexts are never shared;
one context is current per process; 63 Apple-only entries stay with
Apple's engine.

**Not measured:** Quartz Extreme off for comparison; Exposé; larger
windows; what "a bit slow" (the user, 2026-10-05) was.

## 2026-10-07 — Round 1 for Quartz Extreme and Core Image: written and built, not yet run on the card

**How it was made.** Four subagents in worktrees, in parallel (the user's
wish), each with one package; all four stopped at the session's usage
limit with their work part done, and I finished the packages one after
the other. Branch `qe-ci-round1`.

**What there is now, all off or silent by default:**
- `tools/guest/qebench.sh`: window moves, drags, Exposé and idle, ten
  seconds each, with the window server's CPU time, GPU command buffers,
  a `sample` profile and the 2D plug-in's call counts. `fences now`.
- The 2D plug-in counts its blitter calls with `/tmp/rdnga.on`
  (`/tmp/rdnga.stats`); the surface client counts its calls per `control`
  selector and `flush` options.
- `tools/guest/ciprobe.m`: CGL pbuffers directly, Core Image on a CGL
  context of a given renderer, Core Image's software renderer, and a
  comparison of two pictures.
- The bundle's log has the thread in every line, dumps for shared
  contexts and unknown drawables, and, while logging, counts every call
  of the GL entries Mesa lacks and Apple's engine keeps.
- Front end: `OSMesaMakeCurrentStore` (a context draws into video memory
  the caller names: a pbuffer) and `OSMesaTexStoreImage` (that memory as a
  2D or rectangle texture, with alpha if asked, no copy); share lists
  checked. `rdn_gltest -P` tests them, and with `RDN_SOFT=1` on Linux runs
  without the card on softpipe (`mesa/tests/rdn_soft.c`).
- `rdn_blit_fill()` and `rdn_blit_move()` in `hw/`, the user client
  methods `RDN_UC_SCREEN_FILL` and `RDN_UC_SCREEN_COPY`, and with
  `/tmp/rdnga.gpu` the 2D plug-in fills and copies with the GPU.
  `tools/guest/gablit.c` calls the two methods directly.

**Checked:** `make test` passes, with the new `blit_ops` (54 cases, same
digest on x86 and PowerPC). `rdn_gltest -P` on softpipe: all five checks
pass on x86 with glthread on and off, direct and copying, and big-endian
under `qemu-ppc`. The Tiger bundle builds and `nm -u` names nothing of
ours. Kext, plug-in, `ciprobe`, `gablit` and `fences` build in the guest
with Apple's gcc; `ciprobe soft` runs there (Core Image's software
renderer: 2.5 s the first picture, 347 ms each after, under emulation).

**Not done:** nothing of this has run on the card or been installed on
the G5. The Tiger build of `rdn_gltest` does not link at -O2 ("bl PPC
branch out of range", the program is over 16 MB); not looked into, so
`-P` on r600 waits for that. The bundle side of pbuffers and shared
contexts is round 2 and needs what `ciprobe` shows first.

## 2026-10-07 — Core Image on the card, first run: a black picture; pbuffers refused before the driver

**On the G5**, with the driver as installed before round 1 (bundle
f4d22db0), `ciprobe` built in the guest:
- `ciprobe soft` (Core Image's software renderer, `CIGaussianBlur` on a
  512x384 generated picture): 125 ms the first render, 8.3 ms each after.
  The picture is right (blurred checker over a gradient).
- `ciprobe gl 0x21a00` (a `CIContext` on a CGL context of our renderer,
  off-screen drawable): CGL gives our renderer (accelerated, off-screen,
  pbuffer capable by its own account), `GL_RENDERER` is Mesa's AMD TURKS,
  no error, 101 ms then 10.6 ms a render. The picture is black except
  for a 16x16 square in the bottom left corner: 196352 of 196608 pixels
  differ from the software picture. Sixteen pixels a side is the dummy
  drawable `rdn_make_current()` binds a context to when it has none it
  knows.
- `ciprobe pbuffer 0x21a00`: `CGLCreatePBuffer` succeeds,
  `CGLSetPBuffer` fails with `kCGLBadEnumeration` (10010). No
  `gldAttachDrawable` reaches the bundle for it: CGL or the engine
  refuses first.

**So** Core Image does choose the card and gets nothing usable from it:
it is not falling back to software, it is drawing wrong. What reading
the code said (journal, audit entry above) holds.

**Not done:** the round 1 driver is not installed. Copied to the G5
(`~/RadeonNI-g5.r1`, tools in `~/gl`, the installed set saved as
`~/RadeonNI-g5.before-round1`); running `install.sh` and restarting was
refused by the session's permission rules. So the fuller log (which
context Core Image draws in, shared contexts, the engine-kept entries),
`gablit`, `qebench` and the Quartz Extreme off comparison have not run.
Why `CGLSetPBuffer` is refused is not known.

## 2026-10-07 — Round 1 on the G5: GPU fill and copy right; what Core Image really does; one crash of ours fixed

**Installed** on the G5 with the user's go-ahead: the round 1 package
(`~/RadeonNI-g5.r1`, `install.sh --accel --hwcursor`), restart; Quartz
Extreme in use. The set from before is `~/RadeonNI-g5.before-round1`.

**GPU fill and copy** (`~/gl/gablit 100 300`, by readback): the ground
and the four squares have their colours, the red and green pair copied 30
pixels right and down over itself is right. `RDN_UC_SCREEN_FILL` and
`RDN_UC_SCREEN_COPY` work on the card. The plug-in's own use of them
(`/tmp/rdnga.gpu`) has not run.

**A crash of ours.** With the bundle's log on, `CGLCreateContext` crashed
in `cglAssignDispatch` at an address that is a PowerPC instruction: the
new counting wrappers were put into the program's table while the context
was still being made (the early takeover), and the engine then followed
one as data. Fixed: wrappers only go into the table of a context that has
a drawable (`rdn_kept_now`). Without the log nothing was wrong. Bundle
39a16a74 on the G5 (`~/RadeonNIGLDriver.r1b`, copied over the installed
one).

**What Core Image does in `ciprobe gl`** (every GL call traced):
- One context, one thread. No second context, no pbuffer, no framebuffer
  object, no fragment program, no `glGetProgram*`.
- It asks `glGetString` for vendor, renderer, version and extensions,
  then filters on the CPU and gives GL the finished picture: a 512x384
  `GL_TEXTURE_RECTANGLE` (`GL_BGRA`, `GL_UNSIGNED_INT_8_8_8_8_REV`, row
  length 528, client storage on, `glTextureRangeAPPLE` on the same
  memory, storage hint 0x85bf), drawn as one quad under
  `glOrtho(0, 512, 0, 384)`; `glFinishObjectAPPLE(GL_TEXTURE, id)`
  around it.
- So on this card Core Image decides against hardware filtering from
  the strings or the renderer, before trying anything. Which of them is
  not known.
- The quad comes out black but for 16x16 pixels at the bottom left, which
  hold the right corner of the picture: the off-screen context still
  clips to the dummy drawable it was bound to before its own existed.
  `glprobe draw 0x21a00` on the same bundle leaves its buffer all zero.
  Not looked into further; whether it is new with this bundle is not
  known (the bundle before drew the same black picture for `ciprobe`).
- Apple-only entries used: `glTextureRangeAPPLE`, `glFinishObjectAPPLE`.

**Still open from this run:** `ciprobe gl` crashes in `CGLDestroyContext`
with the log on (address 0x39290001; not looked into). The Tiger build
of `rdn_gltest` does not link.

**Not run:** `qebench`, the plug-in's counters, the window server's log,
Quartz Extreme off. Restarting the window server and running the
benchmark as root were refused by the session's permission rules.

## 2026-10-07 — Quartz Extreme on against off, measured on the G5

`~/gl/qebench.sh` (results in `~/qebench` on the G5), 1920x1080, a
600x400 Finder window, ten seconds a phase, `sample` every 10 ms in both
runs. On: the round 1 driver with `--accel --hwcursor`, the window
server logging its `gld*` calls. Off: the same package installed without
`--accel` (a restart each way).

| Phase | | Quartz Extreme on | off |
|---|---|---|---|
| AppleScript moves in 10 s | moves done | 4763 | 1335 |
| | window server CPU | 5.87 s | 10.04 s |
| | GPU command buffers | 5224 | - |
| 8 drags of 300,120 pixels | window server CPU | 1.38 s | 5.29 s |
| | GPU command buffers | 640 | - |
| Exposé in and out, 4 times | window server CPU | 1.61 s | 3.49 s |
| | GPU command buffers | 670 | - |
| Idle | window server CPU | 0.02 s | 0.02 s |

So with the card compositing, the window server moves a window 3.6
times as often with 0.6 of the CPU time, and a paced drag costs a
quarter.

**Also seen with Quartz Extreme on:**
- The 2D plug-in's blitters were not called once (fill, copy, copy
  region: 0). The plug-in is only the gate; its GPU path
  (`/tmp/rdnga.gpu`) has nothing to speed up while Quartz Extreme is on.
- The window server's surface client: `setIDMode` once and `setShape`
  6666 times, nothing else, with no program's GL window open.
- Of the entries Mesa lacks, the window server calls
  `glTextureRangeAPPLE` and `glTestObjectAPPLE` (55 wrapped).
- It never comes through `rdn_mesa_present()`.

The accelerated driver is installed again (bundle 39a16a74), Quartz
Extreme in use, no switch files.

## 2026-10-07 — Call of Duty 2 starts; World of Warcraft's models; the log no longer crashes programs

All on the G5, by readback. Bundle 49733cbc installed (the one before is
`~/RadeonNIGLDriver.before-cod2`). A power cut restarted the host and the
G5 in the middle; the window server there now runs this bundle too, Quartz
Extreme in use.

**Call of Duty 2 Demo** quit by itself with status 0, after
`gldGetRendererInfo` and nothing else. Read from its display detection
(`CDisplayInfo`): it takes a display only if a renderer is accelerated,
full screen, has more than 64 MB, 8-bit stencil and the 24-bit depth mode
(0x800). Ours had the software renderer's depth modes, 0x1000 only. With
the mode added it went on to `aglChoosePixelFormat` with
`AGL_AUX_BUFFERS 2` and `AGL_AUX_DEPTH_STENCIL`, got nothing, and crashed
on its own null device. `tools/guest/aglfull.c` reproduces the request;
flipping bits of our own format's word 2 (`RDN_GLD_PF`) found bit 11 to be
that attribute. Both are in the bundle now: the game reaches its main menu
from a plain launch, 5246 command buffers in 5 s. Not seen: anything past
the menu (its pointer takes relative motion and `~/gl/drag` could not
click "Play Demo"). Mesa has no auxiliary buffers (`GL_AUX_BUFFERS` 0)
though the format says 4; whether the game draws into them is not known.

**The log crashed programs** in `aglSetFullScreen` and `aglSetInteger`, at
an address that is a wrapper's first instruction (also this morning's
`ciprobe` crash in `CGLDestroyContext`). Bisected with the new
`RDN_GLD_KEPT=first-last`: the one wrapper that does it is the table's
last entry, `buffer_parameteri_APPLE`. The engine keeps data in that
word. Not wrapped any more, nor `pad`.

**World of Warcraft 1.12.1** (5875) showed its login screen without the
portal and, the user says, the world without characters, creatures and
trees. With the log on: it calls `glWeightPointerARB(4, GL_UNSIGNED_BYTE,
48, 12)`, which Mesa does not have (no `GL_ARB_vertex_blend` at all; the
extension is not in our list either, the game does not ask), and 4 of its
29 ARB programs failed to compile at `ATTRIB v17 = vertex.weight;`
("unexpected MASK1": Mesa's grammar has no `vertex.weight`). Those are
the programs for models. The bundle now logs every program's text and
Mesa's answer.

Fixed in the bundle (`gld/gen_dispatch.py`), Mesa untouched: the vertex
program extension defines `vertex.weight` as generic attribute 1, so the
text is rewritten to `vertex.attrib[1]`, `glWeightPointerARB` is
`glVertexAttribPointerARB(1, ...)` with integers normalized, and
`GL_WEIGHT_ARRAY_ARB` in the client state calls is attribute array 1. All
29 programs compile and the login screen has its portal, statues and sky.
Not done: `glWeight*vARB`, `glVertexBlendARB`, blending without a vertex
program; the game called none of them up to the login screen. Not seen:
the world (it needs the user's login). The four programs seen declare
`vertex.weight` and never read it.

## 2026-10-07 — World of Warcraft is slow: glMapBuffer waits for the GPU

The user: the world looks correct, a bit slow. `sample` of the running
game for 10 s (G5, 1920x1080, Northshire): the process uses 30 % of a
CPU; 82 % of its main thread is under `glMapBuffer`, 68 % of it in
`r600_buffer_map_sync_with_rings` -> `rdn_buffer_map` ->
`rdn_number_wait` (waiting for the GPU) and 13 % waiting for glthread.
No `glBufferData` in the sample: the game maps buffers the GPU is using
and does not replace them first. 900 command buffers a second, most of
them the flush before each wait.

The game's binary names `GL_APPLE_flush_buffer_range`, the extension that
tells the driver not to wait. Tiger 10.4.11's libGL has neither of its
two functions, so offering it here would do nothing; the SDK header's
last two table entries are for a later system (which is why the engine
has data where `buffer_parameteri_APPLE` would be).

**Built:** `RDN_MAPBUFFER=unsync|discard|sync` in a program's environment,
or a line `unsync Name` in `/Library/Application Support/RadeonNI/mapbuffer`,
makes `glMapBuffer(GL_WRITE_ONLY)` a `glMapBufferRange` of the whole
buffer with `GL_MAP_UNSYNCHRONIZED_BIT` (or `INVALIDATE_BUFFER`). The
default is Mesa's. Bundle 4acbb66d on the G5, with the file naming World
of Warcraft. **Not run yet:** the game was the user's running session;
whether unsync is right for it (it is if the game only adds to a buffer
between replacements) and what it gains is not known.

## 2026-10-07 — World of Warcraft: glMapBuffer with new storage, 30 to 188 frames a second at the login screen

**unsync was wrong.** The user started the game with `unsync`: the 2D
interface came out as stretched quads over a correct 3D scene (readback).
A trace of the buffer calls at the login screen (40 s: 53387
`glMapBuffer`, 67997 `glDrawRangeElements`, 21 `glBufferData`) shows why:
the dynamic buffers are small (0x80 to 0xc000 bytes, `GL_STREAM_DRAW`),
and each use is bind, map, write, unmap, draw from vertex 0, often the
same buffer twice in a row. The game writes over what it has just asked
to be drawn. That is the discard case, not the append one I had guessed
from the extension it knows.

**discard is right** by readback of the login screen (interface and
scene). It moved the wait, twice: first to my own
`glGetBufferParameteriv` for the size (52 % of the main thread, a
glthread wait), so the bundle now keeps buffer sizes itself from
`glBindBuffer` and `glBufferData`; then to glthread's own catching up
before `glMapBufferRange` (60 %), which costs nothing in the end:

| Login screen, 1920x1080 | frames a second | CPU |
|---|---|---|
| sync (Mesa's), glthread | 30 | 33 % |
| discard, glthread | 188 | 79 % |
| discard, no glthread | 188 | 72 % |
| sync, no glthread | 30 | 31 % |

`RDN_FPS=1` in a program's environment prints frames a second on standard
error every five seconds (new, for this). Bundle 1c5555c7 plus the
counter on the G5; `/Library/Application Support/RadeonNI/mapbuffer`
there says `discard World of Warcraft`. Not seen: the world with it.
The default for programs not named is still Mesa's wait.

## 2026-10-07 — GL_APPLE_flush_buffer_range from the bundle; discard was wrong in the world

**discard in the world** (the user, then readback): the ground at some
angles a flat dark blue, models said to be corrupted too; trees,
buildings and characters right in the grab. So the game does not rewrite
all of every buffer it maps. Neither guess (`unsync`, `discard`) fits
all of its buffers; the switch file is removed from the G5 again.
`RDN_MAPBUFFER` stays as a tool, off.

**The extension instead** (the user's choice). The game looks its two
functions up with `CFBundleGetFunctionPointerForName` on the bundle
`com.apple.opengl`, found in three steps:
1. A library with the two functions inserted with `DYLD_INSERT_LIBRARIES`
   and the name added to the list of extensions: the game gets the list
   and calls nothing. It does not search the program's images.
2. The same library interposing `CFBundleGetFunctionPointerForName`
   (dyld's `__interpose` works on 10.4.11) and answering the two names:
   the game calls `glBufferParameteriAPPLE(GL_ARRAY_BUFFER, 0x8a12, 0)`
   and `(…, 0x8a13, 0)` for two buffers, then maps them and names what it
   wrote, each range after the last (0+6272, 6272+7424, 13696+5504, …).
   165 frames a second at the login screen, picture right.
3. No library: the bundle hooks that lookup itself, the way it already
   hooks `CGLSetCurrentContext` (`gld/rdn_hook.c`), and has the two
   functions (`gld/gen_dispatch.py`, flushrange). A plain launch:

| Login screen, 1920x1080 | frames a second | CPU |
|---|---|---|
| as installed, nothing set | 165 | 106 % |
| `RDN_GLTHREAD=0` | 168 | 102 % |
| `RDN_NO_FLUSHRANGE=1` | 30 | 34 % |

In Mesa the two parameters are `glMapBufferRange`'s unsynchronized and
explicit flush bits, and the flush is `glFlushMappedBufferRange`. Every
program but the window server gets the extension in its list
(`~/gl/glext` shows it). Bundle 70946417 on the G5. Not seen: the world.
Only buffers the game marks skip the wait; its other maps wait as before.
The library of steps 1 and 2 is not kept.

## 2026-10-07 — World of Warcraft confirmed by the user

With bundle 70946417 and nothing set, started normally, in the world:
"It all looks good". So the vertex blend mapping and
`GL_APPLE_flush_buffer_range` are confirmed on the monitor for this game.
Call of Duty 2 past its menu is still unseen.

## 2026-10-07: Call of Duty 2 runs out of video memory in its first map

The user got past the menu: the game crashed after loading the first map,
at a null pointer in `_mesa_glthread_upload` under `glDrawRangeElements`
(`RB_TessXModelSkinned`). The disassembly of our bundle at that address is
the load from what `new_upload_buffer()` returned: Mesa does not check it.
The game's standard error (`/Library/Logs/Console/501/console.log` when it
is started from the Finder) had dozens of `rdn: out of video memory`, 3 to
22 MB asked each time, 1.3 to 1.4 GB in use, the last one for the 1 MB
upload buffer.

Why so much: the game imports no `glBindBuffer` at all. It draws from its
own memory (`glVertexArrayRangeAPPLE`, fences, which Mesa lacks and the
engine's entries swallow) and only with `glDrawRangeElements`, and
`CDirect3DDevice::DrawIndexedPrimitive` gives Direct3D's MinVertexIndex and
NumVertices as the range unless a flag makes it read the indices
(`GetHighAndLowIndices`). glthread trusts the range and copies that many
vertices of every array for every draw, each copy above 1 MB in a buffer of
its own, all alive until the commands are flushed.

Change: the bundle's `glDrawRangeElements` calls `glDrawElements` when the
range is wider than the number of indices (`gld/gen_dispatch.py`,
`DRAW_RANGE`), so Mesa reads the indices and copies what they use.
`RDN_GLD_RANGE=1` passes the range as given. Installed on the G5 (the
bundle before it is `~/RadeonNIGLDriver.wow5`).

Not verified in the map. `+devmap eldaba` on the command line (the demo's
only map, `maps/eldaba.d3dbsp`) is not a way in: the user saw the loading
screen start, the Activision film cut in and the picture stop on one of
its frames, while the game went on swapping at 960 frames a second. Quit
with `killall`. Not looked into: it is my way of starting it, not the
user's.

## 2026-10-07: Call of Duty 2: the range was not it; the GART chunks; a bind that never returns

The user: "It still crashes". Same place, and `t_draw_range_elements` had
passed the range on: it was not wider than the indices. The range change
stays (it is right for what it describes) but it was not the cause. I had
not measured before saying so.

Measured, with `RDN_STATS=1` and the user clicking through to the map. At
the first `out of video memory` (17 MB asked) only 735 MB were in use: 185
MB of the aperture's 256 (11 buffers of about 17 MB among them), 444 MB of
the 768 beyond it, 71 MB behind the GART (209 at most). The device layer
for Tiger (`mesa/target/rdn_device_darwin.c`) had 48 chunk slots, never
gave a chunk back, and a buffer above 16 MB took a chunk of its own size
that only a buffer of at most that size could use again. The game's
buffers of 17 to 22 MB, each a different size, used the slots up; from then
on everything the CPU maps went to the aperture until that was full.

Change 1: 256 slots; buffers up to 16 MB only in chunks of 16 MB; a larger
one gets a chunk of its own that is unbound and deallocated when it is
freed (the first use of `RDN_UC_GART_UNBIND` outside a program's end).

Result: no out of memory, and the game stopped on the map's title card
("The End of the Beginning", the user). `sample`: the main thread in
`IOConnectMethodScalarIScalarO` under `dev_gart_alloc`, for a 1 MB upload
buffer, so in `RDN_UC_GART_BIND` for a new 16 MB chunk. Process state `U`,
`kill -9` does nothing, 1.11 GB wired in the machine (271 MB before the
game). The kext's other calls still answer (`rdnuc grab`), so its lock is
not held: the thread sleeps before it, in `IOMemoryDescriptor::prepare()`.
A guess, not verified: the G5's DART has no room left for the mapping and
the kernel waits for some. The G5 needs a restart to get rid of the
process.

Change 2, so that it cannot happen again: one program may have 512 MB
bound (`GART_MOST_BYTES`); beyond that the bind is refused and the winsys
falls back as before. `RDN_STATS=1` prints its picture again whenever 128
MB more are in use than at the last failure. Installed on the G5.

Open: why the game needs more than 1.4 GB of the card's memory at all. Not
known. The buffers that fail are the same sizes in every run (21504000,
17734656, 8344320 bytes ...).

## 2026-10-07: Call of Duty 2 in its map: GL_APPLE_vertex_array_range from the bundle

After the G5's restart, with the 512 MB limit: no hang, out of memory and
the crash again, as expected. The statistics at three moments say what
the memory is. Beyond the aperture (textures) it stays at 430 to 480 MB.
What grows is what the CPU maps: at the end 483 MB behind the GART and 214
MB in the aperture, in buffers of 1 to 22 MB (124 of 1 to 2 MB, 17 of 8 to
16 MB, 7 of 16 to 32 MB ...).

Those are Mesa's copies of the game's vertices. From its disassembly: the
game keeps every vertex buffer in memory of its own (`new[]`), makes one
vertex array object for every (pointer, length) it draws from
(`CVAOPacket`, pointer = buffer + stride x lowest vertex), and for each
calls `glVertexArrayRangeAPPLE`, `glFlushVertexArrayRangeAPPLE(length,
pointer)` once and the array pointers. It flushes again in pages of 1 KB
when it has changed a static buffer (`CStaticCacheInfo::Flush`), takes new
memory when a dynamic one is locked with discard, and sets fences to know
when the GPU has read what it is about to overwrite. It does all of that
only when `GL_APPLE_vertex_array_object`, `GL_APPLE_fence` and
`GL_APPLE_element_array` are in the list of extensions (it never asks for
the range by name and never calls the element array's functions); Mesa
names none of the three, so with us it drew from plain client arrays. Mesa
then copies, for every draw, from the lowest to the highest vertex the
indices use: the world's draws reach across a buffer of 20 MB each, and
each vertex array object keeps its last copy alive. Hence 1.2 GB, and 0.4
frames a second while it got there. The range in `glDrawRangeElements`
never mattered.

Built: the extension in the bundle (`gld/gen_dispatch.py`, `VAR_HELP`).
A "mirror" is a buffer object with a copy of the program's memory: of the
whole malloc block a range lies in when that is found (`vm_region`, then
`malloc_size` along the region), so that all ranges in one of the game's
buffers share it; else of the range. A flush sends the pieces of 16 KB
that differ from a shadow copy kept in ordinary memory (the game flushes
20 MB again for every new vertex array object over the same buffer). An
array pointer into a mirror becomes an offset into its buffer object.
Each vertex array object counts the mirrors it points into; mirrors nobody
points into go after 120 swaps. The fences are always finished. The four
extension names are added to the list. Only for the programs named in
`/Library/Application Support/RadeonNI/vertexrange` or with `RDN_VAR=1`:
other programs work without it today, and it draws stale vertices for a
program that changes its memory without a flush.

Result on the G5, the user playing: "It's a bit slow but it works!" No
out of memory; 64 MB of mirrors; the picture right by readback (the grab
tears, taken while the view moved). 13 to 16 frames a second. `sample`:
Mesa's thread waits 83 % of the time; the game's thread spends 51 % in my
searches through 6000 mirrors, nearly all of them small ranges of a few KB
(I had only looked for the malloc block when the range was 64 KB or more).
Changed: the block is looked for at any size, regions without one are
remembered, the search starts at the mirror found last. Installed; not yet
run.

## 2026-10-07: Call of Duty 2 sampled again with the block lookup at any size

The user started it again; `sample` for 8 s in the map. The searches are
gone from the game's thread (`t_flush_vertex_array_range_EXT` 0.8 % self,
`var_pointer` not in the list; they were 33 % and 18 %). No frame rate:
started from the Finder, so no `RDN_FPS`.

Where the game's thread is now: 33 % in the game's own frame
(`CG_DrawActiveFrame`), 63 % in its renderer, of which
`DrawIndexedPrimitive` 40 %. Ours in that: the flushes 14 % inclusive, 10 %
of it `glBufferSubData`, and 6 % of the whole is that call waiting for
room in glthread's queue (`util_queue_add_job`). Mesa's thread waits 58 %
of the time, so it is behind only in bursts: the game flushes its skinned
vertices a KB at a time (`CStaticCacheInfo::Flush`), each flush a bind, a
`glBufferSubData` and an unbind.

Changed: a flush only marks the mirror; touching flushes are joined, and
the mirror is brought up to date when a pointer is set, a draw comes, a
range is named or a vertex array object is bound. Installed on the G5, not
yet run.

## 2026-10-07: Call of Duty 2 in its map: 45 to 70 frames a second

Started from ssh with `RDN_FPS=1`, the user clicking through and playing
the map (1920x1080): 45 to 73 frames a second, mostly around 50. It was 13
to 16 before the block lookup at any size and the joined flushes. No out
of video memory.

`sample`, 8 s, the game's thread: 37 % the game's own frame, 60 % its
renderer. Ours: bringing mirrors up to date (`var_update`) 22 %
inclusive, all of it for skinned models (`RB_TessXModelSkinned`, the
game's generic vertex array object): `glBufferSubData` 14 % (6 % the copy
into glthread's batch, 7 % waiting for room in its queue), 5 % copying
into the shadow, 2 % comparing with it. Mesa's thread waits 60 % of the
time and spends 8 % copying the same data out of the batches.

Changed, installed, not yet run:
- A mirror whose flushes differ from the shadow nearly every time (more
  than three quarters of a megabyte flushed) drops its shadow: computed
  vertices only pay for it.
- `allow_glthread_buffer_subdata_opt` for the programs in the vertex range
  list (`mesa/target/rdn_target.c`, on the screen's caps after r600 made
  it; `RDN_SUBDATA_COPY=1|0` for any program): glthread copies
  `glBufferSubData`'s data into an upload buffer in the program's thread
  and the GPU copies it from there, so it goes through no queue. Only
  built for Tiger so far; the Linux builds of the target have not been
  rebuilt with it.

## 2026-10-07: Call of Duty 2 with the direct glBufferSubData and no shadow for computed vertices

Run by the user in the map, `RDN_FPS=1`: 37 to 50 frames a second where
this sample was taken ("it looks about the same", another place in the map
than the run before, so the two rates do not compare). No out of video
memory, and the user reported nothing wrong with the animated models.

`sample`, 8 s, the game's thread: the waits for glthread's queue are gone
(7 % before, 1.5 % now for all waits), the skinned models' path is 11 %
inclusive (25 % before), and what is left of ours in it is one copy,
`_mesa_glthread_upload` 10.6 %. The game's own frame is 46 % and its
renderer 50 %. Mesa's thread waits 67 % of the time. So the game's thread
is the limit and nearly all of it is the game.

The one copy left is the vertices the game computes every frame. Apple's
drivers have none: the GPU reads the program's memory. The same could be
had here by binding the game's memory behind the GART (the kext's bind
call takes any page-aligned memory of the program) and giving Mesa buffer
objects over it (r600 has `resource_from_user_memory`; our winsys says
`has_userptr = false`); the fences would have to be real then. Not
started.

## 2026-10-07: the GPU draws from a program's own memory (zero copy, stage 1)

The user: "try the zero-copy route", then asked why it would be quicker.
It is not, for the GPU; it only takes the copy (10 % of Call of Duty 2's
thread) away from the one thread that is the limit.

Built: `gart_bind_user()` in the device layer for Tiger (the kext's
`RDN_UC_GART_BIND` on memory the program gives, page aligned, counted in
the 512 MB limit; `RDN_NO_USERPTR=1` leaves it out), `buffer_from_ptr` in
the winsys (destroying such a buffer waits for the GPU and unbinds), and
the screen's `resource_from_user_memory` set by our target: r600 leaves it
off on big-endian. With that Mesa names `GL_AMD_pinned_memory`. No kext
change. The entry point's split of large `glBufferData` must skip that
target (it sent the data in pieces after a null: GL_INVALID_OPERATION).

`tools/guest/pinned.c` (cross-built on the host, `~/gl/pinned` on the G5;
our renderer is 0x21a00 there): a buffer object over 1 MB from `valloc`, a
triangle from its last three vertices drawn into a texture. Read back:
red; after the colours were changed in memory with no OpenGL call, green.
So the GPU reads the program's memory, big-endian vertices and all. With
the memory freed before the buffer object is deleted (1 MB and 24 MB):
still right, and the machine's wired pages are where they were afterwards
(64854 before, 64852 after).

Found on the way, not looked into: an off-screen context on our renderer
reads zeroes back from `glReadPixels` on the G5 (`glprobe draw 0x21a00`
too), so the test draws into a framebuffer object.

## 2026-10-07: vertex array range without copies, in Call of Duty 2 (stage 2)

Built (`gld/gen_dispatch.py`, `var_pin` and around it): a mirror of a
page-aligned malloc block of 64 KB or more is a buffer object over the
block itself (`GL_AMD_pinned_memory`), for programs with a "+" before
their name in the vertex range list or `RDN_VAR=2`; up to 64 of them. The
malloc zone's `free` and `realloc` are replaced by ours to see such a
block go. `glFinishFenceAPPLE` is `glFinish` when something was drawn since
the fence was set; `glTestFenceAPPLE` stays true.
`tools/guest/vartest.c` (`~/gl/vartest`) draws through the extension and
shows the three modes apart: without a flush the changed colour shows only
with `RDN_VAR=2`.

First run in the game, the user: "there's glitching, flickering geometry
especially". 57 to 104 frames a second in the map. The log: the 22 MB,
8 MB and 4 MB buffers pinned once each, and one block of 2101248 bytes
pinned 719 times, 700 "freed under us": the game's buffer of computed
vertices, freed and taken again 8 times a second, at the same few
addresses. My code let the mirror go at the free and made a new buffer
object at the next use. The game keeps track of the pointers it has set
and does not set one again that has not changed, so its vertex array
object went on drawing from the old buffer object: the old pages, still
wired, with vertices two frames old.

Changed: a freed mirror keeps its buffer object and is put over the
memory anew (`glBufferData` on the same object, `var_revive`) when the
address is used again and the same block is there; `vartest` step 4 now
frees, takes the memory again and only flushes, and reads blue. The
winsys no longer waits for the GPU when such a buffer is destroyed: the
pages go to its cache as a fourth region, RDN_USER, that is only ever
unbound, when the GPU is done. That region overran three arrays of three
in `shim/radeon_drm_winsys.h` in the first build (seen in the statistics
of the test, before the game ran with it); four now.

Unexplained, from `vartest` with `RDN_VAR=0`: the triangle from plain
client arrays, first vertex 65533, reads back black.

## 2026-10-07: zero-copy mirrors: memory taken again without a flush

Second run in the game, with the buffer object kept across a free: the
user: "there's still some flickering", "visible even in the main menu".
35 to 75 frames a second in the map this time.

In the menu every frame is the same, so after the game has freed and taken
its 2 MB again every (pointer, length) it draws from is one it has a vertex
array object for already (`CVAOPacket::IsCached`): it binds that and
draws, with no flush and no pointer set. Nothing told my code that the
address was in use again, the mirror stayed over the freed pages, and the
picture was drawn from the vertices of the cycle before: the same menu,
except where it moves.

Changed: the zone's `malloc`, `calloc` and `valloc` are watched too
(`var_taking`); memory handed out at a freed mirror's address sets a flag,
and the next draw puts the mirror over it first (`var_retake`).
`VARTEST_NO_FLUSH=1 vartest` is that case: blue with `RDN_VAR=2`.

Checked on the way, because the menu runs at 400 frames a second and the
game frees a discarded buffer two frames later: do pages the GPU reads keep
what was in them when the program frees them? `pinned ... freefirst` now
frees, takes and writes 64 MB of other memory, and draws from the freed
buffer once more: still green. They do.

Installed on the G5; the user's game was still the build before.

## 2026-10-07: zero-copy vertex ranges in Call of Duty 2: not right yet, put back on copies

Three more runs by the user, each with the newest build.

1. With memory taken again noticed at the next draw: "The flickering is
   gone in the main menu, but it's still visible in the map". 30 to 67
   frames a second in the map. The log of that run: the game's 2 MB block
   appears at many addresses, and a freed mirror kept its slot (64 of
   them) and its pages for as long as one of the game's vertex array
   objects pointed at it, which is for good; once the slots were gone
   every new 2 MB block was a copy mirror, copied whole when made (897 MB
   copied in that run).
2. The same build with `RDN_GLD_SWAP_FINISH=1` (every swap waits for the
   GPU), to see whether the game's thread running ahead of the GPU is the
   rest of it. The user first: "There's no flicker now"; then: "Actually,
   the main menu has flickering. And there's a couple angles where slight
   flickering happens anyway". 20 to 40 frames a second in the map. So
   running ahead is part of it at most, and waiting at every swap costs
   more than the copies ever did.

The full-screen swap is a `glFlush` that glthread only records
(`rdn_mesa_present`): nothing makes the game's thread wait for Mesa's or
for the GPU, and the game takes memory it drew from two frames ago for
read. A bound that waits at a swap for the frame before the last was
designed (a job in glthread's queue that only reads the winsys's last
fence number, so that it touches no context) and not built.

Changed after run 1 and installed, run only in `vartest`: a mirror that
goes stale gives its pages back at once (`glBufferData` of 4 KB on the
same object), and there are 1024 slots.

Where it stands: no run of the game without flicker with `RDN_VAR=2`, the
menu's flicker under `RDN_GLD_SWAP_FINISH=1` is not explained, and no run
was faster than with copies (55 to 90 in the map). The list on the G5 is
back to `Call of Duty 2` without the "+": copies. `vartest` with
`RDN_VAR=1` passes on the installed build. The code stays, off unless
asked for.

Seen in the last log, in the menu, copies or not: four mirrors of 256
bytes made every frame (ranges in small malloc blocks at ever new
addresses) and let go 120 swaps later, 1024 at a time.

## 2026-10-07: copy mode: draws that come without a flush

The user, back on copies with the newest build: no visible flicker in the
map, "a slight flickering in the menu when exiting from a map".

The game does not keep the extension's rule. `CreateAndSetProgrammableVAO`
flushes a range when it makes the vertex array object for it; when
`CVAOPacket::IsCached` finds one for the same (pointer, length) it binds
it and draws, whatever it has written there since. On the Macs it ran on
the GPU read its memory, so that worked. A copy holds what was last
flushed at that address: after a map, the map's vertices where the menu
draws from. (It is the same gap that the zero-copy mode had with a freed
address, seen from the other side.)

Changed (`var_drawing`): each vertex array object remembers the range
last flushed while it was bound; a draw with no flush since the last draw
marks that range again, so it is compared with the shadow and what
differs is sent, if the mirror is of memory that a flush has ever found
changed. That keeps the world's 20 MB ranges out of it. The first change
of a block made without any flush is still not seen.
`RDN_VAR=1 VARTEST_NO_FLUSH=1 vartest` step 4 reads blue now (green
before). Installed on the G5; not yet run in the game.

## 2026-10-07: copy mode: the menu's text after a minute of play

The user: the map is as fast as before; the menu still flickers, but only
after a minute or so of play. Eight grabs of the menu while it flickered:
they differ only in the menu's text (x 1106 to 1330), whose letters have
bands of rows drawn from the wrong place. So some of the glyphs' vertices
are stale: the game's 2 MB of computed vertices again.

The look before a draw only took mirrors that a flush had found changed. A
mirror made for one range of a small block or of a region where no block
was found (the lookup remembered such regions for good, which goes wrong
once malloc has put a large block there) is flushed once, when it is
made, and so never counted as changing.

Changed: the default malloc zone is watched from the first mirror on, in
copy mode too (`var_watch`). Every block of 15 KB or more that malloc
frees or hands out is noted (`var_moving`, 64 of them kept), and before a
draw the mirrors that overlap one are marked as changing
(`var_moved_look`); the lookup forgets its regions without a block when
that happens. A buffer rewritten in place with neither a flush nor a free
is still not seen. Tests as before. Installed on the G5; the game that was
running is the build before.

## 2026-10-07: Call of Duty 2, copy mode: the menu's flicker gone

The user, after playing the map and going back to the menu with the build
that marks mirrors in blocks malloc has moved: "It seems to be gone".
That build (bundle md5 4555f6d9f8777c8a199997979bb6d047) is the one on the
G5, with `Call of Duty 2` in the vertex range list (copies). The bundle
from before this work on the game is `~/RadeonNIGLDriver.wow5` there.

## 2026-10-07: Apple's other extensions: what is missing, who asks, and the small ones built

The user's question: are there more Apple-only extensions of the kind
Call of Duty 2 and World of Warcraft needed.

**How it was looked for.** Our list (`~/gl/glext` on the G5, 295 names)
against the 107 extension names in 10.4.11's `GLEngine`, which is every
name any Tiger driver can give: 38 are not ours, 13 of them `APPLE`. Then
who asks: the extension names, the GL function names and the imports in
Apple's frameworks and applications and in the games on the G5. Apple's
frameworks call GL through CGL macros, so their imports say nothing about
the GL entries they use; only a trace does.

| Extension | Who looks for it | Before | Now |
|---|---|---|---|
| `GL_APPLE_client_storage` | Core Image requires it | enum dropped, not named | `glGet` answers; named only with `RDN_EXT_ADD` |
| `GL_APPLE_float_pixels` | Core Image, window server, Quartz Composer | not named | `GL_COLOR_FLOAT_APPLE` answers false; named only with `RDN_EXT_ADD` |
| `GL_APPLE_pixel_buffer` | Core Image, Core Video, Quartz Composer, the USB camera digitizer, World of Warcraft (AGL) | `CGLSetPBuffer` refused | the same: not done |
| `GL_EXT_gpu_program_parameters` | World of Warcraft, by name | named, the functions not to be had | answered by name |
| `GL_APPLE_texture_range` | window server, Core Image, iTunes Artwork, camera digitizer, all without asking | the engine's entries | ours, named |
| `GL_APPLE_transform_hint` | Quake 3 | hint dropped, not named | named, `glGet` answers |
| `GL_APPLE_fence` | Call of Duty 2 asks; DVD Player and iChat import its functions | the engine's entries, which never wait | ours, real waits, named |
| `GL_APPLE_vertex_array_object` | Call of Duty 2 | Mesa's functions, named only with the vertex range | named |
| `GL_APPLE_flush_render` | nobody found | the engine's entries | ours, named |
| `GL_ATI_array_rev_comps_in_4_bytes` | Call of Duty 2 and World of Warcraft have the name | nothing | nothing: neither uses it (below) |
| `GL_APPLE_ycbcr_422` | iChat; video players | nothing | nothing: r600 cannot sample YUV (`r600_state_common.c`, "XXX") |

Not worth building, because every program found that names them prefers a
path we have, or Mesa has removed them: `GL_ATI_text_fragment_shader`,
`GL_ATI_pn_triangles`, `GL_NV_register_combiners*`, `GL_NV_texture_shader*`,
`GL_NV_vertex_program*`, `GL_EXT_paletted_texture`, `GL_ARB_imaging`,
`GL_SGI_color_matrix`, `GL_ARB_shadow_ambient`, `GL_APPLE_specular_vector`,
`GL_APPLE_vertex_program_evaluators`. Not Apple's, seen on the way: Doom 3
and Quake 4 use `GL_EXT_depth_bounds_test` when it is named, and r600 has
none.

**Built** (`gld/gen_dispatch.py`, `APPLE_HELP`), for every program but the
window server, `RDN_NO_APPLE=1` turns it off:

- Fences on Mesa's sync objects. The engine's own, which programs got
  until now, never wait: `tools/guest/appletest.c` draws 4000 blended
  quads of 1024x1024, sets a fence and finishes it; before, 0.0 ms and
  then 822 ms in `glFinish`; now 820 ms and 0.1 ms. Programs with the
  vertex array range keep the fences they had.
- Mesa's `glFenceSync` does not draw what `glBegin` and `glEnd` have
  gathered: the fence was reached at once while the quads were still to
  come (the first build failed the test that way). The bundle calls
  `glFlush` before it makes the sync object.
- `glTextureRangeAPPLE`, the storage hint, client storage and the
  transform hint are remembered and answered by the `glGet` calls. Core
  Image asks for `GL_UNPACK_CLIENT_STORAGE_APPLE` with `glGetIntegerv` to
  put it back afterwards; until now that was a GL error and no answer.
- `glProgramEnvParameters4fvEXT` and `glProgramLocalParameters4fvEXT` by
  name (`RDN_NO_PROGPARAMS=1`: not). World of Warcraft looks both up and
  then uses the first (98 parameters in one call, 21, 1): 164.6 frames a
  second at its login screen without, 166.8 with, twice each. In the
  world: not measured.
- With the log on, every `gl` name a program looks up through
  `CFBundleGetFunctionPointerForName` is logged, answered or not.
- `RDN_EXT_ADD="GL_a GL_b"` adds any names to the list and
  `RDN_RENDERER="name"` replaces `GL_RENDERER`, for one program.

Names made defaults after a run with `RDN_EXT_ADD`: transform hint,
fence, vertex array object, texture range, flush render. Quake 3 says
"using GL_APPLE_transform_hint", 148.2 against 148.3 frames a second.

**Core Image, why it filters on the CPU** (`ciprobe gl 0x21a00`, every GL
call traced, and QuartzCore's `accel_load_screen_info`, `fe_accel_new`
and `fe_accel_get` read in disassembly to learn what it asks):

1. It requires `GL_APPLE_client_storage` and `GL_EXT_texture_rectangle`;
   without either the renderer is not used. With our list as it was it
   stopped here. `GL_ARB_vertex_program`, `GL_ARB_fragment_program` and
   `GL_APPLE_float_pixels` each set a flag.
2. With `RDN_EXT_ADD="GL_APPLE_client_storage GL_APPLE_float_pixels"` it
   goes on to ask for eight program limits of each kind. Three of them
   are fragment-only counts asked of the vertex program too: Mesa says
   invalid enum and leaves the answer unset, Apple's software renderer
   answers 0 with no error (`tools/guest/proglimits.c`). The bundle now
   answers 0. It made no difference to the outcome.
3. It gives the renderer a class, from the renderer ID (0x21800 ATI
   Radeon, 0x21900 Radeon X1000, 0x22400 NVIDIA, 0x24000 Intel, 0x20200
   and 0x20400 Apple's own) or else from how `GL_RENDERER` starts
   ("NVIDIA GeForce FX ", "NVIDIA NV34", "NVIDIA GeForce ", "NVIDIA Quadro
   ", "ATI Radeon X1", "ATI Radeon ", "Intel ", "Generic"). A renderer in
   no class has no buffer format it may use and a speed of 0 (ATI Radeon
   100, Radeon X1000 and GeForce 200, software 1). Ours is in none. This
   is where it turned to the CPU.
4. With `RDN_RENDERER="ATI Radeon HD 7570"` as well it takes the card:
   two more contexts that share with the program's, one with 32 bits of
   colour and one with 64, both attached with no drawable (the pbuffers
   it could not set), "CoreImage: ROI is not tilable" on standard error,
   and no drawing at all in the program's context. 1.5 ms a render
   against 12, of nothing: every pixel differs from the software picture.

So hardware Core Image needs the two names, a renderer it knows (the ID
or the name) and pbuffers, one of them floating point. None of the three
is on by default.

**`GL_ATI_array_rev_comps_in_4_bytes`** has one enum, 0x897C, and no
public text. Named for one run of each game, with the enables, client
states, hints and array pointers traced: neither World of Warcraft at its
login screen nor Call of Duty 2 in its menu passes 0x897C anywhere, and
their colour arrays stay four unsigned bytes. Nothing to build.
Call of Duty 2 disables `GL_TEXT_FRAGMENT_SHADER_ATI` (0x8200) every
frame without asking, which Mesa refuses; harmless.

**On the G5:** bundle 1a200fd0 installed, the one from before is
`~/RadeonNIGLDriver.before-ext` (4555f6d9). `appletest` passes; `vartest`
with copies as before; Doom 3 `bench` 48.6, Quake 3 148.6, World of
Warcraft's login screen 166, Call of Duty 2's menu 394 frames a second;
that menu and Chess right by readback. The G5 was restarted by someone
else in the middle of this, so its window server runs the third build of
this work (49b5e401), which does not differ for it. Not seen by the user:
any of it. Not run: DVD Player and iChat, which are the programs the real
fences are new for.

Wrong in the plan written before the work: it said the fence entries were
already ours for every program. They were only for programs with the
vertex array range.

## 2026-10-08: "Core Image: Supported" was one extension name; the window server no longer gets it

**The user:** what would Core Image gain, and if nothing useful is in
there now, stop reporting it as supported.

**What says "supported".** System Profiler asks the window server
(`CGSServerOperationState`). Read in CoreGraphics' `CGXGPUCapability` and
`CGXGLDisplayContextCapabilities`: state 0xf, Core Image, is set when the
list of extensions the window server's own context gets has
`GL_ARB_fragment_program`, and nothing else is looked at. State 0xd
(Quartz 2D Extreme possible) wants the same name; 0xe is whether that is
turned on (it is not). `AccelCaps` has no part in it. The Dock asks for
the same state before it does Dashboard's ripple
(`CGSNewCIFilterByName("CIShapedWaterRipple")`, `CGSAddWindowFilter` with
flags 0x3001).

**Is anything behind it.** `tools/guest/wsfilter.c` makes the Dock's calls
with `CIColorInvert` on a transparent window of 600x400: both calls
return no error, the states read 0xd 1, 0xe 0, 0xf 1, and three grabs
(before, with the filter, after) are the same in every pixel. The window
server's log has one line from that second: "CGXGLAccelCompositeLayer_ :
GL error 0500 entering". So a filter on a window draws nothing. (The test
has not been run on a Mac where filters work; that the window server
went into its filter layer is what the log line shows.) Programs' own
Core Image filters on the CPU whatever is reported (the entry before
this). Nothing useful comes of the report.

**Changed:** the bundle leaves `GL_ARB_fragment_program` out of the list
the window server gets (`ext_no_core_image()` in `gld/gen_dispatch.py`).
Only the name: fragment programs work as before, for it and for every
program. The file `/Library/Application Support/RadeonNI/coreimage`,
there when the window server starts, keeps the name in, for when A6 is
worked on. Checked with a copy of `appletest` named `WindowServer`: the
name is gone, and back with the file.

**That was not enough.** With the user's go-ahead the window server was
restarted with that bundle: still "Core Image: Supported", state 0xf
still 1. The window server asks its context for the list right after
`cglsCreateContext`, through the context's table, before the bundle has
given that table to Mesa (in the window server that happens when a
drawable is attached). `tools/guest/earlyext.c` asks at that moment: the
renderer is "Apple Software Renderer" and the list has 52 names, with
`GL_ARB_fragment_program`. A second try, taking the name out of what
`gldGetString` returns, did nothing either (another restart): the engine
asks the plug-in for the renderer's name only and makes the list itself.

How it makes it, read in `GLEngine`'s `glGetString_Exec`: a fixed part,
then one name for each bit set in three words at 0x124 of the record
that `gldCreateContext`'s fifth argument points to (inside the engine's
context; the software renderer fills it in). Bit 15 of the first word is
`GL_ARB_fragment_program` (49 `GL_APPLE_float_pixels`, 50
`GL_APPLE_pixel_buffer`). Cleared from `earlyext` on Apple's software
renderer, the name goes: 52 names, then 51.

**Changed, the third build:** for the window server the bundle clears
that bit when `gldCreateContext` has returned (`forward()` in
`gld/RadeonNIGLDriver.c`, `rdn_ws_no_core_image()`), and Mesa's list
leaves the name out as before. A copy of `earlyext` named `WindowServer`
gets neither list with the name, and both with the file.

**On the G5, after the third restart of the window server:** System
Profiler says "Core Image: Not Supported" and "Quartz Extreme:
Supported"; states 0xd, 0xe and 0xf read 0; `qe`: Quartz Extreme in use;
desktop, a Finder window and Chess right by readback; no crash log;
Quake 3 as fast as before. Bundle 9f7f487f is installed; the one from
before the extension work is still `~/RadeonNIGLDriver.before-ext`. The
user has not looked yet.

**What Core Image on the card would gain,** for the record: Dashboard's
ripple, filters in the window server and in programs that use Core Image
(Preview's image correction, Quartz Composer compositions, and Aperture
and Motion, which want such a card to start) several times faster than
the CPU does them. Nothing for the games. It needs pbuffers, a floating
point one among them, and a renderer identity Core Image knows.

## 2026-10-08: A6 stages 0 and 1 (docs/CORE-IMAGE-TODO.md)

**Stage 0, reproduced** on the G5 with the bundle built from `main`
(md5 9f7f487f, the same as the installed one; copy kept as
`~/RadeonNIGLDriver.before-ci`). `appletest` all passed, `earlyext` as
before. `ciprobe soft`: 8.2 ms a render (the plan said 12), checksum
83e28896. `ciprobe gl 0x21a00` with the two switches and the full trace:
three `gldCreateContext`, the second and third sharing with 0x280ea00,
two attached with type 0, no `program:` lines. Same as the journal.

**Stage 1, the "16x16" bug was not a size.** `glprobe draw 0x21a00` left
the program's buffer all zero. A trace line in `rdn_make_current()`
(`rdn_trace` only) showed `read_record()` failing at every call: for
`CGLSetOffScreen` (type 0x35) the third argument's record is the
program's own, `{width, height, rowbytes, base}` in words 0 to 3 (as
`docs/GLD-INTERFACE.md` says), and `read_record()` read the window
layout (words 4, 5, 11, 27) for both. Words 4 to 11 of the off-screen
record are not filled. The context therefore stayed bound to the 16x16
dummy for good, drawing and reading back there, and the program's memory
was never written. `appletest` passed because it reads pixels back
through `glReadPixels`, which came from the dummy; `ciprobe` reads the
buffer itself, so it saw nothing (196352 of 196608 pixels different).

**Fixed** in `read_record()`: off-screen records use words 0 to 3. After
it: `glprobe draw` fills all 96x64 with green; `appletest` all passed;
`vartest` as before; `ciprobe gl 0x21a00` without switches (Core Image
on the CPU, drawn through us) compares equal to `ciprobe soft`: 0 of
196608 pixels differ by more than 8, checksum 83e28896 both; 13.0 ms a
render against 8.2.

## 2026-10-08: A6 stage 2, shared contexts

`gldCreateContext`'s fourth argument is the gld context to share with
(0x280ea00 in Core Image's second and third, the first context's own
`*(void **)a`). The bundle keeps it (`struct context.share`) and
`mesa_for()` makes the Mesa context with the other's as its share list,
making the other's first when it does not exist (contexts are made on
first use; all four places that made one now go through it).

**The limit, as the plan asked to note:** `rdn_current_rend` and the
OSMesa current context are one variable for the whole process, so this is
right for one thread switching between contexts, as Core Image does, and
not for several threads each with a context of their own. Not widened.

`tools/guest/sharetest.c`: A, B sharing with A, C sharing nothing, each
with its own off-screen memory. A makes a texture and an ARB fragment
program (red and blue swapped); B sees both, draws a quad with them,
12 checks all PASS with the default and with `RDN_GLTHREAD=0`: the swapped
colour by `glReadPixels` and in B's own memory (0xff804020), A's memory
untouched, C sees neither, A draws it too, and a texture B makes later
is seen in A. Built on the host with `scripts/darwin.sh
powerpc-apple-darwin8-gcc` (no guest needed).

Speed, G5, 1920x1080: Quake 3 `four` 147.9 fps (148), Doom 3 `bench` 49.1
(48). `appletest` and `vartest` as before.

## 2026-10-08: A6 stage 3a to 3c, pbuffers

**3a, why `CGLSetPBuffer` was refused** (the plan said before any call
reached the bundle; it does reach it). Read in disassembly (`otool -tV`
on the G5): `CGLSetPBuffer` puts the pbuffer in a list of drawables and
calls `_cglSetAnyDrawable`, which sets the context's screen
(`_cglSetContextScreen`) and then calls the renderer's `gldAttachDrawable`
with the list entry's type (0x5a) and, as its third argument, the
`CGLPBufferObj` itself. Whatever that returns is the CGL error code, with
1 meaning "set viewport and scissor from the object's size (words 6, 7)"
and 2 "nothing more to do". Our bundle forwarded the call to Apple's
software renderer, which answers 0x271a, `kCGLBadEnumeration`. Nothing
about renderer info or pixel format flags was needed (`kCGLPFAPBuffer`
was already set: "pbuffer 1").

The record is the pbuffer object: word 2 its surface ID, word 3 the
texture target, word 4 the format, words 6 and 7 the size.

**3b, drawing into one.** The bundle answers type 0x5a itself (not in the
window server; `RDN_NO_PBUFFER=1` leaves the refusal) and returns 1.
`gld/rdn_mesa.c`: `struct pbuffer`, keyed by the surface ID, with video
memory made at first use (64 pixels to the row's multiple, bottom row
first, like a texture), and `DRAWABLE_PBUFFER` in `read_record()` and
`rdn_make_current()`, which binds with `OSMesaMakeCurrentStore`.
`CGLDestroyPBuffer` calls nothing of ours, so the memory is given back
when the object's address returns with another ID and at the end of the
program (the kext frees a client's memory). A limit to remember for the
window server.

**3c, as a texture.** `CGLTexImagePBuffer` is `CGLSetParameter(ctx, 997,
{ID, target, format, w, h, 0x8367, source, ...})`, which goes to the
engine's `gliSetInteger`, not to a `gld*` function. The engine looks at
*its own* texture bound to the target (Mesa's `glBindTexture` never
reaches it) and refuses with `kCGLBadState` (10007). So the bundle hooks
`CGLTexImagePBuffer` the way it hooks `CGLSetCurrentContext` (symbol
pointers of every image, `rdn_hook_tex_image_pbuffer`) and, for a context
of ours, calls `OSMesaTexStoreImage` on Mesa's bound texture (no copy,
flags bottom-up and alpha).

**Check:** `ciprobe pbuffer 0x21a00`: PASS for the 2D texture (256x256)
and the rectangle (320x200), 0 of 64 points wrong both when drawn into
and when read through the texture, also with `RDN_GLTHREAD=0`; with
`RDN_NO_PBUFFER=1` both FAIL as before. `appletest`, `sharetest`,
`glprobe draw` as before.

## 2026-10-08: A6 stages 3d and 4, Core Image draws on the card in a program

**What the plan had wrong.** Core Image never reached `CGLSetPBuffer` for
its own pbuffers in the earlier runs, because it had decided before that
every region of a picture is too big ("CoreImage: ROI is not tilable"),
and that was not about pbuffers. Found with `gdb` on the G5 (Xcode's gdb
works; there is no `timeout`) and `otool -tV` of QuartzCore:
`fe_tree_node_push_ROI` splits a region until it fits the limits of the
context (`max width`, `max height`, `max bytes` = 16384, 16384 and 0 for
us), and cannot split a one pixel region. The 0 is `fe_cgl_total_vram`,
which adds up the video memory of the accelerated renderers for the
**display mask of the context's pixel format** (`CGLQueryRendererInfo`,
`CGLDescribePixelFormat(kCGLPFADisplayMask)`); an off-screen pixel format
has mask 0 and so no memory at all. (The window server's counterpart,
`fe_cgls_total_vram`, is a constant 64 MB.) Not ours: a program that
draws into a window, or into a pbuffer with a non-off-screen format, has
a mask. `ciprobe gl` now makes its context that way (`CIPROBE_DRAW=
offscreen` for the old).

**3d, pixel formats.** Core Image asks `CGLChoosePixelFormat` for
`{51, 8, 32|64, 70, <renderer>, 73}` (minimum policy, colour size 32 and
64, the renderer, accelerated); the bundle already answers both. The
64-bit one is not floating point in the format: the pbuffers it makes for
its intermediate pictures say `GL_RGBA16` (0x805b) in
`CGLCreatePBuffer`'s format. With them in 8-bit stores the picture came
out within 19 of the software one (12503 pixels over 8); with 16-bit
stores it is within 1. `OSMESA_STORE_RGBA16`, `_FLOAT16` and `_FLOAT32`
(`mesa/frontend/include/GL/osmesa.h`, `osmesa.c`:
`osmesa_store_format()`) give `OSMesaMakeCurrentStore` and
`OSMesaTexStoreImage` 64- and 128-bit stores; the bundle picks the flag
from the pbuffer record's format word (`pbuffer_pixels()` in
`gld/rdn_mesa.c`: 0x805b, 0x881a, 0x8814). The float stores are made but
nothing here has drawn into one yet (no test asked for it), so floating
point is not shown to work, only 16-bit integer.

**Tools made:** logging hooks for `CGLCreatePBuffer`, `CGLSetPBuffer` and
`CGLChoosePixelFormat` (`rdn_hook_cgl_log`, only with `RDN_GLD_LOG`).

**4, the filters** (G5, 512x384, `ciprobe soft <f>` against `CIPROBE_DRAW=
pbuffer ciprobe gl 0x21a00 <f>` with `RDN_EXT_ADD="GL_APPLE_client_storage
GL_APPLE_float_pixels"` and `RDN_RENDERER="ATI Radeon HD 7570"`, 10 renders):

| filter | greatest difference | pixels over 8 | CPU ms/render | card ms/render |
|---|---|---|---|---|
| CIGaussianBlur | 1 | 0 | 8.3 | 1.3 |
| CIColorInvert | 1 | 0 | 7.7 | 1.2 |
| CISepiaTone | 1 | 0 | 8.7 | 1.2 |
| CIBumpDistortion | 191 | 13 | 11.3 | 1.4 |
| CIHueAdjust | 0 | 0 | 2.9 | 1.3 |
| CIPixellate | 1 | 0 | 10.5 | 1.4 |
| CIColorControls | 0 | 0 | 2.9 | 1.3 |
| CIGammaAdjust | 1 | 0 | 9.7 | 1.2 |
| CIZoomBlur | 1 | 0 | 16.1 | 1.2 |

(CIVignette does not exist on Tiger.) The 13 pixels of CIBumpDistortion are
all on the edges of the test picture's checkered cells, where the
distorted sample point falls on a texel boundary and one implementation
rounds to the neighbouring cell (blue 64 against 255); none is wrong by
more than that anywhere else. Not a driver difference that was looked into
further. Traces: for six of the filters (blur, invert, sepia, bump, hue,
zoom blur) 1 to 5 fragment programs each are made without an error, bound
and drawn with. Without the two switches Core Image still filters on the
CPU and the picture is exact (checksum 83e28896, 9.5 ms a render, the
pbuffer context or not).

`ciprobe pbuffer 0x21a00`, `appletest`, `sharetest`, Quake 3 and Doom 3
were run again after the bundle changes of this stage: see the list
below for the bundle on the G5.

## 2026-10-08: A6 stage 5, prepared (nothing run in the real window server yet)

Read in disassembly (CoreGraphics, on the G5): the window server's Core
Image makes its pbuffers with `cglsCreatePBuffer`, `cglsAttachPBuffer` and
`cglsTexImagePBuffer`, not CGL's. The pbuffer object is the window
server's own: surface ID at word 2, texture target at word 3, format at
word 4, size at words 8 and 9 (CGL's has it at 6 and 7), and its pixel
format modes are 0x24 for 0x8058 and 0x1908, 0x2b for GL_RGBA16, 0x2c and
0x2d for the float formats. `cglsAttachPBuffer` creates an `IOAccelSurface`
for it like for a window and then calls the renderer's attach with type
0x5a and the object as the record. `cglsTexImagePBuffer` is
`cglsSetInteger(ctx, 0x3e6, ...)`, the engine's own and not ours, as for
CGL (0x3e5), so the bundle hooks the symbol the same way
(`rdn_hook_cgls_tex_image`). Its memory budget needs no mask:
`fe_cgls_total_vram` is 64 MB.

Everything behind `/Library/Application Support/RadeonNI/coreimage`,
present when the window server starts (`rdn_ws_core_image()`):
- the extension list has `GL_APPLE_client_storage` and `GL_APPLE_float_pixels`
  (and keeps `GL_ARB_fragment_program`, so System Profiler reports Core
  Image again), `GL_RENDERER` says "ATI Radeon HD 7570";
- type 0x5a attaches are ours (the window server's record layout), they
  return 2;
- contexts attached to a pbuffer get Mesa's dispatch table (their own, not
  the screen's);
- `cglsTexImagePBuffer` is ours.
Without the file nothing changes. A copy of `earlyext` named `WindowServer`
shows the lists with and without the file as designed (297 names and the
Radeon name against 293 and Mesa's). Bundle on the G5's disk is
d9bfb565; the running window server still has 9f7f487f mapped.

## 2026-10-08: A6 stage 5, Core Image in the window server works (by readback)

With the user's go-ahead for as many window server restarts as stage 5
needs (`sudo killall WindowServer`; the session comes back in ten seconds,
logged in by itself), the `coreimage` file and the prepared bundle:
`system_profiler` says "Core Image: Supported", `qe` Quartz Extreme in
use, no new crash log.

**What the window server does for a filter on a window** (trace of every
GL call with `/tmp/rdngld.on` and `.trace`, then CoreGraphics and QuartzCore
in disassembly): the plan's wording was wrong. The filter is on what is
*behind* the window, and the window's own picture is drawn over the
result afterwards. It
1. draws the layers underneath into its screen context's buffer;
2. makes a texture and binds it to the screen's surface with
   `cglsSetInteger(ctx, 0x3e6, {surface ID, target, 0x1908, w, h, 0x8367,
   0x400, 0})` (inside CoreGraphics, a plain `bl`: the engine's own
   `gliSetInteger` takes it and does nothing useful for Mesa); the picture
   is the window's rectangle of the drawable, row 0 the top;
3. makes a Core Image context that shares with its own (the 32 bit one;
   the 64 bit one was not used for CIColorInvert), compiles the filter's
   programs, attaches a pbuffer of the filter's size (`cglsAttachPBuffer`,
   type 0x5a, its own record) and copies the backdrop into it with the
   fixed function pipeline, without ever enabling rectangle texturing (its
   contexts start with it on);
4. binds the pbuffer as a texture in the screen context
   (`cglsTexImagePBuffer`, which is `cglsSetInteger(0x3e6)` again) and
   draws a quad over the window with the filter's vertex and fragment
   programs, then the window's own tiles over that.

**What was built** (all behind the file, `rdn_ws_core_image()`):
- the extension names, `GL_RENDERER` "ATI Radeon HD 7570", the bundle's
  Apple handling (`apple_on()`; without it `glPixelStorei(
  GL_UNPACK_CLIENT_STORAGE_APPLE)` gave GL error 0x500 and the window
  server printed "CGXGLAccelCompositeLayer_: GL error 0500 entering");
- pbuffers by the window server's record (size in words 8 and 9), attach
  returns 2, Mesa's dispatch for those contexts, start state with
  `GL_TEXTURE_RECTANGLE` on;
- `rdn_hook_cgls_set_integer`: an inline hook, since the call cannot be
  reached by a symbol pointer: the twelve words of `cglsSetInteger` are
  checked (nothing is changed if they differ), four are replaced by a jump
  to our function, which does what the rest did (the engine's table slot
  0x30) unless it takes the call. Needs `vm_protect` of the text page
  with `VM_PROT_COPY`; the bundle is the only code that changes anything
  in CoreGraphics, and only with the file;
- `OSMesaTexCopyDrawable` (the front end): a GPU copy of a part of the
  current drawable into a new texture image, rows in memory order (the
  first attempt, `glCopyTexImage2D`, had the picture upside down);
- `cglsDestroyPBuffer` and `CGLDestroyPBuffer` hooks: contexts let go of
  the pbuffer, its memory is given back after six more (a finish first),
  because the window server makes a pbuffer for every filter pass.

**Seen by readback** (screen grabs of the G5, `wsfilter`, window 600x400
at 300,300, `WSF_PLAIN=1` = nothing drawn in it): CIColorInvert shows the
desktop behind it inverted (blue to orange, the Finder window black with
white text, right way up); CIGaussianBlur blurs the region, CISepiaTone and
CIPixellate ran. Not seen by the user. Dashboard's ripple not tried yet.

## 2026-10-08: A6 stage 5, end: the first-pass bug, Dashboard, state left on the G5

**The first filter after every window server start came out black**
(later ones right; with the full GL trace on, also right). A copy of the
backdrop that finished first (`fence_finish`) changed nothing; reading the
pbuffer after the Core Image context's draws (debug, with the log on)
showed 0xffffffff in the middle, i.e. the quad had been drawn without a
texture; logging the context's state at its first make-current showed the
rectangle texture binding **0** although the window server had bound
texture 0x73 there (`glIsTexture(0x73)` was 1). The window server's
first calls on a new Core Image context come before its drawable is
attached and carry the screen context's engine context, so they run in
whichever Mesa context is current (no "not ours" line is logged: the
engine context is a known one). The new context therefore never had its
binding; later passes work because that context is the current one when
those calls are made. Fix: when a window server pbuffer context is first
made current it takes over the rectangle texture bound in the context that
was current (`rdn_make_current()`, `inherit`). The trace file can now name
the functions to trace (`/tmp/rdngld.trace` with names, comma or line
separated), which is what made the sequence readable without changing the
timing.

**Dashboard.** `open /Applications/Dashboard.app` shows the widgets over
the dimmed desktop; the widget bar opens (a click on its plus, with
`~/gl/drag`); dragging a widget out and releasing makes the other widgets
ripple for about a second (a grab 1.2 s after the release shows the
Calculator and the World Clock distorted like water, the next one the
settled picture). The window server's own log shows two
`kCGErrorIllegalArgument` lines from the Dock's widget handling
(`CGXSetWindowListAlpha: Invalid window 0`,
`CGXRemoveTrackingArea ... not owned by caller`); not looked into, no
crash log, same window server process. The widget was removed and
Dashboard dismissed again afterwards (a click outside the widgets).

**Left like this on the G5:**
- bundle e6b0b54b (`/System/Library/Extensions/RadeonNIGLDriver.bundle`);
  the one from before this work is `~/RadeonNIGLDriver.before-ci`
  (9f7f487f); `~/gl` has the new `ciprobe`, `wsfilter` and `sharetest`
  (`ciprobe.before-mask` is the old one);
- `/Library/Application Support/RadeonNI/coreimage` present and the
  window server (pid at the time 6006) started with it, no log files: Core
  Image runs on the card there ("Core Image: Supported" in System
  Profiler). To go back: `sudo rm` that file and `sudo killall
  WindowServer`;
- no `pbuffer` file, so programs get pbuffers only with `RDN_PBUFFER=1`;
  nothing else of a program's behaviour changed (Quake 3 148.4, Doom 3
  `bench` 49.0 frames a second after the last change).

**Not done:** floating point pbuffers (stores exist; nothing drew into
one), a long run of ripples (the pbuffers' memory is given back with a
delay by the destroy hooks; no leak check beyond a few filters),
Core Image in a program with a window, stage 6's questions (which renderer
name, default or not) and goal 3 (the user's eyes on the ripple).

## 2026-10-08: the user's decisions after A6, and the ripple's flicker

**The user:** changing the renderer to "ATI Radeon" looks good and should
be the default; the widget ripple looked a bit flickery.

**Defaults now** (stage 6 answered): `GL_RENDERER` is "ATI Radeon HD 7570"
for every program (`RDN_RENDERER=` with nothing in it gives Mesa's own
name back, any other value is used), `GL_APPLE_client_storage` and
`GL_APPLE_float_pixels` are in the extension list of every program that
has the bundle's Apple names (`EXT_DEFAULT`), pbuffers are on (`RDN_NO_PBUFFER=1`,
`RDN_PBUFFER=0` or the file `/Library/Application Support/RadeonNI/nopbuffer`
turn them off), and the window server does Core Image on the card unless
the file `/Library/Application Support/RadeonNI/nocoreimage` exists (the
file `coreimage` is no longer read). Without any switch `ciprobe gl 0x21a00`
takes the card (1.2 ms a render, 0 pixels over 8 against the CPU's);
`appletest`, `sharetest` pass, Quake 3 148.3 and Doom 3 `bench` 47.8
frames a second (49.0 before; one run each).

**The flicker.** A poll of the screen through the aperture
(`rdnuc poll x y w h n`: every 8th pixel of a rectangle summed, 1.8 ms a
sample) over a widget drop showed the picture falling back to the
exact undistorted frame now and then: 40 times in 5 s, twice 5 ms. Logs with a
time on every line (`/tmp/rdngld.time`) and a line for each copy to the
screen (`PRESENT`) showed two copies per frame: the window server draws
the layers underneath in its screen context, switches to Core Image's
context, and Mesa flushes the context it leaves; the screen context
copies to the screen at a flush, so the frame without the filter was
shown until the real `glFlush()` at the end of the frame. Two changes:
- `GL_APPLE_flush_render` (`glFlushRenderAPPLE`, `glFinishRenderAPPLE`)
  was Mesa's `glFlush`/`glFinish`, which also copy to the screen for a
  context that draws on it; now `OSMesaFlushRender` (submit, optionally
  wait, no copy). Does not by itself change what is shown for the window server (the
  context switch is what showed it) but is what the extension means;
- `OSMesaPresentOnSwitch(GL_FALSE)` for the window server: making another
  context current does not copy the old one's picture to the screen.
After: one `PRESENT` per frame, 0 undistorted blips in 5 s (and 20 the run
before the first change), a frame about every 10 ms. Looked at in two
grabs only; the user's eyes decide.

Also made: Dashboard was left with two extra widgets by the test runs (a
click missed); put back (Calculator, Weather-less four as before, Calendar
moved back). A restart of the window server was done for each test (the
user's yes was for stage 5; this was the same work).

On the G5 now: bundle d484fe00, window server started with it, no log
files, no switch files but the older `glthread` and `vertexrange`.

## 2026-10-08: the ripple's remaining flickers, and the G5 out of processes

**The user:** slightly less flickered, but still a couple of flickers.

Every present of the screen context was logged with a time
(`/tmp/rdngld.time`, a `PRESENT` line for each copy to the screen) and,
with `/tmp/rdngld.dump`, a downsampled picture of the area was written
for each (build/ only; removed again, not in the tree). Frames that
differ from both neighbours while the neighbours match each other: one in
twenty, each the frame without the dragged widget, followed 4 ms later
by the whole one. The window server composes a frame in passes (the
layers up to the filtered one, the filter, then the layers above) and
ends each with a `glFlush`, which copies to the screen; a real driver
shows the same partial picture for microseconds.

**Fix:** copies to the screen are put off and merged for up to 6 ms
(`OSMesaDeferPresent`, `OSMesaPresentPending`, `osmesa_present_pending_now`;
the places are kept in the screen's own coordinates, and made at once
before the drawable is dropped). The window server has no run loop (a
`CFRunLoopTimer` test never fired), so the timer is `mach_msg`: its thread
waits with a 16 ms timeout between frames, and `rdn_hook_mach_msg` makes
that wait only as long as the copy has left of its 6 ms, presents on
timeout and then waits out the rest; a message that comes first is handed
on. It turns itself on after the thread has gone idle ten times;
`/Library/Application Support/RadeonNI/nodefer` turns it off. 786 flushes
became 395 copies, and none is closer to the one before than 8 ms (80
pairs were 3 to 7 ms apart). Desktop correct after the animation and after
dragging a Finder window (grabs). Also: `OSMesaTexCopyDrawable` no longer
waits for the GPU. Not seen again by the user.

**The G5 ran out of processes.** Each restart of the window server leaves
a few processes of the old session behind (about two, among them a Dock;
90 after some 45 restarts), and `tiger` hit its limit: every fork failed
("Resource temporarily unavailable"), ssh included except for shell
built-ins. Cleared with the shell's own `kill` over the pids below the
current session's (the user allowed it); a restart with the cleaning step
(`kill -9` of tiger's pids between 77 and the new loginwindow's, from a
shell started after it) keeps the count at about 45. Do that after every
window server restart.

On the G5 now: bundle 7439dfcb (the test debris of this round is out of
it only in part: the mach_msg first-of-a-kind log and the present log run
only with the bundle's log on).

## 2026-10-08: Core Image Fun House

**The user:** in Core Image Fun House the window shows an effect only after
another window has been moved over it.

Two causes, both in the path of a program's own window (a surface of the
window server's):
1. Fun House is single-buffered and ends an update with `glFlush`; the
   window server was told of a new picture only by a buffer swap
   (`rdn_swap`), never by a flush. Now `glFlush` and `glFinish` of a
   surface context that has not swapped finish the picture and tell the
   window server (`rdn_flush_surface`, the same two calls a swap makes). A
   double-buffered program is as before (its context swaps).
2. Adding a Gaussian Blur turned the window black: Core Image keeps about
   a dozen pbuffers of 15 MB (1820x1035 `GL_RGBA16`, 68 made, 4 destroyed
   in a few seconds) and the pbuffers' stores came from the 222 MB heap
   that every window surface and the CPU-visible aperture share; "pbuffer
   1820x1035: no video memory" 30000 times. The stores now come from the
   video memory beyond the aperture first (`rdn_target_vram_alloc_hidden`,
   `RDN_UC_ALLOC_HIDDEN`, 768 MB, GPU only), the heap if that is full.

After: Color Invert and Gaussian Blur (radius 10) show at once on a
1693x961 image, 0 allocation failures, a dozen pbuffers live (181 MB).
Tried by clicks (`drag`) and grabs: Color Invert, Gaussian Blur. Other
filters of its list not tried; Quartz Composer, Preview not tried.

## 2026-10-08: Fun House, an effect above the image (user's report)

**The user:** Copenhagen plus Motion Blur added with the *top bar's* "+"
(which puts the effect above the image row, so it has no input) does
nothing and, when a window is moved over the picture, that region goes
black. A well-placed Motion Blur is fine. The user's guess: an error that
is handled wrongly, not a shader problem.

Reproduced on the G5 (Copenhagen, top "+", Motion Blur: the old picture
stays, then dragging the Finder window over it leaves black staircase
blocks where the window was). The programs compile (log: no error
position). What I could read in the code: when a pbuffer cannot be had
(a size over 16384, a 32-bit overflow of the size, no memory) `pbuffer_for`
returned nothing, `read_record()` failed, and the context was bound to the
16x16 dummy with the attach reported as a success: Core Image went on
drawing into a buffer nobody sees. Changed: sizes are checked in 64 bits and
over 16384 a side is refused, and the attach of a pbuffer that cannot be
made now fails with `kCGLBadAlloc` (0x2720) for both CGL's and the window
server's, so the caller sees the error. Whether this is what Fun House hit
was not confirmed (the clicks I send to the app became unreliable after the
window moved; no log of the failing case was caught). The bundle on the G5
has the change (fd75a5f6), logs off.

## 2026-10-08: all of Core Image's built-in filters

`ciprobe list` (100 names) and a shell loop (`CIPROBE_N=2`, each filter
a process for the soft and for the card render, compared; the table is
`docs/ci-filters-2026-10-08.txt`): all 100 render on the card, none fails,
crashes or says "not tilable". Greatest difference from the CPU picture
within 8 of 255 for 89; for the rest, pixels over 8 out of 196608:
CIBumpDistortion 13, CIGlassDistortion 2, CITriangleTile 255,
CICircleSplashDistortion 270, CIEdgeWork 517, CIParallelogramTile 2418,
CIVortexDistortion 11856, CITwirlDistortion 13159 (all on the edges of
sharp features, by eye), CIRandomGenerator 129358 (noise; the pattern
differs and has the same character), CISpotLight 146505 (the card's is
brighter and bluer; its program uses POW and RSQ; which side is right was
not decided).
Soak: 40 window server filters in a row (Gaussian blur): window server
alive, Core Image still reported, video memory allocations and a Core
Image render still work. Preview opens an image fine. Not tried:
Quartz Composer and its screen savers, iChat effects, floating point
pbuffers, hours of use. The crash log of DashboardClient from 05:24 is
from a window server restart (a client dies when its server does), not
from the driver.

## 2026-10-08: piglit on the G5 (A7)

Built piglit for Tiger (`scripts/build-piglit.sh`; the toolchain image got
cmake, numpy, six; 1,610 test programs, two skipped for a `pipe` name clash
through `tiger_compat.h` -> `unistd.h`). Runner `tools/piglit/run.py` (host)
and `run.pl` (G5, perl 5.8), 5,972 GL 2.1 tests chosen by
`tools/piglit/select.py`.

- Result: 4,774 pass of 4,962 with a verdict (96.2 %); 181 fail, 2 timeouts,
  2 crashes; 81 not built, 21 lost quoting. Multisample: 353/6 fail/287 skip.
  Clusters and ranking in `docs/PIGLIT.md`.
- Negative result 1: first run of every test failed with
  "glGetStringi not found": the context says GL 3.2 but Tiger exports only
  GL 2.1 entry points and the bundle answers four names by `CFBundle`
  lookup, none by `dlsym`. piglit patched to ask `OSMesaGetProcAddress`
  (`RDN_PIGLIT_MESA=1`).
- Negative result 2: a batch started with `nohup &` and then its ssh
  session closed aborted every test ("CFMessagePort: bootstrap_register()
  failed 1100"); Tiger processes lose the window server connection with
  the login session. The run now stays in the foreground of the ssh call.
- No baseline on Apple's renderer yet.
- Biggest clusters: packed pixel types (5551/4444/10_10_10_2/565_REV,
  channels reversed), front/back buffer, occlusion queries returning 0,
  two-sided vertex program lighting, DEPTH32F_STENCIL8's stencil,
  RGB9_E5. `large-tex` shows a 256 MB request failing at 538 MB in use.

## 2026-10-08: piglit cluster 1, packed pixel types on big-endian (fixed)

Reproduced with `tools/guest/packedtest.c` (G5): 4444, 5551, 565_REV and
10_10_10_2 came back with channels reversed or R and B swapped, with
`glGetTexImage` and drawn, for GL_RGBA8 and for native internal formats.
Negative results on the way: the endian macros are right on Tiger
(`UTIL_ARCH_BIG_ENDIAN` 1); the generated format tables are identical to the
Linux big-endian build; gallium's unpack and `_mesa_format_convert` give the
right colours on the G5 (small programs linked against `libmesa_util.a` and
three Mesa sources); `rdn_gltest -K` passes on x86 and under `qemu-ppc` on
softpipe. The error was in the GPU: the staging upload is a blit sampling
the packed format. Cause and fix in `mesa/patches/0006`: the big-endian
description lists the channels of sub-byte packed formats from the top bits,
r600 expects the little-endian order. Adding 5_5_5_1 and 10_10_10_2 to
`r600_colorformat_endian_swap` alone changed nothing; the little-endian view
in the sampler fixed the sampling, the same in colorformat and colorswap
fixed the render-target side (reading a texture back as its own packed type).
Re-run: 66 more tests pass, no regression; multisample 5 more.
Bundle on the G5: this build; the one before is `~/RadeonNIGLDriver.before-packed`.
The window server still has the old copy loaded.

## 2026-10-08: piglit, occlusion queries and RGB9_E5

Occlusion queries counted 0: `r600_query.c` read the GPU's little-endian
result dwords as host dwords (mesa/patches/0007). Timer queries were swapped
the same way. `occtest` 400/0/0. First attempt at a test of mine had the
depth direction wrong (glOrtho maps z -0.5 to the far side). RGB9_E5 needed
the 8IN32 swap (patch 0006). A scan of the games on the G5 found none that
references the query functions, so nothing used the broken results.
Mistake: after editing the tree for a change already covered by patch 0006
the build script's reverse dry-run failed, it tried to apply the patch
forward, and left .rej files; regenerate a patch whenever the tree changes
under it. Regression runs: `ours4` 4,852 pass; the bundle on the G5 is the
build with both patches (`~/RadeonNIGLDriver.rgb9`).
Read of the remaining failures: see docs/PIGLIT.md.

## 2026-10-08: back buffer (branch `back-buffer`), the measurement

The frontend had one colour buffer (`vis->buffer_mask` front only) and every
glFlush showed it. Now a double-buffered window or full-screen context
(`kCGLPFADoubleBuffer`, not the window server) gets a back buffer:
`OSMesaDoubleBuffer`, `OSMesaSwapBuffers`, `OSMesaSwapBuffersAsync`;
`RDN_GLD_NO_BACKBUFFER=1` keeps the old single buffer. piglit: drawbuffer-
modes, front-invalidate-back, swapbuffers-behavior, read-front (both),
fbo-sys-blit, fbo-sys-sub-blit, fcc-front-buffer-distraction now pass.
Two bugs on the way: the draw buffer stayed GL_FRONT (Mesa chose it at the
first make-current, on a drawable with no back buffer); and Mesa validates
again when the front buffer is first needed and the frontend replaced the
back buffer's texture with an empty one (the textures are kept now).
Timedemos, same bundle, `RDN_GLD_NO_BACKBUFFER=1` against not (Quake 3 `four`
1920x1080, 3 runs; Doom 3 `bench`, 2 runs):

| | single buffer | back buffer, swap waits for glthread | back buffer, async swap |
|---|---|---|---|
| Quake 3 | 148.5 | 119.9 | 147.5 to 148.1 |
| Doom 3 | 48.6 / 48.2 | 33.8 | 48.2 / 47.0 |

A swap that drains glthread (`osmesa_sync`) removes the overlap of the
program's thread and Mesa's: -19 % and -30 %; with glthread off the two modes
are equal (119 against 120). `mesa/patches/0008-glthread-done-callback.patch`
lets the swap run in glthread's thread after the frame; a full-screen swap is
queued, a window's (read by the window server at once) still waits. The
result is neutral within 0 to 2.5 %. Not yet seen by eye in the games; the
window server is untouched. Full piglit re-run on the branch still to do.
