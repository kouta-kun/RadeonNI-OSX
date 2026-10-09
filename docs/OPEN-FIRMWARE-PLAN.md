# Open Firmware output: unattended runbook

For a session that runs while the user sleeps. The reasoning and the
alternatives are in `docs/OPEN-FIRMWARE.md`. Everything here is a plan and a
set of leads, not results; each lead is verified by the first step that uses
it, and every step has a stated "if it fails" so that nobody has to be asked.
Nothing is flashed.

## What "unattended" means here, and what it cannot mean

Can be done without the user: everything that is a command over ssh from the
host, an Open Firmware session driven from the host over telnet, reading
registers back, building and testing code on the host, journaling, committing.

**Cannot be done without the user**, and is left for the morning:
1. **Seeing the monitor.** Only the user's eyes confirm that text or a picture
   is on screen (CLAUDE.md, Verification). The night's pass/fail is by register
   readback, logs and frame counters, and every milestone is reported as "by
   readback, not yet seen". The final parked state (below) makes the look in
   the morning one glance.
2. **Recovering from a hard hang.** If the G5 stops answering (a hang in
   Open Firmware, a machine check, Tiger not coming up) there is no remote power
   control, so it stays down until the user power-cycles it. The design makes
   that harmless: one power cycle brings back a normal boot (see "Safe by
   construction"), nothing persistent is left in a bad state, and the run
   stops instead of trying more.
3. **The session itself.** If the Claude Code session ends or hits its usage
   limit, the run stops where it is, in a safe state (invariants below).

## Before the user goes to sleep (the only human part, about five minutes)

1. Power the G5 on and let Tiger boot; leave it at the desktop (it may be
   logged out; the kext loads at boot). Do not use it afterwards.
2. In the first message of the session, give one explicit approval of the
   things below, so nothing needs asking later (the classifier and CLAUDE.md
   require the user's own words; a file cannot grant it):
   - restarts of the G5 over ssh (the plan uses at most 12, and states when);
   - setting `boot-command` (and, only if needed, `nvramrc`) from Tiger with
     `nvram`, one-shot, with the original saved and restored; `auto-boot?`,
     `boot-device`, `input-device` and `output-device` are never changed;
   - opening an Open Firmware console on the network from the host to the G5
     on an unused address of the G5's own subnet that the session picks and
     checks (no other host is touched);
   - writing to the card's registers from Open Firmware (including a PCIe
     secondary bus reset of its bridge); never its flash;
   - installing files under `/Users/tiger/of/` on the G5's boot volume only.
3. Nothing else is asked of the user until they wake up.

## Safe by construction (invariants held at every step)

- **One-shot boot-command.** The Forth line put in `boot-command` begins with
  `$setenv` restoring `boot-command` to its original value (saved from
  `nvram -p`), *before* anything that could hang. A power cycle after any hang
  therefore boots normally. `auto-boot?` stays true throughout.
- **The host restores too.** The host script restores `boot-command` from Tiger
  at the end of every step that returns to Tiger, and at exit (a trap), and
  verifies it with `nvram -p`.
- **Every wait has a timeout**, in the client (timebase polls; no loop without a
  limit; a global budget of 120 s per run, then it returns to Open Firmware)
  and on the host (the G5 must answer ssh within 8 minutes of a restart or the
  host stops; the Open Firmware console must answer within 90 s).
- **The card is put back before Tiger boots.** Any run that POSTed the card
  ends with a secondary bus reset of its bridge, so the card is cold again and
  the kext's own POST starts from the state it was written for. The one
  exception is M4, which is gated (below).
- **Stop, never loop.** A step that fails twice is skipped and the run goes on
  to steps that do not depend on it, or, if none is left, parks the G5
  (below) and writes the report. No retry storms, no more restarts than budgeted.
- **Never touched:** the user's apps and files, the card's flash, other
  `nvram` variables, other hosts, `killall`.

## The orchestrator (host side; written first, tested on the host)

`scripts/of-run.py` (Python 3, run with `-I`), driven by a small state file
(`build/of-run/state.json`) so a restart of the host script resumes:

- helpers over `scripts/mac.sh g5 ssh|sudo|put|get|wait`;
- `tiger_state()`: uptime, `nvram -p` snapshot, the Tiger-side device-tree dump;
- `set_one_shot(forth)`, `restore_boot_command()`, `restart_and_wait(timeout)`;
- `of_console(ip)`: connects to the telnet console (plain sockets, scripted
  expect: send a line, wait for `ok`, capture everything), with a hard timeout
  per command; a failed connect after 90 s is recorded and the run falls back
  to the Tiger-side results only;
- one result file per step in `build/of-run/` (raw transcript, parsed values),
  a summary `build/of-run/report.md`, and journal text assembled from them in
  the morning (the session writes the journal, nothing raw is committed:
  CLAUDE.md, no raw traces).
- Unit-tested on the host with a fake `mac.sh` and a fake console before the
  run (the G5 is not needed for that and can be done any time).

## The run

**Step 0: Tiger-side facts (no restart, no risk).** Save `nvram -p`; dump the
device tree: `ioreg -p IODeviceTree -n pci1002,675d -w0 -l` and the bridge chain
above it; the cards present (`ioreg -c IOPCIDevice`, `system_profiler
SPDisplaysDataType`); `/cpus` `timebase-frequency`; the boot volume's
`bless --info`, `nvram boot-device`, the partition number. Derive the Open
Firmware path of the card, the `hd:` path of `/Users/tiger/of/`, and an unused
address on the G5's subnet (`ping` and `arp` scan from the host: pick the
highest free one, record it). Failure: no OF path or no `hd:` partition found
→ stop with a report (nothing changed yet).

**Step 1: an Open Firmware console without a human (the first restart).** Set
the one-shot boot-command to:
restore itself; `dev /packages/telnet`; `" enet:telnet,<ip>" io`; (then OF waits
for the host to connect); the host connects within 90 s and the run is now in an
Open Firmware session at the `ok` prompt, driven from the host. Failure (no
connection, `/packages/telnet` missing): the console is not available, so the
run uses only Tiger-side restarts with a boot-command that does the work and
then `mac-boot`, and its results are read back from `nvram` variables (below);
if that fails too, the report says so and the run ends, parked at the desktop.
First thing in the session: `printenv`, `devalias`, `show-devs`, `dev /` `ls`,
the words available (`byte-load`, `load`, `go`, `interpret`, `fb8-install`,
`map-in`, `config-l@`, `$setenv`), the card's node `.properties`.

**Step 2 (M0 and M1): registers from Open Firmware.** In the console session:
`dev` to the card's parent bus, `map-in` the register BAR (from
`assigned-addresses`), read the `rdnuc reg` registers (`0x0`, `0x8010`,
`0x5420`, `CONFIG_MEMSIZE`), byte-swap (`lbflip`), compare with what Tiger
reads; then one harmless write and read-back. Pass: the values equal Tiger's. A
machine check or no answer: that is a hang; the host stops and waits for the
G5 within the timeout; if it does not return the run ends (the user power
cycles in the morning; the report says where). Gate: M2 only if this passes.

**Step 3 (M2): the client program.** Meanwhile on the host (this part does not
need the G5 and can be built and tested first, even the day before): `of/`, a
PowerPC ELF32 built with the Linux PPC cross compiler of the tests (`PPC_CC`);
client-interface wrapper; an `rdn_os` for Open Firmware (MMIO through the mapped
BAR, byte-reversed, `eieio/sync`; no I/O BAR; config through the bus package;
delays from the timebase; allocation from a `claim`ed region; log to the
console and to a ring in `nvram`); the VBIOS from the ROM BAR (decoding enabled
first); then the existing `rdn_post`, EDID and `rdn_modeset`. Tested under
`qemu-ppc` and under QEMU's OpenBIOS (its client interface runs ELF clients
too) up to the Open Firmware calls; the card itself only on the G5. Run on the
G5 from the console: `load hd:,\of\rdn.elf` `go`. Pass, by readback only: POST
done (`CONFIG_MEMSIZE` right, the engine registers sane), the mode set (CRTC
enabled, scanout address and pitch right, the **frame counter advancing**,
the transmitter and the monitor's hot-plug line reading connected). Always
ends with the secondary bus reset of the card's bridge unless the run is at a
step that keeps it (M3, M4). Failure: record the last log lines (also in
`nvram`), reset the bus, go on to the report.

**Step 4 (M3): the display node.** From the client: `interpret` the Forth that
creates the node and runs `fb8-install`, then `output-device` to it (the
variable only for this session, not saved), print text, draw a test pattern in
VRAM. Pass by readback: the node exists with the right properties, `fb8` words
work, the frame counter advances, VRAM holds the pattern (read back through
the aperture). The user's look is for the morning.

**Step 5 (M4): hand-over to Tiger. Gated.** Only if steps 2 to 4 all passed
without a single anomaly. Before it, the kext gets the "card already POSTed"
check (`hw/`, as Linux's `radeon_card_posted()`): the kext treats the card as
POSTed only if a magic value left by the client in a scratch register is
present **and** the card passes sanity reads (memory size, engine idle, CRTC
state); otherwise it behaves exactly as today. The kext is built in the guest
(`scripts/kext.sh build`, under QEMU: it cannot see the real card, so the logic
is tested with the hardware library's tests and a fake). Installing the new kext
on the G5 follows `g5/install.sh` practice, keeps the old one as
`~/RadeonNI.kext.before-posted` and needs one more restart. Risk: if Tiger does
not come up the user finds a dead boot in the morning; the old kext is on the
disk but not reachable remotely. If the gate is not clearly passed, **skip this
step** and say so. Pass, if run: Tiger boots, ssh answers, `dmesg | grep
RadeonNI` shows "already POSTed" and the desktop mode set, `qe` says Quartz
Extreme in use, a grab shows the desktop; then the kext's own POST is not
needed in that boot.

**Step 6: park and report.** The final state depends on how far the night
got, always safe:
- if M3 passed: the G5 is left **in Open Firmware at the `ok` prompt with its
  console on the 7570** (and the telnet console still open), the one-shot
  boot-command already restored, `auto-boot?` true. In the morning the user
  looks at the monitor: Open Firmware text means M3 is confirmed, and types
  `mac-boot` (or presses the power button twice: a normal boot) to go on;
- otherwise: the G5 is left booted in Tiger at the desktop with everything
  restored.
The session writes `build/of-run/report.md`, the journal entry (what ran, what
each step showed, by readback), updates `docs/OPEN-FIRMWARE.md` "Findings" and
commits. It does not mark any milestone done: that is the user's look.

## Budget (rough)

Step 0: minutes. Step 1: one restart. Steps 2 to 4: no restarts if the console
works (the G5 stays in Open Firmware); otherwise one restart per experiment, at
most 6. Step 5: up to three restarts (new kext, test, restore if needed). Total
at most 12 restarts, within about 6 hours of waiting; the host-side build and
tests of `of/` and the orchestrator are the real work and need no G5.

## Morning checklist for the user (two minutes)

1. Look at the monitor: Open Firmware text, or the desktop, or nothing.
2. If the G5 is down or stuck: power cycle it (the next boot is normal).
3. Read `build/of-run/report.md` and the journal entry; say what you saw.

## Rules for the whole thing

- `hw/` stays freestanding and shared; the Open Firmware code is only an OS
  layer, a client wrapper and a loader, with no card logic of its own.
- No flashing, nothing written to the card's ROM; no `boot-command` left
  modified; nothing from `nvram` besides `boot-command` (and a small log
  ring, removed at the end) is changed.
- Commit small, journal each experiment (negative too); say "by readback".

## Other open threads (not part of this run)

- Display and system sleep: `docs/POWER-MANAGEMENT.md` (register the
  framebuffer's power states; Apple menu > Sleep must not be used before).
- Quake 3's black intro at 1920x1080 and the `r_smp 1` end-of-map race.
- Hot-plug on DisplayPort and on the other connector (`docs/HOTPLUG.md`).
