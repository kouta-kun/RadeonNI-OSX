# Display output from Open Firmware for the 7570: evaluation

Written 2026-10-08 (research and reasoning; nothing built or tried on the G5).

## What exists today

- The card's ROM is one legacy x86 image (PCIR code type 0, last-image flag
  set). **No FCode image and no EFI image** (`docs/HARDWARE.md`, checked again
  2026-10-08 by parsing `private/vbios.rom`).
- On the G5 Open Firmware enumerates the card, assigns its BARs and the 128 KB
  expansion ROM (`0x80120000`), and names the node `pci1002,675d`
  (`docs/RESEARCH.md`, section 4). Without FCode the node has no
  `device_type display`, no text methods, and nobody POSTs the card. So: **no
  Open Firmware console, no boot picker, no Apple logo or progress spinner on
  this card.** The screen stays dark until our kext loads. (Not re-confirmed
  with the user: what they see between power on and the desktop.)
- The kext cold-POSTs the card itself (AtomBIOS through `hw/`), which is why
  it works at all without firmware help.

## What "output from Open Firmware" needs

Open Firmware drives a screen through a device node with `device_type
"display"`, a set of properties (`width`, `height`, `depth`, `linebytes`,
`address`/`frame-buffer-adr`) and a text package (`fb8-install` fills in
`draw-character`, `erase-screen`, ... for an 8-bit framebuffer). BootX and the
kernel take their boot video (Apple logo, spinner, early console, panic text)
from the same node. Nothing talks to a card whose POST nobody did, so any
solution has to do three things before that node can exist:

1. Cold POST the card (AtomBIOS command tables; our `hw/rdn_post*`).
2. Read the EDID and set one mode (our `hw/rdn_mode*`, `rdn_modeset`).
3. Publish the framebuffer: the node, its properties, the text words.

## Routes

| Route | Verdict |
|---|---|
| A. Put an FCode image in the card's ROM (flash) | Out: "nothing is ever flashed" (CLAUDE.md), and it still needs FCode. |
| B. Hand-written FCode loaded from disk with `byte-load` | The loading mechanism is real and needs no flashing (MacRumors "Testing FCode ROMs before you flash": `load hd:...\ppc\x.rom`, `dev <node>`, `<addr> 1 byte-load`). But the work (POST through AtomBIOS tables) is thousands of lines of C today; FCode/Forth for that is not realistic. Good for tiny probes only. |
| C. A native PPC client program started from Open Firmware (like yaboot), built from our `hw/` library | **The route that fits the project.** `hw/` is freestanding C with one OS layer (`hw/rdn_os.h`); an Open Firmware implementation of that layer (MMIO through the PCI bus package's `map-in`, `mftb` for delays, `claim` for memory, log to the OF console) lets the same POST and modeset code run before any OS. The client then makes the display node with `interpret` (Forth text: node, properties, `fb8-install`) and returns to Open Firmware, which keeps running with the card as its screen. |
| D. Run the x86 VBIOS in Open Firmware | Out: no x86 emulation in Open Firmware. |
| E. An existing Mac FCode+NDRV ROM for Turks | None known: Apple's Mac Radeon ROMs for PowerPC stop at the HD 2000/3000/4000 and X-series; the Turks Mac ROMs are EFI (x86 Mac Pro). |

## Development loop without a screen on the card

The G5's Open Firmware can be driven over the network: hold Cmd-Opt-O-F at
boot, then `dev /packages/telnet` (an "ok" means it is there) and
`" enet:telnet,<ip>" io`, and `telnet <ip>` from the host (MacRumors, "How
does one telnet into Open Firmware?"; the first entry is blind). That gives an
Open Firmware prompt and the device tree without any display, and `load` /
`go` or `boot hd:,\file` from it. A second video card (the GeForce 6600 LE)
is the fallback screen for the prompt. Both are untried here.

## Milestones, cheapest first (none started)

- **M0 (no risk, one session):** boot to Open Firmware over telnet or with the
  6600 LE, `dev` to the card's node, `.properties`, `show-devs`; record
  `assigned-addresses`, `reg`, `ranges` of the bridge, what the G5's Open
  Firmware says about the node, and whether `fcode-debug?` and `byte-load`
  exist. Answers: where the BARs are, whether register reads work.
- **M1:** from the prompt, `map-in` the register BAR and read a few registers
  (the ones `rdnuc reg` reads, e.g. `0x8010`, `0x5420`); then a write that is
  harmless. Proves MMIO from Open Firmware.
- **M2:** `of/` client (ELF, PowerPC, our cross toolchain) with the Open
  Firmware OS layer: POST, EDID, mode set at 1920x1080 (or what the EDID says),
  no display node yet. Check with a grab (`rdnuc grab` works only inside
  Tiger; the monitor is the check, the user must look) or by reading CRTC
  registers back.
- **M3:** the display node with `interpret`; `output-device` set to it; text
  appears on the monitor; Open Firmware's own console works there.
- **M4:** hand-over: Tiger boots with the node in place, the boot logo and
  spinner show, and our kext notices the card is POSTed and skips its own POST
  (needs a "card posted" test in `hw/`, as Linux's `radeon_card_posted()`) and
  takes over at the same framebuffer address.
- **M5:** make it automatic (`boot-command` or `nvramrc` loading the client
  from the boot volume): a persistent NVRAM change, only with the user's
  yes, and with the way back written down (Cmd-Opt-P-R resets NVRAM blind).

Rough size: M0 and M1 one session; M2 one to two (mostly the OS layer and
making the code position independent for Open Firmware's memory map); M3 one to
two with unknowns (what the G5's Open Firmware wants from a `display` node;
`fb8-install` and the 32-bit pixel formats; BootX reads `depth`, `linebytes`);
M4 one to two. Several sessions in all.

## Risks and unknowns

- Nothing is flashed; a client that hangs in Open Firmware hangs the boot until
  NVRAM is reset (blind Cmd-Opt-P-R), so every step runs from a prompt
  first and `boot-command` stays untouched until it is proven.
- How the G5's Open Firmware maps 64-bit BARs and whether its `map-in` gives
  uncached, ordered access (our code uses explicit barriers and `lwbrx/stwbrx`
  equivalents already).
- Whether a second POST by the kext after an Open Firmware POST is harmless
  (it should skip it) and which of the two owns the framebuffer address.
- Open Firmware text is 8 bit indexed; Tiger's boot video wants what BootX
  reads from the node: the mode may have to be set once for Open Firmware and
  again when the kext loads (we already do that at every window server start).
- The G5's Open Firmware is closed source; the rest is from forum posts and
  the standard (IEEE 1275). Treat every command above as a lead.

## What it would buy

- Open Firmware prompt and boot picker (Option at boot) on the 7570.
- Apple logo and spinner during boot, verbose boot (`-v`) and **kernel panic
  text** on the 7570 (the panic of 2026-10-08 left nothing on screen to read;
  the log was read after a restart).
- A way to test card bring-up without Tiger running.
- Not needed for acceleration (phase 2) or for the current desktop.

## Sources

- MacRumors, [Testing FCode roms before you flash](https://forums.macrumors.com/threads/testing-fcode-roms-before-you-flash.2123070/)
  (the page returned 403 here; the commands above are from a search summary).
- MacRumors, [How does one telnet into Open Firmware?](https://forums.macrumors.com/threads/how-does-one-telnet-into-open-firmware.2300359/)
- MacRumors, [Just how open is Apple's Open Firmware (particularly on the G5)](https://forums.macrumors.com/threads/just-how-open-is-apples-open-firmware-particularly-on-the-g5.2455988/)
- `docs/RESEARCH.md` section 4 (no FCode, assigned-addresses, ROM at
  0x80120000), `docs/HARDWARE.md` (the ROM image).

## Findings (2026-10-09, first night of `docs/OPEN-FIRMWARE-PLAN.md`; by console transcript)

- Open Firmware 5.2.7f1 on the G5; telnet console works on the **second**
  Ethernet port when named by its full path (`/ht@0,f2000000/pci@2/bcom5714@4,1`),
  not through `enet`; the console exists only while one connection stays open.
- The card is `/ht@0,f2000000/pci@5/pci1028,2b20@0`; open the bridge
  (`open-dev`) and call `map-in`, `config-l@`, `config-l!` on it. Memory decode
  is **off** at the prompt (command 0x0004); with 0x0006 registers read as in
  Tiger (0x8010 = 0x3828) and scratch writes read back (M1, by readback).
- `load hd:3,\path` loads an ELF client; `go` passes the client interface in r5.
- M2 is not reached: the client hangs after reading the VBIOS (journal).
