# Acceleration state: phase 2 (Mesa, GL bundle, Quartz Extreme, Core Image)

Moved out of CLAUDE.md. Read when working on `gld/`, `mesa/`, the
accelerator half of the kext, GL games, Quartz Extreme or Core Image. Update
it when that state changes (dated entries; the journal keeps the history).

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
- Reported GL version per program (2026-10-17, journal): Mesa says GL 4.6 (compatibility). Gecko
  (PowerFox) then wants `glGetStringi`, which Leopard's OpenGL framework does not export, and
  stops itself at start. A program named in `/Library/Application Support/RadeonNI/glversion21`
  (one line each, `-name`, `*`) or with `RDN_GL_VERSION21=1` sees 2.1 / GLSL 1.20; no other
  program changes. On the G5 the file names `powerfox`. VLC only uses its PBO upload from GL 3.0.
- A program that attaches the same surface again keeps its picture buffer in the kext
  (`rdn_mesa_expect_surface`); before 2026-10-17 each re-attach erased it and the window server
  drew nothing (`tools/guest/rdnsurf.c` lists the surfaces the kext has a buffer for).
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
- Modes (2026-10-08, `docs/HOTPLUG.md`): the kext builds the mode list from the
  EDID (54 modes at three depths on the Mi Monitor, 640x480 to 1920x1080);
  Doom 3 starts in 800x600 and enters a map, and a 640x480/800x600/1024x768
  desktop is clean once the device re-reads the screen's pitch. Hot-plug
  works on DVI-I (2026-10-08, seen by the user); display sleep is untried
  (the idle timer never reaches the kext; never write properties to
  `IODisplayWrangler`: Tiger panics, journal). `cgmode set ... seconds`
  holds a mode (it is put back when the program ends). Never `killall` a game
  on the G5: the user may be playing it.
- Piglit (2026-10-08, `docs/PIGLIT.md`): 4,869 of 4,963 GL 2.1 tests pass
  on the G5. Fixed from it: big-endian packed formats in r600 (`mesa/patches/
  0006`), query results as little-endian dwords (0007), a real back buffer
  (`OSMesaDoubleBuffer`, `OSMesaSwapBuffers[Async]`, patch 0008 for glthread;
  `RDN_GLD_NO_BACKBUFFER=1` keeps the old single buffer; the window server is
  never double-buffered). After a change to presentation grab the screen
  before measuring (`scripts/mac.sh g5 grab`): a frame rate proves nothing.
- The Sims 2 (Aspyr 1.0, 2026-10-22, Tiger, G5): it copies GL_EXTENSIONS into a 4096-byte stack
  buffer, so the 7.9 KB list smashed its stack (crash pc 0x6a656374, "ject" from
  GL_ARB_uniform_buffer_object at byte 4124). The user's decision: for the program "The Sims 2" only,
  `ext_cap()` in `gld/gen_dispatch.py` cuts the list to `sims_ext` (an OpenGL 2.0 card's names, Mesa's
  order) plus Apple's names, 2184 bytes, cap 4000. `RDN_EXT_FULL=1` turns it off. Other programs keep the
  full list. On Leopard the same game dies earlier, before any GL call (jump to 0 at startup): not ours.
  `scripts/gl-ext-scan.py <binary>` finds this pattern before a game runs (glGetString(GL_EXTENSIONS) into a
  stack buffer, from the PowerPC code; it flags both Sims 2 sites). After the cap the Sims 2 gets past the
  license and then crashes in Apple's QuickTime YUV422 conversion while playing its intro movie
  (`TSData/Res/Movies`, Sorenson SVQ3; our QuickTime component is not installed on the Tiger volume, no
  gld/kext frame in the stack). Rev F (the patched build) crashes identically (same registers; the fault
  page is `0x?38aa000`, the heap base moving 16 MB a run), with `RDN_NO_APPLE=1`, and with `RDN_SOFTWARE=1`
  (Mesa not used at all; the game shows a blue screen): so not ours. Without the three intro movies in
  `TSData/Res/Movies` the original build reaches its menu through our driver.
  Profile and trace of the Sims 2 on Tiger (2026-10-23, Rev F, glthread on 15 fps, off 6 fps: glthread stays on):
  the main thread stalls on `glGetBooleanv(GL_DEPTH_WRITEMASK)` (about 0.65 a frame, a glthread sync, 15 % of
  its time), and in the frames where water is on screen it makes 1024 one-row `glCopyTexSubImage2D` calls (a
  vertical flip done row by row: Direct3D port, `cDeviceDX`) that are the water reflection (without them the
  reflections break: `RDN_NO_ROWCOPY=1`, an experiment switch). The game uses only ARB vertex programs
  (7, no ARBfp, whatever `useShaders` says). Why "Programmable 1.1 / Pixel program 1.1" in its config log
  (`~/Documents/EA Games/The Sims 2/Logs`): its startup probe (Rev F `0x1c15c..0x1c300`) fills a D3D caps
  structure with constants, vertex shader `0xFFFE0101` if `GL_ARB_vertex_program` is there and pixel shader
  `0xFFFF0101` always; nothing in GL, the PCI ID (`tools/g5/fake_device_id.c` fakes it per process) or the card
  database changes it. The rules that the game reads are in the app bundle's `Contents/Resources/Config`
  (`Graphics Rules.sgr` sets `useShaders false` for every non-software device), not in `TSData/Res`.
  `RDN_SOFTWARE=1` in a program's environment, or its name in `/Library/Application Support/RadeonNI/software`
  (the lines of `glversion21`), leaves that program to Apple's software renderer (`gld/rdn_mesa.c`).
  Why the Sims 2 shaders fail to build (2026-10-23): of 809 materials validated, the tree and forest shadow
  and `neighborhood_roundshadow` materials (60 of 145 shader creations) failed because the game's own D3D
  assembler (Rev F `0x2a7a8` -> `0x58764`) returns null for any `ps_` text (`0x589c4`: a `bne` on the shader-type
  byte, status `0x8876086A` = D3DERR_NOTAVAILABLE); its `vs_1_1` shaders all assemble. With that `bne` made a
  nop (a patch of the game copy, file offset 30316996 of the ppc slice, `60 00 00 00`; backup
  `sm2-binary-before-psnull`) the byte code comes out, and the engine then passes it (`0xFFFF0101`, `def`
  `0x51`, `tex` `0x42`, `mul` `0x05`, ..., `0x0000FFFF`) to `glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB)` as
  text, its length a strlen of the tokens (4). The bundle translates ps_1_x byte code to ARB fragment text
  (`rdn_d3d_ps_to_arb` in `gld/rdn_arbtext.c`, `tests/d3d_ps.c`; any program, valid text never starts with
  `0xFF`); Mesa accepts it. Result with the patch: 800 of 809 validate, the one material left is
  `nhoodterrainlightingmaterial` (its texture `nhoodlightmap` has no image when validated: the game's data,
  not the driver) and the frame rate is unchanged (the shadow shader is a `tex` and a `mul`; 9.8 fps).
  Constants not set with `def` are read as `program.env[n]`: not verified, the Sims 2 has none.
  Sims 2 speed work (2026-10-23, Tiger, G5, `scripts/sims2-cpu.sh`: main-thread ms per frame, steady Pleasantview
  view; run to run it varies by about 6 %, only paired runs mean something): `RDN_CALL_STATS=1` counts calls per
  entry (`/tmp/rdn-callstats.txt`); half of the 67M counted calls were `glActiveTexture` and `glClientActiveTexture`
  (25 % each, almost none redundant: it flips units). Holding the switches back until a unit-dependent call comes
  (`lazy_flush` in `gld/gen_dispatch.py`, `RDN_LAZY_UNIT=0` off) sends 14 % of them and took the main thread from
  74.2 to 66.4 ms a frame (+12 % fps); a removed call is worth about 270 ns. Still redundant and not elided:
  `glDisableVertexAttribArrayARB`, `glBindTexture`, `glTexParameteri`, `glSecondaryColor3f`,
  `glProgramEnvParameter4fvARB`, probably 5 % together. Caching the client-array uploads (88 % of the bytes
  glthread copies are identical to the last copy from the same address; `mesa/experiments/`) does not pay: exact
  comparison is 7-13 % slower, a sampled comparison (wrong when only other bytes change) is level; any check reads
  the cold vertex data, and that read is the cost, not the copy. 30 fps is not reachable from the driver
  side: the game's own work is a large part of the 66 ms.
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
  (`gld/gen_dispatch.py`, `VAR_HELP`), for every program but the window
  server since 2026-10-11 (user's decision; `RDN_VAR=0` or a `-name` line in
  `/Library/Application Support/RadeonNI/vertexrange` turns it off for one;
  UT2004 +15 to 20 %, Quake 3 and 4 unchanged; its fences are fake only
  once the program has named a range). The user played the first map with it, 55
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
