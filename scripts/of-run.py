#!/usr/bin/env python3 -I
"""Host side of the Open Firmware runbook (docs/OPEN-FIRMWARE-PLAN.md).

  of-run.py status            what the G5 says (uptime, boot-command)
  of-run.py restore           put boot-command back to its saved original
  of-run.py console [IP]      one-shot boot into an Open Firmware telnet
                              console, run the commands on stdin (one per
                              line), print the transcript, leave OF at ok
  of-run.py cmds              (library use) see OFConsole

Safety: boot-command is saved once to build/of-run/boot-command.orig and every
one-shot line begins by restoring it.  Every wait has a timeout.

Copyright (c) 2026 kouta-kun and Claude
SPDX-License-Identifier: MIT
"""
import os, re, socket, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "of-run")
MAC = os.environ.get("OF_MAC_SH", os.path.join(ROOT, "scripts", "mac.sh"))
NIC = os.environ.get("OF_NIC", "/ht@0,f2000000/pci@2/bcom5714@4,1")
OF_IP = os.environ.get("OF_IP", "192.168.1.140")
ORIG = os.path.join(OUT, "boot-command.orig")


def g5(*args, timeout=120):
    try:
        r = subprocess.run([MAC, "g5", *args], capture_output=True, text=True,
                           timeout=timeout)
    except subprocess.TimeoutExpired:
        return 124, "timeout"
    return r.returncode, r.stdout + r.stderr


def boot_command():
    rc, o = g5("ssh", "nvram boot-command")
    m = re.match(r"boot-command\s+(.*)", o.strip())
    return m.group(1).strip() if m else None


def save_original():
    os.makedirs(OUT, exist_ok=True)
    if os.path.exists(ORIG):
        return open(ORIG).read().strip()
    cur = boot_command()
    if cur is None or "telnet" in cur or not cur:
        raise SystemExit("refusing to save boot-command %r as the original" % cur)
    open(ORIG, "w").write(cur + "\n")
    return cur


def set_boot_command(line):
    # single-quote for sh; the Forth we use has no single quotes
    assert "'" not in line
    rc, o = g5("sudo", "nvram boot-command='%s'" % line)
    return rc == 0, o


def restore():
    orig = open(ORIG).read().strip()
    ok, o = set_boot_command(orig)
    now = boot_command()
    return now == orig, now


def wait_down(timeout=90):
    t = time.time()
    while time.time() - t < timeout:
        rc, _ = g5("ssh", "true", timeout=20)
        if rc != 0:
            return True
        time.sleep(3)
    return False


class OFConsole:
    """Plain-socket telnet to Open Firmware; expect 'ok ' prompts."""

    def __init__(self, ip=OF_IP, connect_timeout=90):
        self.log = b""
        t = time.time()
        while True:
            try:
                self.s = socket.create_connection((ip, 23), timeout=3)
                break
            except OSError:
                if time.time() - t > connect_timeout:
                    raise TimeoutError("no Open Firmware console at %s" % ip)
                time.sleep(2)
        self.s.settimeout(1)

    def _read(self, secs):
        end = time.time() + secs
        data = b""
        while time.time() < end:
            try:
                d = self.s.recv(4096)
            except socket.timeout:
                d = b""
            except OSError:
                break
            if d:
                data += self._iac(d)
        self.log += data
        return data

    def _iac(self, d):
        # drop telnet negotiation; refuse every option
        out = bytearray()
        i = 0
        while i < len(d):
            if d[i] == 255 and i + 2 < len(d) + 0 and d[i + 1] in (251, 252, 253, 254):
                verb = {251: 254, 253: 252}.get(d[i + 1])
                if verb:
                    try:
                        self.s.sendall(bytes([255, verb, d[i + 2]]))
                    except OSError:
                        pass
                i += 3
            else:
                out.append(d[i])
                i += 1
        return bytes(out)

    def run(self, line, timeout=20):
        """Send a line; return output up to the next 'ok' prompt."""
        self.s.sendall(line.encode() + b"\r")
        got = b""
        end = time.time() + timeout
        while time.time() < end:
            got += self._read(1)
            if got.rstrip().endswith(b"?") and b"More [" in got[-60:]:
                self.s.sendall(b"a")
                got += self._read(1)
            t = re.sub(rb"\x1b\[[0-9;]*[A-Za-z]|\[\d*[CK]", b"", got)
            if re.search(rb"\d+ > \s*$", t):
                break
        return re.sub(r"\x1b\[[0-9;]*[A-Za-z]|\[\d*[CK]", "", got.decode("latin-1"))

    def close(self):
        self.s.close()


def cmd_console(ip):
    orig = save_original()
    forth = ('" %s" " boot-command" $setenv dev /packages/telnet '
             '" %s:telnet,%s" io' % (orig, NIC, ip))
    ok, o = set_boot_command(forth)
    if not ok:
        raise SystemExit("could not set boot-command: " + o)
    print("boot-command set:", forth, file=sys.stderr)
    g5("sudo", "shutdown -r now", timeout=30)
    wait_down()
    try:
        c = OFConsole(ip, 150)
    except TimeoutError as e:
        print("FAIL:", e, file=sys.stderr)
        return 2
    transcript = []
    for line in sys.stdin:
        line = line.rstrip("\n")
        if line:
            out = c.run(line)
            print(">>", line)
            print(out)
    return 0


def serve(ip):
    """One-shot boot into the console, then keep the session: commands are
    files q/NNN.cmd (one Forth line per line); answers q/NNN.out.  The file
    q/stop ends the session (the G5 stays at ok)."""
    import glob
    q = os.path.join(OUT, "q")
    os.makedirs(q, exist_ok=True)
    for f in glob.glob(q + "/*"):
        os.remove(f)
    orig = save_original()
    forth = ('" %s" " boot-command" $setenv dev /packages/telnet '
             '" %s:telnet,%s" io' % (orig, NIC, ip))
    if not os.environ.get("OF_ATTACH"):
        ok, o = set_boot_command(forth)
        if not ok:
            raise SystemExit("set failed " + o)
        g5("sudo", "reboot", timeout=30)
        wait_down()
    c = OFConsole(ip, 180)
    c.run("", 10)
    open(q + "/ready", "w").write("1")
    idle = time.time()
    while not os.path.exists(q + "/stop"):
        for f in sorted(glob.glob(q + "/*.cmd")):
            out = ""
            for line in open(f).read().splitlines():
                out += ">> %s\n%s\n" % (line, c.run(line, 60))
            open(f[:-4] + ".tmp", "w").write(out)
            os.rename(f[:-4] + ".tmp", f[:-4] + ".out")
            os.remove(f)
        time.sleep(0.5)
    if not os.path.exists(q + "/keep"):
        c.s.sendall(b"reset-all\r")   # clean restart, boots Tiger; never strand the G5 at ok
        time.sleep(2)
    return 0


def capture(ip, steps, secs=150):
    """One-shot boot-command that opens the telnet console first and then runs
    `steps` (Forth, short); the host only listens.  Prints what Open Firmware
    says."""
    orig = save_original()
    forth = ('" %s" " boot-command" $setenv dev /packages/telnet '
             '" %s:telnet,%s" io %s' % (orig, NIC, ip, steps))
    print("boot-command (%d chars): %s" % (len(forth), forth), file=sys.stderr)
    ok, o = set_boot_command(forth)
    if not ok or boot_command() != forth:
        raise SystemExit("could not set boot-command")
    g5("sudo", "reboot", timeout=30)
    wait_down()
    try:
        c = OFConsole(ip, 150)
    except TimeoutError as e:
        print("FAIL:", e)
        return 2
    out = b""
    end = time.time() + secs
    while time.time() < end:
        d = c._read(2)
        if d:
            out += d
        else:
            try:
                c.s.send(b"")
            except OSError:
                break
    text = re.sub(r"\x1b\[[0-9;]*[A-Za-z]|\[\d*[CK]", "", out.decode("latin-1"))
    print(text)
    return 0


def capture_nvramrc(ip, steps, secs=150):
    """Like capture(), but the line is installed as nvramrc (use-nvramrc? true).
    Its first words switch use-nvramrc? off and clear nvramrc, so it runs once."""
    rc, o = g5("ssh", "nvram nvramrc use-nvramrc?")
    print("before:", o.strip().replace("\n", " | "), file=sys.stderr)
    forth = ('" false" " use-nvramrc?" $setenv " " " nvramrc" $setenv '
             'dev /packages/telnet " %s:telnet,%s" io %s' % (NIC, ip, steps))
    assert "'" not in forth
    print("nvramrc (%d chars): %s" % (len(forth), forth), file=sys.stderr)
    rc, o = g5("sudo", "nvram nvramrc='%s'; nvram 'use-nvramrc?'=true" % forth)
    rc, o = g5("ssh", "nvram nvramrc use-nvramrc?")
    if forth not in o:
        raise SystemExit("nvramrc not stored as given:\n" + o)
    g5("sudo", "reboot", timeout=30)
    wait_down()
    try:
        c = OFConsole(ip, 150)
    except TimeoutError as e:
        print("FAIL:", e)
        return 2
    out = b""
    end = time.time() + secs
    while time.time() < end:
        d = c._read(2)
        if d:
            out += d
    print(re.sub(r"\x1b\[[0-9;]*[A-Za-z]|\[\d*[CK]", "", out.decode("latin-1")))
    return 0


def do(text, timeout=300):
    q = os.path.join(OUT, "q")
    n = "%d" % int(time.time() * 1000)
    open(q + "/" + n + ".tmpc", "w").write(text + "\n")
    os.rename(q + "/" + n + ".tmpc", q + "/" + n + ".cmd")
    t = time.time()
    while time.time() - t < timeout:
        if os.path.exists(q + "/" + n + ".out"):
            return open(q + "/" + n + ".out").read()
        time.sleep(0.5)
    return "TIMEOUT"


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    a = sys.argv[1:] or ["status"]
    if a[0] == "status":
        print(g5("ssh", "uptime")[1].strip(), "| boot-command:", boot_command())
    elif a[0] == "restore":
        print(restore())
    elif a[0] == "serve":
        sys.exit(serve(a[1] if len(a) > 1 else OF_IP))
    elif a[0] == "capture-nvramrc":
        sys.exit(capture_nvramrc(OF_IP, " ".join(a[1:])))
    elif a[0] == "capture":
        sys.exit(capture(OF_IP, " ".join(a[1:])))
    elif a[0] == "do":
        print(do(" ".join(a[1:]) if len(a) > 1 else sys.stdin.read()))
    elif a[0] == "console":
        sys.exit(cmd_console(a[1] if len(a) > 1 else OF_IP))
    else:
        sys.exit(__doc__)
