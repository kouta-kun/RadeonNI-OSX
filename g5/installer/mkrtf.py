#!/usr/bin/env python3
"""Turn a small text markup into RTF for Installer's Welcome/ReadMe/Conclusion.

"# text" is a title, "## text" a heading, "- text" a bullet, anything else a
paragraph. Usage: mkrtf.py in.txt out.rtf
Copyright (c) 2026 kouta-kun and Claude
SPDX-License-Identifier: MIT
"""
import sys


def esc(t):
    out = []
    for ch in t:
        if ch in "\\{}":
            out.append("\\" + ch)
        elif ord(ch) > 126:
            out.append("\\u%d?" % ord(ch))
        else:
            out.append(ch)
    return "".join(out)


def main(src, dst):
    body = []
    for line in open(src, encoding="utf-8").read().splitlines():
        if not line.strip():
            continue
        if line.startswith("## "):
            body.append("{\\pard\\sb200\\sa80\\b\\fs26 %s\\par}" % esc(line[3:]))
        elif line.startswith("# "):
            body.append("{\\pard\\sa160\\b\\fs34 %s\\par}" % esc(line[2:]))
        elif line.startswith("- "):
            body.append("{\\pard\\fi-280\\li360\\sa60\\fs22 \\u8226?\\tab %s\\par}" % esc(line[2:]))
        else:
            body.append("{\\pard\\sa120\\fs22 %s\\par}" % esc(line))
    rtf = ("{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss Lucida Grande;}}\n\\f0\\fs22\n"
           + "\n".join(body) + "\n}\n")
    open(dst, "w", encoding="ascii").write(rtf)


main(*sys.argv[1:])
