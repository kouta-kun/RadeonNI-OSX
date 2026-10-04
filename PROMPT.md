# Prompt for Claude Code: Radeon HD 7570 driver for Mac OS X 10.4 PowerPC (phase 1)

Paste this file as the first message. `RESEARCH.md` must be in the same directory: it holds the prior research and its sources.

---

## Goal

We are going to write a new graphics driver for Mac OS X 10.4 Tiger on PowerPC, for an AMD Radeon HD 7570 ("Turks" family, Northern Islands), a GPU Apple never supported on that platform. The code will be published as open source.

The final target is a Power Mac G5 Late 2005 (PowerMac11,2, dual core, with a GeForce 6600 LE as the console card), and in the long run we want Quartz Extreme and OpenGL. None of that is part of this phase. **This phase ends when Tiger, running in QEMU, shows its desktop at native resolution on a monitor connected to the real 7570.**

I chose 10.4 for Classic compatibility. If 10.4 ever turns out to be a serious obstacle compared with 10.5, tell me why before switching.

## Where you are running

You are on an x86_64 Linux server that has the HD 7570 installed as a secondary GPU, alongside another GPU used by the system. The host has an IOMMU. The 7570 will have a monitor attached that only I can see: there is no capture card.

The first thing you do should be to inspect the host (distro, kernel, IOMMU state, `lspci -nnvv` for both GPUs, which driver each one has, QEMU version) and write it down. Do not take any of this as known.

## Milestones for this phase

**Milestone 0. Know the card and capture ground truth.**
- Confirm the PCI ID. It should be Turks (`1002:6759` or `1002:675D`). Some "HD 7570" cards are rebranded Redwood (Evergreen, DCE4); if that is the case, stop and tell me, because it changes the plan.
- Confirm the memory type. I believe it is DDR3/GDDR3 and not GDDR5, which would avoid needing the `TURKS_mc.bin` microcode. Verify it from the VBIOS or from what the Linux driver reports.
- Dump the VBIOS from the expansion ROM and record its hashes. The dump does not go into the repository.
- With the Linux `radeon` driver driving the card, capture a register trace of initialisation and of a modeset (mmiotrace works on x86). This is the reference our code will be compared against.
- Success criterion: ID and memory confirmed, VBIOS dumped, trace saved and documented.

**Milestone 1. Tiger in QEMU with the card passed through.**
- Boot Mac OS X 10.4 in `qemu-system-ppc -M mac99` (TCG emulation) and pass the 7570 to it with `vfio-pci`. The emulated VGA stays as the primary console.
- The Tiger image does not exist yet: I will build it. Tell me exactly what you need from me (install media, Xcode 2.5, disk size) and prepare scripts so the installation is as automatic as possible. Leave ssh enabled in the guest.
- Success criterion: the card shows up in `ioreg` inside Tiger as an `IOPCIDevice`, with its BARs assigned and accessible.
- It is quite possible that OpenBIOS will not assign the 64-bit or 256 MB BARs correctly. If OpenBIOS or QEMU needs patching, do it and document the patch. If it turns out to be unworkable, stop and present alternatives before continuing.

**Milestone 2. Cold POST and modeset from Linux userspace.**
- Write a hardware library (VBIOS reading, AtomBIOS interpreter, `asic_init`, EDID over DDC, DCE5 modeset) and a Linux userspace program that uses it against the real card.
- The card must come up without anyone having initialised it first, because on the Mac, Open Firmware does not run the x86 ROM. Find a reliable way to return it to the un-POSTed state between tests and document it.
- Success criterion: starting from an un-POSTed state, the program shows a test pattern at the monitor's native resolution.

**Milestone 3. `IOFramebuffer` kext in Tiger on QEMU.**
- The same hardware library, wrapped in a kext with a native `IOFramebuffer` subclass that matches through `IOPCIMatch`. First a single fixed mode; then modes from EDID, resolution and depth switching, and cursor.
- Success criterion: the Tiger desktop is visible on the 7570's monitor at native resolution, and survives a resolution change from System Preferences.

## Design decisions already made

- **One hardware library for userspace and kernel.** The code that touches the card must not depend on Linux or on IOKit: isolate it behind a minimal layer (MMIO access, config space access, delays, memory, logging). That way what is validated in milestone 2 is what runs in milestone 3.
- **Endian-clean from the first line.** The target is big-endian even though milestone 2 runs on x86. Every register access and every VBIOS table access goes through explicit accessors. Find a way to run VBIOS parsing and the AtomBIOS interpreter in big-endian without hardware (for example by building for PowerPC Linux and running under qemu-user against the dump), and make it an automated test.
- **No acceleration, but do not close the door on it.** Do not implement the command ring, microcode or interrupts in this phase. Do avoid decisions that would prevent them later.
- **The kext is built inside the Tiger guest**, over ssh, with Xcode 2.5 and Apple's gcc. Cross-compiling PPC kexts with modern toolchains is unproven; do not spend time there unless the in-guest loop is unbearable.
- **The kext is always loaded by hand** with `kextload` from a temporary directory. Never install it in `/System/Library/Extensions`, so that a reboot always recovers the guest. Use QEMU snapshots.
- **MIT licence.** You may port code from the MIT-headed files of the Linux `radeon` driver and from Haiku's `radeon_hd`, keeping the copyright notices. Do not copy GPL code (QemuMacDrivers, the GPL parts of Linux), Apple code under APSL, or anything derived from decompiled Apple binaries. Consulting them to understand an interface is fine.
- **Code, comments, commits and project documentation in English.** I may write to you in Spanish; answer me in the language I use.

## What the prior research already knows

Read `RESEARCH.md` in full before planning. The essentials:

- VFIO does not depend on KVM or on host and guest sharing an architecture. There is a precedent of a real Rage 128 passed to `qemu-system-ppc` with 10.4.11, which required patching OpenBIOS. Nobody has reported a Radeon HD under `mac99`.
- With `x-no-mmap=on` and QEMU's `vfio_region_read`/`vfio_region_write` trace events you get a complete trace of the registers the guest touches. Use it to compare the kext against the milestone 0 trace.
- `mac99` only exposes PCI/AGP, not PCIe, and caps RAM at 2 GB.
- The closest prior attempt (pc297, MacRumors) got a kext loading on a real G5 and stalled at a black screen because it looked for the VBIOS at the x86 legacy address. The VBIOS has to be read from the PCI expansion ROM, with a fallback of loading it from a file.
- The AmigaOS 4 driver hit two traps worth anticipating: bitfields in AtomBIOS parameter structures on big-endian, and POST ordering (disable VGA and wait for the memory controller before `asic_init`).
- MorphOS fails to cold-start several cards of this family on the G5, while Linux manages it. Follow the Linux initialisation order.

`RESEARCH.md` marks what is verified and what is inference. Treat inferences as hypotheses: check them against source code or hardware before building on them, and correct the document when you find something different.

## Verification

Only I can see the 7570's video output. So:

- First exhaust the checks you can do alone: read back CRTC and PLL registers, compare against the reference trace, check that the EDID you read is valid, inspect `ioreg` and the guest kernel logs.
- When you need my eyes, ask with a concrete question, and use test patterns that are easy to describe unambiguously (colour bars in a known order, a one-pixel border on all four sides, text showing the resolution). Batch visual checks so you do not interrupt me at every step.
- Do not declare a milestone done until I have confirmed what is on screen.

## When to stop and ask me

Work autonomously on everything else, but stop and check before:

- Anything that requires rebooting the host or changing how it boots (kernel parameters, initramfs modules, driver blacklists).
- Anything that touches the host's main GPU or could leave me without access to the server.
- Writing to the card's flash. Nothing gets flashed, ever.
- Abandoning one of the design decisions above.

When something needs me physically (connecting the monitor, obtaining install media), tell me as soon as you know, not once you are already blocked.

## What to produce first

Before writing any driver code:

1. Initialise the git repository and create `CLAUDE.md` with what a future session needs to know without having read this conversation: goal, current state, how to start the harness, how to build and load, host safety rules, and project conventions. Keep it short and current.
2. Create the base documentation in `docs/`:
   - `RESEARCH.md` (the file I am handing you; from now on you maintain it).
   - `PLAN.md` with the milestones, their success criteria and the state of each.
   - `HARNESS.md` with the reproducible environment: host configuration, QEMU command line, patches, how to recover the card and the guest.
   - `JOURNAL.md`, a dated log of experiments: what was tried, what was observed, what was concluded. Negative results are recorded too.
   - `HARDWARE.md` with what we learn about this specific card (IDs, BARs, connectors, relevant VBIOS tables).
3. A `.gitignore` that excludes VBIOS dumps, microcode, disk images and Apple install media.
4. The host inventory and a plan for milestone 0, including which host changes need my approval.

Then proceed milestone by milestone. Make small, frequent commits, and update `PLAN.md` and `JOURNAL.md` as you go.
