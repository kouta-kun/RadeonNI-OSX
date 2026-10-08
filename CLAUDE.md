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
- `images/tiger-g5.qcow2` (2026-10-05) is a copy of the guest's disk with
  the G5 package installed, to be written to a disk for the real G5
  (`docs/PLAN.md`, "First run on the real G5"). Never booted there. The
  guest's own disk has the snapshot `before-g5-image` from the same moment.
- The G5 (`PowerMac11,2`, 192.168.1.127 since 2026-10-07, before that
  .128) runs with the card and the
  installed phase 1 kext at 1920x1080 (2026-10-05, kernel log read over
  ssh). The card is in the G5, not in the host, so nothing on the host can
  use it. Reach the G5 like the guest but on port 22: user `tiger`, key
  `private/ssh/tiger_rsa`, the old-algorithm options from
  `scripts/tiger.sh ssh`; `sudo` there asks for the password (`tiger`).
  Open Firmware assigns the card's ROM BAR there (journal). Since the
  same day it boots with `install.sh --accel` and `~/gl/qe` reports
  Quartz Extreme in use; the user has not yet said what they see.
- DisplayPort works (2026-10-07, G5, seen by the user: "It looks about
  correct"): the monitor is on the card's DisplayPort connector, 4 lanes
  at 1.62 Gbit/s, 1920x1080, Quartz Extreme in use. `hw/rdn_dp.c`; the
  kext tries DVI-I first, then DisplayPort, and logs DPCD and the
  training result. No Linux trace of DisplayPort exists; `tests/dp_link`
  is a simulated sink. Not done: hot-plug and monitor wake (the link is
  trained only at a mode set), 2.7 Gbit/s on a real sink, passive
  adapters. The kext before it on the G5 is `~/RadeonNI.kext.before-dp`
  (journal 2026-10-07; `docs/HARDWARE.md`, "DisplayPort connector").
- The host's sshd accepts Tiger's old ssh algorithms
  (`/etc/ssh/sshd_config.d/10-tiger.conf`, user's request).
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
- `bool` has four bytes on Tiger PowerPC. Mesa code that assumes one
  breaks silently there and not in the big-endian Linux test build
  (`mesa/patches/0002`, Quake 3's black floors, journal 2026-10-06).
  Found twice so far; the second time it was the list of extensions
  (below). Suspect it first when something works on Linux and not here.
- The extension list (2026-10-06, journal): Mesa made the list
  `glGetString(GL_EXTENSIONS)` returns by reading its one-byte flags
  through a `bool` pointer, so on Tiger programs got a list of 220 with
  14 the context lacks and without 87 it has (`GL_ARB_vertex_program`,
  `GL_EXT_stencil_two_side`, S3TC, texture rectangles). Doom 3 drew with
  its fixed-function path because of it; with the true list (293) it
  uses its ARB2 path, at twice the frames, and the user saw it look
  better. Fixed in Mesa by `mesa/patches/0005` (the user's choice, with
  the rule that Mesa is only ever changed through `mesa/patches/`).
  `~/gl/glext` on the G5 prints the list. An audit of the rest of the
  Mesa we build found no other case that matters to programs here
  (journal).
  `RDN_SYNC=1|2|3` (wait for every command buffer, one per draw) and
  `RDN_GLD_TRACE_ONLY=glName,...` (trace only those calls) in a
  program's environment are for telling races from such bugs.
- A4's second half works by readback (2026-10-05), not yet seen by the
  user, and the default since the bundle built on 2026-10-05 20:54
  (`touch /tmp/rdngld.nosurface` or `RDN_GLD_NOSURFACE=1` turns it off;
  older bundles, like the guest's, need `touch /tmp/rdngld.surface`): a
  program's OpenGL window is a window server surface, drawn by Mesa and
  shown by the card, no CPU copy. Apple's Chess, started normally, is
  correct that way, also while its window is dragged (the user saw
  the first version flicker and leave trails; the fix is checked by
  readback only). How it works is in the journal's last entries (the window server
  textures from the surface's buffer directly). `/var/log/windowserver.log`
  in the guest is the window server's own log.
  `~/gl/drag x0 y0 x1 y1` drags with the mouse from inside the guest.
- Full-screen OpenGL works by readback (2026-10-05) with the same switch
  (Quake 3 full screen seen by the user on the G5):
  Sauerbraten (the user's test game, on the guest's `/Volumes/sauerbraten`
  while its image is mounted) shows a level at 6 frames a second. Without the file, windows
  go through the software renderer's buffer as before, and Chess is
  wrong.
- Speed (2026-10-06, journal): Quake 3's demo `four` at 1920x1080 on the
  G5 went from 47.8 to 96 fps. Three driver causes fixed (the winsys
  reported a GART size of zero, so r600 flushed per draw; the swap and
  buffer destruction waited for the GPU), and the card had been running
  at its VBIOS boot clocks (engine 100 MHz, memory 150 MHz).
  `~/gl/rdnuc power performance 3` on the G5 raises voltage and engine
  clock to 650 MHz; the kext does that by itself at start
  (`rdn_bootclocks=1` keeps the boot state). Mask 4 (memory clock, 800 MHz) works only when the
  memory controller's microcode was loaded at boot (`sudo nvram
  boot-args="rdn_mc=1"`, `hw/rdn_mc.c`; set on the G5 since 2026-10-06);
  without it the table does nothing. Going down with `boot` and mask 4 is
  untried. Raising the clock
  without display watermarks made rows of the screen show stripes;
  `hw/rdn_watermark.c` programs them and the user confirmed the fix.
- `RDN_STATS=1` in a program's environment prints its fence waits at
  exit; `~/gl/rdnuc reg off [n]` reads card registers; `sample <name> 10`
  on the G5 profiles a program. Apple's VNC server is on on the G5
  (user's request, password `tiger`); it can hang in a log loop
  (`kickstart -restart -agent`).
- glthread (2026-10-06, journal): Mesa's second-thread mode, with a small
  r600 patch (`mesa/patches/0003`). On for every program except the
  window server (user's decision, 2026-10-06). `RDN_GLTHREAD=0` or `=1`
  in a program's environment decides for it; otherwise a line of
  `/Library/Application Support/RadeonNI/glthread` may: the program's
  name turns it on, `-name` off, `*` and `-*` for all not named. Worth
  70 to 90 % in Doom 3 on its ARB2 path, 25 % in Quake 3. Seen right by
  readback in the two games, Chess, TuxRacer and the test programs;
  never run in the window server. `tools/guest/d3save.sh
  <save>` times 300 frames of a Doom 3 save game on any card (the user's
  saves: `bench`, `bench2`).
  glthread makes the program's thread wait for the other one at every
  call it cannot record: any `glGet*`, `glGetError`, texture uploads,
  and buffer data larger than a batch. The entry points split large
  `glBufferData` and `glBufferSubData` for that reason
  (`RDN_GLD_NO_SPLIT=1` does not). Look for `_mesa_glthread_finish` in a
  profile of the program's thread.
- Profiling a game (2026-10-06): `tools/guest/d3saveprof.sh` samples a
  Doom 3 save; `scripts/sample-profile.py` reads what `sample` wrote,
  per thread. `RDN_STATS=1` also prints how many command buffers and
  bytes of commands went to the GPU. `tools/guest/apcopy.c` measures how
  fast the CPU writes through the aperture: 720 MB a second whatever the
  size of the stores, against 1400 to 2000 into ordinary memory.
- Video memory (2026-10-06): programs get the 768 MB beyond the 256 MB
  aperture too, for what the CPU never maps, and the winsys keeps
  released memory in a cache instead of calling the kext for every
  buffer.
- GART (2026-10-06, journal), on unless the boot argument `rdn_gart=0`
  is given (user's decision; the G5 runs with no boot arguments):
  `hw/rdn_gart.c`, a self-test at start, and programs' own memory bound
  into it (`RDN_UC_GART_BIND`). If the self-test fails the kext logs it
  and goes on without; that path has never run. Under QEMU the GART has
  never run at all: `TIGER_BOOTARGS=rdn_gart=0` leaves it off there. The winsys puts upload
  and staging buffers there and, when video memory is full, new buffers
  of any kind. Nothing is ever moved out of video memory (no eviction).
  After changing the device layer check the Tiger bundle with `nm -u`
  for our own symbols: a missing one only shows when the window server
  fails to load the bundle.
- Multisampling (2026-10-06): a program's request for samples reaches
  Mesa's buffers and is resolved on the way to the screen; pixel format
  record word 9 carries sample buffers and samples. Doom 3 at ultra with
  4 samples: 20.9 and 21.7 fps, the GeForce 6600 LE 5.0 and 3.6.
- Piglit (2026-10-08, `docs/PIGLIT.md`): 4,869 of 4,963 GL 2.1 tests pass
  on the G5. Fixed from it: big-endian packed formats in r600 (`mesa/patches/
  0006`), query results as little-endian dwords (0007), a real back buffer
  (`OSMesaDoubleBuffer`, `OSMesaSwapBuffers[Async]`, patch 0008 for glthread;
  `RDN_GLD_NO_BACKBUFFER=1` keeps the old single buffer; the window server is
  never double-buffered). After a change to presentation grab the screen
  before measuring (`scripts/mac.sh g5 grab`): a frame rate proves nothing.
- Where the two games stand (2026-10-06, end of day, G5, 1920x1080):
  Doom 3 demo 48 and 51 fps on the saves `bench` and `bench2` (ARB2
  path, glthread; the GeForce 6600 LE 26 and 19), Quake 3 `four` 149.
  In Doom 3 Mesa's thread is the limit. The screen saver shows black
  and draws nothing with the card; not looked into. The window server
  runs with the true extension list since the evening of 2026-10-06
  (desktop, windows, Chess right by readback; the user has not said).
- Call of Duty 2 Demo and World of Warcraft 1.12 (2026-10-07, journal,
  by readback): the first needed the 24-bit depth mode in the renderer
  info and the aux depth stencil flag in our full-screen format, and now
  reaches its menu. The second needed the vertex program half of
  `GL_ARB_vertex_blend`, which Mesa lacks and Mac programs use without
  asking; the bundle maps it to generic attribute 1
  (`gld/gen_dispatch.py`, `WEIGHT`). Confirmed by the user in the world;
  Call of Duty 2 runs in its first map (below). With `RDN_GLD_LOG` the bundle logs every ARB
  program's text and Mesa's error, and which entries Mesa lacks a program
  calls ("kept:"): look there first when a game misses draws.
  `RDN_GLD_KEPT=first-last` wraps only some of those entries.
  `~/gl/aglfull` asks AGL for a full-screen context step by step.
- `GL_APPLE_flush_buffer_range` (2026-10-07, journal): Mesa waits for the
  GPU when a program maps a buffer it has just drawn from, and World of
  Warcraft does that 1500 times a second (30 frames a second at its login
  screen). The extension is how a program says not to wait; Tiger's OpenGL
  lacks its two functions, and the game looks them up in the framework's
  bundle by name. The bundle answers that lookup itself (a second hook in
  `gld/rdn_hook.c`), has the functions and names the extension for every
  program but the window server: 165 frames a second there, and the user
  confirmed the world ("It all looks good"). `RDN_NO_FLUSHRANGE=1` turns
  it off.
  `RDN_MAPBUFFER=discard|unsync` (or a line in
  `/Library/Application Support/RadeonNI/mapbuffer`) makes every
  `glMapBuffer(GL_WRITE_ONLY)` not wait, for programs without the
  extension; both corrupted World of Warcraft and it is off everywhere.
  `RDN_FPS=1` prints frames a second on standard error. When a program is
  slow and uses little CPU, look for the wait in a `sample`.
- `GL_APPLE_vertex_array_range` with `GL_APPLE_fence` (2026-10-07,
  journal): Call of Duty 2 draws only from its own memory and relies on
  that extension; without it Mesa copied up to 20 MB of vertices per draw
  and ran out of video memory in the first map. The bundle implements it
  with buffer objects that mirror the program's memory
  (`gld/gen_dispatch.py`, `VAR_HELP`), for the programs named in
  `/Library/Application Support/RadeonNI/vertexrange` (on the G5: Call of
  Duty 2) or with `RDN_VAR=1`. The user played the first map with it, 55
  to 90 frames a second at 1920x1080, and confirmed the last flicker gone.
  The game does not flush what it rewrites when it draws through a vertex
  array object it already has, so before such a draw the bundle compares
  the object's range with its copy again, for memory a flush has found
  changed or in a block malloc has freed or handed out since (the bundle
  replaces the default malloc zone's functions to see that). When a Mac
  game is slow or runs out of memory and its imports show no
  `glBindBuffer`, this is why.
- Zero-copy vertex ranges (2026-10-07, journal): the winsys can put a
  program's own memory behind the GART (`buffer_from_ptr`,
  `GL_AMD_pinned_memory`; `~/gl/pinned` draws from it, right by readback),
  and the vertex range code can use that instead of copies ("+Name" in
  the list, or `RDN_VAR=2`; `~/gl/vartest` tells the modes apart). In
  Call of Duty 2 it flickers and was never faster than copies; the cause
  is only partly known (the game reuses memory two frames after drawing
  from it, and nothing bounds how far its thread runs ahead of the GPU).
  Off; the G5's list has the game on copies. `RDN_SUBDATA_COPY` and the
  list also turn on glthread's direct `glBufferSubData` upload.
- Apple's small extensions (2026-10-07, journal, "Apple's other
  extensions"): for every program but the window server the bundle has
  `GL_APPLE_fence` as real waits (Mesa sync objects; the engine's own
  never waited), `GL_APPLE_texture_range`, `GL_APPLE_flush_render`,
  `GL_APPLE_transform_hint` and `GL_APPLE_vertex_array_object`, all
  named, and answers `GL_EXT_gpu_program_parameters`' two functions when
  a program looks them up by name (World of Warcraft: 164.6 to 166.8
  frames a second at its login screen). `RDN_NO_APPLE=1` and
  `RDN_NO_PROGPARAMS=1` turn those off. `~/gl/appletest`
  (`tools/guest/appletest.c`) checks all of it. Not seen by the user; DVD
  Player and iChat, which use fences, not run. The journal entry lists
  the 38 names Tiger's OpenGL knows that we lack and who asks for each.
- Core Image filters on the CPU for three reasons, found on 2026-10-07
  (`docs/2D-ACCELERATION.md`): it requires `GL_APPLE_client_storage`, it
  only uses renderers it knows by ID or by the start of `GL_RENDERER`
  ("ATI Radeon ", "NVIDIA GeForce ", ...), and then it needs pbuffers.
  `RDN_EXT_ADD="GL_a GL_b"` (any names into the list) and
  `RDN_RENDERER="ATI Radeon HD 7570"` in a program's environment give it
  the first two; it then takes the card and draws nothing. Both are
  experiment switches, off by default. `~/gl/proglimits` prints the
  program limits it asks for.
- "Core Image: Supported" is not reported (2026-10-08, the user's
  decision: nothing useful was behind it). The window server reports it
  when its list has `GL_ARB_fragment_program`, so the bundle leaves that
  one name out of the window server's list; fragment programs still work
  everywhere. `/Library/Application Support/RadeonNI/coreimage`, present
  when the window server starts, keeps the name in (for A6).
  `~/gl/wsfilter` (`tools/guest/wsfilter.c`) puts a Core Image filter on
  a window the way the Dock does for Dashboard's ripple and prints the
  window server's states. The window server asks for the list before its
  context's table is Mesa's, when OpenGL's own engine still answers, so
  the bundle also clears the engine's bit for the name at
  `gldCreateContext` (`docs/GLD-INTERFACE.md`; `~/gl/earlyext` shows a
  context's list at that moment). On the G5: "Core Image: Not Supported",
  Quartz Extreme in use, by System Profiler and readback.
- The Tiger device layer binds at most 512 MB behind the GART for one
  program (`GART_MOST_BYTES`): with about 860 MB bound the kext's bind
  call never returned and the process could not be killed (G5 restarted;
  cause not known, a guess is the G5's DART). Buffers above 16 MB get a
  chunk of their own that is unbound when freed.
- A6, Core Image on the card (2026-10-08, journal and
  `docs/2D-ACCELERATION.md`; `docs/CORE-IMAGE-TODO.md` was the plan, its
  header lists what it got wrong): works by readback and **is the default**
  (the user's decision, after seeing "ATI Radeon" as the renderer name):
  `GL_RENDERER` "ATI Radeon HD 7570" for every program (`RDN_RENDERER=`
  empty gives Mesa's), `GL_APPLE_client_storage` and `GL_APPLE_float_pixels`
  named, pbuffers on (`RDN_NO_PBUFFER=1` or the file
  `/Library/Application Support/RadeonNI/nopbuffer` off), and in the window
  server Core Image on the card unless the file
  `/Library/Application Support/RadeonNI/nocoreimage` exists (then: the
  window server reports no Core Image, as before). `ciprobe gl 0x21a00
  [filter]` (built on the host with `scripts/darwin.sh
  powerpc-apple-darwin8-gcc ... -framework Cocoa -framework QuartzCore
  -framework OpenGL`) equals `ciprobe soft` within 1 on nine filters, 1.2
  to 1.4 ms a render against 3 to 16 on the CPU. Core Image only counts
  video memory for a context whose pixel format has a display mask (not an
  off-screen one), else "ROI is not tilable". Pbuffers: video memory the
  bundle makes, Mesa draws into (`OSMesaMakeCurrentStore`, 8-bit, 16-bit
  and float stores) and takes as textures; `CGLTexImagePBuffer`,
  `cglsTexImagePBuffer` and the window server's `cglsSetInteger` 0x3e6
  (texture of a surface by ID, also the screen's, for a filter's backdrop)
  are taken by hooks in `gld/rdn_hook.c`, the last an inline patch of
  twelve checked words. Shared contexts work (`sharetest`). Off-screen
  drawables were never bound before `read_record()` was fixed. In the
  window server: `wsfilter` (`WSF_PLAIN=1` for a transparent window) puts
  CIColorInvert, CIGaussianBlur, CISepiaTone and CIPixellate behind its
  window; Dashboard's widget drop ripples; the user saw it a bit flickery,
  which came from copies to the screen when Mesa switched contexts in the
  middle of a frame (`OSMesaPresentOnSwitch`, `OSMesaFlushRender`); fixed
  by poll and log, not yet seen again by the user. `rdnuc poll x y w h n`
  samples the screen for flicker; `/tmp/rdngld.time` puts a time on every
  log line, and `/tmp/rdngld.trace` may name the GL calls to trace.
  Floating point pbuffers are untried. A restart of the window server
  needs the user's yes. The G5's bundle is d484fe00, the one before the A6
  work `~/RadeonNIGLDriver.before-ci`. A7 has started (GART).
- What is on by default since 2026-10-06, all decided by the user: the
  performance clocks and the memory controller's microcode, the GART,
  glthread for every program but the window server, the true extension
  list. The G5 has no boot arguments and no switch files that matter.
- The VBIOS comes from the card's ROM (2026-10-06, journal): on the G5
  the kext reads the expansion ROM at start, before POST, and the Mac
  runs with no VBIOS file installed (the kext's `Info.plist` there has no
  `VBIOS` key; the last kext that carried one is `~/RadeonNI.kext.before-rom`
  on the G5). The ROM's 64 KB equal `private/vbios.rom`. A file given to
  `install.sh` is the fallback, tried with `rdn_rom=0`. Under QEMU the
  file is still the only source. Never happened, so never run: a ROM that
  does not answer or fails its checksum.
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
- `docs/2D-ACCELERATION.md`: Quartz Extreme and Core Image as audited and
  measured on 2026-10-07 (Quartz Extreme is real; Core Image filters on the
  CPU and its picture comes out wrong, and why), what was built, what is
  next.
- `docs/CORE-IMAGE-TODO.md`: a plan for hardware Core Image (A6), written
  2026-10-08 for another agent to carry out; nothing started.
- `docs/EXTENDED-DESKTOP-TODO.md`: the expected steps for two monitors as
  two displays (a plan from 2026-10-07; nothing started).
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
- `TIGER_EVDEV=/dev/input/by-id/...` with `scripts/tiger.sh` and
  `scripts/guest-cycle.sh ready`, or `scripts/tiger.sh evdev [node|off]`
  on a running guest: a real keyboard and pointer for the guest through
  QEMU's `input-linux` (works, 2026-10-05). The user's is
  `usb-Logitech_USB_Receiver-if02-event-mouse`. Not `usb-host`: it stalls
  the guest's USB bus (journal, 2026-10-05). Run `evdev` again after a
  guest restart or after replugging the device.
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
  `scripts/build-mesa.sh darwin [targets]` builds Mesa with it, at -O2
  (`RDN_MESA_OPT=s`: for size, into `build-darwin-Os`);
  `.../build-darwin/src/gallium/targets/rdn/rdn_gltest` is the test program
  to copy into the guest (`-s` draws on the screen).
- `scripts/guest-cycle.sh down`, then `sudo scripts/tiger.sh passthru
  0000:10:00.0` in the background, then `scripts/guest-cycle.sh ready`: a
  clean guest restart (a loaded framebuffer kext cannot be unloaded) with
  the card's error masking re-applied. `TIGER_BOOTARGS=debug=0x100` makes
  a guest kernel panic print its backtrace on the emulated screen.
- `tools/guest/qe.c`: is Quartz Extreme in use on each display.
- `scripts/make-g5-package.sh [--with-vbios]`: package the guest-built
  kext and 2D plug-in, the host-built OpenGL bundle, the microcode with
  its licence (`scripts/fetch-firmware.sh` gets it from the host's
  linux-firmware or kernel.org when `firmware/` lacks it) and
  `g5/install.sh`, `g5/uninstall.sh`, `g5/README.txt` into
  `build/RadeonNI-g5.tar.gz`, to be unpacked and installed on the real
  Mac. There, `sudo ./install.sh --accel [--hwcursor]` installs the kext
  with the Quartz Extreme personality `kext.sh` uses under QEMU, plus the
  bundle and the plug-in; without options, phase 1's. The installer's
  bundle and plug-in step has not run anywhere yet.
- `scripts/build-piglit.sh`, `tools/piglit/run.py` (stage, start, fetch,
  summary) and `docs/PIGLIT.md`: piglit's GL 2.1 tests built for Tiger and
  run on the G5 (2026-10-08). `run.py start` must stay in the foreground of
  its ssh session (run it as a background job), else the tests abort.
- `scripts/make-dist.sh [--keep-gl]`: the archive for other people,
  `build/RadeonNI-<date>-<commit>.zip`: what `make-g5-package.sh` builds
  plus `README.md`, `INSTALL.txt` (`g5/README.txt`) and the licences
  (ours, the microcode's, Mesa's), never a VBIOS. `--keep-gl` skips the
  Mesa build and packs the bundle as last built. Unpacked and installed
  from on the G5 (2026-10-06); needs nothing but Tiger's base install
  there. Never tried on a Mac without Xcode or on any other Mac.
- `rdn_rom=0` as a boot argument (`sudo nvram boot-args="rdn_rom=0"`):
  the kext leaves the card's ROM alone and uses the VBIOS image in its
  personality; it does not start if `install.sh` was given none.
- `kext/RomProbe`: a kext to load by hand on a running Mac (`make` on
  Tiger, `kextload` from a root-owned copy, `kextunload -b
  org.osxgpu.driver.RadeonNIRomProbe`). It reads the card's whole ROM BAR
  and publishes it as the property `ROM` of its registry entry (`ioreg -c
  RadeonNIRomProbe -l -w0`), without touching the driver.

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
  the two standard headers Kernel.framework lacks. It reads the VBIOS from
  the card's expansion ROM (`biosFromRom()`, every word through the
  kernel's `ml_probe_read()`, which survives a machine check). Where the
  ROM gives no image, as under QEMU, it uses a `VBIOS` data property
  injected into the personality by `scripts/kext.sh load` or
  `g5/install.sh`; that is never part of the built bundle or the
  repository.
- `hw/rdn_accel.h` is the acceleration core: `rdn_gpu.c` (3D engine setup),
  `rdn_cp.c` (microcode, ring, fences, indirect buffers), `rdn_selftest.c`.
  Everything the GPU reads is in video memory, written little-endian through
  the aperture; see the header for the rules (host data path flush).
  `hw/linux/` holds register headers copied from Linux unchanged.
- `hw/rdn_pm.c` reads the PowerPlay table and switches voltage and clocks
  through the VBIOS's command tables; `hw/rdn_watermark.c` sets the line
  buffer and display watermarks from the mode and the clocks kept in
  `struct rdn_card`, at every mode set and clock change.
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
- `hw/rdn_dp.c` is DisplayPort: AUX through the card's registers, DPCD,
  EDID over AUX, link training. `struct rdn_output` (`hw/rdn_card.h`)
  describes a connector's encoder path; `rdn_output_detect()` picks the
  one with a display and `rdn_modeset()` drives `card->output`.
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
  It may be distributed in the G5 package, unmodified and with
  `LICENSE.radeon` (user's decision, 2026-10-06); still not in the
  repository.
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
  patch it; on the real G5 it reads the ROM (done 2026-10-06, `docs/PLAN.md`,
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
