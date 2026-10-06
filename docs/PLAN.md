# Plan

Phase 1 ends when Mac OS X 10.4 Tiger, running in QEMU, shows its desktop at
native resolution on a monitor connected to the real Radeon HD 7570.

A milestone is not done until the user has confirmed what is on the screen
wherever its criterion involves video output.

**Phase 1 is complete (2026-10-04).** The sections "Known gaps" and
"Deferred to the real G5" at the end list what was left out on purpose.

| # | Milestone | State |
|---|---|---|
| 0 | Know the card, capture ground truth | **Done 2026-10-04** (see REFERENCE-TRACE.md, HARDWARE.md) |
| 1 | Tiger in QEMU with the card passed through | **Done 2026-10-04**: `IOPCIDevice` with BAR0/BAR2 assigned; a probe kext reads the registers through BAR2 (needs the QEMU PCI-hole patch) |
| 2 | Cold POST and modeset from Linux userspace | **Done 2026-10-04**: test pattern at 1366x768 from an un-POSTed card, confirmed on screen by the user |
| 3 | `IOFramebuffer` kext in Tiger on QEMU | **Done 2026-10-04**: desktop on the 7570 at 1366x768, resolution change from System Preferences to 1920x1080, depth switching and cursor, confirmed on screen by the user |

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

## Known gaps after phase 1

None of these blocks the phase 1 goal; each is a deliberate omission or an
unexplained observation.

Driver:

- **Topology is hard-coded** in `hw/rdn_modeset.c`: CRTC 0, pixel PLL 1,
  digital encoder 0, UNIPHY link A, hot-plug line 1, DDC line 0x93. It must
  come from the VBIOS object table before any other connector or card works.
  The DisplayPort connector is not driven at all.
- **Modes** are only the EDID's detailed timings (two on the test monitor).
  No standard or established timings, no CEA modes, no scaling.
- **HDMI** signalling is used when the EDID asks for it, with the AVI
  infoframe (since 2026-10-05) but without audio. The boot argument
  `rdn_dvi=1` makes the kext use DVI signalling instead. The user saw white looking slightly yellow; untested whether
  that is the monitor or this. `rdn_tool -d modeset` forces DVI signalling.
- **Line buffer and watermark setup** (Linux's `radeon_bandwidth_update`) is
  not ported.
- **Cursor** is IOGraphics' software cursor; the hardware cursor is not
  implemented.
- **8 and 16 bpp** were switched without error but not each inspected by
  eye for long.
- **No power management**: no DPMS, sleep or wake handling, no display
  hot-plug detection. The EDID is read once at start.
- **Unloading** the `IOFramebuffer` kext while the window server uses it has
  not been tried.
- **The MC microcode is not loaded.** Memory works through the 256 MB
  aperture after `ASIC_Init`; only a sparse test and normal desktop use
  back that.
- **Framebuffer address range** is left where the BIOS put it
  (`0xF00000000`); Linux relocates it. Fine for scanout, to revisit for
  acceleration.

Harness:

- A framebuffer loaded with `kextload` after boot is only used once the
  window server restarts (`scripts/kext.sh activate`). Not an issue for the
  installed kext, which loads before it.
- QEMU needs `patches/qemu/0001-...`; OpenBIOS's `ranges` property and the
  bridge's address-select register still describe 256 MB.
- Unbinding the host's `radeon` from the card led to a host kernel oops;
  `radeon` is blacklisted on the host as a result.
- Apple's `installer` hangs after "Assembling receipt" on large packages in
  the guest.
- With two displays the scripted pointer (`guest-ctl.py`) lands off target.

## First run on the real G5 (not planned in detail yet)

`scripts/make-g5-package.sh --with-vbios` produces the package; the user
installs it with `sudo ./install.sh`. Alternatively the whole system:
`images/tiger-g5.qcow2` (2026-10-05) is a copy of the guest's disk with
that package installed (phase 1 kext, no acceleration keys) and unpacked
in `~/RadeonNI-g5`; it loads the kext at boot under QEMU. Write it to a
disk of 32 GB or more with `sudo qemu-img convert -p -O raw
images/tiger-g5.qcow2 /dev/sdX`. It has not booted on the G5. Things to look at first on the Mac:
`ioreg -p IODeviceTree -n pci1002,675d -w0` (did Open Firmware assign BAR0
and BAR2, and the ROM?), `sudo dmesg | grep RadeonNI`, and whether the
6600 LE stays the console.

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

# Phase 2 — hardware acceleration

Planned with the user on 2026-10-04. Goal: Quartz Extreme, Core Image and
OpenGL 2.0 on the 7570 under Tiger, reached milestone by milestone. The
background and the evidence are in RESEARCH.md section 10.

## Approach

```
app / WindowServer -> OpenGL.framework -> GLEngine (gli*)
   -> RadeonNIGLDriver.bundle (gld* lifecycle; dispatch table -> Mesa)
        Mesa: GL state tracker + r600 gallium driver + "rdn" winsys
   -> IOUserClient -> RadeonNI.kext: IOAccelerator service
        hw/ accel core: CP ring, microcode, buffers, fences (shared C)
```

- Mesa's `r600` is the 3D driver. It is a complete GL implementation, so it
  cannot be an ordinary `gld*` driver under Apple's `GLEngine`; the bundle
  implements the lifecycle calls and gives the public GL dispatch table
  (`gliDispatch.h`) to Mesa. A0 showed the bundle can reach and change that
  table; `gldInitDispatch` does not hand it over, the engine's context does.
- The kernel half (command processor, memory, fences) is ported from the
  MIT-headed Linux `radeon` files into `hw/`, under the same rules as phase
  1, and shared by `rdn_tool` and the kext.
- First memory model: everything in VRAM (ring, command buffers, textures)
  through the 256 MB aperture. No GART, no bus mastering, no interrupts;
  fences are polled. GART and interrupts come in A7.

## Milestones

| # | Milestone | Success criterion | State |
|---|---|---|---|
| A0 | Prove the plug-in route, no hardware | A GL program in the guest runs through our `gld*` bundle and shows the effect of a dispatch entry we replaced | **Criterion met 2026-10-05** for an off-screen context: one entry (`glClear`) replaced. Replacing all 686 for the life of a context is still unproven (GLD-INTERFACE.md, "Not known yet") |
| A1 | Command processor from Linux userspace (x86, real card) | `rdn_tool` draws a triangle into the scanout buffer; user confirms | **Works by framebuffer readback 2026-10-05** (`rdn_tool accel`, `rdn_tool grab`); the user has not yet confirmed it on the monitor |
| A2 | Mesa on our winsys (x86 Linux, real card, no Linux DRM) | Mesa renders an animated test on the monitor; then the big-endian build under `qemu-ppc` does the same | **Works by readback 2026-10-05** on x86 and big-endian (`qemu-ppc`), pixel-identical; not yet confirmed on the monitor |
| A3 | Kext accelerator + Mesa in Tiger | A full-screen CGL program in the guest renders on the 7570 | **Mostly done 2026-10-05**: kext engine and user client; Mesa built for Tiger; CGL contexts (off-screen and window) render through Mesa on the card. Not done: a full-screen context |
| A4 | Windowed OpenGL | A windowed GL program on the desktop; a renderer query reports our renderer and GL 2.0 | **First form confirmed on the monitor by the user 2026-10-05** (tag `working-cpu-copy`): a GLUT program in a window renders through Mesa on the card (reports AMD TURKS, GL 3.2), by copying each frame into the window's buffer. Second form 2026-10-05, by readback, the default since the evening of 2026-10-05 (`/tmp/rdngld.nosurface` turns it off): the window is a window server surface, drawn by Mesa and shown by the card with no CPU copy; Apple's Chess is correct that way. Not seen by the user; shadows over a surface are wrong |
| A5 | Quartz Extreme | Quartz Debug reports it enabled; user confirms the effects | **Confirmed on the monitor by the user 2026-10-05** (QUARTZ-EXTREME.md): with all gates met the window server composites the 7570's display with OpenGL through Mesa on the card; desktop, windows, dragging and Exposé work. Open: it looks a bit slow. The software cursor flickered slightly; the hardware cursor (`RDN_HWCURSOR=1`, confirmed by the user) does not |
| A6 | Core Image | Hardware-rendered Core Image filters (Dashboard ripple) | not started |
| A7 | Hardening | GART and interrupts, an accelerated 2D GA plug-in (the one from A5 uses the CPU), performance, piglit subset | not started |

A0 and A1 are independent and may run in parallel. If A0 fails, stop and
present alternatives (a `gli*`-level engine replacement, or a native `gld*`
driver without Mesa).

The real G5 is a parallel track that needs the user: first boot of the
phase 1 package, then each A-milestone repeated there. Its 6600 LE under
working Quartz Extreme is the only reference for A4 and A5, because the
guest has no accelerated device.

## Risks, in order

1. The dispatch takeover (A0); everything from A3 on depends on it.
2. Private structures in the `gld*` lifecycle calls (pixel format, renderer
   info, drawable).
3. The window server's requirements for Quartz Extreme cannot be observed
   under QEMU.
4. Mesa `r600` big-endian bugs. The newest release is used (26.2.4; user's
   decision, 2026-10-05: the big-endian fixes for this GPU are recent).
5. Tiger userland: no thread-local storage, missing libc functions, and a
   C++17 compiler and runtime, which current Mesa `r600` requires.
6. GDDR5 without the MC microcode under 3D load; load `TURKS_mc` if A1
   shows memory errors.
7. TCG speed: fine for correctness, useless for judging performance.

## Decisions (user, 2026-10-04)

- Apple's GL plug-in ABI may be learned by observation (symbol lists, a
  logging shim around Apple's software renderer, VMsvga2's MIT sources) and
  by reading disassembly to understand the interface. All code is written
  fresh; nothing decompiled is copied. Findings go in `docs/GLD-INTERFACE.md`.
- Microcode is handled like the VBIOS: git-ignored `firmware/`, injected at
  load time, never committed.
- Microcode, the command ring and (in A7) interrupts are in scope.
- Mesa and the GL bundle may be cross-compiled on the host; the kext stays
  guest-built.
- The GL bundle may be installed in the guest's `/System/Library/Extensions`
  if A0 shows the framework only loads it from there.
