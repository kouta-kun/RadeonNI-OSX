# Plan

Phase 1 ends when Mac OS X 10.4 Tiger, running in QEMU, shows its desktop at
native resolution on a monitor connected to the real Radeon HD 7570.

A milestone is not done until the user has confirmed what is on the screen
wherever its criterion involves video output.

| # | Milestone | State |
|---|---|---|
| 0 | Know the card, capture ground truth | **Done 2026-10-04** (see REFERENCE-TRACE.md, HARDWARE.md) |
| 1 | Tiger in QEMU with the card passed through | **Done 2026-10-04**: `IOPCIDevice` with BAR0/BAR2 assigned; a probe kext reads the registers through BAR2 (needs the QEMU PCI-hole patch) |
| 2 | Cold POST and modeset from Linux userspace | **Done 2026-10-04**: test pattern at 1366x768 from an un-POSTed card, confirmed on screen by the user |
| 3 | `IOFramebuffer` kext in Tiger on QEMU | In progress: the kext POSTs the card, reads the EDID and sets 1366x768 from inside Tiger, pattern confirmed by the user; the `IOFramebuffer` subclass itself is next |

## Needed from the user

| What | For | Status |
|---|---|---|
| Card seated in the CPU x16 slot (`PCI_E1`), host powered back on | M0 | done 2026-10-04 |
| BIOS: primary display left on the integrated GPU (IGD) | M0 | done (iGPU is `boot_vga`) |
| Monitor connected to the 7570 | M0 (EDID, modeset trace) | done: DVI-I via DVI-to-HDMI adapter |
| Approval of the M0 host actions listed below | M0 | given 2026-10-04 (rebinding, module load/unload; trace in an x86 guest, no mmiotrace) |
| Host reboot after the kernel oops of 2026-10-04 | M1, M2 | done; `radeon` stays unloaded |
| Whether the slightly yellow white is the monitor or the signal (try `rdn_tool -d modeset`) | later | open, user said to ignore for now |
| Which resolution counts as native | M2, M3 | decided: the EDID's preferred timing, 1366x768@59.79 |
| Mac OS X 10.4 PowerPC install DVD image | M1 | done: `media/tiger-install.iso` |
| Mac OS X 10.4.11 Combo Update (PPC) `.dmg`, if the DVD is older | M1 | not needed: Apple's Software Update still serves it |
| Xcode 2.5 Developer DVD `.dmg` | M1 | done: converted to `media/xcode25.img` |

Media goes in `media/` (git-ignored). Guest disk: 32 GB qcow2 (sparse).

## Milestone 0 — Know the card, capture ground truth

Success: PCI ID and memory type confirmed, VBIOS dumped and hashed, register
trace of init and of a modeset saved and documented.

Steps:

1. ~~**Card enumerates.**~~ Done: `0000:10:00.0`, `1002:675d` Turks PRO, own
   IOMMU group (10, with its audio function).
2. **What Linux says.** Done except EDID: family TURKS, 1024 MB **GDDR5**
   (not DDR3 as assumed), DisplayPort + DVI-I, firmware leaves the card
   un-POSTed. EDID read: preferred 1366x768@59.79, also 1920x1080@60.
3. **VBIOS dump.** Done; hashes in HARDWARE.md. `echo 1 > rom; cat rom > private/vbios.rom; echo 0 > rom`
   on the sysfs node. Read-only; nothing is written to the card. Record size,
   SHA-256 and MD5 in HARDWARE.md. Cross-check the memory type against the
   VBIOS tables.
4. **Register trace.** Done with the VFIO method in an x86 guest
   (`scripts/x86-trace-guest.sh`): cold init, fbcon modeset, two explicit
   modesets, blank and unblank. mmiotrace was not used.
5. **Document.** Done: `docs/REFERENCE-TRACE.md`.

Host actions that need approval before they happen:

| Action | Why | Risk |
|---|---|---|
| Power off, seat card, power on | Card not detected | Host downtime; needs physical access |
| Unbind `radeon` from the 7570 and bind `vfio-pci` at runtime | 4(a), and all later milestones | None to the host GPU; no persistent change |
| Load `radeon` with `modprobe`, unload with `rmmod` | Controlled init for tracing | `radeon` never binds to the Cezanne iGPU |
| Enable `mmiotrace` | 4(b) only | Host runs on one CPU while tracing |

Not planned: kernel parameters, initramfs changes, blacklists, anything on
`30:00.0`, any write to the card's flash.

## Milestone 1 — Tiger in QEMU with the card passed through

Success: the card shows in `ioreg` in Tiger as an `IOPCIDevice` with its BARs
assigned and accessible.

1. ~~Build QEMU (`ppc-softmmu`, `ppc-linux-user`) from source in `third_party/`.~~ Done 2026-10-04 (`scripts/build-qemu.sh`).
2. Install Tiger on `mac99` with emulated VGA; update to 10.4.11; install
   Xcode 2.5; enable Remote Login; install an ssh key. Snapshot.
3. Add `vfio-pci`. Inspect the OpenBIOS device tree and `ioreg`.
3a. Done 2026-10-04: OpenBIOS assigns BAR0 (256 MB) at `0x90000000` and BAR2
   at `0xa0000000` with no patch. It does not assign the I/O BAR or the ROM.
   Remaining: a probe kext that maps BAR2 and reads registers, to show the
   BARs are accessible. The kext takes the VBIOS from a file under `mac99`
   (see "Deferred to the real G5").
4. Expected trouble: OpenBIOS and the 64-bit 256 MB BAR0. Patch OpenBIOS or
   QEMU as needed; document in HARNESS.md. If unworkable, stop and present
   alternatives.

## Milestone 2 — Cold POST and modeset from Linux userspace

Success: from an un-POSTed card, the program shows a test pattern at the
monitor's native resolution (user confirms).

1. ~~Find and document a reliable way back to the un-POSTed state.~~ Done:
   `scripts/card-reset.sh` (secondary bus reset through the root port).
   - ~~Does VRAM work after `asic_init` without `TURKS_mc.bin`?~~ Yes, as far
     as the aperture test goes. No microcode loader is planned.
2. Hardware library behind a small OS layer (MMIO, config space, delay,
   memory, log). No Linux or IOKit dependencies inside it. OS layer done
   (`hw/rdn_os.h`).
3. VBIOS reader, AtomBIOS interpreter, `asic_init`, DDC/EDID, DCE5 modeset.
   - Done: interpreter (`hw/atom/`), bring-up (`hw/rdn_post.c`), verified
     against the reference trace without hardware and then on the card
     (`tools/rdn_tool.c`), with and without the I/O BAR.
   - Done: EDID over DDC (`hw/rdn_i2c.c`), identical to the EDID Linux read.
   - Next: the DCE5 modeset for the DVI-I digital
     output, validated by replay against trace phases `a3`/`a4` before it
     runs on the card. Then the test pattern and the user's visual check.
4. Big-endian test: build for PowerPC Linux, run under `qemu-ppc` against the
   VBIOS dump, compare the interpreter's register writes with the x86 run.
   Automated. Done for `asic_init` (`make test`); extend as code is added.
5. Compare against the milestone 0 trace before asking for a visual check.

## Milestone 3 — `IOFramebuffer` kext in Tiger on QEMU

Success: Tiger desktop on the 7570's monitor at native resolution, surviving a
resolution change from System Preferences (user confirms).

1. Kext skeleton matching on `IOPCIMatch`, built in the guest, loaded by hand.
2. Single fixed mode.
3. Modes from EDID, resolution and depth switching, cursor.

## Deferred to the real G5

Things knowingly left untested under QEMU, to revisit once the driver runs
on the Power Mac G5.

- **VBIOS from the PCI expansion ROM.** Under `mac99` the kext loads the
  VBIOS from a file, because OpenBIOS does not assign the ROM BAR (user
  decision, 2026-10-04; OpenBIOS is not patched for this). The ROM-read path
  in the kext is therefore never exercised in the harness. On the G5: check
  that Open Firmware assigns the ROM (`assigned-addresses` entry for
  register 0x30), implement or enable reading it through the ROM BAR, compare
  the image against the file byte for byte, and make the ROM the primary
  source with the file as fallback. Keep in mind the pc297 failure (VBIOS
  taken from the wrong place) and the warning that the ROM size in the
  device tree may be truncated.
