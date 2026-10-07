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

One persistent host change exists, approved by the user on 2026-10-04:
`/etc/modprobe.d/osx-gpu.conf` contains `blacklist radeon`, so the stock
driver never binds to the card (unbinding it is the suspected cause of the
host oops below). The initramfs holds `amdgpu` only, not `radeon`, so the
blacklist takes effect through udev without regenerating it. Delete the file
to undo. After a boot, check `lsmod | grep radeon` is empty before touching
the card; `snd_hda_intel` still takes the audio function and is unbound by
the script.

Beyond that the script uses sysfs `driver_override` on
both functions, releases fbcon first, and refuses to act on anything that is
not `1002:675d` or that is the boot VGA. `vfio`, `none` and `status` have been run; `radeon` has not.

Before the blacklist, the stock `radeon` auto-bound about 60 s into every
host boot, POSTed the card and put fbcon on it. Unbinding it logged two kernel WARNs
(`irq_domain_remove`, `msi_device_data_release`) and tainted the kernel with
W. With the blacklist the card stays un-POSTed and driverless after boot,
and the monitor shows nothing until our code drives it.

## What OpenBIOS does with the card (2026-10-04, unpatched)

| Resource | Result |
|---|---|
| BAR0, 64-bit prefetchable, 256 MB | assigned at `0x90000000` |
| BAR2, 64-bit, 128 KB | assigned at `0xa0000000` |
| BAR4, I/O | not assigned |
| Expansion ROM | not assigned |
| Interrupt | line 0x1b (IRQ 27) |

Tiger shows the card as `pci1002,675d@10`, an `IOPCIDevice` with those two
ranges in `assigned-addresses` and `IODeviceMemory`. Inspect it with
`ioreg -p IODeviceTree -n pci1002,675d -w0`; `ioreg -l` on the whole tree
fails in this guest.

While idle on `vfio-pci` the card sits in D3hot and its registers read
all-ones from the host; `scripts/card-state.py` reports that as unreadable.

Start the passthrough guest with `sudo scripts/tiger.sh passthru
0000:10:00.0 <trace-name>` (root is needed for VFIO); then `guest-ctl.py`
also needs sudo, because the QMP socket belongs to root.

## Known host hazard: kernel oops on INTx setup (2026-10-04)

The first `scripts/tiger.sh passthru` run oopsed the host kernel in
`vfio_pci_set_intx_trigger`. It happened in a boot where `radeon` had
earlier been unbound from the card (with WARNs). In the next boot, with
`radeon` blacklisted and never bound, the same run worked. Do not start a
`mac99` passthrough guest in a boot where `radeon` has been bound to the
card. See JOURNAL.

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

### Installing Tiger

Media (git-ignored): `media/tiger-install.iso` is a link to the user's DVD
image. The Xcode 2.5 `.dmg` has to be converted, because QEMU cannot read it:

    7z x -tdmg -oimages/x xcode_2.5_8m2558_developerdvd.dmg
    (cd images/x && cat 0.ddm 1.Apple_partition_map 2.Apple_Driver_ATAPI 3.hfs 4.free) > images/xcode25.img

The installer is driven without a VNC client:

    scripts/tiger.sh create
    scripts/tiger.sh install media/tiger-install.iso &
    scripts/guest-wait.sh             # until the screen stops changing
    scripts/guest-ctl.py shot         # build/shot.png, then look at it
    scripts/guest-ctl.py click X Y    # screenshot coordinates
    scripts/guest-ctl.py type '...' ; scripts/guest-ctl.py key ret

Steps taken (2026-10-04): choose English; Utilities > Terminal;
`diskutil partitionDisk disk0 1 APMFormat "Journaled HFS+" Tiger 31G`;
quit Terminal; Continue; Agree; destination `Tiger`; Install (Easy Install).

When the copy finishes the guest restarts into the DVD again: stop QEMU and
start it with `scripts/tiger.sh run`. First boot, then:

- Setup assistant: OK, `z`, `/` for the keyboard; defaults elsewhere; leave
  the Apple ID blank; Command-Q on the registration form and Skip; account
  `tiger`, password `tiger`.
- In Terminal (Spotlight: Command-Space, "Terminal"):
  `sudo launchctl load -w /System/Library/LaunchDaemons/ssh.plist` and
  `sudo pmset -a sleep 0 displaysleep 0 disksleep 0`.
- ssh key: RSA key pair in `private/ssh/tiger_rsa` (Tiger's OpenSSH 3.8 does
  not know newer key types); its public half goes in
  `~tiger/.ssh/authorized_keys`. `scripts/tiger.sh ssh` uses it.
- `tiger ALL=(ALL) NOPASSWD: ALL` appended to `/etc/sudoers`.
- `sudo softwareupdate -i MacOSXUpdCombo10.4.11PPC-10.4.11`, restart.

The guest is only reachable from the host's loopback (port 2222), which is
why the weak password is tolerable.

The `softwareupdate` step does not work (it never downloads). Instead fetch
`MacOSXUpdCombo10.4.11PPC.tar` from the URL in Apple's catalog on the host,
copy it in with `scripts/tiger.sh ssh 'cat > /tmp/combo.tar' < file`, unpack
twice with `tar`, and run `sudo installer -pkg ... -target /`.

Apple's `installer` hangs after "Assembling receipt" on large packages under
QEMU. The files are in place by then: check `/var/log/install.log`, restart
the guest, and verify. Do not start another `installer` before the restart.

Xcode 2.5 (`scripts/tiger.sh cdrom media/xcode25.img`), packages under
`/Volumes/Xcode Tools/Packages/Packages`:

- `installer -pkg` into `/`: `DevToolsSystem`, `DeveloperToolsCLI`,
  `gcc4.0`, `DevSDK`, `BSDSDK` (restart after one hangs).
- Unpacked into `/Developer` with
  `gzip -dc <pkg>/Contents/Archive.pax.gz | sudo pax -r -pe`:
  `MacOSX10.4.Universal`, `DeveloperToolsCLI`, `gcc4.0`, `DeveloperTools`.

Snapshots: `installed-raw` (10.4.6, before first boot), `tiger-10.4.11`,
`clean-install` (10.4.11 with Xcode 2.5, ssh and sudo set up).

Only one QMP client can be connected at a time: do not run `guest-ctl.py`
while `guest-wait.sh` is running.

### A real keyboard and pointer

`TIGER_EVDEV=/dev/input/by-id/<node>` makes `scripts/tiger.sh` start QEMU
with `-object input-linux` for that host event device, and
`scripts/guest-cycle.sh ready` then runs `scripts/tiger.sh evdev`, which
(re)attaches it over QMP and makes the emulated relative mouse the first
pointer device (`mouse_set`). Without that step a click goes to the
absolute tablet and throws the pointer into the top left corner. QEMU
grabs the device; both Ctrl keys together give it back to the host. After
unplugging the device QEMU does not reopen it: run `scripts/tiger.sh
evdev` again. `scripts/tiger.sh evdev off` detaches it.

Do not pass a USB keyboard or mouse through with `usb-host`: QEMU's OHCI
allows one pending packet for the whole controller, an idle device's
interrupt poll holds it, and Tiger logs a USB timeout every 13 s.

## Guest recovery (PLANNED)

- The kext is only ever loaded with `kextload` from a temporary directory; a
  guest reboot always returns to a clean system.
- A `clean-install` qcow2 snapshot is taken once Tiger, updates, Xcode and ssh
  are in place; `qemu-img snapshot -a clean-install` restores it.

## Patches

Patches live in `patches/qemu/` and are applied by `scripts/build-qemu.sh`
when it unpacks the source. After adding one to an existing tree, apply it by
hand and run `ninja qemu-system-ppc` in `third_party/qemu/`.

| Patch | Why |
|---|---|
| `0001-uninorth-widen-mac99-pci-hole.patch` | `mac99`'s main PCI bus exposed only `0x80000000`-`0x8fffffff` to the CPU. OpenBIOS puts the card's BARs at `0x90000000` and `0xa0000000`, which read as zeros without reaching the device. The patch widens the alias to `0x80000000`-`0xefffffff`. |

OpenBIOS is not patched.

## Building and loading the kext

Under QEMU the kext is always built fresh and loaded from a temporary
directory, never from an installed copy, so that what runs is never stale.
With a passthrough guest running, `scripts/kext.sh up` does it all; the
steps are:

    scripts/kext.sh build     # tar hw/ and kext/ to the guest, make there
    scripts/kext.sh load      # root-owned copy in /tmp/rdnkext, kextload
    scripts/kext.sh activate  # restart the window server so it uses the screen
    scripts/kext.sh up        # build + load + activate
    scripts/kext.sh log       # the driver's lines from the kernel log
    scripts/kext.sh unload

After `load`, Tiger's window server has to be restarted before it uses the
new screen: `scripts/kext.sh activate` does that and waits until the display
is attached (about 15 s; the login session ends and the guest logs in
again). `scripts/kext.sh up` is build, load and activate in one. Asking the
framebuffers to re-probe (`tools/guest/fbprobe.c`) does not help.

Changing the 7570's resolution from the GUI without seeing its monitor:
`open /System/Library/PreferencePanes/Displays.prefPane` over ssh, click
"Gather Windows", then `open -a "System Preferences"` and Command-` to bring
the 7570's panel to the front on the emulated screen. With two displays the
scripted pointer is off: to hit (X, Y) send roughly
(400 + (X - 400) / 1.174, 300 + (Y - 300) / 1.146), and check the screenshot.

`scripts/tiger.sh passthru` does not trace by default, so that drawing is at
normal speed. `TIGER_TRACE=1` adds `x-no-mmap=on` and the trace events; every
framebuffer write is then trapped and the desktop takes minutes to draw.

## Package for the real Mac

    scripts/make-g5-package.sh --with-vbios    # build/RadeonNI-g5.tar.gz

The package holds the kext as built in the guest, `install.sh`,
`uninstall.sh`, a README and, with `--with-vbios`, the VBIOS dump (leave it
out of anything published). On the Mac: unpack, `sudo ./install.sh`,
restart. The kext reads the VBIOS from the card's ROM (2026-10-06), so the
dump is not needed; if one is there, `install.sh` puts it into the kext's
Info.plist as the fallback. Without one it checks in the device tree that
Open Firmware gave the ROM an address and refuses to install if not. It
validates the kext with `kextload -t -n`, copies it to
`/System/Library/Extensions` and invalidates the extension caches.

Rehearsed in the guest on 2026-10-04: after `install.sh` and a restart the
kext loaded at boot, cold-POSTed the card and the window server used the
screen without being restarted; after `uninstall.sh` and a restart the guest
was clean again. It has never run on a real Mac.

Notes for Apple gcc 4.0.1 on PowerPC: no `-mkernel`; link the objects before
`-lkmodc++ -lkmod -lcc_kext`; `OSBundleLibraries` for `com.apple.kernel.*`
at version 6.0.
