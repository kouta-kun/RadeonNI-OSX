# Extended desktop: expected steps

A plan, written 2026-10-07 at the user's request. Nothing here is started.
The goal: two monitors on the HD 7570, one on DisplayPort and one on DVI-I,
as two independent displays of one Mac OS X desktop, both accelerated.

## Where the driver is today

- One display. `rdn_output_detect()` picks the first connector with an
  EDID (DVI-I, then DisplayPort) and `rdn_modeset()` drives it from display
  controller (CRTC) 0. With both monitors plugged in, DisplayPort stays
  dark.
- `hw/` assumes CRTC 0 in the mode set, the colour tables, the watermarks
  (`rdn_watermark.c`), the hardware cursor (`rdn_cursor.c`) and the state
  kept in `struct rdn_card` (`crtc_on`, `wm_*`, `output`, `dp`).
- The kext is one `IOFramebuffer` (`RadeonNI`) matched on the
  `IOPCIDevice`. It owns the card: VBIOS, POST, the register and aperture
  mappings, the accelerator service's parent.
- The scanout surface is at aperture offset 0; the accelerator hands out
  the video memory after it.
- The accelerator, the surfaces, the copy to the screen (`rdn_blit()`) and
  the 2D plug-in know one screen.

What already fits two displays: the two outputs use different transmitters
(UNIPHY and UNIPHY2), different digital encoders (0 and 4) and different
clock sources (pixel PLL 1 for DVI, the DCPLL for DisplayPort), so neither
has to share with the other. Turks has six CRTCs.

## Steps

Each step ends in something that can be checked. Steps 1 to 3 change no
behaviour for one monitor and can be checked against the reference trace
as before.

### 1. `hw/`: a CRTC number everywhere

- Give the mode set, the colour tables, `set_base`, overscan, scaler, lock,
  blank and enable calls a CRTC argument; register addresses become
  `base + crtc_offset[n]` (Linux: `radeon_crtc->crtc_offset`).
- Move per-display state out of `struct rdn_card` into a per-CRTC
  structure: on/off, mode for the watermarks, output, DisplayPort link.
- `SelectCRTC_Source`, `SetPixelClock`, `SetCRTC_UsingDTDTiming`,
  `EnableCRTC` and the rest already take `ucCRTC`; pass it.
- Check: `make test` unchanged for CRTC 0 (same digests).

### 2. `hw/`: watermarks and line buffer for two displays

- `rdn_watermark.c` follows Linux's `evergreen_bandwidth_update()` for one
  CRTC. Port the two-display part: CRTCs come in pairs that share a line
  buffer, and the split depends on the other CRTC's mode
  (`evergreen_line_buffer_adjust()`); the watermark's available bandwidth
  is divided by the number of active displays.
- Decide which CRTCs to use. Linux would assign 0 and 1, which are a
  line-buffer pair; that is the case its code is exercised with.
- Check: the reference trace has one display only, so only the CRTC 0,
  one-display numbers can be compared. The two-display numbers can be
  compared with Linux's arithmetic in a host test, not with a trace.

### 3. `hw/`: detection returns every connected output

- `rdn_output_detect()` becomes "list the outputs with a display", each
  with its EDID. DisplayPort detection is unchanged per output.
- A mode set on one CRTC must not disturb the other: the encoder and
  transmitter calls are per output already; check that
  `rdn_display_init()` runs once and that nothing in the mode set path
  resets shared state (the display engine clock, spread spectrum on the
  DCPLL).
- Open question to settle here: what `SetPixelClock` with the DCPLL does
  when DVI is already running. Today the picture is right with one
  display; with two, a change of the display engine clock under a running
  CRTC would show. Read the command table's disassembly before the first
  run.

### 4. Kext: two framebuffers on one card

This is the structural change. `IOFramebuffer` is one display each, and
only one driver can match the `IOPCIDevice` in the `IOFramebuffer`
category.

- Split `RadeonNI` into a card object and a framebuffer object:
  - The card object matches the `IOPCIDevice`, does what `bringUp()` does
    up to detection (VBIOS, POST, microcode, mappings), owns the
    accelerator, and publishes one child nub per connected output.
  - The framebuffer class matches those nubs, one instance per output,
    and keeps only per-display state (modes, current mode and depth,
    colour tables, cursor, its CRTC and its surface).
- Mac drivers with two heads normally get two device-tree nodes from the
  card's Open Firmware ROM. This card has no FCode, so the nubs have to
  be made by the kext. How `IOGraphicsFamily` and the window server treat
  framebuffers whose provider is not an `IOPCIDevice` is not known here:
  `getApertureRange`, `getVRAMRange`, the `IOFBDependentID` and
  `IOFBDependentIndex` properties (how Apple's drivers mark two heads of
  one card) and the AGP shim the accelerator hangs under all need a look
  in the IOGraphics sources (`third_party/ref/iographics`).
- Locking: both framebuffers and the accelerator call into one `hw/`
  card. AtomBIOS tables and the AUX channel are not re-entrant.
- Check: with one monitor, everything as today (Quartz Extreme, cursor,
  mode change). With two, `ioreg` shows two framebuffers and System
  Preferences shows two displays, even before the second one is
  accelerated.

### 5. Kext: a second scanout surface

- Reserve room for the largest mode of each display at the start of the
  aperture and give each framebuffer its own offset; the accelerator's
  allocator starts after both.
- `getVRAMRange` per framebuffer: Quartz Extreme wants a minimum of video
  memory behind each display (`docs/QUARTZ-EXTREME.md`, gate 3). Decide
  what each reports.
- Check: unaccelerated extended desktop works: both pictures right, the
  pointer crosses from one to the other, windows can be dragged across,
  each display changes mode on its own.

### 6. Cursor per display

- `rdn_cursor.c` takes a CRTC; each framebuffer has its own cursor image
  in video memory. The window server shows the cursor on the display the
  pointer is on and hides it on the other.
- Check: `~/gl/curmove` on each display.

### 7. Acceleration on both displays

- The accelerator's "screen" becomes "screen of display n": the user
  client's screen information, `rdn_blit()`'s destination, the surface's
  `setShape` (its second argument is a framebuffer index that has always
  been 0).
- A window that straddles both displays: find out what the window server
  asks for (one surface with two shapes, or one per display) by logging,
  as Quartz Extreme was learned.
- The 2D plug-in (`ga/`) is instantiated per framebuffer; check that it
  holds no global state.
- The GL bundle's pixel formats carry a display mask; today it is one
  bit. Full-screen contexts must go to the right display.
- Whether Quartz Extreme needs every display to qualify or each on its
  own is listed as unknown in `docs/QUARTZ-EXTREME.md`.
- Check: `~/gl/qe` reports both displays; Exposé on both; a GL window
  dragged from one to the other keeps drawing; a full-screen game on
  either.

### 8. Arrangements that are not two-of-two

- One monitor only, on either connector: must behave as today.
- A monitor plugged in or removed while running: there is no hot-plug
  handling at all (the DisplayPort link is trained only at a mode set).
  Either do it here (hot-plug interrupt or polling, `IOFramebuffer`'s
  connect-change notification) or state that the set of displays is fixed
  at boot.
- Mirroring from System Preferences: the window server can mirror in
  software across two framebuffers; see what it asks of the driver.
- Sleep and wake of each display.

### 9. Package and documents

- `g5/install.sh` and the personality if matching changes; `README`,
  `CLAUDE.md`, `docs/PLAN.md`, `docs/QUARTZ-EXTREME.md`,
  `docs/GLD-INTERFACE.md`.

## How it can be tested

- The card is in the G5. Two monitors there, one per connector, are
  needed from step 4 on (the user's hardware).
- Under QEMU the card is not attached now. If it returns to the host, the
  emulated display plus the card's is already a two-display desktop in
  the guest, which exercises the window server's side of step 7 but not
  two heads of one card.
- No reference trace has two displays. A new Linux trace with both
  monitors attached would give steps 1 to 3 the same check the
  one-display mode set has; without it they rest on the Linux sources, as
  DisplayPort did.
- Steps 4 and 5 change how the kext attaches. A kext that fails to start
  leaves the Mac without a picture but reachable over ssh; a panic at
  boot needs someone at the machine (Safe Boot). Keep the previous kext
  on the G5 at every step.

## Order and size

Steps 1 to 3 are mechanical and checked on the host. Step 4 is the one
with unknowns and decides whether the rest is straightforward; a small
experiment (two nubs, two bare framebuffers, no acceleration) should come
before committing to the design. Steps 5 and 6 follow from it. Step 7 is
the largest and the most exploratory. A sensible first milestone is step
5's check: an unaccelerated extended desktop.

## Decisions for the user before starting

1. Is an unaccelerated second display acceptable as an intermediate
   state, and is Quartz Extreme on only one of two displays acceptable if
   the window server allows it?
2. Fixed at boot, or hot-plug (step 8)?
3. A Linux trace with two monitors first (card back in the host), or the
   Linux sources only?
