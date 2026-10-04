# The card

Everything we learn about this specific Radeon HD 7570. Facts only, each with
its source.

## Identity (2026-10-04)

| Item | Value | Source |
|---|---|---|
| PCI ID (VGA function) | `1002:675d`, Turks PRO, revision 00 | `lspci -nn` |
| PCI ID (HDMI audio function) | `1002:aa90` | `lspci -nn` |
| Subsystem | `1028:2b20` (Dell OEM); audio `1028:aa90` | `lspci -nnvv` |
| Family | `CHIP_TURKS`, Northern Islands, DCE5 | radeon dmesg: `TURKS 0x1002:0x675D 0x1028:0x2B20 0x00` |
| Memory type | **GDDR5**, 64Mx32 | VBIOS string; `MC_SEQ_MISC0` (0x2a00) = `0x500026a9`, top nibble 5 |
| VRAM size | 1024 MB | radeon dmesg; `CONFIG_MEMSIZE` (0x5428) |
| PCIe | Legacy Endpoint, capable of 5 GT/s x16; trained at 2.5 GT/s x4 on this host | `lspci -vv` |
| Reset methods | `bus` (VGA function); no FLR | sysfs `reset_method`, `DevCap FLReset-` |

## BARs (as assigned by the x86 host)

| BAR | Type | Size | Use |
|---|---|---|---|
| 0 | 64-bit, prefetchable | 256 MB | Framebuffer aperture |
| 2 | 64-bit, non-prefetchable | 128 KB | Registers |
| 4 | I/O | 256 ports | Legacy I/O register access |
| ROM | | 128 KB decoded | Expansion ROM (64 KB of content) |

Audio function: BAR0 64-bit, 16 KB.

## VBIOS

Dump: `private/vbios.rom` (git-ignored), read from the sysfs `rom` node.

| Item | Value |
|---|---|
| Size | 65536 bytes, one image, legacy x86 code type 0, last-image flag set |
| SHA-256 | `591e5d5d9b35d8c6cc3092f9404e14263ae2fa2b9079cf1dc753bde9139bf1fb` |
| MD5 | `9e1e08facfbedf0cad46866e949a7c49` |
| Signature / checksum | `55 AA`, image checksum 0 (valid) |
| ATOM header | offset `0x1b2`, magic `ATOM` |
| Part number | `113-C3340200-101` |
| Build date | 09/05/11 05:06 |
| Version string | `ATOMBIOSBK-ATI VER013.012.000.032.041591` |
| Board string | `TURKS ProL C33402 GDDR5 64Mx32` |
| PCIR | `1002:675d` |

There is no EFI (GOP) image and no FCode image in the ROM.

## Connectors (radeon dmesg)

| # | Connector | HPD | DDC registers | Encoders |
|---|---|---|---|---|
| 0 | DisplayPort (`DP-2`) | HPD4 | `0x6450`–`0x645c` | DFP1: `INTERNAL_UNIPHY2` |
| 1 | DVI-I (`DVI-I-1`) | HPD1 | `0x6460`–`0x646c` | DFP2: `INTERNAL_UNIPHY`; CRT1: `INTERNAL_KLDSCP_DAC1` |

## Behaviour observed

- The host firmware does not POST the card. With the iGPU as primary display
  and no EFI image in the ROM, Linux found it un-POSTed ("GPU not posted.
  posting now...") and brought it up through AtomBIOS `asic_init`.
- radeon logs "ACPI VFCT table present but broken" and then gets the VBIOS
  from another source (the PCI ROM).
- With GDDR5, `ni_mc_load_microcode()` loads `TURKS_mc.bin` when the memory
  sequencer is not running. After radeon's init `MC_SEQ_SUP_CNTL` (0x28c8)
  reads `0xb1800001` (run bit set).

## Monitor (attached 2026-10-04)

On the DVI-I connector through a DVI-to-HDMI adapter. EDID saved to
`private/monitor-edid.bin`: 256 bytes, checksums valid, manufacturer `XXX`,
name `AAA`, year 2023, digital input.

| Timing | Pixel clock | H total | V total | Sync offsets / widths | Refresh |
|---|---|---|---|---|---|
| 1366x768 (preferred) | 85.50 MHz | 1792 | 798 | H 70/143, V 3/3 | 59.79 Hz |
| 1920x1080 | 148.50 MHz | 2200 | 1125 | H 88/44, V 4/5 | 60.00 Hz |

## After ASIC_Init from cold (our code, 2026-10-04)

| Register | Value | Meaning |
|---|---|---|
| `CONFIG_MEMSIZE` (0x5428) | `0x00000400` | 1024, in megabytes |
| `MC_SEQ_MISC0` (0x2a00) | `0x500026a9` | GDDR5; reads 0 before `ASIC_Init` |
| `MC_SEQ_SUP_CNTL` (0x28c8) | `0x00000000` | Sequencer not started by microcode |
| `CRTC_CONTROL` x6 | `0x00400310` | All CRTCs disabled |

The memory behind the 256 MB aperture reads back correctly in this state,
with no MC microcode loaded. The I/O BAR is not required: `ASIC_Init` also
works with AtomBIOS indirect I/O routed through MMIO.

## Still unknown

- Whether the panel is physically 1366x768 or 1920x1080.
- Reference clock and default engine/memory clocks (FirmwareInfo table).
