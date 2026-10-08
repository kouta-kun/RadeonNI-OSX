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
- The window server loads the bundle and asks for a surface user client
  only once its Quartz Extreme gates are met (`QUARTZ-EXTREME.md`). Its
  `gldAttachDrawable` has type 0x50 and a record whose third word is the
  ID of its `IOAccelSurface`; the software renderer must not be given
  that call inside the window server (it deadlocks). `gldAttachDrawable`
  returns 2 and `gldInitDispatch` 4 when the software renderer answers an
  application; the bundle returns the same when it answers itself.

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
pixels in host byte order. (Words 4 to 11 and 27, which a window's record
has, are not filled for it: reading the window layout here left the
context on its dummy drawable for good, until 2026-10-08.)

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

## Renderer identity and capabilities [V, 2026-10-05]

Found by flipping one bit at a time and asking CGL (`glprobe -v`).

- The low half of the renderer ID appears in three places that must agree:
  the fourth value of `gldGetVersion`, word 1 of the renderer info, and
  (as the full ID) word 1 of the pixel format. The engine files the
  plug-in under the `gldGetVersion` value; with the other two changed
  alone, `CGLCreateContext` fails with `kCGLBadPixelFormat` before any
  driver call.
- Renderer info word 2 is flags: bit 0 window, 1 full screen, 2 off-screen,
  3 backing store, 4 MP safe, 6 robust, 8 accelerated, 9 multi-screen,
  10 compliant. Word 3 is buffer modes, 4 colour modes, 5 accumulation
  modes, 6 depth modes, 7 stencil modes; word 9 holds aux buffers (high
  half) and sample buffers (low half), word 10 samples (high half); word
  12 is video memory and word 13 texture memory, in bytes.
- Pixel format word 2 has the accelerated flag at bit 8 too.
- Pixel format word 2, bit 11, is `kCGLPFAAuxDepthStencil` (57). The
  software renderer has no format for a request with it; CGL and AGL refuse
  a returned record without the bit (2026-10-07, `tools/guest/aglfull.c`).
- The software renderer's depth modes are 0x1000 (32 bits) only. Ours adds
  0x800 (24 bits), which is what Mesa's buffer has.
- The last entry of the SDK's dispatch table, `buffer_parameteri_APPLE`, is
  not a function in 10.4.11's engine: it keeps data there and follows it.
  Never write that word.
- Tiger's libGL exports no `glBufferParameteriAPPLE` and no
  `glFlushMappedBufferRangeAPPLE`. A program can still be given
  `GL_APPLE_flush_buffer_range` if it looks the functions up by name:
  World of Warcraft does, with `CFBundleGetFunctionPointerForName` on
  `com.apple.opengl`, and the bundle hooks that call (`gld/rdn_hook.c`).
- The same holds for `GL_EXT_gpu_program_parameters`: Mesa names it,
  10.4.11's libGL exports neither of its functions and its table has no
  entries for them (the SDK's header declares two it does not have), and
  World of Warcraft looks them up by name too. The bundle answers
  (2026-10-07). With the log on it logs every `gl` name looked up that way.
- 10.4.11's `GLEngine` holds 107 extension names, all that any Tiger
  driver can give. Of those, Mesa on this card lacks 38; the journal
  (2026-10-07, "Apple's other extensions") has who asks for which.
- The engine's own fence entries (`glSetFenceAPPLE` and the rest), which a
  program gets when the driver replaces none, never wait for our GPU. The
  bundle's are Mesa sync objects. Mesa's `glFenceSync` does not draw what
  `glBegin` and `glEnd` have gathered, so a `glFlush` goes before it.
- Apple's renderers answer 0 with no error when a vertex program is asked
  for the counts only fragment programs have (`glGetProgramivARB`, 0x8805
  to 0x8810); Mesa gives an error and no answer. Core Image asks.
- A context that has just been made answers through the table OpenGL
  filled in itself, until the bundle gives the table to Mesa (at the
  program's first `gld*` call or when it makes the context current; in
  the window server only when a drawable is attached). Until then
  `GL_RENDERER` is "Apple Software Renderer" and `GL_EXTENSIONS` is a list
  the engine makes: the plug-in is asked (`gldGetString`) for the
  renderer's name only. The list is a fixed part and one name for each
  bit set in three words at 0x124 of the record `gldCreateContext`'s
  fifth argument points to. Bit 15 is `GL_ARB_fragment_program`, 16
  `GL_ARB_fragment_shader`, 25 texture rectangles, 49
  `GL_APPLE_float_pixels`, 50 `GL_APPLE_pixel_buffer`
  (`tools/guest/earlyext.c`, 2026-10-08). The window server decides
  "Core Image" from that early list.
- CGL removes `kCGLPFAAccelerated` and the renderer ID from the attribute
  list before the plug-in sees it, and decides acceptance from the flags in
  the returned pixel format.
- With ID 0x00021a00 in all three places and both accelerated flags set,
  our renderer is listed first, is what a default context gets, and
  satisfies a request for an accelerated one. Apple's float renderer
  (0x20400) then appears again beside it.

## Windows [V, 2026-10-05]

- `gldAttachDrawable(ctx, 0x50, record, ...)` for a window. From word 4 on
  the record matches the off-screen one: width, height, the same again, 1,
  the type, a word, and at word 11 the base address of a buffer of 32-bit
  ARGB pixels. Word 27 holds the row length in pixels in its high half
  (the width rounded up to a multiple of 16) and the bytes per pixel in
  its low half; the off-screen record has the same word. The first three words
  look like connection, window and surface identifiers.
- The table the driver fills at `gldInitDispatch` (33 entries) is what the
  engine calls to do the work. Counted per frame of a test program: entry
  1 clears, 8 draws lines, 11 draws triangles, 24 presents. There is no
  `gld*` call for presenting; `gldUpdateDispatch` runs once per frame.
- Apple's software renderer presents the window itself, through the window
  server (`CGSLockWindowBits`, `CGSFlushSurface`). If Mesa's frame is in
  the record's buffer when entry 24 runs, it reaches the screen: a GLUT
  program in a window shows Mesa's rendering.

## Shared contexts [V, 2026-10-08]

`gldCreateContext`'s fourth argument is the context to share with (the
`*(void **)a` the first call returned): Core Image's two helper contexts
get the program's. The bundle makes a Mesa context with the other's as
its share list (`mesa_for()` in `gld/rdn_mesa.c`), making the other's
first when it does not exist (contexts are made on first use).
`tools/guest/sharetest.c` checks it. One current context for the whole
process (`rdn_current_rend`): right for one thread switching between
contexts, as Core Image does, not for several threads.

## Pbuffers [V, 2026-10-08]

- `CGLCreatePBuffer`'s object is the record `gldAttachDrawable` gets for
  type 0x5a: word 2 a surface ID (from `cglUniqueSurfaceID`), word 3 the
  texture target, word 4 the format (0x1908, 0x8058, 0x805b GL_RGBA16,
  0x881a, 0x8814), word 5 the number of levels, words 6 and 7 the size.
- `CGLSetPBuffer` goes to `_cglSetAnyDrawable`, which calls the renderer's
  attach with that record and returns what it returns: 1 means "set
  viewport and scissor from the object's size", 2 "nothing more", anything
  else is the error (Apple's software renderer's 0x271a is
  `kCGLBadEnumeration`). A program only gets a drawable-less context to
  draw into a pbuffer with a pixel format that has a display mask
  (an off-screen format has mask 0, and Core Image counts the video memory
  of a context's display only: `fe_cgl_total_vram`).
- `CGLTexImagePBuffer` is `CGLSetParameter(ctx, 997, {ID, target, format,
  width, height, 0x8367, buffer, levels})`, which the engine's
  `gliSetInteger` (renderer table slot 0x24) does itself, for *its* bound
  texture (Mesa's `glBindTexture` never reaches it) and refuses with
  `kCGLBadState`. No `gld*` call is made. The bundle hooks the symbol.
- `CGLDestroyPBuffer` tells the driver nothing.

## Core Image in the window server [V, 2026-10-08]

The window server's own GL ("cgls", in CoreGraphics) mirrors CGL: its
pbuffers are `cglsCreatePBuffer` (format modes 0x24 for 0x8058 and
0x1908, 0x23, 0x2b for GL_RGBA16, 0x2c, 0x2d), `cglsAttachPBuffer` (an
`IOAccelSurface` for it, then the renderer's attach with type 0x5a; size in
words 8 and 9 of its object, ID in word 2, target in 3, format in 4),
`cglsTexImagePBuffer`, which is `cglsSetInteger(ctx, 0x3e6, ...)`, a plain
call inside CoreGraphics, `cglsSetInteger` itself a jump through the
renderer's table (`ctx + offset + 0xc`, slot 0x30). Parameter 0x3e6
(0x3e5 for CGL's) binds the texture bound in the context to a surface
**by ID**: a pbuffer's, or the screen's, which the filter layer uses for
the backdrop: values `{surface ID, target 0x84f5, 0x1908, width, height,
0x8367, 0x400, 0}`. The picture is the window's rectangle of the screen
context's drawable, row 0 the top.

What a filter on a window costs in GL calls (`CGSAddWindowFilter`, flags
0x3001): the layers underneath are drawn into the screen context; a
texture is made and bound to the screen's surface (above); a Core Image
context sharing with the screen context compiles the filter's programs;
its pbuffer (size of the filter's region) gets the backdrop copied in by
the fixed function pipeline with rectangle texturing never enabled (its
contexts start with it on); the pbuffer is bound as a texture in the
screen context and a quad is drawn over the window with the programs; the
window's own tiles are drawn over that. Its first GL calls on the new
context come before the attach and carry the screen context's engine
context, so they land in whatever Mesa context is current.

How the bundle answers (`rdn_ws_core_image()`, behind the file
`/Library/Application Support/RadeonNI/coreimage`): the extension names
and a renderer name Core Image knows; type 0x5a attaches returning 2;
Mesa's dispatch for the pbuffer contexts; texturing on and the screen
context's rectangle texture inherited when a pbuffer context is first made
current; `OSMesaTexCopyDrawable` for the screen's surface;
`cglsSetInteger` taken by an inline hook (twelve words checked, four
replaced by a jump; `rdn_hook_cgls_set_integer`); `cglsDestroyPBuffer`
and `CGLDestroyPBuffer` hooked to give the memory back.

## Not known yet

- Whether the engine rewrites entries of `disp` during longer use. It
  cannot do so from inside a GL call any more (none reaches it), but it
  may at lifecycle events; only a short program has been run.
- The meaning of most fields of the renderer info and pixel format records,
  the 33-entry driver table, and every call a window or full-screen
  drawable brings.
- What the return values 2 and 4 above mean.
