#!/usr/bin/env python3 -I
"""The Open Firmware hook for the Radeon HD 7570 (docs/OPEN-FIRMWARE.md).

Builds the nvramrc text that patches, in RAM, two words of the G5's Open
Firmware 5.2.7f1 (checked before patching):
  quit      -> at the first prompt entry of a boot: run the console client
               (8 bpp mode, display node, output on the 7570), then the prompt
  mac-boot  -> run the console client if a key is pressed, else the hand-over
               client (which sets the card up and boots Tiger itself)
Files are found with hd:,\\ (no partition number) under DIR.

  of-hook.py text [--oneshot] [--force-console] [--dir PATH]   print the text
  of-hook.py apply [same options]                              nvram write
  of-hook.py remove                                            clear nvramrc

--oneshot: the text first switches use-nvramrc? off and clears nvramrc, so it
runs once (for testing).  Without it the hook is permanent.

Copyright (c) 2026 kouta-kun and Claude
SPDX-License-Identifier: MIT
"""
import os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAC = os.path.join(ROOT, "scripts", "mac.sh")
BEGIN = '\\ RadeonNI-OF begin'   # a Forth comment: ends at the end of the line
END = '\\ RadeonNI-OF end'


def text(oneshot=False, force_console=False, dir=r"\Users\tiger\of"):
    # --force-console is for remote tests only: it makes the boot path behave as
    # if a key was held.  Without it the mac-boot hook always boots (with the logo).
    key = ": rdn-key? ( -- flag ) %s ; " % ("true" if force_console else "false")
    parts = []
    if oneshot:
        parts.append('" false" " use-nvramrc?" $setenv " " " nvramrc" $setenv ')
    parts += [
        '0 value rdn-q? 0 value rdn-b? ',
        ': rdn-con ( -- ) " dev / load hd:,%s\\rdnk.elf" evaluate go ; ' % dir,
        ': rdn-ho ( -- ) " dev /chosen" evaluate 0 0 " rdn-chain" property '
        '" dev / load hd:,%s\\rdnk.elf" evaluate go ; ' % dir,
        key,
        ': rdn-q ( -- ) rdn-q? 0= if true to rdn-q? rdn-con then ff86f0a0 execute ; ',
        ': rdn-b ( -- ) rdn-b? 0= if true to rdn-b? rdn-key? if true to rdn-q? rdn-con '
        'else rdn-ho then then ff975d80 execute ; ',
        ': rdn-patch ( -- ) ff852d00 l@ ff975d80 = ff852960 l@ ff86f0a0 = and if '
        '" rdn-q" $find drop ff852960 l! " rdn-b" $find drop ff852d00 l! then ; ',
        'rdn-patch',
    ]
    t = "".join(parts)
    assert "'" not in t
    return t


def sh(*a):
    r = subprocess.run([MAC, "g5", *a], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


if __name__ == "__main__":
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    opts = dict(oneshot="--oneshot" in a, force_console="--force-console" in a)
    if "--dir" in a:
        opts["dir"] = a[a.index("--dir") + 1]
    if a[0] == "text":
        print(text(**opts))
    elif a[0] == "apply":
        t = text(**opts)
        print(len(t), "characters")
        rc, o = sh("sudo", "nvram nvramrc='%s'; nvram 'use-nvramrc?'=true" % t)
        rc, o = sh("ssh", "nvram nvramrc use-nvramrc?")
        print("stored:", t in o)
    elif a[0] == "remove":
        print(sh("sudo", "nvram nvramrc=''; nvram 'use-nvramrc?'=false")[1])
    else:
        sys.exit(__doc__)
