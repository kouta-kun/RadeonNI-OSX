#!/usr/bin/env python3
"""Report whether the HD 7570 is POSTed, using the same test as Linux's
radeon_card_posted(): any CRTC enabled, or CONFIG_MEMSIZE non-zero.

Usage: sudo scripts/card-state.py [pci-address]

Read-only on the card's registers. Turns on memory decoding in the PCI
command register if it is off (the card may be driverless) and puts the
command register back afterwards. Exit status: 0 posted, 1 not posted.
"""

import mmap
import os
import struct
import sys

CRTC_CONTROL = (0x6e70, 0x7a70, 0x10670, 0x11270, 0x11e70, 0x12a70)
CRTC_MASTER_EN = 1 << 0
CONFIG_MEMSIZE = 0x5428
MC_SEQ_SUP_CNTL = 0x28c8
MC_SEQ_MISC0 = 0x2a00
PCI_COMMAND = 4
PCI_COMMAND_MEMORY = 2


def main():
    addr = sys.argv[1] if len(sys.argv) > 1 else "0000:10:00.0"
    dev = f"/sys/bus/pci/devices/{addr}"

    cfg = os.open(f"{dev}/config", os.O_RDWR)
    cmd = struct.unpack("<H", os.pread(cfg, 2, PCI_COMMAND))[0]
    if not cmd & PCI_COMMAND_MEMORY:
        os.pwrite(cfg, struct.pack("<H", cmd | PCI_COMMAND_MEMORY), PCI_COMMAND)

    try:
        fd = os.open(f"{dev}/resource2", os.O_RDONLY)
        m = mmap.mmap(fd, 0x20000, mmap.MAP_SHARED, mmap.PROT_READ)

        def rd(off):
            return struct.unpack_from("<I", m, off)[0]

        crtcs = [rd(r) for r in CRTC_CONTROL]
        memsize = rd(CONFIG_MEMSIZE)
        posted = any(c & CRTC_MASTER_EN for c in crtcs) or memsize != 0
        print("CRTC_CONTROL    " + " ".join(f"{c:08x}" for c in crtcs))
        print(f"CONFIG_MEMSIZE  {memsize:08x}")
        print(f"MC_SEQ_SUP_CNTL {rd(MC_SEQ_SUP_CNTL):08x}")
        print(f"MC_SEQ_MISC0    {rd(MC_SEQ_MISC0):08x}")
        print("posted" if posted else "NOT posted")
        m.close()
        os.close(fd)
    finally:
        os.pwrite(cfg, struct.pack("<H", cmd), PCI_COMMAND)
        os.close(cfg)
    sys.exit(0 if posted else 1)


if __name__ == "__main__":
    main()
