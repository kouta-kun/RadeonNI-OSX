# Tiger's OpenGL driver interface, as observed

What is known about how Mac OS X 10.4.11 (PowerPC) loads and talks to an
OpenGL driver bundle. Apple never published this interface. Everything here
comes from symbol lists, the public SDK headers, VMsvga2's MIT sources and
the call log of our pass-through bundle (`gld/`, `scripts/gld.sh log`).
**[V]** is observed in the guest; **[I]** is inference.

## How a bundle gets loaded [V]

- An `IOAccelerator` service sits under the card's `IOPCIDevice` and carries
  `IOGLBundleName` (a bundle name without extension) and `IOAccelRevision`.
- The framebuffer carries `IOAccelTypes` (the accelerator's path in the
  service plane), `IOAccelIndex` and `IOAccelRevision`.
- With that in place, any process that calls CGL loads
  `/System/Library/Extensions/<IOGLBundleName>.bundle` and calls
  `gldInitializeLibrary`, then `gldGetVersion`. No kernel user client is
  needed for this much: our accelerator creates none and was asked for none.
- The bundle is asked for renderer info for every display mask, not only its
  own display's, and it takes the place of `GLRendererFloat` (ID 0x20400) in
  the renderer list. The generic renderer 0x20200 stays.
- The window server restarted with the accelerator present, did not load the
  bundle and asked for no user client. It kept working unaccelerated.

## Entry points [V]

A driver bundle exports 62 `gld*` functions; the list is `GLD_ENTRIES` in
`gld/RadeonNIGLDriver.c`. `GLRendererFloat`, `ATIRadeon9700GLDriver` and the
other hardware bundles all export the same set. `GLEngine.bundle` exports 17
`gli*` functions, which OpenGL.framework looks up by name.

Calls seen for an off-screen context, in order, with what the arguments are:

| Call | Arguments and results |
|---|---|
| `gldInitializeLibrary(a, b, mask, d, e)` | Third argument was 2, the display mask of our display. |
| `gldGetVersion(&a, &b, &c, &d)` | Software renderer answers 2, 4, 11, 0x400. Returns 1. |
| `gldGetRendererInfo(info, mask)` | Fills a 0x40-byte record. Word 1 is the low 16 bits of the renderer ID (0x400); CGL reports 0x00020000 plus that. |
| `gldChoosePixelFormat(&fmt, attrs)` | `attrs` is the zero-terminated CGL attribute list, with the renderer ID attribute removed. `fmt` is allocated by the driver; word 1 is the full renderer ID. |
| `gldDestroyPixelFormat(fmt)` | |
| `gldCreateShared(&shared, 0, ...)` | |
| `gldCreateContext(&ctx, fmt + 4, 0, 0, ...)` | Second argument points at the pixel format's renderer ID word. The other pointers are inside the engine's context. |
| `gldCreateTexture`, `gldCreatePipelineProgram`, `gldCreateVertexArray` | Default objects, created right after the context. |
| `gldAttachDrawable(ctx, type, ...)` | Type 0x35 (53) is `kCGLPFAOffScreen`. |
| `gldInitDispatch(ctx, table, state)` | See below. Returned 4. |
| `gldUpdateDispatch(ctx, table, ...)` | Called later, after drawing started. |
| `gldGetString(ctx, name)` | `name` is `GL_VENDOR` etc.; returns a C string. |

## The dispatch tables [V]

- The application's `CGLContextObj` is public (`CGLContext.h`): `rend`, the
  engine's context, followed by `disp`, a `GLIFunctionDispatch` of 686
  function pointers (`gliDispatch.h`). `glClear` and friends call through it.
- The `table` given to `gldInitDispatch` is **not** that table. It lies
  inside the engine's context (at `rend + 0x4698`), is about 33 entries long
  and is filled by the driver with its own functions for the engine to call.
- Six words before it (`table - 0x18`, `rend + 0x4680`) the engine keeps a
  pointer to the application's `disp`. Found by searching the engine context
  for the address; the offset belongs to 10.4.11's `GLEngine`.
- Replacing `disp->clear` through that pointer, at `gldInitDispatch` and
  `gldUpdateDispatch` time, works: the application's `glClear` reaches our
  function, and the entry was still ours after drawing.

## Off-screen drawables [V]

`gldAttachDrawable(ctx, 0x35, drawable, ...)` for `CGLSetOffScreen`: the
third argument points at a record that begins with width, height, row
bytes and base address, each a 32-bit word. The buffer holds 32-bit ARGB
pixels in host byte order.

## Giving a context to Mesa [V, 2026-10-05]

- The application's table in 10.4.11 has **684** entries, two fewer than the
  10.4u SDK's header declares (`program_env_parameters4fv_EXT` and
  `program_local_parameters4fv_EXT` are not there). The CGL context's
  private fields follow the table directly: writing entries 685 and 686
  corrupts them, and `CGLDestroyContext` then crashes.
- With every one of the 684 entries that Mesa has a function for (621)
  replaced at `gldInitDispatch` time, an unmodified CGL program (create
  context, `CGLSetOffScreen`, clear, `glFinish`, `glReadPixels`, destroy)
  runs on Mesa's r600: `GL_RENDERER` is Mesa's, the buffer holds the
  GPU's picture, and the program exits cleanly. The software renderer
  underneath keeps receiving the engine's lifecycle calls.
- The 63 entries Mesa lacks are Apple's own extensions (vertex array
  range, element array, fences, texture range and the like) and stay the
  engine's.

## Not known yet

- Whether the engine rewrites entries of `disp` during longer use. It
  cannot do so from inside a GL call any more (none reaches it), but it
  may at lifecycle events; only a short program has been run.
- How to carry a renderer ID of our own through the pixel format: with word 1
  of the pixel format changed, `CGLCreateContext` fails with
  `kCGLBadPixelFormat` before any driver call.
- The meaning of most fields of the renderer info and pixel format records,
  the 33-entry driver table, and every call a window or full-screen
  drawable brings.
- What makes the window server use an accelerator at all.
