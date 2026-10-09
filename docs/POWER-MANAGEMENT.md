# Framebuffer power management on Tiger: why sleep hangs, and what to build

Written 2026-10-08 (research, nothing built yet). Read with `docs/HOTPLUG.md`
and the journal entries "display sleep, first try" and "System sleep".

## What happened on the G5

- `pmset displaysleep 1` and 3 minutes idle: the display never slept and the
  kext never logged a power request.
- Apple menu > Sleep: "System Doze", back within seconds, the picture frozen,
  `cgmode list` showing no display, ssh fine. Killing the window server: the new
  one hung in `IOFBRebuild` in the kernel, state `Us`. A shutdown over ssh then
  killed the network but the Mac stayed on with a frozen picture. A hard power
  off was the only way out.

## The cause (found by reading Apple's IOGraphics, checked in Tiger's binary)

The sources used are Apple's, tag `IOGraphics-179`
(`github.com/apple-oss-distributions/IOGraphics`), which is the nearest tag
with the same wrangler properties as the G5's IOGraphicsFamily 1.4.2; the
tags before it (123 to 128) lack them. They are APSL: read to understand, never
copied (CLAUDE.md). Files kept under `build/tmp/iog/` (git-ignored).

1. `IOFramebuffer::extEntry()` is the first thing every call from user space
   into the framebuffer does (`IOFBRebuild`, mode queries, the shared cursor,
   `extSetBounds`, ...). It closes the framebuffer gate and, **while a flag
   (`pagingState`) is clear, sleeps on the gate, uninterruptibly**
   (`sleepGate(&serverConnect, false)`). The disassembly of Tiger's
   `IOFramebuffer::extEntry` (0xb348 in the kext's own addressing) does the same:
   a test of one bit in the framebuffer object, `sleepGate`, test again.
2. System sleep clears it for every framebuffer: `systemPowerChange` ->
   `startThread` -> `sleepWork` -> `checkPowerWork`: the window server is told
   (`notifyServer(false)`), `handleEvent(kIOFBNotifyWillSleep)` is called, the
   flag is cleared, and the system's sleep is acknowledged.
3. Only the way back sets it: `handleEvent(kIOFBNotifyDidPowerOn)` while the
   system is up sets the flag and delivers `kIOFBNotifyDidWake`, and `wakeServerState`
   wakes the gate. That runs when the framebuffer is told to go to its highest
   power state: `IOFramebuffer::setPowerState` queues it and the sleep thread
   calls the driver's `setAttribute(kIOPowerAttribute, state)` and then
   `acknowledgeSetPowerState()`.
4. **`setPowerState` is only called if the framebuffer registered a table of
   power states with power management.** The base class does `PMinit()` and
   `joinPMtree()` but not `registerPowerDriver()`; the subclass must. Apple's own
   reference subclass does (`IONDRVFramebuffer::initForPM`); ours does not (no
   `registerPowerDriver` anywhere in `kext/RadeonNI`). So: nothing ever tells our
   framebuffer to power on, the flag stays clear after the first system sleep, and
   every call from the window server sleeps in `extEntry`, forever and
   uninterruptibly. That is the frozen display, the window server stuck in
   `IOFBRebuild`, the unkillable process, and the shutdown that stops half way
   (loginwindow waits for the window server).
5. It also explains why display sleep never reached us: the wrangler's idle
   timer works through the same power tree.

## What Apple's reference does (`IONDRVFramebuffer`, same tag)

- `initForPM()`: three states: 0 (off: no flags), 1 (doze: input power on,
  output off, not usable), 2 (`IOPMDeviceUsable`, input and output power on).
  `pm_vars->theNumberOfPowerStates = 0; registerPowerDriver(this, states, 3);
  temporaryPowerClampOn(); changePowerStateTo(doze)`. Overrides
  `maxCapabilityForDomainState`, `initialPowerStateForDomainState` (domain on:
  highest state, else sleep) and `powerStateForDomainState` (domain on: current
  state, else sleep).
- When the board cannot be powered off safely (legacy desktops, a driver that
  says it cannot) the states carry `kIOPMPreventSystemSleep`: the system then
  cannot sleep while the framebuffer is in them, only the display can.
- `setAttribute(kIOPowerAttribute, n)` -> `ndrvSetPowerState`: leaving the top
  state: `handleEvent(kIOFBNotifyWillPowerOff)`, sync off (DPMS); entering the
  sleep state: tell the accelerators (`handleEvent(kIOFBNotifyWillSleep, true)`)
  and redirect the VRAM mapping (`getVRAMRange()->redirect(kernel_task, true)`)
  so processes touching the aperture do not touch a dead card; coming back from
  sleep: undo the redirect, `handleEvent(kIOFBNotifyDidWake, true)`, re-read
  whether the display is online (`kConnectionEnable`), re-probe, reset AGP;
  reaching the top state: `handleEvent(kIOFBNotifyWillPowerOn)`, then
  `handleEvent(kIOFBNotifyDidPowerOn)` and the sync back on.
- `setAttribute(kIOSystemPowerAttribute, kIOMessageSystemWillPowerOff/Restart)`:
  quiesce the hardware before the machine goes away.
- `initForPM` is called from the driver's start/enable path, after the base
  `IOFramebuffer::start`.

IODisplay and IODisplayWrangler register their own power states the same way
(`IODisplay.cpp`, `IODisplayWrangler.cpp`).

## Plan for the kext (not started)

1. `RadeonNI::initForPM()` as above, called after `enableController` succeeds.
   Start with `kIOPMPreventSystemSleep` on every state: the card's survival of a
   doze is unknown (the G5 may cut the slot's power; VRAM contents, the GART
   table and the engine's state would all be lost), and with the flag the
   machine only does display sleep and "Sleep" does nothing. Check first on the
   G5: `ioreg -c IOPMrootDomain -l` for the PM features (the reference tests
   `kPMHasLegacyDesktopSleepMask` and `kPMCanPowerOffPCIBusMask`); the G5 logged
   "System Doze", which fits a doze-only desktop.
2. `maxCapabilityForDomainState`, `initialPowerStateForDomainState`,
   `powerStateForDomainState` as in the reference.
3. `setAttribute(kIOPowerAttribute, n)`: move our `setOutputPower` logic into
   the reference's ordering (WillPowerOff before the output goes off,
   WillPowerOn before it is brought back, DidPowerOn after the mode set), and
   never block in it (it runs in the framebuffer's sleep thread holding the
   framebuffer gate).
4. `kIOSystemPowerAttribute` for restart and power off.
5. If system sleep is allowed later: redirect the aperture mapping the
   accelerator gives to processes, `kIOFBNotifyWillSleep/DidWake` with `true`,
   a re-POST and re-init of the engine, GART and surfaces on wake (the whole of
   `rdn_post`, `rdn_gpu`, `rdn_cp`, `rdn_gart` again) and a test with a game
   running.
6. Keep `rdn_dpms=0` as the kill switch (it exists: `fDpms`), and have it also
   skip `registerPowerDriver`.

## Test procedure (the user must be at the G5)

Never a first test of a power path without the user present and a plan to get
out: a hung window server needs the power button. Order: (a) display sleep from
an Exposé hot corner "Put Display to Sleep" with the kext log read after sleep and
wake; (b) system sleep only after step 5 above; (c) hot-plug during each.
Do not write properties to `IODisplayWrangler` (Tiger panics; journal). Tiger's
`pmset` has no `sleepnow` and no `displaysleepnow`.

## Sources

- IOGraphics (tag 179): `IOGraphicsFamily/IOFramebuffer.cpp` (`extEntry`,
  `checkPowerWork`, `sleepWork`, `setPowerState`, `powerStateWillChangeTo`,
  `systemPowerChange`, `handleEvent`, `extAcknowledgeNotification`),
  `IONDRVSupport/IONDRVFramebuffer.cpp` (`initForPM`, `ndrvSetPowerState`,
  `maxCapabilityForDomainState`), `IOGraphicsFamily/IODisplay.cpp`,
  `IODisplayWrangler.cpp`.
- Tiger's own `IOGraphicsFamily` 1.4.2 on the G5, disassembled with `otool -tV`
  (`IOFramebuffer::extEntry`; the strings of the wrangler's properties).
