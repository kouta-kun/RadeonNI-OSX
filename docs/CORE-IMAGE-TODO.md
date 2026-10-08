# Core Image on the card: a plan for the agent that does the work

Written 2026-10-08 at the user's request, for another agent to carry out
and test. **Done through stage 5 the same day (journal, 2026-10-08;
`docs/2D-ACCELERATION.md`); stage 6 is the user's.** What this plan got
wrong, as found: the "16x16" bug was `read_record()` reading the wrong
record for off-screen drawables; `CGLSetPBuffer` does reach the bundle
(`gldAttachDrawable` type 0x5a, answered by Apple's engine with
`kCGLBadEnumeration`); Core Image's "ROI is not tilable" was a missing
display mask on an off-screen pixel format, not pbuffers; the window
server's filter acts on what is *behind* the window; and `CGLTexImagePBuffer`
and the window server's texture of a surface are `gliSetInteger` calls of
the engine that the bundle takes by hooks, not `gld*` calls. The text
below is left as written. Read `CLAUDE.md`, `docs/PLAN.md` and
the last entries of `docs/JOURNAL.md` first, as that file says; this plan
adds to them and repeats little.

## The goal

Core Image filters run on the Radeon in the Power Mac G5: in a program
that gives Core Image an OpenGL context, and in the window server
(Dashboard's ripple). This is milestone A6.

Done means, in this order:

1. `~/gl/ciprobe gl 0x21a00` on the G5 saves a picture that
   `ciprobe compare` finds equal to `ciprobe soft`'s (no pixel more than 8
   apart in any channel), with a GL trace that shows fragment programs
   made and drawn with, for at least five different filters.
2. `~/gl/wsfilter CIColorInvert 10` inverts the desktop behind its window,
   seen in a screen grab.
3. The user has looked at a widget dropped on Dashboard and said what
   they saw. Only they can see the ripple move.

Until the user says otherwise, everything stays behind switches and the
defaults do not change: a Mac that installs the driver behaves as it does
today.

## Where it stands

All of this is verified; the journal entries of 2026-10-07 ("Apple's other
extensions") and 2026-10-08 have the detail, `docs/2D-ACCELERATION.md` the
summary.

Core Image uses a renderer only if three things hold:

1. **Two names in the extension list:** `GL_APPLE_client_storage` and
   `GL_EXT_texture_rectangle`. We name the second. `GL_APPLE_float_pixels`
   sets a flag (deep colour).
2. **A renderer it knows.** It gives each renderer a class, from the
   renderer ID in the pixel format (0x21800, 0x21900, 0x22400, 0x24000)
   or else from how `GL_RENDERER` starts ("ATI Radeon X1", "ATI Radeon ",
   "NVIDIA GeForce ", ...). Ours (0x21a00, "AMD TURKS ...") has no class,
   which means no buffer formats and a speed below the software
   renderer's.
3. **Pbuffers.** Given 1 and 2 it makes two more contexts that share with
   the program's, one with 32 bits of colour and one with 64 (floating
   point), and wants a pbuffer on each. `CGLSetPBuffer` fails with
   `kCGLBadEnumeration` (10010) before any call reaches our bundle, so
   both end with no drawable and nothing is drawn.

The switches that give it 1 and 2 for one program:

    RDN_EXT_ADD="GL_APPLE_client_storage GL_APPLE_float_pixels" \
    RDN_RENDERER="ATI Radeon HD 7570" ~/gl/ciprobe gl 0x21a00

What the bundle lacks, by reading `gld/rdn_mesa.c`:

- **Shared contexts.** Every Mesa context is made alone
  (`OSMesaCreateContextExt(..., NULL)` in `rdn_make_current()`), whatever
  the program asked for. The context to share with arrives as
  `gldCreateContext`'s fourth argument (seen: 0x180ea00, the first
  context, for Core Image's two).
- **Pbuffers as drawables.** `rdn_mesa_attach()` knows off-screen memory,
  windows, the screen and surfaces. A pbuffer is drawable type 0x5a.
- **A pbuffer as a texture** (`CGLTexImagePBuffer`). Which `gld*` call
  that becomes is not known.
- **Floating point colour.** Our pixel formats are 32-bit only.
- **Off-screen drawing after an early takeover.** A context that made GL
  calls before it had a drawable and then gets off-screen memory draws
  only 16x16 pixels (the dummy `rdn_make_current()` binds it to;
  `RDN_GLD_DUMMY=n` changes its size). `ciprobe` reads its result from
  such a context, so this hides every other result until it is fixed.

What exists already and has never run on the card: in the Mesa front end
(`mesa/frontend/osmesa.c`), `OSMesaMakeCurrentStore` (a context draws into
video memory the caller names), `OSMesaTexStoreImage` (that memory as a
2D or rectangle texture, no copy) and share lists. `rdn_gltest -P` tests
them on Linux with `RDN_SOFT=1`.

## Rules for this work

These are in `CLAUDE.md`; the ones that matter most here:

- The G5 is the user's machine. Use `scripts/mac.sh` (the `test-macs`
  skill). Say what will run before anything takes its screen. **Ask
  before every restart of its window server or of the Mac.**
- Install a bundle with `~/gl/inst.sh` on the G5 (it renames into place),
  after copying the installed one to `~/RadeonNIGLDriver.before-ci` once.
  Say so when you do.
- Apple's binaries may be read in disassembly to learn what they ask of a
  driver (`otool -tV` on the G5; this is how the three gates were found).
  No code is copied from them.
- Mesa itself changes only through `mesa/patches/`.
- An entry in `docs/JOURNAL.md` for every experiment, failed ones too.
  Small commits. Report what was checked and how; "by readback" for
  anything only a grab showed.
- `bool` is four bytes on Tiger. Suspect it first when something works
  in the Linux test build and not on the Mac.
- With the bundle's log on (`RDN_GLD_LOG=file`, `/tmp/rdngld.trace` for
  every GL call) programs behave a little differently: the table is taken
  over at other moments. Check a result once without the log.

Builds: `scripts/build-mesa.sh darwin
src/gallium/targets/rdn/RadeonNIGLDriver.dylib` (15 seconds when only
`gld/` changed), then `nm -u` on it for symbols of ours. C test programs
cross-build with `scripts/darwin.sh powerpc-apple-darwin8-gcc` (see the
header of `tools/guest/appletest.c`). `ciprobe.m` is Objective-C and was
built in the Tiger guest (`scripts/tiger.sh run`, then
`scripts/mac.sh guest ...`); try the cross compiler first.

## Stages

Do them in order. Each ends in a check; do not start the next until the
check passes. If a stage's check still fails after three different
attempts, write down what was tried and what was seen, and stop: report
to the user instead of going on.

### 0. Reproduce

- Build the bundle from `main` unchanged, install it, run
  `~/gl/appletest` (all pass) and `~/gl/earlyext`.
- Run `ciprobe soft`, `ciprobe gl 0x21a00`, and `ciprobe gl 0x21a00` with
  the two switches and a full trace. Confirm what "Where it stands" says:
  three `gldCreateContext`, two attached with type 0, no `program:` lines.
- Check: your numbers match the journal's. If they do not, stop and find
  out why before changing anything.

### 1. The 16x16 off-screen bug

- Find why a context that was bound to the dummy keeps its size after
  off-screen memory is attached (`rdn_make_current()`, `read_record()`,
  `rdn_mesa_attach()`; `glprobe draw 0x21a00` shows it with no Core Image
  involved).
- Check: `glprobe draw 0x21a00` gives its picture, and `ciprobe gl
  0x21a00` without switches (Core Image on the CPU, drawn through us)
  compares equal to `ciprobe soft`. `appletest` and `vartest` still pass.

### 2. Shared contexts

- Make a Mesa context share with the one `gldCreateContext` names. The
  other's Mesa context may not exist yet (they are made on first use), so
  it has to be made then. Confirm from the log which argument it is.
- GL calls name their context (CGL macros), and Core Image goes back and
  forth between three on one thread. `RDN_ENTER` switches Mesa's current
  context whenever the context differs; `rdn_current_rend` is one variable
  for the whole process. That is right for one thread; note it in the
  journal as the limit it is, and do not widen the work to threads unless
  a test needs it.
- Write a C test (`tools/guest/sharetest.c`): two contexts that share,
  each with off-screen memory, a texture and a fragment program made in
  the first and drawn with in the second, a pixel read back.
- Check: `sharetest` passes with and without `RDN_GLTHREAD=0`. Quake 3
  (`~/gl/td.sh`) and Doom 3 (`~/gl/d3save.sh bench`) as fast as before
  (148 and 48 frames a second).

### 3. Pbuffers

3a. **Why `CGLSetPBuffer` is refused.** `ciprobe pbuffer 0x21a00` shows
it. Read `CGLSetPBuffer` in the OpenGL framework and what it calls
(`otool -tV`; look for where 10010, 0x271a, is loaded as the result). The
likely places: a flag in our renderer info or pixel format record
(`RDN_GLD_PF` flips bits of the format, as was done for the aux depth
stencil flag), or the engine's feature bit 50, `GL_APPLE_pixel_buffer`
(`docs/GLD-INTERFACE.md`). Check: `CGLSetPBuffer` returns 0 and a
`gldAttachDrawable` with type 0x5a reaches the bundle.

3b. **Drawing into one.** Attach: video memory for the pbuffer's size,
bound with `OSMesaMakeCurrentStore`. Check: a context draws into a
pbuffer and `glReadPixels` in that context gives the picture back.

3c. **Using one as a texture.** Log what `CGLTexImagePBuffer` makes the
engine ask of the driver, then answer it with `OSMesaTexStoreImage`.
Check: `ciprobe pbuffer 0x21a00` prints PASS for the 2D and the rectangle
texture.

3d. **Floating point.** Log the whole attribute list of Core Image's
64-bit request (the log shows only the first words today). Offer a pixel
format for it and a store of 16-bit floats per channel (the card can
render to it). Add a float case to the pbuffer test: values above 1.0
drawn, read back through the texture unclamped. Check: that case passes.

### 4. Core Image in a program

- With the two switches: `ciprobe gl 0x21a00` for `CIGaussianBlur`,
  `CIColorInvert`, `CISepiaTone`, `CIBumpDistortion`, `CIHueAdjust` and
  two more of your choice.
- Expect problems in what Core Image's fragment programs need from Mesa;
  with the log on, every ARB program's text and Mesa's error are logged
  ("program:"). Fix what is ours; a Mesa fault goes in `mesa/patches/`.
- Check: goal 1 above, and the time of a render against the CPU's 12 ms.

### 5. The window server

- The window server uses the same library through `cgls*` calls and gets
  none of the bundle's Apple extras today (`apple_on()` leaves it out, on
  purpose). It needs the two names, a renderer Core Image knows, and
  `/Library/Application Support/RadeonNI/coreimage` so that it reports
  Core Image again. Put all of that behind that one file.
- This stage needs window server restarts, and a fault here can end the
  user's session. Ask first, each time. To get back: remove the file over
  ssh and restart the window server.
- Check: goal 2. Quartz Extreme still in use (`~/gl/qe`), the desktop
  right in a grab, nothing new in `/var/log/windowserver.log` or the crash
  logs.

### 6. Hand back to the user

Stop here and ask; these are theirs to decide:

- **Which renderer to be for Core Image.** The name is what every program
  sees, and games read it for their own workarounds; the ID is what CGL
  files the driver under. A third way is to give the other name only when
  Core Image itself asks. Say what each would touch and which you would
  choose.
- **Whether it becomes the default,** and with it the two extension names
  and "Core Image: Supported".
- Goal 3: ask them to drop a widget on Dashboard and say what they see.

## What to report

For each stage: what was done, the check and its result in numbers, what
was not done. Update `docs/2D-ACCELERATION.md`, `docs/GLD-INTERFACE.md`
(everything learned about pbuffers and shared contexts in the driver
interface), the A6 row of `docs/PLAN.md` and `CLAUDE.md`. Name the bundle
installed on the G5 and the one kept from before.
