# Mac OS X 10.5 (Leopard, PowerPC): framebuffer kext and GL bundle study

Read this before touching 10.5 support. The user reports that the
hand-patched kext worked on a 10.5 Mac; the sections below that predate it
(paper checks, 2026-10-11) say "never run" and are kept as the record of
what was compared. Since 2026-10-11 there is also a **Leopard guest** (Sorbet
Leopard R15 = 10.5.9, Xcode 3.1.4) in which the kext is built natively and
the GL bundle can be inspected (next section). The early GL bundle result is recorded under "GL bundle on 10.5".
The native G5 backing-surface path below supersedes that result.

Build and package: `scripts/leopard.sh build|check|package`. The kext is
built in the Tiger guest with `make LEOPARD=1` (its own `build-leopard/`),
which adds `kext/RadeonNI/leopard.s`. `g5/install.sh` accepts 10.5 only in
a package with the file `LEOPARD` and without `--accel`; `--check`
(`kextload -t -n`, writes nothing) and `--try` (load from /tmp, not
installed) are for a first run.

## Native G5 backing surfaces (2026-10-22)

Packed AppKit backings are pinned in GART and copied to compositor-owned
VRAM with CP_DMA. The compositor receives metadata and completed GPU
storage, never another task's CPU pointer. Imported buffers retain their
Mesa external-BO references across resize and producer closure.

CPU write unlocks refresh retained imports; GPU producer submissions refresh
imports from the producer's VRAM buffer. A registered GPU producer is also
allowed to query its backing metadata. Its first VRAM buffer is initialized
by GPU DMA before partial rendering, and the viewport binding adopts a
backing registered after the initial drawable attachment. This preserves
Preferences toolbars as well as CPU-drawn content.

Backing synchronization holds the accelerator allocation lock through DMA
completion. Unknown completion retains the command buffer, source GART
binding and destination allocation until reboot. Leopard builds require
`com.apple.kpi.bsd` for compositor authorization. Shared browser/GPU process
mode is not required.

Native checks cover cross-process retained updates, resize, producer closure,
alpha, explicit CPU reads, unflushed GL rendering and windowless surfaces.
The strided-copy model covers 3,010 guarded and invalid-input cases, including
a native big-endian run. `tools/guest/backinggputest.c` imports once per size;
its later frames must update without another import. Activity Monitor live
updates and resize, and Preferences pane changes, were checked on .98. The
native NV12 delivery regression passed 4,535,634 exact bytes; Aurora UVD GPU
playback also passed its smoke check. These runtime checks used the fix on
`cf5cab4`, before reconciliation with upstream `a879559`. The reconciled tree
preserves upstream's SurfaceGate default and excludes the new authorization
calls from Tiger builds. It passes host checks and native Leopard builds;
it has not replaced the running .98 driver.

## The Leopard guest (2026-10-11)

`scripts/leopard-guest.sh run|ssh|snapshot|restore` is `scripts/tiger.sh`
with its own disk (`images/leopard.qcow2`), ssh port 2223, VNC `:2`
(5902) and sockets `build/leopard-{mon,qmp}.sock` (`GUEST_QMP` and
`GUEST_SHOT` point `guest-ctl.py` at them). It runs beside the Tiger guest;
no passthrough is offered from it. User `tiger`, the same key.

How it was made, since the obvious ways fail:

- The Sorbet dmg is one HFS+ volume, no partition map: write it into a
  disk with a partition map (`parted mklabel mac`, partition at sector 64)
  and the Open Firmware of QEMU (OpenBIOS) **still cannot boot it**: its
  catalog file is fragmented (8 extents), and OpenBIOS reads only the
  first. Fix: boot the Tiger DVD with the volume as a second disk,
  `diskutil partitionDisk` a new one and `ditto -rsrc` the volume over, then
  `bless --folder .../CoreServices --bootinfo`. The result boots.
- Setup Assistant answers the keyboard step and then stops taking input
  (QEMU's emulated keyboard). Worked around in single user mode (`-s`):
  `touch /var/db/.AppleSetupDone`, a user created with `dscl -f
  /var/db/dslocal/nodes/Default localhost` after
  `launchctl load .../com.apple.DirectoryServicesLocal.plist`, the key in
  `authorized_keys`, `sudoers`, and ssh enabled in `ssh.plist`.
- Xcode 3.1.4: the dmg is plain (concatenate its `.hfs` with the small
  parts, `hdiutil attach -imagekey diskimage-class=CRawDiskImage`), but
  `installer -pkg ... -target /` puts the relocatable parts in `/` instead
  of `/Developer`; move `Documentation Extras Headers Makefiles Platforms
  SDKs Tools` and the Xcode apps into `/Developer` and run
  `xcode-select -switch /Developer`. gcc 4.0.1 (Apple build 5493), gcc 4.2,
  llvm-gcc and the 10.4u and 10.5 SDKs come with it.

### Native kext build (verified)

`LEOPARD_NATIVE=1 scripts/leopard.sh build|check` builds `kext/RadeonNI`
with `make LEOPARD=1` inside the guest. Result: builds first time with
Xcode 3.1.4's gcc 4.0.1 and 10.5's Kernel.framework; the 586 undefined
symbols are **identical** to the Tiger-guest cross-build's; the relocation
check passes; `kextload -t -n` on 10.5.9 says "appears to be loadable" and
`kextlibs` resolves `com.apple.kpi.*` 9.8.0, IOPCIFamily 2.6, IOGraphicsFamily
1.7.3. No card, so nothing was started.

## GL bundle on 10.5 (2026-10-11, no card in the server)

What is known by running things in the guest:

- **The Tiger bundle loads.** The host-built `RadeonNIGLDriver.dylib`
  (Mesa inside, 10.4u SDK) `dlopen`s on 10.5.9: its imports (libz, IOKit,
  libSystem) exist, and 62 of Leopard's 66 `gld*` names are present.
- **Update, same day:** the engine drops a plug-in missing any of the 66
  names, so with 62 the bundle would have been refused. The four are in
  the entry list now (`gld/RadeonNIGLDriver.c`, `gld/rdn_gld.exports`):
  66 of 66 `dlsym`, on 10.5.9 and on 10.4.11. Details in
  `docs/GLD-INTERFACE.md`, "Mac OS X 10.5.9".
- **`gld*` differences.** Apple's X1000 driver on 10.5 exports 66; the
  bundle's table has 63 (62 of them in Leopard's list). New on 10.5:
  `gldCreateProgram`, `gldDestroyProgram`, `gldObjectPurgeable`,
  `gldObjectUnpurgeable`. Tiger-only name: `gldRelatePipelineProgram`.
  Signatures of the four are not known.
- **Dispatch table.** `gliDispatch.h` of the 10.5 SDK has 759 entries
  against 686 in the 10.4u SDK, and the first 686 are the same names in
  the same order: the table only grew at the end. The glue generated from
  the 10.4u header therefore covers the first 686 entries on 10.5 and the
  rest stay the engine's. (Do not generate from the 10.5 header for a
  bundle that must also run on Tiger: it would write past Tiger's table.)
- **Window server hook.** `cglsSetInteger` on 10.5.9 is another function
  (its first words are `mflr r0; cmpwi r3,0; bcl 20,31; ...`), so the
  twelve-word check fails and the hook leaves it alone, as designed. What
  the hook was for (a Core Image backdrop texture) will not work until the
  10.5 function is read.
- **Not known**: the engine context offsets (`rend + 0x4698` table,
  `+ 0x4680` `disp`), the pixel format and renderer info records, and the
  drawable records. Without the card the guest never selects our bundle (the
  kext announces the accelerator only with the card), so these need either
  disassembly of 10.5's `GLEngine` or a run on the G5.

### A Leopard-only bug found: libstdc++ coalescing

The bundle statically links GCC 14's libstdc++. On 10.5 dyld coalesces its
exported weak symbols with the system's `libstdc++.6`, an older ABI:
during `dlopen`, `std::ios_base::Init` (ours) called the system's
`basic_ios::init`, and `locale::_Impl::_M_install_facet` freed pointers it
did not allocate ("Non-aligned pointer being freed" ×15, from
`_GLOBAL__sub_I_r600_sfn.cpp`). Fix: export only the `gld*` names
(`gld/rdn_gld.exports`, `-Wl,-exported_symbols_list` in
`mesa/target/meson.build`). Relinking the finished bundle with the list
(without rebuilding Mesa) removed the errors on 10.5.9; the Tiger link
accepts the same flag. Not yet seen on a Tiger run: needs a rebuild with
`scripts/build-mesa.sh darwin` from the checkout that has the Mesa tree.

## What was compared (2026-10-11, from Apple's public sources)

Sources: `apple-oss-distributions/xnu` tags `xnu-792.24.17` (10.4.11) and
`xnu-1228.15.4` (10.5.8), `IOGraphics` 179 (10.4.11) against 191.5, 193.2.2
and 233.3, `IOPCIFamily` 79.1 to 162.3. Which tag shipped in which 10.5.x was
not confirmed; every tag up to 233.3 (IOGraphics) and 162.3 (IOPCIFamily) gave
the same answer, and the layouts only move in IOGraphics 305 and
IOPCIFamily 196 (10.6). Reading only; nothing copied.

| Class | Result |
|---|---|
| IOFramebuffer, IOGraphicsDevice | same 97 virtual and reserved slots, same 54 data members (`Point` is `IOGPoint`, typedef'd back to `Point`, same struct) |
| IOService, IORegistryEntry | slots compatible; one reserved slot became a method (below); `IOPMpriv *priv` is `IOServicePM *pwrMgt`, a pointer either way |
| IOUserClient | `externalMethod` took reserved slot 0; ivar block is 32 bytes either way (`void *[8]` against 3 + 1 bytes + `void *[7]`) |
| IOPCIDevice, IOAGPDevice | own slots and data members unchanged through IOPCIFamily 162.3 |
| IOAccelerator | exists in IOGraphics 233.3, no data members |
| IOMemoryDescriptor, IOBufferMemoryDescriptor, IOWorkLoop, IOTimerEventSource | compatible (reserved slots used, none moved) |

Method used (and its trap): a class's vtable slots are its `virtual`
declarations plus one per `OSMetaClassDeclareReservedUnused`; `...Used`
declares nothing and the method that took the slot is declared in its
place. Comparing textual order instead gives false "moves" wherever a
newer header redeclares an inherited virtual (an override takes no slot).

## What breaks, and the stubs

Leopard's 10.4 compatibility libraries (`com.apple.kernel.iokit`, `.libkern`,
`.mach`, `.bsd`, `.6.0`: version 7.9.9, compatible from 1.0.0b1) satisfy the
kext's "6.0" requests. Of the 586 symbols the kext imports, 426 are in the
kernel's export lists (`_IOLockLock` as an alias of `lck_mtx_lock`), 160
come from the PCI and graphics families, and **two do not exist in
Leopard**: `IOService::_RESERVEDIOService4` and
`IOUserClient::_RESERVEDIOUserClient0`. Every vtable of a Tiger-built
subclass points at them. They are `IOService::systemWillShutdown` and
`IOUserClient::externalMethod` in Leopard; `leopard.s` defines the old names
as jumps to those (through CTR, the kernel is out of range of `b`). IOGraphics
does not override `systemWillShutdown`, so the jump is exact.

For the 160 family symbols only names and reserved slots were checked: all
31 `IOFramebuffer`, 17 `IOAGPDevice` and 13 `IOPCIDevice` reserved slots
the kext references are still unused in the Leopard-era headers, and every
method name is in the header. Signatures were not compared.

## Outputs from the VBIOS (2026-10-11)

The first run on a Mac with a retail HD 6670 (`1002:6758`) found no EDID: the
driver only knew the 7570 board's two connectors (DVI-I on UNIPHY0, DDC 0x93;
"DisplayPort" on UNIPHY2, DDC 0x92). The BIOS answered sink type 0x04, dual-link
DVI-D, on the "DisplayPort" lines, and HPD4 read high. Since then
`rdn_output_detect()` (`hw/rdn_modeset.c`) tries, in order: the two built in; the
DisplayPort lines as TMDS (`card->twin`); and the outputs the VBIOS's object
table describes (`board_outputs_read()`, a port of
`radeon_get_atom_connector_info_from_object_table()`: UNIPHY and link from the
encoder object, DDC line from the connector's I2C record, hot-plug pin from its
HPD record through the GPIO pin table). Every path is logged as `board: path ...`.
**First run (same day, a retail HD 6670, DVI direct cable):** the table was
HDMI-A on UNIPHY2 (DDC 0x94, HPD1), dual-link DVI-D on UNIPHY0 link A (DDC 0x92,
HPD4) and VGA on DAC1 (DDC 0x90, no HPD). The EDID came from the DVI port's
line 0x92, found through the "twin" candidate, which pairs that line with the
7570's UNIPHY2: the picture went out of the HDMI port's transmitter and the
DVI port stayed dark (log: `EDID found`, mode set returned 0, no display). Fixed:
with a usable table only the built-in outputs the table also has are tried,
then the table's; the twin is only for a VBIOS with no digital path.
`tests/board_outputs.c` now rebuilds that case (two DDC lines moved in the
7570's connector records) and fails on the old order.

Link B is supported in the transmitter blocks (`rdn_output.linkb`); nothing has
driven a link B output. `tests/board_outputs.c` checks the parse against the
7570's VBIOS: it must return the built-in wiring.

## The kext linker crash (2026-10-11, `8258a77`)

The tester's Mac, a PowerMac11,2 on stock xnu-1228.15.4, took the build that
had loaded at boot the day before and then, after an install and a reboot,
never started it: `kextd: forked load task exited by signal (10)`, and the
same crash in `kextcache`. Both reports: `EXC_BAD_ACCESS (SIGBUS)` at 0x0 in
`kld_file_prepare_for_link + 200` (IOKit.framework 1.5.2), called from
`__kload_load_modules` and `kload_load_dgraph`.

Cause (IOKitUser `kext.subproj/kld_patch.c`, tag 388): `relocateSection()`
caches a relocation record per entry of the sections that can hold vtables
and, for scattered entries and anything but a 32-bit VANILLA, `continue`s
without filling the record; `prepareFileForLink()` then loops over every
entry and reads `rec->fRInfo->r_address`. A **scattered relocation in
`__const` or `__data`** therefore crashes every kext load. Ours came from
`const struct rdn_output *builtin[2] = { &outputs[0], &outputs[1] }`: a
constant array of pointers into another array (pointer plus offset to local
data). The build before it had 168 scattered entries, all in `__text`
(HA16/LO16), none outside. Fixed by assigning the elements in code, and
`scripts/check-kext-relocs.sh` (called by `leopard.sh check` and
`make-g5-package.sh`) refuses any such entry outside `__text`.

The same limit applies to Tiger's `kextload` (the same file in IOKit); rule
for `hw/` and `kext/`: no static data holding the address of an element other
than the first, or of a member, of local data.

## Not done, not known

- ~~A load on a real 10.5~~: done by the user with the hand-patched kext;
  `kextload -t -n` of the native build is "loadable" (above).
- The family kexts' own version requirement: `Info.plist` asks for
  `IOGraphicsFamily` and `IOPCIFamily` 1.0; not checked against 10.5's.
- Runtime behaviour: the window server and `IOFramebuffer` of 10.5 differ
  from 10.4 in ways the headers do not show (mode validation, display
  change notifications, clamshell).
- **Acceleration is not done** (see "GL bundle on 10.5"): the GL bundle follows the Tiger `gld*` interface,
  the window server hook (`cglsSetInteger`, twelve checked words) is
  10.4.11's, and the 2D plug-in is Tiger's. 10.5 would be a new interface
  study (VMsvga2's 10.5/10.6 signatures are the only lead, `[I]` in
  `RESEARCH.md`).
- The Tiger kext does not include `leopard.s`: `systemWillShutdown` does not
  exist there and the link would fail.
