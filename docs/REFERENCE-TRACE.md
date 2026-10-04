# Reference trace

What the stock Linux `radeon` driver does to this card, from the un-POSTed
state through init and several modesets. This is the ground truth our code is
compared against. The raw files are in `traces/` (git-ignored); this document
describes them and records what they showed.

## How it was captured

`scripts/x86-trace-guest.sh ref-radeon-2`, 2026-10-04, with the card on
`vfio-pci` (`scripts/card-bind.sh vfio`).

- Throwaway x86 KVM guest: the host's own kernel (`6.18.50-2-lts`, radeon
  2.51.0) and an initramfs built from the host's `radeon` module and firmware.
- Card behind a `pci-bridge` in the guest, `x-no-mmap=on`, trace events
  `vfio_region_read`, `vfio_region_write`, `vfio_pci_read_config`,
  `vfio_pci_write_config`.
- Monitor on the DVI-I connector through a DVI-to-HDMI adapter, so the
  digital encoder path (`DFP2: INTERNAL_UNIPHY`) is the one exercised.
- The guest read the VBIOS through the passed-through ROM BAR; no `romfile`.

`scripts/trace-split.py traces/<name>.log` turns the raw log into one compact
file per phase with the framebuffer aperture dropped and no timestamps, so
that two runs can be compared with `diff`.

## Phases

The guest delimits phases by writing a marker byte to the Interrupt Line
config register (0x3c). `trace-split.py` splits on those.

| Phase | What happens | MMIO reads | MMIO writes | I/O port | Aperture writes |
|---|---|---|---|---|---|
| `pre` | SeaBIOS and kernel PCI enumeration (config space only) | 0 | 0 | 0 | 0 |
| `a1` | `insmod radeon`: POST check, `asic_init`, MC microcode, full driver init, fbcon modeset to 1366x768 | 35086 | 62931 | 18 R / 158 W | 6.67 M |
| `a2` | dmesg, `modetest -c` (connector probing, EDID over DDC) | 21316 | 12626 | 0 | 256 |
| `a3` | `modetest` modeset to 1920x1080@60 | 17711 | 8804 | 0 | 2.08 M |
| `a4` | `modetest` modeset to 1366x768@59.79 | 13897 | 8299 | 0 | 1.05 M |
| `a5` | fbdev blank | 1297 | 25 | 0 | 128 |
| `a6` | fbdev unblank | 53 | 317 | 0 | 0.53 M |

Phases `a3` and `a4` each write 241 distinct registers. They are the
smallest self-contained modeset references.

## Findings

- **A VFIO reset returns the card to the un-POSTed state.** The card had
  been fully initialised by the host's `radeon` before the run, yet the
  guest's first reads show all six `CRTC_CONTROL` registers disabled and
  `CONFIG_MEMSIZE` (0x5428) = 0, and radeon logs "GPU not posted. posting
  now...". The same happened on the next run, after the previous guest had
  initialised the card.
- **Linux loads the MC microcode on cold init.** Phase `a1` has exactly 6024
  writes to `MC_SEQ_SUP_PGM` (0x28cc), which is the 24096-byte `TURKS_mc.bin`
  written one word at a time, plus 736 writes to the `MC_SEQ_IO_DEBUG` pair
  (0x2a44/0x2a48). `MC_SEQ_SUP_CNTL` (0x28c8) is written `8, 0x10, 8, 4, 1`.
  The first microcode write is access number 2666 of the phase, after
  `asic_init`.
- **The I/O BAR is used.** 176 accesses to BAR4 at offsets 0x30/0x34
  (index/data), all in `a1`. Linux routes AtomBIOS indirect I/O through the
  I/O BAR when it exists and falls back to MMIO when it does not, so this is
  a choice to make for the Mac, not a requirement.
- **The first accesses of init** are the POST check: `CRTC_CONTROL` of the
  six CRTCs (0x6e70, 0x7a70, 0x10670, 0x11270, 0x11e70, 0x12a70) and then
  `CONFIG_MEMSIZE`.
- radeon's own framebuffer in the guest is 1366x768, 24-bit depth, pitch
  5632, at aperture offset 0x363000.

## Caveats

- This is a full accelerated driver. Phase `a1` includes GART setup, the
  command rings, UVD and power management, none of which we implement. The
  comparison that matters is the subset up to the end of `asic_init`, the
  memory controller setup, and the modeset phases.
- With every aperture access trapped, the guest takes about 90 s to load
  radeon and prints soft-lockup warnings while it clears the framebuffer.
  They are an artefact of tracing.
- QEMU printed "vfio: Error: Failed to enable MSI" once; radeon still
  reported its ring and IB tests as passing.

## Files

| File | Content |
|---|---|
| `traces/ref-radeon-2.log` | Raw QEMU trace, 929 MB |
| `traces/ref-radeon-2.serial` | Guest console, including radeon's dmesg |
| `traces/ref-radeon-2.noaperture.log` | Raw trace without region 0, with timestamps |
| `traces/ref-radeon-2.phase-*.txt` | Compact per-phase logs |
| `traces/host-radeon-dce-regs.txt` | Register readback (0x300–0x700, 0x5c00–0x5d00, 0x6000–0x7600) while the host's radeon drove 1366x768 |
| `private/monitor-edid.bin` | EDID of the attached monitor |
