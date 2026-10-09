# Open Firmware output: outline for the next session

Rough, for the session that starts with the G5 powered on. The reasoning and
the alternatives are in `docs/OPEN-FIRMWARE.md`; this file is the order of work.
Everything here is a plan and a set of leads, not results. Nothing is flashed;
no persistent NVRAM change without the user's yes (M5 only).

## Goal and how we know

Open Firmware shows its console on the 7570's monitor, Tiger boots with the
Apple logo on it, and the kext takes over from a card that is already POSTed.
Each milestone has a check the user or a log can confirm; do not call a
milestone done before the user sees it (CLAUDE.md, Verification).

## Ask the user at the start

1. Which cards are in the G5 and which slot? Is the GeForce 6600 LE available
   as a fallback console while the 7570 is also present, or is the 7570 alone?
2. A wired USB keyboard (Bluetooth keyboards do not work at the Open Firmware
   prompt) and a network cable. Which free IP address may Open Firmware use for
   its telnet console (not the G5's own address; `scripts/mac.sh g5 addr` finds
   that one)?
3. Are several restarts into Open Firmware and back to Tiger fine? Roughly how
   long may a session run?
4. Does the G5 boot to a black screen today until the desktop appears (what
   they see)? Any Apple logo at all?

## Session 1: M0 and M1 (no risk)

**Step 0, in Tiger, before any Open Firmware.** The device tree is readable
from the running system; write it down first (and commit the facts to
`docs/HARDWARE.md`, never raw dumps):

```
ioreg -p IODeviceTree -n pci1002,675d -w0 -l
ioreg -p IODeviceTree -w0 -l            (the bridge chain above the card)
nvram -p                                (boot-command, auto-boot?, input/output-device, nvramrc)
```

Record: the node's Open Firmware path, `reg`, `assigned-addresses` (BAR0
256 MB, BAR2 registers, the ROM at 0x80120000), `device_type`, `name`,
`AAPL,*` properties, the parent bridges' `ranges` and `bus-range`, the
`timebase-frequency` property of `/cpus`, and `IOPlatformFeatures`/power
features if shown. Save the nvram variables as the way back.

**Step 1, get an Open Firmware prompt with no display needed.** Restart,
hold Cmd-Opt-O-F. At the `0 >` prompt (blind, type carefully):

```
dev /packages/telnet        (ok means the package exists)
" enet:telnet,<ip>" io      (the space after the quote matters)
```

then `telnet <ip>` from the host. Fallback: the 6600 LE as the screen. Try
`printenv`, `devalias`, `show-devs`, `dev /` `ls`. Note which standard words exist
(`byte-load`, `load`, `go`, `interpret`, `fcode-debug?`, `fb8-install`,
`map-in`, `config-l@`).

**Step 2 (M0), look at the card's node:** `dev <path of the card>`,
`.properties`, `words`. Expect: no `device_type display`, no methods. Compare
with the Tiger-side dump from step 0.

**Step 3 (M1), registers from Open Firmware.** The PCI bus package maps a
child's BAR with `map-in` called in the bus node's context (a lead, not a
recipe): select the bridge or bus, push the BAR's physical address and size,
`map-in`, then read with `rl@` and swap (the card is little-endian: `lbflip`).
First reads only: the `rdnuc reg` ones (`0x0`, `0x8010`, `0x5420`); then one
harmless write and read back. If anything machine-checks, stop and go back.
Also try config reads (`config-l@`) of the card and the ROM BAR enable.

**Gate.** Record what worked in `docs/OPEN-FIRMWARE.md` ("Findings") and the
journal. Go to M2 only if registers read right from Open Firmware.

## Session 2 (maybe 3): M2, the client program

- New directory `of/`: a PowerPC ELF32 client built with the Linux PPC cross
  compiler the tests use (`PPC_CC` in the Makefile), linked at an address that
  does not collide with Open Firmware (use `claim` for load and heap), entry
  `_start` with the client interface pointer in r5.
- A small client-interface wrapper (`finddevice`, `getprop`, `call-method`,
  `claim`, `write`, `interpret`, `exit`) and **an `rdn_os` implementation for
  Open Firmware** (`hw/rdn_os.h` lists what it needs): MMIO through the mapped
  BAR with byte-reversed loads and stores and `eieio`/`sync`; no I/O BAR
  (`-n` behaviour); config space through the bus package; delays from the
  time base (`mftb`, frequency from `/cpus`); `alloc` from a claimed region;
  log to the Open Firmware console.
- The VBIOS from the ROM BAR (enable decoding first, as the kext's
  `biosFromRom` does; file fallback via `load` is possible).
- Then `rdn_post`, `rdn_edid`/`rdn_mode`, `rdn_modeset` at the EDID's preferred
  mode. No text yet. Check: CRTC registers read back; the monitor shows a mode
  with a solid colour fill (the user looks).
- Run it by hand from the prompt (`load hd:,\of\rdn.elf` `go`). Built and
  host-tested pieces only under `qemu-ppc` (the card is in the G5, not the host).

## Session 3 (maybe 4): M3, the display node

- From the client, `interpret` Forth text that creates the node (a name, `device_type`
  `display`, `width`, `height`, `depth`, `linebytes`, `address`/`frame-buffer-adr`),
  runs `fb8-install` (8 bit first; colours through the card's palette or a
  fixed 8-bit CLUT), and set `output-device`/`screen` to it.
- Check: Open Firmware text appears on the 7570's monitor; `ok` prompt visible.
- Unknowns to settle here: what the G5's Open Firmware requires of a display
  node, how `screen` and `/chosen` stdout are set from a running client, what
  BootX reads (`depth`, `linebytes`, `address`).

## Session 4: M4, hand-over to Tiger

- Boot Tiger from that state (`boot hd:...` from the prompt after the client).
  Check: the Apple logo and spinner on the 7570, then the desktop.
- Kext side: a "card already POSTed" test in `hw/` (as Linux's
  `radeon_card_posted()`), skip the POST, keep the framebuffer address, then the
  normal mode set. Without the Open Firmware client the kext still POSTs.
- Panic text and `-v` boot: check with `TIGER`-side boot args
  (`debug=0x100`) only with the user present.

## M5, automatic (needs the user's yes)

`boot-command` (or `nvramrc`) loading the client from the boot volume before
`boot`; keep the variables saved from step 0, and say how to undo: Cmd-Opt-P-R
at power on resets NVRAM without a display. Leave `auto-boot?` as it was.

## Rules for the whole thing

- A step that can hang the firmware runs from the prompt, never from NVRAM,
  until it is proven.
- No flashing, nothing written to the card's ROM.
- Mesa-style rule for the project: `hw/` stays freestanding and shared; the
  Open Firmware code is only an OS layer and a loader, no card logic of its own.
- Commit small, journal every experiment (negative ones too), keep this file and
  `docs/OPEN-FIRMWARE.md` current. Say "by readback" or "the user saw it".
- Say what runs on the G5 before long runs (`say-what-runs-on-the-g5`).

## Other open threads for a session with the G5 (pick with the user)

- Display and system sleep: `docs/POWER-MANAGEMENT.md` (register the
  framebuffer's power states; do not use Apple menu > Sleep until then).
- The black intro at 1920x1080 in Quake 3 and the `r_smp 1` end-of-map race
  (journal 2026-10-08).
- Hot-plug on DisplayPort and the other connector (`docs/HOTPLUG.md`).
