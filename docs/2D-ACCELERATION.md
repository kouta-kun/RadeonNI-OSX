# 2D acceleration: Quartz Extreme and Core Image, where they stand

State on 2026-10-07, after an audit and one round of work (branch
`qe-ci-round1`). The experiments are in `docs/JOURNAL.md` under that date;
the window server's requirements are in `docs/QUARTZ-EXTREME.md`. **[V]**
measured or seen on the G5, **[I]** inferred.

## Quartz Extreme: real, and measured

The window server composites with OpenGL through Mesa on the card. It is
not a stub. What is a stub is the 2D accelerator plug-in
(`ga/RadeonNIGA.plugin`), which only has to exist for the window server to
try Quartz Extreme at all.

On the G5 at 1920x1080 (`~/gl/qebench.sh`, a 600x400 Finder window, ten
seconds a phase) **[V]**:

| Phase | | Quartz Extreme on | off |
|---|---|---|---|
| AppleScript window moves | moves completed | 4763 | 1335 |
| | window server CPU | 5.87 s | 10.04 s |
| 8 paced drags | window server CPU | 1.38 s | 5.29 s |
| Exposé in and out, 4 times | window server CPU | 1.61 s | 3.49 s |

What the CPU still does around it:
- Window contents (Quartz 2D), as on every Tiger Mac.
- Changed window tiles are copied into textures; client storage is
  ignored. `glTexSubImage2D` was 0.25 % of the window server's main thread
  in a drag **[V]**: not worth the GART work that was planned for it.
- The window server does not wait for the GPU: it calls `glFlush` and
  never reaches `rdn_mesa_present()` **[V]**.

What the window server asks of the driver with Quartz Extreme on **[V]**:
- The 2D plug-in's blitters: never called (0 fills, copies, region
  copies in all four phases).
- The surface client: `setIDMode`, `setShape`; with a program's GL window
  also `control`, `flush` and read locks. `setScale`, `setShapeBacking`,
  write locks and `read` have never been called.
- Of the GL entries Mesa lacks, which stay with Apple's engine:
  `glTextureRangeAPPLE` and `glTestObjectAPPLE`.

Not done: alpha in a program's surface, shadows over a surface, a surface
that changes size (`docs/QUARTZ-EXTREME.md`, "Not done").

## The 2D plug-in on the GPU: built, works, not needed

`rdn_blit_fill()` and `rdn_blit_move()` in `hw/rdn_blit.c`, the user client
methods `RDN_UC_SCREEN_FILL` and `RDN_UC_SCREEN_COPY`, and in the plug-in a
GPU path behind the file `/tmp/rdnga.gpu` (looked for when the window
server starts), with the CPU path as fallback.

- `~/gl/gablit x y` calls the two methods directly: fills and an
  overlapping copy are right on the card, by readback **[V]**.
- The plug-in's own GPU path has never run. Since the window server does
  not call the plug-in under Quartz Extreme, it would only matter on a
  display without Quartz Extreme. Leave it off.

## Core Image: does not use the card, and what it draws is wrong

`tools/guest/ciprobe.m` (`ciprobe gl 0x21a00`, `soft`, `pbuffer`,
`compare`), on the G5 **[V]**:

- Core Image, given a CGL context on our renderer, makes no fragment
  program, no pbuffer, no framebuffer object and no second context. It
  reads `GL_VENDOR`, `GL_RENDERER`, `GL_VERSION` and `GL_EXTENSIONS`,
  filters on the CPU, and gives GL the finished picture as one
  `GL_TEXTURE_RECTANGLE` (BGRA, client storage, `glTextureRangeAPPLE`,
  `glFinishObjectAPPLE`) drawn as a quad. 10.6 ms a blur of 512x384,
  against 8.3 ms with its software renderer alone.
- That quad comes out black except for 16x16 pixels in the bottom left
  corner, which hold the right part of the picture. Sixteen pixels is the
  dummy drawable `rdn_make_current()` binds a context to before it has
  one of its own; the off-screen context keeps clipping to it.
  `glprobe draw 0x21a00` leaves its buffer all zero with the same bundle.
- `CGLSetPBuffer` fails with `kCGLBadEnumeration` before any call reaches
  the bundle.

So System Profiler's "Core Image: Supported" meant only that a gate was
passed. A6 is not started in any real sense.

The gate **[V, 2026-10-08]**: the window server sets that state
(`CGSServerOperationState` 0xf) when its own context's extension list has
`GL_ARB_fragment_program`, and the Dock asks for Dashboard's ripple on
the strength of it. A filter put on a window that way changes nothing on
the screen (`tools/guest/wsfilter.c`, three grabs). Since 2026-10-08 the
bundle keeps that name from the window server, so nothing is reported
that is not there: System Profiler says "Core Image: Not Supported" on
the G5. The window server asks before its context's table is Mesa's, so
the name has to go from the list OpenGL's engine makes as well
(`docs/GLD-INTERFACE.md`). The file
`/Library/Application Support/RadeonNI/coreimage` brings it back.

### Why it filters on the CPU [V, 2026-10-07, later the same day]

Found with `ciprobe gl` under a full GL trace and by reading QuartzCore's
`accel_load_screen_info`, `fe_accel_new` and `fe_accel_get` in disassembly
(journal). Core Image uses a renderer only when all of this holds:

1. `GL_APPLE_client_storage` and `GL_EXT_texture_rectangle` are in the
   list. We do not name the first (texture data is copied, so naming it
   would be true enough; it is left out because of what follows).
2. The renderer has a class: from its ID (0x21800 ATI Radeon, 0x21900
   Radeon X1000, 0x22400 NVIDIA, 0x24000 Intel) or from the start of
   `GL_RENDERER` ("ATI Radeon X1", "ATI Radeon ", "NVIDIA GeForce ",
   "NVIDIA Quadro ", "NVIDIA GeForce FX ", "NVIDIA NV34", "Intel "). One
   without a class gets no buffer formats and a speed of 0, below the
   software renderer's 1. Ours has none.
3. Pbuffers. Once 1 and 2 are given (`RDN_EXT_ADD="GL_APPLE_client_storage
   GL_APPLE_float_pixels" RDN_RENDERER="ATI Radeon HD 7570"`) it makes two
   contexts that share with the program's, one with 32 bits of colour and
   one with 64 (floating point), cannot set a pbuffer on either, and draws
   nothing.

`GL_APPLE_float_pixels` and the two program extensions only set flags.
The program limits it reads (`tools/guest/proglimits.c`) are all larger on
our renderer than on Apple's software renderer.

## What exists for the next round

- Front end: `OSMesaMakeCurrentStore` (a context draws into video memory
  the caller names: what a pbuffer needs) and `OSMesaTexStoreImage` (that
  memory as a 2D or rectangle texture, with alpha, no copy); share lists
  work. `rdn_gltest -P` checks them, and `RDN_SOFT=1` runs it on Linux
  without the card (softpipe, `mesa/tests/rdn_soft.c`): passes on x86 and
  big-endian. Never run on r600. The bundle does not use them yet.
- The bundle's log (`RDN_GLD_LOG=file`, or `/tmp/rdngld.on`): the thread
  in every line, dumps for shared contexts and unknown drawables, and a
  count of every call to the entries Apple's engine keeps ("kept:").
  `/tmp/rdngld.trace` as well logs every GL call.
- `qebench.sh`, `fences now`, the plug-in's counters (`/tmp/rdnga.on`,
  `/tmp/rdnga.stats`), the surface client's call counts (registry
  property `RadeonNICalls`, and the kernel log when a client closes).

## Next, in this order

1. The off-screen clipping bug: a context taken over before its drawable
   exists, then given an off-screen one, draws only 16x16 pixels.
2. Done (above): Core Image wants `GL_APPLE_client_storage`, a renderer
   it knows by ID or name, and pbuffers.
3. Pbuffers (`CGLSetPBuffer`'s refusal first), one of them with floating
   point colour, and shared contexts in the bundle. Core Image asks for
   them as soon as it takes the card.
4. Then, the user's to decide: which renderer to be for Core Image. The
   name is what programs see too, and games read it for their own
   workarounds; the ID is what CGL files the driver under.
5. The window server's own filters (Dashboard's ripple): with the name
   back (the file above), `wsfilter` is the test. They draw nothing today
   and the window server logs a GL error from its filter layer; not looked
   into further.

## Known problems left by this round

- `ciprobe gl` crashes in `CGLDestroyContext` when the bundle's log is on
  (not with it off). Not looked into.
- The Tiger build of `rdn_gltest` has not linked since Mesa is built at
  `-O2` (2026-10-06): the program is over 16 MB and Apple's `crt1.o` makes
  a short call across it. The bundle is not affected.
- On the G5: the round 1 driver is installed (bundle 39a16a74); the set
  before it is `~/RadeonNI-g5.before-round1`.
