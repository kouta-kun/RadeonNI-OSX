#!/usr/bin/env python3 -I
"""Print the Forth chunks of the console node, exactly as of/of_main.c defines
them (console_chunks[]), one chunk per output record separated by a NUL byte
(--json for a JSON list).  For testing them over the Open Firmware console.

Copyright (c) 2026 kouta-kun and Claude
SPDX-License-Identifier: MIT
"""
import json, os, re, sys

src = open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        "of", "of_main.c")).read()
a = src.index("console_chunks[] = {")
b = src.index("\n};", a)
body = src[a:b]
chunks = []
for part in re.split(r"/\*[^*]*\*/", body)[1:]:
    strs = re.findall(r'"((?:[^"\\]|\\.)*)"', part)
    if strs:
        chunks.append("".join(x.replace('\\"', '"') for x in strs))
# the client fills these in from the device tree; for tests here are this G5's values
DEFAULTS = {"P": "/ht@0,f2000000/pci@5/pci1028,2b20@0", "PP": "/ht@0,f2000000/pci@5",
            "AP": "90000000", "AH": "c3080010", "RP": "80140000", "RH": "83080018",
            "RS": "20000", "C4": "80004", "W": "780", "H": "438", "LB": "780"}
chunks = [re.sub(r"\{\{([A-Z0-9]+)\}\}", lambda m: DEFAULTS[m.group(1)], c) for c in chunks]
if "--json" in sys.argv:
    print(json.dumps(chunks))
else:
    sys.stdout.write("\0".join(chunks))
