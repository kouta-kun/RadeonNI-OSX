# What Tiger's window server wants before it composites with OpenGL

Found by trial and error in the 10.4.11 guest, 2026-10-05: reading the
disassembly of CoreGraphics to understand its checks, calling those checks
from a debugger, and watching what the window server asks of the kext and
of the driver bundle. Nothing here is copied from Apple's code. **[V]**
seen in the guest; **[I]** inferred.

Tools: `tools/guest/qe.c` (`CGDisplayUsesOpenGLAcceleration` per display),
`system_profiler SPDisplaysDataType` ("Quartz Extreme: Supported"), a
debugger attached to `WindowServer` (`sudo gdb -p`), the bundle's log
(`touch /tmp/rdngld.on` makes every process log to `/tmp/rdngld.<pid>.log`),
`TIGER_BOOTARGS=debug=0x100` for readable kernel panics.

## Gates, in the order the window server meets them

1. **A 2D accelerator plug-in for the framebuffer [V].** When a mapped
   display starts, the window server calls `IOPSAllocateBlitEngine` (open
   source, IOKitUser `IOGraphicsLib.c`). It loads the CFPlugIn named by
   the framebuffer's `IOCFPlugInTypes` property (type
   `ACCF0000-0000-0000-0000-000a2789904e`) and needs `GetBlitter` to
   return a copy-rectangles and a solid-fill routine. Only if that
   succeeds does the display get the internal flags (0x60) from which the
   flags the update path tests (0x08 and 0x80) are derived. Without such
   a plug-in the window server never tries by itself.
2. **`IOAccelRevision` greater than 1 [I]**, read as a number property
   next to those flags. The kext publishes 2.
3. **Video memory [V].** The framebuffer's VRAM range must be at least
   `GLCompositorMinimumVRAM` (16 MB). The kext reported 8 MB (the screen's
   surfaces); with the accelerator on it now reports the whole aperture.
4. **The accelerator [V].** Found through the framebuffer's `IOAccelTypes`
   path (`IOAccelFindAccelerator`); must conform to `IOAccelerator`.
5. **`AccelCaps` [V]**, a 4-byte property of the accelerator. VMsvga2
   publishes 3 for Quartz Extreme; so do we (`RDN_ACCELCAPS=3`). What each
   bit means is not known.
6. **Some AGP device must exist [V].** `checkForValidDevice` takes the first
   service matching `IOAGPDevice` and refuses if there is none, or if its
   `model` property starts with `ATY,Rage128`. The emulated Mac has no AGP
   device; neither does a PCI Express Power Mac's card, as far as its
   class goes [I].
7. **An `IOAGPDevice` among the accelerator's ancestors [V].**
   `isAccelUsable` walks up the registry from the accelerator and wants an
   object whose class name is exactly one of `GLCompositorRequiredClasses`
   in CoreGraphics' `Configuration.plist`, which holds only `IOAGPDevice`.
   (Editing that file is the old "PCI Extreme" hack; not needed, see below.)
8. **A pixel format [V].** The window server asks the driver bundle for
   `{80 (window), 20: 8, 21: 8, 22: 8, 12 (depth): 0}`; 20 to 22 are not
   public. It then requires, of a record in the answer: its display bit in
   word 12, any of flags 0x111 in word 2, colour mode 0x8000 (ARGB 8888)
   in word 4, depth mode 1 and stencil mode 1 (none) in words 6 and 7, no
   auxiliary buffers. Apple's software renderer, which our bundle forwards
   to, answers every request with no format when it runs inside the
   window server.
9. **Renderer info [V]**: video memory (word 12) at least the minimum.

## What the driver does about them

- Gates 6 and 7 without touching system files (`RDN_AGPSHIM=3`,
  `kext/RadeonNI/RadeonNIAccel.h`): a bare, never-registered `IOAGPDevice`
  between the PCI device and the accelerator, and a registered subclass
  that no driver can match, with a `model` property. Both go into the
  registry with `attachToParent`; `IOPCIDevice::attach` and the generic
  calls a registered PCI nub passes to its bridge (`getResources`,
  `compareName`, `matchPropertyTable`) panic without a real bridge.
- Gate 8: the bundle builds the record itself when the software renderer
  has none.

- Gate 1: `ga/RadeonNIGA.plugin` (`scripts/ga.sh`), named by the kext
  when loaded with `RDN_GA=1`. Its fill and copy routines work on the
  screen with the CPU.

## What the window server then does [V]

With every gate met (`RDN_ACCEL=1 RDN_ACCELCAPS=3 RDN_AGPSHIM=3 RDN_GA=1
RDN_SURFACES=1 scripts/kext.sh up`) it composites the card's display with
OpenGL by itself, from its first frame:

1. Opens an `IOAccelSurface` user client on the accelerator (type 0) and
   calls `setIDMode(1, 0x24)`: surface ID 1, windowed, ARGB 8888.
2. Loads the bundle, chooses the pixel format, creates a shared state and
   one context.
3. For every screen update: `setShape(0xd, framebuffer 0, region)` on the
   surface, where the region is exactly the part of the screen that
   changed (many rectangles when it is not a box); then
   `gldAttachDrawable(context, 0x50, record)`, whose record's third word
   is the surface ID; then `gldInitDispatch`; then it draws; `glFlush`;
   and `setShape(0x1, ...)` with a 1x1 region.
4. The drawing is plain OpenGL 1.x: `glDrawBuffer(GL_FRONT_LEFT)`, a
   viewport the size of the region's bounding box, `glOrtho(left, right,
   bottom, top)` in global desktop coordinates with y downwards, and per
   window `GL_TEXTURE_RECTANGLE` tiles of at most 256x256 made with
   `glTexImage2D`/`glTexSubImage2D` straight from the window's backing
   store (`GL_UNPACK_ROW_LENGTH`, `SKIP_ROWS`, `SKIP_PIXELS`,
   `GL_UNPACK_CLIENT_STORAGE_APPLE`, `GL_BGRA`,
   `GL_UNSIGNED_INT_8_8_8_8_REV`; `GL_ALPHA` tiles for shadows), drawn as
   quads with two texture units, `GL_COMBINE` environments, blending and
   alpha test. No scissor: it only draws inside the region and expects
   the rest of the bounding box to stay as it is on the screen.

So the drawable of the window server's context is the surface, the
surface is the changed part of the screen, and GL's origin is the bounding
box's corner.

`/tmp/rdngld.trace` (with `/tmp/rdngld.on`) makes the bundle log every GL
call with its arguments; that is where the list above comes from.

## What the driver does with that

- Apple's software renderer cannot attach a drawable inside the window
  server: it calls `CGSGetSurfaceBounds`, which connects to the window
  server and waits for an answer from the thread that is asking (seen as
  a hang in `mach_msg`, session gone). The bundle therefore does not
  forward `gldAttachDrawable`, `gldInitDispatch` and `gldUpdateDispatch`
  there; it answers them itself (2, 4 and 0, what the software renderer
  answers an application).
- The kext keeps each surface's region; the accelerator user client hands
  it out (`RDN_UC_SURFACE_REGION`).
- Mesa draws each update on a screen-sized surface of its own, and when
  the window server flushes, the GPU copies the region's rectangles from
  there to the screen's surface, which the winsys imports as a linear
  render target (`RDN_WINSYS_HANDLE_SCREEN`, `OSMesaMakeCurrentDirect` in
  our copy of the off-screen front end). Because the window server's
  window coordinates start at the corner of the region's bounding box,
  the bundle adds that corner to x and y of `glViewport`, `glScissor`,
  `glReadPixels`, `glCopyPixels` and the `glCopyTex*` calls
  (`gld/gen_dispatch.py`).
- Drawing on the visible surface itself also works and was tried first,
  but the window server builds an update in steps (background, then each
  window), and the steps showed: the user saw Exposé flicker, and a
  recording had frames with a window in two places. `GL_FRONT` for the
  window server means the surface, which is only shown on flush.
- With `/tmp/rdngld.copy` in the guest the older way is used instead:
  Mesa draws a bounding box off-screen and the CPU copies the region's
  rectangles to the screen when the context is flushed
  (`OSMesaReadbackRects`).
- The other 16 surface methods succeed without doing anything; the locks
  and `read` are refused. The window server has not called them.

Checked by reading the screen back in the guest (`rdnuc grab`), both
ways: desktop picture, Finder windows with shadows, a window moved in
steps, Exposé's dimming and its return, all in the right place. Drawing
directly, a window moved in eight steps took 304 command buffers in 5 s
and the window server had used 4.3 s of CPU time after that and Exposé,
against 19 s at the same point with the copy. **Confirmed on the monitor by the user**, Exposé included; it looks a
bit slow and the cursor flickers slightly.

## Not done

- Texture uploads copy; `GL_UNPACK_CLIENT_STORAGE_APPLE` is ignored
  (correct, slower).
- Applications' OpenGL windows are still drawn into their windows' buffers
  (A4), not into surfaces of their own.
- The software cursor is drawn by `IOFramebuffer` with the CPU into the
  screen the GPU now draws on; whether it leaves marks has to be seen on
  the monitor. A hardware cursor would settle it.
- `glGetIntegerv(GL_VIEWPORT)` and the raster position are not moved to
  the region's corner. The window server has not used them.

## Not known

- Whether every display has to qualify, or each on its own (the emulated
  display stays unaccelerated and works).
- What `AccelCaps`' bits mean, and whether Exposé, the genie effect and
  fast user switching need more than this.
