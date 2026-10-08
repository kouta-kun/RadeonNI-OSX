#!/usr/bin/env python3
"""Drive piglit on the Power Mac G5 (or the Tiger guest).

  run.py stage [--mac g5]            send bin/, tests/ and the generated tests
  run.py start LABEL [--match RE] [--exclude RE] [--timeout S] [--limit N]
  run.py status LABEL                how far the run is
  run.py stop LABEL                  end it after the test in progress
  run.py fetch LABEL                 results and logs to build/piglit/LABEL/
  run.py summary LABEL [LABEL2]      counts per area; with two, the tests that
                                     differ

Inputs: build/piglit/selected.tsv (tools/piglit/select.py), the build in
build/piglit-darwin (scripts/build-piglit.sh). The run itself is tools/piglit/
run.pl on the Mac, started in the background so that an ssh drop does not end
it. Tests in tools/piglit/skip.txt (regular expressions on the name) are not run.

Copyright (c) 2026 kouta-kun and Claude
SPDX-License-Identifier: MIT
"""
import argparse
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(ROOT, "build", "piglit-darwin")
OUT = os.path.join(ROOT, "build", "piglit")
SRC = os.path.join(ROOT, "third_party", "piglit")
MAC = os.path.join(ROOT, "scripts", "mac.sh")
HERE = os.path.dirname(os.path.abspath(__file__))


def mac(args, cmd, **kw):
    return subprocess.run([MAC, args.mac] + cmd, **kw)


def tests():
    out = []
    skip = []
    p = os.path.join(HERE, "skip.txt")
    if os.path.exists(p):
        skip = [re.compile(l.strip()) for l in open(p)
                if l.strip() and not l.startswith("#")]
    for line in open(os.path.join(OUT, "selected.tsv")):
        name = line.split("\t", 1)[0]
        if not any(s.search(name) for s in skip):
            out.append(line)
    return out


def stage(args):
    sel = [l.split("\t", 1)[1] for l in open(os.path.join(OUT, "selected.tsv"))]
    gen = set()
    for c in sel:
        m = re.search(r"(generated_tests/\S+)", c)
        if m:
            gen.add(m.group(1))
    lst = os.path.join(OUT, "stage-generated.txt")
    open(lst, "w").write("\n".join(sorted(gen)) + "\n")
    # bin/ and generated tests from the build, tests/ from the source tree.
    for what, cmd in (
        ("bin", ["tar", "-C", BUILD, "-cf", "-", "bin", "lib"]),
        ("tests", ["tar", "-C", SRC, "-cf", "-", "--exclude=*.py", "tests"]),
        ("generated_tests", ["tar", "-C", BUILD, "-cf", "-", "-T", lst]),
    ):
        print("sending", what, flush=True)
        t = subprocess.Popen(cmd, stdout=subprocess.PIPE)
        r = mac(args, ["ssh", "mkdir -p ~/piglit && tar -C ~/piglit -xf -"], stdin=t.stdout)
        t.stdout.close()
        t.wait()
        if r.returncode or t.returncode:
            sys.exit("sending %s failed" % what)
    # Generated shader tests are listed relative to the build directory.
    r = mac(args, ["put", os.path.join(HERE, "run.pl"), "~/piglit/run.pl"])
    sys.exit(r.returncode)


def start(args):
    sel = tests()
    if args.match:
        sel = [l for l in sel if re.search(args.match, l.split("\t", 1)[0])]
    if args.exclude:
        sel = [l for l in sel if not re.search(args.exclude, l.split("\t", 1)[0])]
    if args.limit:
        sel = sel[:args.limit]
    d = os.path.join(OUT, args.label)
    os.makedirs(d, exist_ok=True)
    lst = os.path.join(d, "list.tsv")
    open(lst, "w").writelines(sel)
    mac(args, ["put", lst, "~/piglit/%s.list" % args.label], check=True)
    envs = " ".join(args.env)
    # The run stays in the foreground of the ssh session: on Tiger the tests
    # lose their window server connection when the session that started them
    # ends (CFMessagePort bootstrap_register failed, SIGABRT). Run this
    # command itself in the background.
    cmd = ("cd ~/piglit && rm -f out-%(l)s/stop out-%(l)s/finished && "
           "%(e)s perl run.pl %(l)s.list out-%(l)s %(t)d") % dict(
               l=args.label, t=args.timeout, e=envs)
    print(len(sel), "tests", flush=True)
    mac(args, ["ssh", cmd], check=True)


def status(args):
    d = os.path.join(OUT, args.label, "list.tsv")
    total = sum(1 for _ in open(d)) if os.path.exists(d) else 0
    r = mac(args, ["ssh", "cd ~/piglit; wc -l < out-%s/results; ls out-%s/finished 2>/dev/null; "
                   "tail -1 out-%s/results" % ((args.label,) * 3)], capture_output=True, text=True)
    print(r.stdout.strip(), "of", total)


def stop(args):
    mac(args, ["ssh", "touch ~/piglit/out-%s/stop" % args.label], check=True)


def fetch(args):
    d = os.path.join(OUT, args.label)
    os.makedirs(d, exist_ok=True)
    t = subprocess.run([MAC, args.mac, "ssh",
                        "cd ~/piglit/out-%s && tar -cf - results logs" % args.label],
                       capture_output=True, check=True)
    subprocess.run(["tar", "-C", d, "-xf", "-"], input=t.stdout, check=True)


def load(label):
    res = {}
    for line in open(os.path.join(OUT, label, "results")):
        p = line.rstrip("\n").split("\t")
        if len(p) >= 3:
            r = p[1]
            if r == "crash" and p[2] in ("127", "2"):
                # Not a crash of the driver: the binary was not built, or the
                # command's arguments lost their quoting on the way.
                f = os.path.join(OUT, label, "logs", re.sub(r"[^A-Za-z0-9_.-]", "_", p[0]) + ".txt")
                try:
                    t = open(f, errors="replace").read(2000)
                except OSError:
                    t = ""
                if "No such file" in t:
                    r = "notbuilt"
                elif "syntax error" in t:
                    r = "harness"
            res[p[0]] = (r, int(p[2]))
    return res


def area(name):
    p = name.split("@")
    if p[0] == "spec" and len(p) > 1:
        return p[0] + "@" + p[1]
    return p[0]


def summary(args):
    a = load(args.labels[0])
    if len(args.labels) == 1:
        c = collections.defaultdict(collections.Counter)
        for n, (r, _) in a.items():
            c[area(n)][r] += 1
            c["TOTAL"][r] += 1
        for k in sorted(c, key=lambda k: -sum(c[k].values())):
            print("%-45s %s" % (k, " ".join("%s=%d" % (r, v) for r, v in sorted(c[k].items()))))
        return
    b = load(args.labels[1])
    c = collections.defaultdict(list)
    for n in sorted(set(a) & set(b)):
        if a[n][0] != b[n][0]:
            c[area(n)].append((n, a[n][0], b[n][0]))
    for k in sorted(c, key=lambda k: -len(c[k])):
        print("%-45s %d differ, e.g. %s %s/%s" % (k, len(c[k]), c[k][0][0], c[k][0][1], c[k][0][2]))


ap = argparse.ArgumentParser()
ap.add_argument("--mac", default="g5")
sp = ap.add_subparsers(dest="cmd", required=True)
sp.add_parser("stage").set_defaults(f=stage)
s = sp.add_parser("start")
s.add_argument("label")
s.add_argument("--match")
s.add_argument("--exclude")
s.add_argument("--timeout", type=int, default=60)
s.add_argument("--limit", type=int)
s.add_argument("--env", action="append", default=[], help="NAME=value for the tests")
s.set_defaults(f=start)
for n, f in (("status", status), ("stop", stop), ("fetch", fetch)):
    s = sp.add_parser(n)
    s.add_argument("label")
    s.set_defaults(f=f)
s = sp.add_parser("summary")
s.add_argument("labels", nargs="+")
s.set_defaults(f=summary)
a = ap.parse_args()
a.f(a)
