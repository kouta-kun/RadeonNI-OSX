# Plan

Phase 1 ends when Mac OS X 10.4 Tiger, running in QEMU, shows its desktop at
native resolution on a monitor connected to the real Radeon HD 7570.

A milestone is not done until the user has confirmed what is on the screen
wherever its criterion involves video output.

| # | Milestone | State |
|---|---|---|
| 0 | Know the card, capture ground truth | **Blocked**: card not on the PCI bus |
| 1 | Tiger in QEMU with the card passed through | QEMU built and guest scripts ready; waiting for install media and the card |
| 2 | Cold POST and modeset from Linux userspace | Not started |
| 3 | `IOFramebuffer` kext in Tiger on QEMU | Not started |

## Needed from the user

| What | For | Status |
|---|---|---|
| Card seated in the CPU x16 slot (`PCI_E1`), host powered back on | M0 | **open** |
| BIOS: primary display left on the integrated GPU (IGD) | M0 | to check while installing |
| Monitor connected to the 7570 | M0 (EDID, modeset trace) | open |
| Approval of the M0 host actions listed below | M0 | open |
| Mac OS X 10.4 PowerPC install DVD image (retail, ideally 10.4.6 or later) | M1 | open |
| Mac OS X 10.4.11 Combo Update (PPC) `.dmg`, if the DVD is older | M1 | open |
| Xcode 2.5 Developer DVD `.dmg` | M1 | open |

Media goes in `media/` (git-ignored). Guest disk: 32 GB qcow2 (sparse).

## Milestone 0 — Know the card, capture ground truth

Success: PCI ID and memory type confirmed, VBIOS dumped and hashed, register
trace of init and of a modeset saved and documented.

Steps:

1. **Card enumerates.** `scripts/host-inventory.sh`; record address, IDs,
   BARs, IOMMU group in HARDWARE.md and HARNESS.md.
   - Stop if the ID is not Turks (`6759`/`675d`). Redwood changes the plan.
   - Stop if the card shares an IOMMU group with anything but its own audio
     function.
2. **What Linux says.** `radeon` will have bound at boot. From dmesg: chip
   family, VRAM size and type, connectors, whether MC microcode was loaded.
   From sysfs: EDID of the attached monitor.
3. **VBIOS dump.** `echo 1 > rom; cat rom > private/vbios.rom; echo 0 > rom`
   on the sysfs node. Read-only; nothing is written to the card. Record size,
   SHA-256 and MD5 in HARDWARE.md. Cross-check the memory type against the
   VBIOS tables.
4. **Register trace.** Two captures of the same thing, init and one modeset:
   - **(a) VFIO trace, preferred.** Pass the card to a throwaway x86 Linux
     guest (QEMU/KVM, `x-no-mmap=on`, `vfio_region_*` trace events) and let
     the guest's `radeon` initialise it. Same tool and trace format we will
     later use for the kext, so the comparison is like for like. Nothing on
     the host changes beyond rebinding the card to `vfio-pci`.
   - **(b) mmiotrace on the host**, as the brief suggests. Works, but while
     the tracer is active the kernel takes every CPU except one offline, on
     the whole server. Only with explicit approval.
5. **Document.** Trace files stay in `traces/` (ignored); a summary of the
   init order and the modeset sequence goes in `docs/`.

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
4. Expected trouble: OpenBIOS and the 64-bit 256 MB BAR0. Patch OpenBIOS or
   QEMU as needed; document in HARNESS.md. If unworkable, stop and present
   alternatives.

## Milestone 2 — Cold POST and modeset from Linux userspace

Success: from an un-POSTed card, the program shows a test pattern at the
monitor's native resolution (user confirms).

1. Find and document a reliable way back to the un-POSTed state.
2. Hardware library behind a small OS layer (MMIO, config space, delay,
   memory, log). No Linux or IOKit dependencies inside it.
3. VBIOS reader, AtomBIOS interpreter, `asic_init`, DDC/EDID, DCE5 modeset.
4. Big-endian test: build for PowerPC Linux, run under `qemu-ppc` against the
   VBIOS dump, compare the interpreter's register writes with the x86 run.
   Automated.
5. Compare against the milestone 0 trace before asking for a visual check.

## Milestone 3 — `IOFramebuffer` kext in Tiger on QEMU

Success: Tiger desktop on the 7570's monitor at native resolution, surviving a
resolution change from System Preferences (user confirms).

1. Kext skeleton matching on `IOPCIMatch`, built in the guest, loaded by hand.
2. Single fixed mode.
3. Modes from EDID, resolution and depth switching, cursor.
