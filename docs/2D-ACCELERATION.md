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

## Core Image on the card [V, 2026-10-08, by readback]

Built in `docs/CORE-IMAGE-TODO.md`'s stages 0 to 5 (journal, 2026-10-08).
What was believed before it (Core Image "makes no pbuffer, no second
context", "the quad comes out black except 16x16 pixels", "`CGLSetPBuffer`
is refused before any call reaches the bundle") was each partly wrong:

- The off-screen "16x16" was `read_record()` reading the window layout
  of the record for `CGLSetOffScreen`'s, which is `{width, height, row
  bytes, base}`: a context stayed bound to the dummy for good and the
  program's memory was never written. Fixed.
- `CGLSetPBuffer` reaches the bundle; `gldAttachDrawable` with type 0x5a
  went on to Apple's software renderer, which answers `kCGLBadEnumeration`.
  The bundle answers it now.
- Core Image's "ROI is not tilable" with the two switches was no pbuffer
  problem. It splits a picture until its pieces fit the memory of the
  context's **display**: `fe_cgl_total_vram` adds up the renderers of the
  pixel format's display mask, and an off-screen format has mask 0. A
  program with a window (or a pbuffer drawable with a non-off-screen
  format, as `ciprobe gl` has now) has one.

**In a program** (`RDN_PBUFFER=1` and the two switches, per program):
Core Image takes the card, makes two contexts that share with the
program's, compiles each filter's ARB programs and draws through pbuffers
of the format it names (`GL_RGBA16` for the 64 bit context's), which are
video memory the bundle makes and Mesa draws into
(`OSMesaMakeCurrentStore`, with 16-bit and float stores) and takes as
textures (`OSMesaTexStoreImage`, no copy; `CGLTexImagePBuffer` is hooked
because the engine wants the texture bound in its own state).
`ciprobe gl 0x21a00` against `ciprobe soft`, 512x384:

| filter | greatest difference | pixels over 8 | CPU ms | card ms |
|---|---|---|---|---|
| CIGaussianBlur | 1 | 0 | 8.1 | 1.2 |
| CIColorInvert | 1 | 0 | 7.6 | 1.2 |
| CISepiaTone | 1 | 0 | 8.5 | 1.2 |
| CIBumpDistortion | 191 | 13 | 11.1 | 1.4 |
| CIHueAdjust, CIColorControls | 0 | 0 | 2.9 | 1.3 |
| CIPixellate | 1 | 0 | 10.3 | 1.4 |
| CIGammaAdjust | 1 | 0 | 9.4 | 1.2 |
| CIZoomBlur | 1 | 0 | 15.8 | 1.2 |

(The 13 pixels of CIBumpDistortion are on the edges of the test picture's
cells, where a sample point falls on a texel boundary and one side
rounds the other way.) Without the switches Core Image still filters on
the CPU and the picture is exact. Floating point pbuffers (`GL_RGBA16F`,
`GL_RGBA32F`) have a store but nothing has drawn into one.

**In the window server** (the file
`/Library/Application Support/RadeonNI/coreimage`, present when it
starts): `wsfilter` (`CGSAddWindowFilter`, flags 0x3001, the Dock's call)
puts the filter on what is *behind* the window, and the window's own
picture is drawn over it. CIColorInvert shows the desktop behind it
inverted, right way up (blue to orange, the Finder window black with white
text), CIGaussianBlur blurs it, CISepiaTone and CIPixellate ran. In
Dashboard, dropping a widget from the widget bar distorts the other
widgets with the ripple for about a second (two grabs). By readback; not
seen by the user. How it works, from the GL trace and CoreGraphics in
disassembly: `docs/GLD-INTERFACE.md`, "Core Image in the window server".
System Profiler says "Core Image: Supported", `qe` Quartz Extreme in use.

## What exists for the next round

- Front end: `OSMesaMakeCurrentStore` (a context draws into video memory
  the caller names: what a pbuffer needs, 8-bit, 16-bit or float),
  `OSMesaTexStoreImage` (that memory as a 2D or rectangle texture, with
  alpha, no copy), `OSMesaTexCopyDrawable` (a part of the drawable as a
  texture, for the window server's backdrop); share lists work, and the
  bundle makes shared contexts with them (`sharetest`). The bundle uses
  them all.
- The bundle's log (`RDN_GLD_LOG=file`, or `/tmp/rdngld.on`): the thread
  in every line, dumps for shared contexts and unknown drawables, and a
  count of every call to the entries Apple's engine keeps ("kept:").
  `/tmp/rdngld.trace` as well logs every GL call.
- `qebench.sh`, `fences now`, the plug-in's counters (`/tmp/rdnga.on`,
  `/tmp/rdnga.stats`), the surface client's call counts (registry
  property `RadeonNICalls`, and the kernel log when a client closes).

## Next, in this order

1. The user's to decide (`docs/CORE-IMAGE-TODO.md`, stage 6): which
   renderer to be for Core Image (the name "ATI Radeon HD 7570" is what
   every program and game sees; the ID is what CGL files the driver
   under; or give the name only when Core Image asks), and whether the
   switches (`RDN_PBUFFER`, the two extension names, the renderer name,
   the window server's file) become the default.
2. Floating point pbuffers, tried with a filter that needs them.
3. Pbuffers' memory is given back through hooks on the destroy calls
   (`CGLDestroyPBuffer`, `cglsDestroyPBuffer`), a few at a time later;
   nothing was looked at after minutes of continuous ripples.
4. Core Image in programs with a window (no off-screen pixel format), the
   case real programs are.

## Known problems left by this round

- `ciprobe gl` crashed in `CGLDestroyContext` with the bundle's log on
  (2026-10-07). Not seen again since the off-screen fix of 2026-10-08;
  many runs with the log and the trace on.
- The Tiger build of `rdn_gltest` has not linked since Mesa is built at
  `-O2` (2026-10-06): the program is over 16 MB and Apple's `crt1.o` makes
  a short call across it. The bundle is not affected.
- On the G5 (2026-10-08, end of A6 stage 5): bundle e6b0b54b is installed;
  the one from before this work is `~/RadeonNIGLDriver.before-ci`
  (9f7f487f). The file `/Library/Application Support/RadeonNI/coreimage`
  is there, so the window server runs Core Image on the card (remove it
  and restart the window server to go back).
