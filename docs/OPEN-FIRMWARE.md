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

## Findings (2026-10-09, first night of `docs/OPEN-FIRMWARE-PLAN.md`)

Status: **M0 to M3 done and seen by the user** (M2: eight colour bars from the
client; M3: Open Firmware's console text and `ok` prompt on the 7570's monitor).
M4 (hand-over to Tiger) and M5 (automatic) not done. The journal entries of
2026-10-09 have the story; this is what is worth knowing.

### Facts about the G5's Open Firmware (5.2.7f1, PowerMac11,2)
- The telnet console works with the NIC named by its full path, on the second
  Ethernet port here: `" /ht@0,f2000000/pci@2/bcom5714@4,1:telnet,<ip>" io`
  after `dev /packages/telnet`. `enet:` is the first port. The console exists
  only while one connection stays open; it dies when stdout is switched away.
- The card is `/ht@0,f2000000/pci@5/pci1028,2b20@0` (bridge `pci@5`, bus 8).
  BARs as in Tiger: aperture 0x90000000 (256 MB), registers 0x80140000, ROM
  0x80120000. Memory decode is OFF at the prompt: set command bits 1 and 2
  (`6 080004 " config-w!" bus $call-method`) before any register read.
- Method calls: `" /ht@0,f2000000/pci@5" open-dev`, then `map-in` (stack:
  lo mid hi size), `config-l@`, `config-l!` with `$call-method`. From a client:
  the `call-method` service (args top of stack first).
- Open Firmware numbers are hexadecimal everywhere (1920 is 0x780).
- `load hd:3,\path` loads an ELF client at its link address and claims it for
  good (a second load there fails with "CLAIM failed"); `go` passes the client
  interface in r5 and the client must return with LR and SP restored.
- A client that never returns (my first `start.S` looped after `of_main`) looks
  exactly like a dead machine: no console, no ping, buffered output lost.
- The card's own ROM was not needed: the VBIOS is embedded into the client at
  build time (never committed). Reading the ROM BAR from Open Firmware was
  never shown to work or to fail (a wrong suspect).
- `fb8-install` plus `default-font set-font` give `draw-character`,
  `erase-screen`, `line#`, `column#`, `#lines`, `#columns`; the node must define
  `line-bytes`, `width`, `height` itself. 8 bpp only: the palette is the linear
  ramp the mode set loads, so set `255 to foreground-color`.
- `output` on a display node without `write` hangs Open Firmware. The terminal
  emulator package would not open from the node (reason unknown); the node has
  its own `write` (`of/display.fs`), no scrolling.
- Open Firmware ignored the user's USB keyboard on the 7570's console.

### The client (`of/`)
Freestanding C: `start.S`, `link.ld` (one load address per stage), `libc.c`,
`of_main.c` = client interface + `rdn_os` + front end, linking `hw/` unchanged.
Stages 0 (VBIOS checksum), 1 (ATOM parser), 99 (POST, EDID, mode set), `rdn8`
(same at 8 bpp for the text console). `make -C of` (needs `private/vbios.rom`),
`of/mockf.c` runs it against a mock Open Firmware under `qemu-ppc`. Images hold
the VBIOS and are never committed (`/of/build/` is ignored). Host driver:
`scripts/of-run.py` (`serve`, `do`; always end a session with `touch
build/of-run/q/stop`, which sends `reset-all`).

### Not done / next
Scrolling, cursor, the terminal emulator, a persistent node (re-created from the
console each time), M4 (kext skips POST when the client left the card running),
M5 (`boot-command`/`nvramrc`: a hang there would hang every boot until NVRAM is
reset). Open Firmware's `screen` alias and `output-device` were never changed.

### What an Apple-compatible card's FCode does (2026-10-09, read-only study)

`~/nv_oem_6600le_2149_pcie_full.rom` (user's file, a GeForce 6600 LE, 10de:0142,
one image of code type 1 = Open Firmware, FCode 67159 bytes) detokenized with
OpenBIOS `fcode-utils` built from source in `third_party/fcode-utils-src`
(GPL; used as a tool only, nothing copied; output in `private/rom-study/`).
Reading, not copying: Apple/NVIDIA firmware is not ours to reuse.
- It defines two helpers that set and clear bit 1 (memory decode) of the PCI
  command register (`my-space 4 + config-l@ 2 or / 2 invert and config-l!`) and
  uses the clearing one around its BAR sizing; i.e. the driver controls
  decode, which is what the hand-over needed (we clear decode and bus master
  at exit; Tiger's kernel enables what it needs again, `PCICommandAtStart` = 2).
- It creates a `display` node with `width`, `height`, `depth`, `linebytes`
  and `address` properties and the `fb8` family of text methods, like our
  `of/display.fs` (ours still lacks the `address`/`width`... properties for
  BootX; not needed for the kernel to boot).
- Not studied: its boot-time mode set, what it leaves for BootX's boot video.

### Prior art for persistent Open Firmware patches (2026-10-09, web research)

Sources: 68kMLA thread 48601 (page 3, joevt, posts 44 and 54, saved by the user
as `~/fwpatch3.html`), MacRumors "Booting a GUID disk on PowerPC Macs", the
NetBSD/macppc System Disk tutorial, Debian `nvsetenv(8)`, a TriLUG post quoting
Debian's install guide, and Apple's System Disk Utility itself.
- **Open Firmware patches are persisted in `nvramrc`.** joevt's patch for
  5.2.7f1 (USB in the boot picker) is live Forth that redirects compiled
  Apple code with `brpatch`; it is installed from Mac OS X with `sudo nvram
  nvramrc="$(tr '\n' '\r' << DONE ... DONE)"` plus `use-nvramrc?` true (and
  `fcode-debug?` true, which he says that patch needs). From the Open Firmware
  prompt the text is built with `encode-bytes` and `" nvramrc" $setenv`
  because the line buffer is short. NVRAM is 8 KB in total.
- Apple did the same for old machines: System Disk 2.3.1 holds the patch as
  Forth in an `OFpt` resource and "Save" writes the NVRAM; the copy
  downloaded from download.info.apple.com (`System_Disk_Utility.smi.bin`, 2000,
  compressed Disk Copy image, kept in `third_party/sysdisk-dl/`, git-ignored)
  contains the strings `use-nvramrc?`, `nvramrc`, `auto-boot?` and the message
  "No Open Firmware NVRAM partition found ... Cmd-Option-P-R", i.e. it edits
  the NVRAM variables. Not unpacked further.
- Nobody in these sources patches the Mac's flash ROM for this. The `tbxi`
  boot script (an on-disk file) runs after the boot device is chosen.
- Consequence: a hook in `nvramrc` is the established way; the NVRAM reset
  removes it, and a Tiger-side installer re-adds it.

## How the hook works (2026-10-09; what runs when, and why each piece is there)

Everything below was measured on the PowerMac11,2 (Open Firmware 5.2.7f1). The
code is `scripts/of-hook.py` (the nvramrc text), `of/` (the clients) and
`hw/rdn_post.c` (`rdn_handover_*`) plus the kext (`HandOver` property).

**Ingredients.** (1) An `nvramrc` script; (2) two files on the boot volume,
`rdnc.elf` (hand-over client: POST, 1920x1080, marker, decode off, then boots)
and `rdnk.elf` (console client: 8 bpp mode, display node, output and input);
(3) the kext, which looks for the marker.

**Power-on, step by step**
1. Open Firmware starts, probes the devices (`probe-all`: the PCI bus, the SATA
   controller, USB, Ethernet; the USB keyboard is enumerated here, so it must be
   plugged in *before* power-on) and then evaluates `nvramrc` if `use-nvramrc?`
   is true. At this moment the disk **cannot be opened yet** (measured: even the
   whole disk fails; waiting does not help because Open Firmware is single
   threaded), nor can the Ethernet console. So `nvramrc` can only change things
   in memory.
2. Our `nvramrc` does exactly that. It defines a few Forth words and then runs
   `rdn-patch`, which first checks that two cells in Open Firmware's own
   dictionary hold the values they have in 5.2.7f1 and only then overwrites them:
   - the body cell of **`mac-boot`** (at 0xFF852D00, normally 0xFF975D80, the real
     implementation) -> our word `rdn-b`;
   - the body cell of **`quit`** (at 0xFF852960, normally 0xFF86F0A0) -> our word
     `rdn-q`.
   The dictionary is writable RAM (joevt's `brpatch` patches do the same). Nothing is
   written to flash; a power cycle or NVRAM loss removes the hook completely.
3. Open Firmware then continues its normal start-up. Two paths diverge:
   - **Normal boot** (`auto-boot?` true, no key): `boot-command` is `mac-boot`, so
     our `rdn-b` runs. By now the disks are ready. It marks itself as having run
     (once per boot), polls the keyboard device for about 1.2 s
     (`" keyboard" open-dev`, `read`; an ordinary `key?` is not used because the
     input device is not yet the keyboard), and with no key loads
     `hd:,\...\rdnc.elf` (`hd:,` has no partition number, so a disk or partition
     change does not matter) and runs it with `go`. The client sets up the card
     (cold POST through the AtomBIOS interpreter, EDID, 1920x1080), draws the
     pattern, sets the marker (SCRATCH_REG7 = "OFRN"), gives back the memory it
     claimed, clears the PCI memory-decode and bus-master bits, and as its last
     act asks Open Firmware to run `mac-boot` (`interpret`). That reaches `rdn-b`
     a second time, which now runs the original `mac-boot` (BootX, the kernel).
     The kext finds the marker and a running card (`HandOver` = 1), skips its
     POST and takes over.
   - **A key held at boot** (Space) makes `rdn-b` run `rdnk.elf` instead: the
     client sets the 8 bpp mode, creates the display node `/rdn-display` through
     the `interpret` service (Forth text inside the client), makes it Open
     Firmware's output (`output`) and its input the USB keyboard (`input`), and
     calls the client `exit` service. `exit` ends the current command line and
     Open Firmware restarts its entry routine: banner, then `quit`, which is
     where the next case starts.
   - **Cmd-Opt-O-F**: Open Firmware does not autoboot (so `mac-boot` is not
     called); it prints its banner and enters its interpreter loop, `quit`. Our
     `rdn-q` replaces it: once per boot it runs `rdnk.elf` (as above), then
     continues to the original `quit` (0xFF86F0A0), so the prompt appears on the
     7570's monitor with the keyboard working.
4. Guards: `rdn-q?` and `rdn-b?` make each hook act once per boot (`quit` is
   re-entered after every error and after the client's `exit`; `mac-boot` is
   re-entered by the client's own chain).

**Why it is built like this**
- Hooking `nvramrc` directly is too early (no disk); `quit` and `mac-boot` run
  after the devices are ready. This was measured with self-clearing test lines
  that left breadcrumbs in an unused variable (`oem-banner`).
- A client may not simply return: under `go` there is no return address and
  Open Firmware aborts the command line, so the client ends with `exit` (console
  case) or with `interpret` of `mac-boot` (hand-over case).
- `load` reads the *whole rest of its line* as arguments, so it is wrapped in
  `evaluate`.
- The client must not release its own image (the memory is unmapped under it)
  but releases its heap and the loader's file buffer, otherwise BootX cannot
  claim what it needs.
- Tiger's boot hangs if the card is left decoding PCI memory (and bus
  mastering): this is why the hand-over client clears both bits at the end.
  The console mode keeps them on (needed to draw), so **booting from the
  console prompt is not safe yet** (see the next section).

## Why NVIDIA's FCode boots and ours (so far) does not (2026-10-09, FCode of a 6600 LE)

Read-only study of `~/nv_oem_6600le_2149_pcie_full.rom` (detokenized, 26,394
lines, 777 colon definitions, 216 `of` branches in 30 `case` blocks: it carries
its own script interpreter and the card's init data, much as our client carries
the AtomBIOS interpreter and the VBIOS):
- Its code is run **by Open Firmware itself at probe time**, because it lives in
  the card's ROM as an Open Firmware image. We have no such image (nothing is
  flashed), which is why we need the hook to run anything.
- Its display node implements Open Firmware's lifecycle: `open` and `close`
  with a use count, and `is-install` / `is-remove`. The *remove* routine is
  empty. The **`close` of the last instance clears the framebuffer and calls the
  routine that clears the PCI memory-decode bit and restores the card's BAR
  registers** (the same helper it uses around its BAR sizing). It never sets the
  bus-master bit in the helpers I found.
- Our hand-written node has an empty `close` and our client enables decode *and*
  bus master (command 6), so when Open Firmware tears the console down before
  starting Tiger, nothing turns the card's decode off, and Tiger's PCI setup then
  hangs. What worked for us (client clears bits 1 and 2 at the end) does by
  hand what NVIDIA's `close` does as part of the node.
- So the right fix is not a special `rdn-boot` word but a proper `close` method
  on our node (clear the screen, clear decode, keep bus master off, set the
  marker), plus enabling only the memory bit in the client. Whether Open
  Firmware really calls `close` on the console before `mac-boot` hands over is
  the one thing to verify (a breadcrumb written from `close`).
