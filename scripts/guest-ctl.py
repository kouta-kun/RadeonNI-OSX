#!/usr/bin/env python3
"""Drive the Tiger guest's screen, keyboard and pointer through QMP, so the
graphical installer can be operated without a VNC client.

  scripts/guest-ctl.py shot [file.png]       screenshot (default build/shot.png)
  scripts/guest-ctl.py click X Y [--double]  left click at screen pixel X,Y
  scripts/guest-ctl.py move X Y
  scripts/guest-ctl.py key KEY[+KEY...]      e.g. ret, tab, meta_l+q, shift+a
  scripts/guest-ctl.py type 'text'           US layout
  scripts/guest-ctl.py qmp '{"execute": ...}'  raw command
  scripts/guest-ctl.py size                  guest screen size

Pixel coordinates refer to the screenshot. Needs scripts/tiger.sh running.
"""

import json
import os
import socket
import struct
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOCK = os.path.join(ROOT, "build", "qemu-qmp.sock")
SHOT = os.path.join(ROOT, "build", "shot.png")

KEYMAP = {
    " ": "spc", "\n": "ret", "\t": "tab", "-": "minus", "=": "equal",
    "[": "bracket_left", "]": "bracket_right", ";": "semicolon",
    "'": "apostrophe", "`": "grave_accent", "\\": "backslash", ",": "comma",
    ".": "dot", "/": "slash",
}
SHIFTED = {
    "!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6", "&": "7",
    "*": "8", "(": "9", ")": "0", "_": "minus", "+": "equal",
    "{": "bracket_left", "}": "bracket_right", ":": "semicolon",
    '"': "apostrophe", "~": "grave_accent", "|": "backslash", "<": "comma",
    ">": "dot", "?": "slash",
}


class Qmp:
    def __init__(self):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.connect(SOCK)
        self.f = self.s.makefile("rw")
        self.f.readline()
        self.cmd("qmp_capabilities")

    def cmd(self, execute, **arguments):
        msg = {"execute": execute}
        if arguments:
            msg["arguments"] = arguments
        self.f.write(json.dumps(msg) + "\n")
        self.f.flush()
        while True:
            r = json.loads(self.f.readline())
            if "return" in r or "error" in r:
                if "error" in r:
                    sys.exit(f"QMP error: {r['error']}")
                return r["return"]

    def keys(self, names, hold_ms=60):
        ev = [{"type": "key", "data": {"down": True, "key": {"type": "qcode", "data": k}}} for k in names]
        self.cmd("input-send-event", events=ev)
        time.sleep(hold_ms / 1000)
        ev = [{"type": "key", "data": {"down": False, "key": {"type": "qcode", "data": k}}} for k in reversed(names)]
        self.cmd("input-send-event", events=ev)
        time.sleep(0.04)

    def move(self, x, y, w, h):
        ev = [{"type": "abs", "data": {"axis": "x", "value": int(x * 32767 / (w - 1))}},
              {"type": "abs", "data": {"axis": "y", "value": int(y * 32767 / (h - 1))}}]
        self.cmd("input-send-event", events=ev)
        time.sleep(0.15)

    def button(self, down):
        self.cmd("input-send-event", events=[{"type": "btn", "data": {"down": down, "button": "left"}}])
        time.sleep(0.08)


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    return struct.unpack(">II", head[16:24])


def shot(q, path=SHOT):
    q.cmd("screendump", filename=path, format="png")
    return path


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    q = Qmp()
    op = a[0]
    if op == "shot":
        print(shot(q, os.path.abspath(a[1]) if len(a) > 1 else SHOT))
    elif op == "size":
        print(*png_size(shot(q)))
    elif op in ("click", "move"):
        x, y = int(a[1]), int(a[2])
        w, h = png_size(shot(q))
        q.move(x, y, w, h)
        if op == "click":
            for _ in range(2 if "--double" in a else 1):
                q.button(True)
                q.button(False)
    elif op == "drag":
        # drag X0 Y0 X1 Y1 [steps [ms]]: press at the first point, move to
        # the second in steps, release. GUEST_SCREEN=WxH gives the screen's
        # size when there is no emulated display to take a screenshot of.
        x0, y0, x1, y1 = (int(v) for v in a[1:5])
        steps = int(a[5]) if len(a) > 5 else 20
        pause = (int(a[6]) if len(a) > 6 else 40) / 1000.0
        if os.environ.get("GUEST_SCREEN"):
            w, h = (int(v) for v in os.environ["GUEST_SCREEN"].split("x"))
        else:
            w, h = png_size(shot(q))
        q.move(x0, y0, w, h)
        time.sleep(0.2)
        q.button(True)
        time.sleep(0.2)
        for i in range(1, steps + 1):
            q.move(x0 + (x1 - x0) * i // steps, y0 + (y1 - y0) * i // steps, w, h)
            time.sleep(pause)
        time.sleep(0.2)
        q.button(False)
    elif op == "key":
        q.keys(a[1].split("+"))
    elif op == "type":
        for ch in a[1]:
            if ch in SHIFTED:
                q.keys(["shift", SHIFTED[ch]])
            elif ch.isupper():
                q.keys(["shift", ch.lower()])
            else:
                q.keys([KEYMAP.get(ch, ch)])
    elif op == "qmp":
        print(json.dumps(q.cmd(**json.loads(a[1])) if False else q.cmd(json.loads(a[1])["execute"], **json.loads(a[1]).get("arguments", {})), indent=1))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
