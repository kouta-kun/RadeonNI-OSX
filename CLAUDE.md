# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Goal

An open-source (MIT) graphics driver for Mac OS X 10.4 Tiger on PowerPC for
the AMD Radeon HD 7570 ("Turks", Northern Islands, DCE5). Phase 1 ends when
Tiger running in `qemu-system-ppc -M mac99` shows its desktop at native
resolution on a monitor attached to the real card, passed through with VFIO.
The eventual target is a Power Mac G5 Late 2005; acceleration is out of scope
for phase 1. Phase 2 is acceleration: Quartz Extreme, Core Image and OpenGL
2.0. `PROMPT.md` is the user's original brief (phase 1).

## Current state

**Phase 1 is complete (2026-10-04), every milestone confirmed on screen by
the user.** Tiger 10.4.11 in QEMU shows its desktop on the real card at
1366x768 and 1920x1080, at 8, 16 and 32 bpp, with resolution changes from
System Preferences and a software cursor.

- The card is Turks PRO `1002:675d` at `0000:10:00.0` (audio at `10:00.1`),
  GDDR5, monitor on the DVI-I connector. VBIOS dump in `private/vbios.rom`.
- `hw/` cold-POSTs the card, reads the EDID over DDC, starts the display
  engine clock and sets modes through AtomBIOS. No MC microcode is needed.
- `kext/RadeonNI` is the `IOFramebuffer` subclass built on it. In the guest
  it is loaded by hand (`scripts/kext.sh up`); the window server only looks
  for framebuffers when it starts, so `up` restarts it. Installed in
  `/System/Library/Extensions` it comes up at boot by itself (verified in
  the guest with the G5 package, then uninstalled).
- `scripts/make-g5-package.sh` builds `build/RadeonNI-g5.tar.gz` for the
  real Power Mac G5. It has never run on a real Mac.
- The tag `working-framebuffer` marks the confirmed phase 1 state including
  the G5 install package.
- The guest: snapshots `clean-install` and `pre-kext-install` (no kext
  installed in either), user `tiger` / password `tiger`,
  `scripts/tiger.sh ssh`. QEMU must be the patched build (`patches/qemu/`).
  Screensaver and display sleep are off in the guest.
- `radeon` is blacklisted on the host (`/etc/modprobe.d/osx-gpu.conf`)
  because unbinding it led to a host kernel oops.

What was left out on purpose is listed in `docs/PLAN.md` under "Known gaps
after phase 1" and "Deferred to the real G5".

**Phase 2 (acceleration) was planned with the user on 2026-10-04.**
`docs/PLAN.md`, "Phase 2", has milestones A0 to A7. The route: Mesa's `r600`
driver behind a `gld*` GL driver bundle that gives Apple's public GL
dispatch table to Mesa, over a kernel half (command processor, memory,
fences) ported from Linux into `hw/`. The first run on the real G5 has not
been planned in detail.

- A0's criterion is met (2026-10-05): Tiger's OpenGL loads our bundle
  (`gld/`, a logging pass-through to Apple's software renderer) and an
  off-screen GL program runs through it with `glClear` replaced by ours.
  Only one of the 686 entries was replaced, and only off-screen.
- A1 works by readback (2026-10-05), not yet confirmed on the monitor by
  the user: `hw/rdn_gpu.c`, `hw/rdn_cp.c` and `hw/rdn_selftest.c` start the
  3D engine and the command processor and draw a textured square and
  triangle, from `rdn_tool accel` on the x86 host.
- A2 works by readback (2026-10-05): Mesa 26.2.4's `r600` renders
  fixed-function GL on the card through `mesa/winsys` and `hw/`, with no
  Linux DRM, on x86 and, pixel-identical, as a big-endian PowerPC build
  under `qemu-ppc` (`scripts/build-mesa.sh x86|ppc`). Not confirmed on the
  monitor.
- A3 and the first form of A4 work in the guest (2026-10-05). The user
  confirmed a windowed GL program on the 7570's monitor; the tag
  `working-cpu-copy` marks that state. A1's and A2's own pictures were
  checked by readback only. The kext starts the
  3D engine and serves a user client (`RDN_ACCEL=1 scripts/kext.sh up`).
  Mesa 26.2.4 is cross-built for Tiger (`scripts/build-mesa.sh darwin`).
  The driver bundle with Mesa inside (`scripts/gld.sh install-mesa`) makes
  ordinary CGL and GLUT programs render through Mesa's r600 on the card,
  off-screen and in windows; each frame is copied into the buffer Apple's
  code presents. Not done: full-screen contexts, presenting without the
  copy.
- A5 (Quartz Extreme) is confirmed on the monitor by the user
  (2026-10-05): with `RDN_ACCEL=1 RDN_ACCELCAPS=3 RDN_AGPSHIM=3 RDN_GA=1
  RDN_SURFACES=1 scripts/kext.sh up` the window server composites the
  7570's display with OpenGL through Mesa on the card; Exposé works.
  `docs/QUARTZ-EXTREME.md` has the gates, what the window server does and
  what is not done. The GPU copies each finished update to the screen;
  `/tmp/rdngld.copy` in the guest selects the older CPU copy. Open: it
  looks a bit slow. The Tiger build of Mesa needs `-fno-strict-aliasing`
  (display lists break without it).
- A4's second half works by readback (2026-10-05), not yet seen by the
  user, and is opt-in (`touch /tmp/rdngld.surface` in the guest): a
  program's OpenGL window is a window server surface, drawn by Mesa and
  shown by the card, no CPU copy. Apple's Chess, started normally, is
  correct that way, also while its window is dragged (the user saw
  the first version flicker and leave trails; the fix is checked by
  readback only). How it works is in the journal's last entries (the window server
  textures from the surface's buffer directly). `/var/log/windowserver.log`
  in the guest is the window server's own log.
  `~/gl/drag x0 y0 x1 y1` drags with the mouse from inside the guest. Without the file, windows
  go through the software renderer's buffer as before, and Chess is
  wrong.
- A6 and A7 have not started.
- The guest currently has `RadeonNIGLDriver.bundle` and
  `RadeonNIGA.plugin` installed in `/System/Library/Extensions`; the
  snapshots do not. They are inert unless the kext is loaded with
  `RDN_ACCEL=1` (and `RDN_GA=1`).
- `docs/GLD-INTERFACE.md` is what has been learned about Apple's GL driver
  interface.

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
- `docs/GLD-INTERFACE.md`: Apple's OpenGL driver interface as observed.
- `docs/QUARTZ-EXTREME.md`: what the window server requires before it
  composites with OpenGL, and how far it gets with us.
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
- `sudo build/x86/rdn_tool accel` (after `post` and `modeset`): start the
  3D engine and command processor and draw the self-test picture.
  `sudo build/x86/rdn_tool grab file.ppm` saves the scanout surface, so the
  picture can be checked without the monitor (`pnmtopng` to view it).
  The microcode must be in `firmware/` (`zstd -d` from `/lib/firmware/radeon`).
- `sudo build/x86/rdn_tool {status|post|vramtest|edid|modeset|peek}`: our code on the real
  card through sysfs; `-t file` logs accesses in the trace format, `-n`
  avoids the I/O BAR. Needs `scripts/card-bind.sh none` first.
- `scripts/card-reset.sh`: back to the un-POSTed state (bus reset).
- `scripts/card-quiet.sh {apply|status}`: stop the card and its root port
  from escalating PCIe errors (see the host safety rules).
- `sudo scripts/card-state.py`: is the card POSTed (read-only).
- `scripts/host-inventory.sh`: read-only host inspection (GPUs, drivers,
  IOMMU groups, tools). Re-run after any hardware or kernel change.
- `scripts/card-bind.sh {status|vfio|none|radeon}`: move the 7570 between
  host drivers at runtime. After a host reboot it is driverless (`radeon` is
  blacklisted) and un-POSTed.
- `scripts/x86-trace-guest.sh <name>` then `scripts/trace-split.py
  traces/<name>.log`: capture and reduce a reference trace of the stock
  Linux driver. The compact per-phase files are diffable.
- `scripts/build-qemu.sh`: builds QEMU 11.1.1 (`qemu-system-ppc`, `qemu-ppc`)
  into `third_party/qemu/`, applying `patches/qemu/*.patch`. The host has no
  packaged PowerPC QEMU; always use this build.
- `scripts/guest-ctl.py {shot|click|key|type}` and `scripts/guest-wait.sh`:
  operate the guest's GUI through QMP (screenshots to `build/shot.png`,
  clicks at screenshot coordinates). Look at the screenshot before clicking.
- `scripts/tiger.sh {create|install|cdrom|run|passthru|snapshot|restore|ssh}`:
  Tiger guest lifecycle. Screen on VNC `127.0.0.1:5901`, ssh on port 2222.
  `passthru <addr>` attaches the card; it needs sudo, and so does
  `guest-ctl.py` while that guest runs. With `TIGER_TRACE=1` it adds
  `x-no-mmap=on` and writes the register trace to `traces/`, which makes
  framebuffer drawing extremely slow.

- `TIGER_NOVGA=1` with `scripts/tiger.sh passthru`: no emulated display,
  so that the 7570 is the only one (works, 2026-10-05; nothing on VNC,
  wait for ssh rather than `guest-cycle.sh ready`, then `kext.sh up`).
  The default keeps the emulated display. `~/gl/cgmode main N` in the
  guest makes a display the main one; the 7570's is, since 2026-10-05.
- `tools/guest/cgmode.c`: build in the guest (`gcc -o cgmode cgmode.c
  -framework ApplicationServices`) to list and switch display modes through
  Quartz and to put the cursor on a display.
- `scripts/kext.sh up`: build `kext/RadeonNI` inside the running guest, load
  it from `/tmp` with `kextload`, and restart the guest's window server so
  that it uses the screen. The steps are also available one by one
  (`build`, `load`, `activate`), plus `unload`, `log` and `uninstall`.
- `RDN_ACCEL=1 scripts/kext.sh up`: as above, and the kext also publishes
  its accelerator service (`kext/RadeonNI/RadeonNIAccel.cpp`), which makes
  OpenGL load the GL bundle. Without the variable the kext is phase 1's.
- `scripts/gld.sh install-mesa`: install the host-built driver bundle that
  has Mesa inside (`scripts/build-mesa.sh darwin
  src/gallium/targets/rdn/RadeonNIGLDriver.dylib`). `RDN_GLD_LOG=file` in a
  program's environment makes it log.
- `tools/guest/glwin.c`: a windowed GLUT test for the guest.
- `tools/guest/fences.c`: how many command buffers the GPU completed in a
  few seconds; zero when nothing draws with the card.
- For Quartz Extreme the user chose trial and error in the guest
  (2026-10-05), not captures from the real G5.
- `scripts/gld.sh {build|install|uninstall|log|clearlog}`: build
  `gld/RadeonNIGLDriver.bundle` in the guest and install it in the guest's
  `/System/Library/Extensions` (OpenGL loads it only from there). `log`
  shows every `gld*` call made to it (`/tmp/rdngld.log` in the guest).
- `RDN_HWCURSOR=1` with `scripts/kext.sh up`: the card's hardware cursor
  (`hw/rdn_cursor.c`) instead of `IOFramebuffer` drawing the cursor with
  the CPU. Confirmed on the monitor by the user (2026-10-05) with
  `~/gl/curmove` (`tools/guest/curmove.c`), which moves the pointer over a
  display for a given time.
- `scripts/ga.sh {build|install|uninstall}`: the 2D accelerator plug-in
  (`ga/RadeonNIGA.plugin`) the window server wants before it tries Quartz
  Extreme; built and installed in the guest like the GL bundle.
- In the guest, `touch /tmp/rdngld.on` makes every process that loads the
  bundle log its `gld*` calls to `/tmp/rdngld.<pid>.log`; with
  `/tmp/rdngld.trace` as well, every GL call. `~/gl/rdnuc grab file.ppm`
  saves the 7570's screen, `~/gl/qe` says whether Quartz Extreme is in
  use, `~/gl/fences` counts GPU command buffers over 5 s
  (`tools/guest/`).
- `tools/guest/glprobe.c`: build in the guest (command in its header) to
  list OpenGL renderers and to draw off-screen on a chosen renderer
  (`glprobe draw 0x20400` goes through our bundle).
- `scripts/build-mesa.sh`: fetch Mesa 26.2.4 into `third_party/`, apply
  `mesa/patches/`, copy `mesa/` and `hw/` into its tree and build
  `librdngl.so` for the host. Run it again after editing `mesa/` or `hw/`.
  Then (card posted and mode set with `rdn_tool`):
  `sudo third_party/mesa-26.2.4/build-x86/src/gallium/targets/rdn/rdn_gltest
  -n 90 -s 1366 768 1408 -o build/gltest.ppm`. `scripts/build-mesa.sh ppc`
  builds the same test static for big-endian PowerPC; run it with
  `sudo third_party/qemu/qemu-ppc -cpu 7447a .../build-ppc/.../rdn_gltest`.
  `-a`, `-b` and `-D` vary the scene so that runs can be told apart.
- `scripts/darwin.sh image` once, then `scripts/darwin.sh <command>`: run
  the cross toolchain for Tiger (GCC 14.2 for `powerpc-apple-darwin8`, the
  10.4u SDK) in a container that sees only the repository.
  `scripts/build-mesa.sh darwin [targets]` builds Mesa with it;
  `.../build-darwin/src/gallium/targets/rdn/rdn_gltest` is the test program
  to copy into the guest (`-s` draws on the screen).
- `scripts/guest-cycle.sh down`, then `sudo scripts/tiger.sh passthru
  0000:10:00.0` in the background, then `scripts/guest-cycle.sh ready`: a
  clean guest restart (a loaded framebuffer kext cannot be unloaded) with
  the card's error masking re-applied. `TIGER_BOOTARGS=debug=0x100` makes
  a guest kernel panic print its backtrace on the emulated screen.
- `tools/guest/qe.c`: is Quartz Extreme in use on each display.
- `scripts/make-g5-package.sh [--with-vbios]`: package the guest-built kext
  with `g5/install.sh`, `g5/uninstall.sh` and `g5/README.txt` into
  `build/RadeonNI-g5.tar.gz`, to be unpacked and installed on the real Mac.

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
- `kext/RadeonNI/` is the Tiger kext, built in the guest by a plain Makefile
  (no Xcode project) that compiles `hw/` into it unchanged. `compat/` holds
  the two standard headers Kernel.framework lacks. The VBIOS reaches it as a
  `VBIOS` data property injected into the personality by `scripts/kext.sh
  load`; it is never part of the built bundle or the repository.
- `hw/rdn_accel.h` is the acceleration core: `rdn_gpu.c` (3D engine setup),
  `rdn_cp.c` (microcode, ring, fences, indirect buffers), `rdn_selftest.c`.
  Everything the GPU reads is in video memory, written little-endian through
  the aperture; see the header for the rules (host data path flush).
  `hw/linux/` holds register headers copied from Linux unchanged.
- `hw/rdn_modeset.c` builds AtomBIOS parameter blocks byte by byte in
  little-endian layout (no structs, no bitfields) and follows the Linux
  call order recorded in `traces/ref-radeon-3.atomcalls.txt`.
- `gld/` is the OpenGL driver bundle. Built in the guest it forwards every
  `gld*` call to Apple's `GLRendererFloat` and logs it, with experiment
  switches. Built on the host with Mesa (`RDN_MESA`), it keeps that
  forwarding for the context's life cycle and gives the application's GL
  dispatch table to Mesa (`rdn_mesa.c`, glue generated by
  `gen_dispatch.py` from the SDK header at build time).
- `mesa/` is what this project adds to Mesa: `winsys/` (r600's winsys over
  `rdn_device.h`, no DRM), `frontend/` (off-screen GL contexts, from Mesa's
  former OSMesa), `target/` (the device for Linux and the library that ties
  it together), `patches/` (the only changes to Mesa itself).
- `kext/RadeonNI/RadeonNIAccel.cpp` is the accelerator service and its user
  client; `hw/rdn_user.h` is the interface user space sees.
  `mesa/target/rdn_device_darwin.c` is Mesa's side of it. `mesa/darwin8/`
  supplies what Tiger's C library lacks.
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
- Starting a `mac99` passthrough guest in a host boot where `radeon` has
  been bound to the 7570: that combination oopsed the host kernel once.
- Driving the card's command processor (`rdn_tool accel`, `rdn_gltest`, a
  guest with the accelerator) without `scripts/card-quiet.sh apply` first.
  On 2026-10-05 the host reset with "an uncorrected error caused a data
  fabric sync flood event" during such tests; the user then approved
  masking the card's PCIe error escalation at runtime. A host reboot undoes
  the masking, and a reset of the card undoes the card's half: apply again.
  `sync` before a test that has never run before.
- Writing to the card's flash. Nothing is ever flashed.
- Abandoning a design decision below.

Bind the 7570 at runtime with sysfs `driver_override`. The only persistent
host configuration is the user-approved `blacklist radeon` file. Tell the user as soon as something needs them physically.

## Design decisions (settled by the user)

- One hardware library shared by the Linux userspace tool (milestone 2) and
  the kext (milestone 3). It must not depend on Linux or IOKit; everything
  goes through a minimal OS layer (MMIO, config space, delay, memory, log).
- Endian-clean from the first line. The target is big-endian. Every register
  and VBIOS table access uses explicit accessors; no bitfields over AtomBIOS
  structures without a big-endian variant. VBIOS parsing and the AtomBIOS
  interpreter must run as an automated big-endian test without hardware
  (PowerPC Linux build under `qemu-ppc` against the VBIOS dump).
- Phase 1 had no command ring, microcode or interrupts. Phase 2 brings the
  command ring and microcode in, and interrupts last (A7). The first memory
  model keeps everything in VRAM: no GART, no bus mastering, fences polled.
- Microcode (`TURKS_pfp`, `TURKS_me`, `BTC_rlc`, `TURKS_mc`) is handled like
  the VBIOS: git-ignored `firmware/`, injected at load time, never committed.
- Apple's GL plug-in ABI is learned by observation (symbol lists, a logging
  shim around Apple's software renderer, VMsvga2's MIT sources) and by
  reading disassembly to understand the interface. All code is written
  fresh. Findings go in `docs/GLD-INTERFACE.md`.
- Mesa is the newest release (26.2.4 on 2026-10-05), not an older one that
  would build more easily: the big-endian fixes for this GPU are recent
  (user's decision).
- Mesa and the GL bundle may be cross-compiled on the host. The GL bundle
  may be installed in the guest's `/System/Library/Extensions` if the
  framework only loads it from there.
- The VBIOS comes from the PCI expansion ROM, with a load-from-file fallback.
  Never from the x86 legacy address. Under QEMU the kext uses the file,
  because OpenBIOS does not assign the ROM BAR and the user chose not to
  patch it; the ROM path must be revisited on the real G5 (`docs/PLAN.md`,
  "Deferred to the real G5").
- Cold POST follows the Linux `radeon` initialisation order.
- The kext is always built inside the Tiger guest over ssh with Xcode 2.5
  and Apple's gcc. Under QEMU it is always loaded from a temporary directory,
  freshly built (`scripts/kext.sh up`), never from an installed copy, so
  that what runs is never stale (user's decision, 2026-10-04). Installing
  into `/System/Library/Extensions` is how it runs on the real Mac; that is
  done by the package from `scripts/make-g5-package.sh` (`g5/install.sh`).
  The package may be rehearsed in the guest, and must be uninstalled again
  afterwards.
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

Copyright holder for new files: "kouta-kun and Claude".

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
