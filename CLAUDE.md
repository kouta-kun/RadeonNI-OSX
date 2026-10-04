# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Goal

An open-source (MIT) graphics driver for Mac OS X 10.4 Tiger on PowerPC for
the AMD Radeon HD 7570 ("Turks", Northern Islands, DCE5). Phase 1 ends when
Tiger running in `qemu-system-ppc -M mac99` shows its desktop at native
resolution on a monitor attached to the real card, passed through with VFIO.
The eventual target is a Power Mac G5 Late 2005; acceleration is out of scope
for this phase. `PROMPT.md` is the user's original brief.

## Current state

Milestone 0 is done: the card is Turks PRO `1002:675d` at `0000:10:00.0`
(audio at `10:00.1`) with GDDR5 memory (not DDR3 as the brief assumed), the
VBIOS is in `private/vbios.rom`, and the reference trace of the stock Linux
driver is in `traces/` and described in `docs/REFERENCE-TRACE.md`. The
monitor is on the DVI-I connector; its EDID prefers 1366x768.

Milestone 2 is in progress: our library cold-POSTs the real card (same
register sequence as Linux, with or without the I/O BAR) and the video
memory works without the MC microcode. EDID over DDC and the DCE5 modeset
are next. Milestone 1 waits for the Tiger install media from the user.
`docs/PLAN.md` has the milestone states and the list of things needed from
the user; check it first.

## Documents

Read `docs/PLAN.md` and the tail of `docs/JOURNAL.md` before doing anything.

- `docs/PLAN.md`: milestones, success criteria, state, what the user owes.
- `docs/JOURNAL.md`: dated experiment log. Append an entry for every
  experiment, including negative results.
- `docs/HARNESS.md`: host facts, QEMU command line, patches, recovery.
  Sections marked PLANNED have not been run.
- `docs/HARDWARE.md`: facts about this specific card.
- `docs/REFERENCE-TRACE.md`: how the Linux driver's trace was captured, its
  phases and findings.
- `docs/RESEARCH.md`: prior research. `[V]` is verified, `[I]` is inference.
  Treat `[I]` as a hypothesis; fix the document when reality differs.

Keep these current as part of the work, and commit small and often.

## Commands

- `make`: builds the hardware library and tests for x86 (`build/x86/`) and
  big-endian PowerPC (`build/ppc/`).
- `make test`: runs each test natively and under `qemu-ppc`; the two outputs
  must be identical. Needs `private/vbios.rom` and the reference trace phase
  files, and skips loudly without them. A single test by hand:
  `build/x86/atom_replay private/vbios.rom traces/ref-radeon-2.phase-a1.txt`.
- `scripts/fetch-deps.sh`: PowerPC cross toolchain and Linux radeon sources
  into `third_party/`. Run once, with `scripts/build-qemu.sh`, on a fresh
  checkout.
- `sudo build/x86/rdn_tool {status|post|vramtest}`: our code on the real
  card through sysfs; `-t file` logs accesses in the trace format, `-n`
  avoids the I/O BAR. Needs `scripts/card-bind.sh none` first.
- `scripts/card-reset.sh`: back to the un-POSTed state (bus reset).
- `sudo scripts/card-state.py`: is the card POSTed (read-only).
- `scripts/host-inventory.sh`: read-only host inspection (GPUs, drivers,
  IOMMU groups, tools). Re-run after any hardware or kernel change.
- `scripts/card-bind.sh {status|vfio|none|radeon}`: move the 7570 between
  host drivers at runtime. After a host reboot it is back on `radeon`.
- `scripts/x86-trace-guest.sh <name>` then `scripts/trace-split.py
  traces/<name>.log`: capture and reduce a reference trace of the stock
  Linux driver. The compact per-phase files are diffable.
- `scripts/build-qemu.sh`: builds QEMU 11.1.1 (`qemu-system-ppc`, `qemu-ppc`)
  into `third_party/qemu/`, applying `patches/qemu/*.patch`. The host has no
  packaged PowerPC QEMU; always use this build.
- `scripts/tiger.sh {create|install|cdrom|run|passthru|snapshot|restore|ssh}`:
  Tiger guest lifecycle. Screen on VNC `127.0.0.1:5901`, ssh on port 2222.
  `passthru <addr>` attaches the card with `x-no-mmap=on` and writes the
  register trace to `traces/`.

The kext does not exist yet. Add its commands here when it does.

## Architecture

- `hw/` is the hardware library: freestanding C99 that may include only
  `<stdint.h>`, `<stddef.h>`, `<stdarg.h>`, `<stdbool.h>` and `<string.h>`.
  It must also compile with Apple gcc 4.0.1 inside the Tiger guest.
- `hw/rdn_os.h` is the only way out of the library: MMIO, I/O BAR, config
  space, delay, clock, allocation, logging. Register values cross it in CPU
  byte order; the implementation swaps. Userspace tool, tests and kext each
  provide one implementation.
- `hw/atom/` is the AtomBIOS interpreter and table headers taken from Linux
  with minimal edits; `atom_port.h` maps the kernel services it expects onto
  the OS layer. Keep it close to upstream and keep the copyright headers.
  `hw/rdn_atom.c` holds the register callbacks, including Linux's I/O BAR
  index/data behaviour.
- `tools/rdn_tool.c` is the Linux sysfs implementation of the OS layer and
  the milestone 2 front end.
- `tests/` replays our code against the reference trace: every register
  access must be the next one Linux made. This is how code is validated
  before it runs on the card, and how big-endian correctness is checked.

## Host safety rules

The host is the user's server, reached over ssh. Stop and ask before:

- Rebooting it or changing how it boots (kernel parameters, initramfs,
  blacklists).
- Touching the host GPU, `0000:30:00.0` (Cezanne iGPU, `amdgpu`), or anything
  that could cost the user access. The chipset's IOMMU group 9 includes the
  NIC; never bind any of it to `vfio-pci`.
- Enabling `mmiotrace` (it takes all CPUs but one offline).
- Writing to the card's flash. Nothing is ever flashed.
- Abandoning a design decision below.

Bind the 7570 at runtime with sysfs `driver_override`; no persistent host
configuration. Tell the user as soon as something needs them physically.

## Design decisions (settled by the user)

- One hardware library shared by the Linux userspace tool (milestone 2) and
  the kext (milestone 3). It must not depend on Linux or IOKit; everything
  goes through a minimal OS layer (MMIO, config space, delay, memory, log).
- Endian-clean from the first line. The target is big-endian. Every register
  and VBIOS table access uses explicit accessors; no bitfields over AtomBIOS
  structures without a big-endian variant. VBIOS parsing and the AtomBIOS
  interpreter must run as an automated big-endian test without hardware
  (PowerPC Linux build under `qemu-ppc` against the VBIOS dump).
- No command ring, microcode or interrupts in this phase, but nothing that
  would prevent them later.
- The VBIOS comes from the PCI expansion ROM, with a load-from-file fallback.
  Never from the x86 legacy address.
- Cold POST follows the Linux `radeon` initialisation order.
- The kext is built inside the Tiger guest over ssh with Xcode 2.5 and
  Apple's gcc. It is always loaded by hand with `kextload` from a temporary
  directory and never installed in `/System/Library/Extensions`. Use QEMU
  snapshots.
- QEMU: emulated VGA stays primary; `vfio-pci` with `x-no-mmap=on` plus the
  `vfio_region_*` trace events gives the register trace to compare against
  the reference. No `x-vga`.
- Mac OS X 10.4, for Classic. If it becomes a serious obstacle versus 10.5,
  explain why before switching.

## Licensing

MIT. Code may be ported from the MIT-headed files of Linux `radeon` and from
Haiku `radeon_hd`, keeping their copyright notices. Do not copy GPL code
(QemuMacDrivers, GPL parts of Linux), APSL code, or anything derived from
decompiled Apple binaries; reading them to understand an interface is fine.

Never commit VBIOS dumps, microcode, disk images, Apple media or raw traces.
They live in git-ignored directories: `private/`, `firmware/`, `images/`,
`media/`, `traces/`, and third-party sources in `third_party/`.

## Verification

Only the user can see the 7570's output. Exhaust self-checks first: register
readback, comparison with the reference trace, EDID validity, `ioreg`, guest
kernel log. When eyes are needed, ask one concrete question, use test
patterns that are unambiguous to describe (colour bars in a stated order, a
one-pixel border on all four edges, the resolution as text), and batch the
checks. Do not mark a milestone done before the user confirms.

## Conventions

Code, comments, commits and documentation are in English. Answer the user in
the language they write in (they may use Spanish).
