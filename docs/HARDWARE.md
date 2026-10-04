# The card

Everything we learn about this specific Radeon HD 7570. Facts only; mark the
source of each one (lspci, VBIOS table, dmesg, register read).

## Status: not yet seen on the bus

As of 2026-10-04 the card does not enumerate on the host (see JOURNAL). Nothing
below is confirmed.

## To fill in during milestone 0

| Item | Value | Source |
|---|---|---|
| PCI ID (VGA function) | ? expected `1002:6759` or `1002:675d` | `lspci -nn` |
| PCI ID (HDMI audio function) | ? expected `1002:aa90` | `lspci -nn` |
| Subsystem ID / board vendor | ? | `lspci -nnvv` |
| Revision | ? | `lspci -nn` |
| Family | ? Turks (NI, DCE5) vs Redwood (Evergreen, DCE4) | PCI ID vs `drm_pciids.h` |
| Memory type | ? DDR3/GDDR3 vs GDDR5 | VBIOS + radeon dmesg + `MC_SEQ_MISC0` |
| VRAM size | ? | radeon dmesg, `CONFIG_MEMSIZE` |
| BAR0 (framebuffer aperture) | ? expected 64-bit prefetchable, 256 MB | `lspci -vv` |
| BAR2 (registers) | ? expected 64-bit, 128 KB | `lspci -vv` |
| BAR4 (I/O) | ? expected 256 ports | `lspci -vv` |
| Expansion ROM size | ? expected 128 KB | `lspci -vv` |
| VBIOS part number / date | ? | ATOM header strings |
| VBIOS SHA-256 / MD5 | ? | `sha256sum` of the dump |
| Connectors | ? | VBIOS object table, radeon dmesg |
| Reference clock, default engine/memory clock | ? | FirmwareInfo table |
| Monitor EDID (native mode) | ? | `/sys/class/drm/*/edid` |
