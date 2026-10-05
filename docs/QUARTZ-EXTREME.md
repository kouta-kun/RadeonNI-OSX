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
   flags the update path tests (0x08 and 0x80) are derived. **We have no
   such plug-in; this is why the window server never tries by itself.**
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

## How far it gets

With gates 2 to 9 satisfied, calling `CGXGLAccelForDisplayDevice` for our
display from a debugger makes the window server load the bundle, choose
the format, create a shared state and a context on our renderer, and
return success; `CGDisplayUsesOpenGLAcceleration` then says yes. It does
not then draw with it, and after a restart it does not try on its own:
gate 1.

## Not known

- Whether the window server's context can render through Mesa the way an
  application's does: it has not attached a drawable or asked for dispatch
  yet. Its context comes from a private `cgls` layer, not CGL.
- What the window server needs from the kext's `IOAccelSurface` user
  client (type 0), which it has not asked for yet.
- Whether every display has to qualify, or each on its own.
