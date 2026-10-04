#!/usr/bin/env python3
"""Reduce a QEMU vfio trace to compact per-phase register logs.

Usage: scripts/trace-split.py traces/<name>.log [--device 0000:10:00.0]

Input lines are QEMU `log` backend trace events (vfio_region_read/write,
vfio_pci_read/write_config) with -msg timestamp=on. Phases are delimited by
config writes of 0xa0..0xaf to the Interrupt Line register (0x3c), as done by
scripts/x86-trace-guest.sh and, later, by our own tools.

Output, next to the input:
  <name>.phase-<xx>.txt   one line per access, aperture (region 0) dropped:
                            R|W  mmio|io|cfg  offset  value  size
  and a summary on stdout.

The compact format has no timestamps, so two runs can be compared with diff.
"""

import collections
import re
import sys

REGION = re.compile(
    r"vfio_region_(read|write)\s+\((?P<dev>[0-9a-f:.]+):region(?P<idx>\d+)\+0x(?P<off>[0-9a-f]+), "
    r"(?:0x(?P<wval>[0-9a-f]+), (?P<wsize>\d+)\)|(?P<rsize>\d+)\) = 0x(?P<rval>[0-9a-f]+))"
)
CFG_W = re.compile(
    r"vfio_pci_write_config\s+\((?P<dev>[0-9a-f:.]+), @0x(?P<off>[0-9a-f]+), 0x(?P<val>[0-9a-f]+), len=0x(?P<len>[0-9a-f]+)\)"
)
CFG_R = re.compile(
    r"vfio_pci_read_config\s+\((?P<dev>[0-9a-f:.]+), @0x(?P<off>[0-9a-f]+), len=0x(?P<len>[0-9a-f]+)\) 0x(?P<val>[0-9a-f]+)"
)

KIND = {2: "mmio", 4: "io"}


def main():
    args = sys.argv[1:]
    device = "0000:10:00.0"
    if "--device" in args:
        i = args.index("--device")
        device = args[i + 1]
        del args[i:i + 2]
    if len(args) != 1:
        sys.exit(__doc__)
    path = args[0]
    base = path[:-4] if path.endswith(".log") else path

    phase = "pre"
    out = open(f"{base}.phase-{phase}.txt", "w")
    stats = collections.OrderedDict()

    def count(key):
        stats.setdefault(phase, collections.Counter())[key] += 1

    with open(path, errors="replace") as f:
        for line in f:
            if device not in line:
                continue
            m = REGION.search(line)
            if m:
                idx = int(m["idx"])
                rw = "W" if m[1] == "write" else "R"
                if idx == 0:
                    count(f"{rw} aperture")
                    continue
                kind = KIND.get(idx, f"region{idx}")
                val = m["wval"] if rw == "W" else m["rval"]
                size = m["wsize"] if rw == "W" else m["rsize"]
                out.write(f"{rw} {kind} {int(m['off'], 16):05x} {int(val, 16):08x} {size}\n")
                count(f"{rw} {kind}")
                continue
            m = CFG_W.search(line)
            if m:
                off, val = int(m["off"], 16), int(m["val"], 16)
                if off == 0x3c and 0xa0 <= val <= 0xaf:
                    out.close()
                    phase = f"{val:02x}"
                    out = open(f"{base}.phase-{phase}.txt", "w")
                    continue
                out.write(f"W cfg {off:05x} {val:08x} {int(m['len'], 16)}\n")
                count("W cfg")
                continue
            m = CFG_R.search(line)
            if m:
                out.write(f"R cfg {int(m['off'], 16):05x} {int(m['val'], 16):08x} {int(m['len'], 16)}\n")
                count("R cfg")
    out.close()

    for ph, c in stats.items():
        print(f"phase {ph}: " + ", ".join(f"{k}={v}" for k, v in sorted(c.items())))


if __name__ == "__main__":
    main()
