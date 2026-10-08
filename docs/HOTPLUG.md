# Display modes, display power management and hot-plug

Written 2026-10-08. Status: **code written and compile-checked, the library
parts tested on the host; not yet run on the G5.** What each part is, what is
not known about IOGraphics, and how to check it on the G5.

## 1. The mode list (`hw/rdn_mode.c`, `hw/rdn_dmt.c`)

Quake 3 and Doom 3 start in 640x480 or 800x600 and said "Could not initialize
OpenGL": the kext offered only the EDID's detailed timings (the monitor's
1920x1080 and nothing else). `rdn_edid_modes()` now builds the list:

1. the detailed timings: the base block's four, then those of CEA extensions
   (interlaced ones and pixel clocks over 165 MHz are left out:
   `RDN_MODE_MAX_CLOCK`);
2. the established timings (bytes 35 to 37) and the standard timings (38 to
   53) that VESA DMT has numbers for (`rdn_dmt_modes`, 54 progressive timings
   up to 165 MHz; of two timings of one size and rate the ordinary one, not
   reduced blanking, unless that is the only one);
3. CEA video codes (480p, 576p, 720p, 1080p);
4. 640x480, 800x600 and 1024x768 at 60 Hz when the monitor's range limits
   (display range descriptor) allow: every monitor with a scaler shows them
   and few list them.

The preferred timing is first (mode 1, the default). `kMaxModes` is 32. The
test `tests/edid_modes.c` (`make test`) reads `private/monitor-edid.bin` (the
TV) and `private/g5-monitor-edid.bin` (the current "Mi Monitor", from ioreg)
and checks the list; x86 and PowerPC give the same output. The Mi Monitor
gets 17 modes, including 640x480, 800x600 (56, 60, 72, 75), 1024x768 (60, 75),
1280x1024, 1440x900, 1600x900, 1680x1050.

Not done: modes the monitor does not list (1366x768 on a 1080p panel, 1280x800
and so on), GTF/CVT timings for standard-timing entries DMT lacks, scaling by
the card (the monitor's scaler does it), interlaced modes.

**Found on the G5 (2026-10-08):** the list works (54 modes: 18 sizes at 3
depths), and a mode set to 640x480, 800x600 or 1024x768 made the desktop
come out with streaks and every other row missing. The mode set was right;
the window server's OpenGL contexts drew into the screen with the pitch of
1920x1080, because the device (`mesa/target/rdn_device_darwin.c`) read the
screen's size and pitch once, when it was opened. Fixed: the device asks the
kext again (`dev_screen_refresh`, at most every 20 ms) and a context
attached to the screen attaches again when they change (`rdn_make_current`).
Needs a window server restart to take effect. Also: `cgmode set` used
`CGDisplaySwitchToMode`, whose mode is put back when the program exits; it
now takes a number of seconds to stay (`cgmode set 0 640 480 32 10`), and
prints bytes a row.

**Check on the G5** (after the kext is installed and the Mac restarted):
`~/gl/cgmode` lists the display's modes; Quake 3 with `r_mode 3` (640x480) and
Doom 3 from the Finder start; System Preferences > Displays shows the list.
The kernel log (`sudo dmesg | grep RadeonNI`) has one line per mode.
A mode with no picture is a wrong timing in `rdn_dmt.c`: the number is in the log.

## 2. Display power management (`rdn_output_disable`, `RadeonNI::setOutputPower`)

`rdn_output_disable()` (hw/rdn_modeset.c) stops the picture and the signal as
Linux does for DPMS off: on DisplayPort the video stream off, the transmitter
off and the sink into D3 (DPCD 0x600); on TMDS the transmitter off and the AVI
infoframe off; then the CRTC blanked and stopped. The way back is
`rdn_modeset()` with the same mode, which brings the sink back to D0 and trains
the link again. The kext does that when the OS says so.

What the OS sends is **not known**: IOFramebuffer's documentation says the
subclass gets `setAttribute(kIOPowerAttribute, state)` for power changes and
should call `handleEvent(kIOFBNotifyWillPowerOff)` before entering a state
that is not the maximum and `handleEvent(kIOFBNotifyDidPowerOn)` after entering
it; DPMS also arrives as `setAttributeForConnection(kConnectionSyncEnable)`
(the bits name the syncs that are off) or `kConnectionPower`. All three are
handled and every request is logged ("power attribute 3 (on is 3)",
"connection syncs 0x3", ...). The value of "on" is taken as the largest power
value seen, so a first request is never taken for off. Read the log after the
first display sleep and fix the mapping if it is wrong.

Boot argument `rdn_dpms=0` ignores all of it (the old behaviour).

**Check on the G5:** System Preferences > Energy Saver, display sleep after 1
minute (or `pmset displaysleepnow` over ssh): the monitor must go to standby,
and wake on a key or the mouse with the same picture; `dmesg | grep RadeonNI`
shows "output off: 0" and "output back on: 0". Also: a system sleep and wake
(`pmset sleepnow`) with the monitor attached.

## 3. Hot-plug (`RadeonNI::pollHotplug`, `monitorReturned`)

There are no interrupts yet (A7), so the kext polls the selected output's
hot-plug line (`DC_HPDx_INT_STATUS` sense bit) twice a second from a timer on
a work loop of its own (`rdn_output_hpd_enable()` enables the lines of both
connectors first; not in `rdn_display_init()`, whose register accesses are
compared with Linux's trace). A change must hold for a second (a waking monitor
pulls the line low). Unplugged: nothing is torn down; `hasDDCConnect` says
no and the OS is told through the connection-change callback it registered
(`registerForInterruptType(kIOFBConnectInterruptType)`). Plugged in:
`rdn_output_detect()` finds the monitor again (also if it moved to the other
connector), its EDID is read, the mode list is made again if the EDID changed
(modes that need more surface than the window server mapped at start are
dropped), the mode of the size on screen (else the preferred one) is set with
the link trained anew, and the OS is told.

A connector whose line reads low while its display answered over DDC at start
(an analog cable, an adapter without the line) is not polled.

Boot argument `rdn_hotplug=0` turns the polling off. Not done: DisplayPort
short pulses (link loss while the monitor stays), several displays at once,
interrupts instead of polling, passive adapters.

**Check on the G5** (the user must be there): unplug the DisplayPort cable for
10 seconds and plug it in: the log says "the monitor was unplugged" and then
"the monitor is back", the picture returns at the same size. Then move the
cable to the DVI-I connector (with its adapter): the picture comes up there.

## Files

`hw/rdn_mode.c`, `hw/rdn_dmt.c`, `hw/rdn_mode.h` (list, `rdn_output_*`),
`hw/rdn_modeset.c` (`rdn_output_disable`, `rdn_output_hpd_enable`,
`rdn_output_connected`), `kext/RadeonNI/RadeonNI.{h,cpp}`,
`tests/edid_modes.c`. Syntax check of the kext without the guest:
`docker run --rm -v $PWD:$PWD -w $PWD/kext/RadeonNI osx-gpu-darwin8
powerpc-apple-darwin8-g++ -fsyntax-only -fpermissive ...` (the flags in
`kext/RadeonNI/Makefile`; the SDK's headers need `-fpermissive` with GCC 14).

## Open: Quake 3's menu (2026-10-08, evening)

Quake 3 (Aspyr's 1.32 port in ~/Desktop/Quake 3) starts, plays the id logo
and then shows only its menu background, or a black screen, and no menu items
or cursor; the process runs at 100 % CPU in its event loop and the GPU
completes about 90 command buffers a second. The user says the menu worked at
some point (before today). What was ruled out, by runs started over ssh and
quit by pid:

- The back buffer: same with `RDN_GLD_NO_BACKBUFFER=1`.
- Today's Mesa and bundle changes: the oldest saved bundle
  (`~/RadeonNIGLDriver.before-packed`) gives the same.
- The refresh rate of the mode: Quartz gives a program that asks for a size
  without a rate the last mode of that size, so Quake 3 got 640x480 at 75 Hz
  and 1080p at 50 Hz. The list now keeps every rate and puts the one nearest
  60 Hz last in each size (`order_for_quartz()` in `hw/rdn_mode.c`); Quake 3
  got 640x480 at 60 Hz and the menu is still empty. Doom 3 starts and enters a
  map.
- The kext's hot-plug polling and the connection callback were not ruled out;
  the polling never reported an unplug.

Not done: restart with the kext from before today's mode work
(`~/RadeonNI.kext.before-modes`) to see whether the menu comes back at 1080p;
a log of Quake 3's GL calls in the menu (`/tmp/rdngld.on`).
Never kill Quake 3 or Doom 3 by name on the G5: use the pid.
