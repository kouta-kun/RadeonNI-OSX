# piglit on the Power Mac G5

A7's "piglit subset", run on 2026-10-08: piglit's OpenGL tests, built for
Tiger with its GLUT framework, run one by one on the G5 through our bundle
(Apple's dispatch, Mesa r600, the card). The point is a list of what is wrong
ranked by how many tests and which programs it touches, to choose the next
work from.

## How to run it

```
scripts/darwin.sh image                       # once; cmake and numpy were added
scripts/build-piglit.sh -k 0                  # build/piglit-darwin/ (patches applied)
# test lists from piglit's profiles (container Python, see the header of
# tools/piglit/select.py), then:
python3 -I tools/piglit/select.py build/piglit/cmds-quick_gl.txt \
        build/piglit/cmds-quick_shader.txt > build/piglit/selected.tsv
python3 -I tools/piglit/run.py stage          # ~200 MB into ~/piglit on the G5
python3 -I tools/piglit/run.py start ours --env RDN_PIGLIT_MESA=1 \
        --exclude ext_framebuffer_multisample # run it as a background job
python3 -I tools/piglit/run.py fetch ours
python3 -I tools/piglit/run.py summary ours   # or two labels: what differs
```

- `tools/piglit/select.py` keeps tests of OpenGL 2.1 / GLSL 1.20 and lower,
  by name and by the `[require]` block of `.shader_test` files: 5,972 tests.
  `tools/piglit/skip.txt` can drop more.
- `tools/piglit/run.pl` runs on the Mac (Tiger's perl 5.8): one process per
  test, 60 s limit, result and exit status per line, resumable.
- The G5 must be kept in the foreground of the ssh session. Started with
  `nohup ... &` and the ssh session closed, every later test aborts at once
  with "CFMessagePort: bootstrap_register() failed 1100": on Tiger a process
  loses its window server connection when the login session that started it
  ends. `run.py start` therefore blocks; run it as a background job.
- `tools/piglit/patches/0001-apple-mesa-lookup.patch` (piglit's Apple
  dispatch): functions that Tiger's OpenGL does not export are asked of the
  Mesa inside the bundle (`OSMesaGetProcAddress`) when `RDN_PIGLIT_MESA` is
  set. See "The first finding".
- Not built (name clash with `pipe()` through our compat headers): the two
  `arb_separate_shader_objects` tests that declare a global `pipe`. 81 more
  commands name binaries that do not exist here (OpenGL ES, yuv); they are
  reported as `notbuilt`. 21 commands lost their quoting between piglit's
  profile and the shell (`GL2:texture() 2DRect`); they are `harness`.

## The first finding: what the context says it is

Our context reports `3.2 (Compatibility Profile) Mesa 26.2.4` and about 290
extensions. Tiger's OpenGL exports only its own 686 entry points. A program
that trusts the version and calls `glGetStringi` (piglit does, for any
version of 3.0 or more) gets nothing from `dlsym`, and every piglit test
failed on the first try with "GetProcAddress failed for glGetStringi". The
bundle answers only four names a program looks up through
`CFBundleGetFunctionPointerForName` (`byname_function`), and none by `dlsym`.
For the run the patched piglit asks the bundle's Mesa for anything else, so
this run measures Mesa on the card, not what an ordinary Tiger program can
reach. To decide: report 2.1 (what Tiger's own OpenGL does), or answer every
name. Nothing else was changed for the run.

## Results, "ours" (5,313 tests, about 40 minutes)

| Result | Tests |
|---|---|
| pass | 4,774 |
| skip (feature missing) | 249 |
| fail | 181 |
| warn | 3 |
| timeout | 2 |
| crash | 2 |
| notbuilt / harness (not the driver) | 81 / 21 |

Of the 4,962 tests that ran and gave a verdict, 96.2 % pass. All of
`glsl-1.10` (2,100) and 1,146 of 1,165 `glsl-1.20` tests pass; the 19 others
are the harness. Multisample: see the end.

## After the packed-format fix (2026-10-08)

`mesa/patches/0006-r600-big-endian-packed-formats.patch`: r600 chose the
hardware format and the swizzle of 565, 4444, 5551 and 2101010 from Mesa's
big-endian description, which lists the channels from the top bits, while
the hardware reads the word after its 8IN16/8IN32 swap as a little-endian
word. It now uses the little-endian view of those formats in
`r600_translate_texformat`, `_colorformat` and `_colorswap`, and swaps
5_5_5_1 and 10_10_10_2. `tools/guest/packedtest.c` (all 14 types, upload,
draw, read back, round trip) passes. Re-run: 4,846 pass (was 4,774), 117
fail (was 181), 66 tests fixed, none regressed; multisample 359 pass, 1
fail (was 6). Cluster 1 below is therefore closed except for `GL_RGB9_E5`
(cluster 6), `s3tc` mipmap generation and `ext_packed_depth_stencil`
stencil, which are other causes.

## Failure clusters, ranked (first run, before the fix)

1. **(fixed, see above) Packed pixel types are wrong (about 400 subtests, 58 formats).** In
   `teximage-colors` every internal format fails for exactly seven
   format/type pairs: `GL_UNSIGNED_SHORT_5_5_5_1`, `4_4_4_4`, `4_4_4_4_REV`,
   `GL_UNSIGNED_INT_10_10_10_2`, `5_6_5_REV`, with RGBA and BGRA. The colours
   come back with their channels reversed (expected 0.27 0.13 1.0 0.73,
   observed 0.73 1.0 0.13 0.27 for `4_4_4_4`). The same cause is likely
   behind `texture-packed-formats`, `s3tc-targeted` (`R4G4B4A4_PACK32`),
   `texsubimage-unpack` (RGB5), `fbo-readpixels` and `getteximage-formats`
   for `RGB9_E5`. On a big-endian host the byte order of a packed type is the
   reverse of Mesa's little-endian table. Linux big-endian (`qemu-ppc`) never
   uploaded such a texture in our tests. Old Mac games use 4444 and 5551
   textures constantly. Look first: Mesa's format table for this build
   (is `UTIL_ARCH_BIG_ENDIAN` defined for Darwin PowerPC?), then the
   `bool`-sized-four-bytes class.
2. **Front and back buffer semantics (about 15 tests).** `gl-1.0-drawbuffer-modes`
   (drawing to GL_BACK changes GL_FRONT_LEFT), `gl-1.0-front-invalidate-back`,
   `gl-1.0-swapbuffers-behavior`, `fcc-front-buffer-distraction`,
   `fbo-sys-blit`, `fbo-sys-sub-blit`, `gl-1.0-simple-readbuffer`, and the
   probes at pixel (0,0) of `clear-accum`, `draw-pixels`, `glsl-fs-fogscale`.
   This is the window-surface path (`docs/QUARTZ-EXTREME.md`): a window's two
   buffers are one surface here.
3. **Occlusion queries count 0 samples (5 tests):** `occlusion_query`,
   `_conform`, `_meta_fragments`, `_meta_save`, `occlusion-query-discard`.
   A real driver or readback problem, and games use them.
4. **Two-sided vertex program lighting (27 tests, one cause):**
   `vertex-program-two-side`, every combination.
5. **`GL_DEPTH32F_STENCIL8` stencil (about 35 tests):** depth and stencil of
   `ARB_depth_buffer_float` / `ARB_framebuffer_object`; also
   `ext_packed_depth_stencil@getteximage` (stencil reads back 0).
6. **`GL_RGB9_E5` (about 10 tests):** `texwrap`, `generatemipmap`, readback.
7. **Video memory (2 tests, A7):** `large-tex` ends with GL_OUT_OF_MEMORY and
   SIGBUS: "out of video memory (268435456 bytes asked, 538586880 in use)".
   A 256 MB request fails with only half of the 768 MB in use: no eviction,
   no spill to the GART for that size, or fragmentation. `tex3d-maxsize`
   asks for 8 GB, which is the test's own business. Not a cluster, but the
   only piece of evidence for eviction.
8. **Single tests:** `ext_timer_query` (32 and 64-bit results differ),
   `arb_timer_query@timestamp-get`, `fs-dfdx/dfdy-accuracy` (warn),
   `arb_debug_output-api_error` (log empty), `arb_get_program_binary`,
   `fbo-blit-check-limits`, `gl-1.0-fog-linear-floor`, `gl-1.0-user-clip-all-planes`,
   `gl-2.1-point_smooth`, `line-smooth-*`, `linestipple`, `polygon-stipple-fs`,
   `rasterpos` (a subtest `glsl_vs_uniforms` and then a hang), 
   `depth-clear-precision-check` (hang after `depth32f_stencil8`),
   `windowoverlap` (no pixel format), compressed `fbo-generatemipmap-formats`.

## What this says about the next work

- **Correctness comes before A7.** One piglit run found four broad bugs
  (1 to 4) that affect programs we have not tried, in a few hours, where
  games found one at a time over days. Recommended order: packed pixel types,
  occlusion queries, front/back buffers, two-sided lighting, then the
  stencil of `DEPTH32F_STENCIL8`.
- **Eviction (A7):** one test, with an unknown cause. Do not start it before
  a game runs out of memory; look at `large-tex` first, since 256 MB may fit
  in the GART, and the failure may only be a missing fallback.
- **Interrupts (A7):** nothing here points at them. The two timeouts are
  hangs after a failed subtest, not fence waits.
- **Accelerated 2D plug-in:** piglit does not test it.
- **The known gaps** (topology from the VBIOS, hot-plug, wake, more modes,
  two monitors, audio) are not exercised by piglit at all; they are the
  user's priorities, not the test results'.
- **What to run again** after each fix: `run.py start ... --match` on the
  area, and `summary ours new` to see what changed.
- **Missing: a baseline.** There is no run on Apple's own renderer, so
  failures that Tiger's OpenGL also has (it is a 2007 OpenGL 2.1) cannot be
  told apart yet. GLUT picks the renderer by the display the window opens
  on; a switch in the bundle that refuses the accelerated formats
  (`RDN_GLD_ACCEL`-like) would give it. Until then, clusters 1 to 5 are
  read as ours because their failures are the reverse of what a correct
  renderer does, not because Apple's was seen to pass.

## Multisample (646 tests, `ext_framebuffer_multisample*`)

353 pass, 287 skip, 6 fail, 8 warn (`blit-scaled`, all eight). The fails:
`formats` for 2, 4, 6, 8 and all samples (it stops at `GL_RGB`), and
`blit-mismatched-formats` (alpha 0.25 expected). Multisampling is in better
shape than the rest; `formats` with `GL_RGB` is worth a look next to cluster 1.
(A first attempt at this batch aborted after its ssh session ended, see "How
to run it"; the numbers are from the re-run.)
