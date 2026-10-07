#!/usr/bin/env python3
"""Per-thread self and inclusive times from a Tiger `sample` call graph.

sample-profile.py file [-t N] [-n TOP] [-c sym]... [-u sym]...
  -t N    only thread number N (0 = first)
  -c sym  list the callers of sym
  -u sym  list what is under sym (direct children)

The file is what `sample <program> <seconds> 1 -file <file>` writes on the
Mac (tools/guest/d3saveprof.sh). Without -c and -u: for each thread, the
functions by the time spent in them and by the time spent under them.

Two things to know when reading it. A function that makes no stack frame
(a leaf, a C library stub) is shown as called by its caller's caller. And
time in code without a name is given to the last name before it: the
bundle's stubs for C library calls show up under the last function of its
text.

Copyright (c) 2026 kouta-kun and Claude
SPDX-License-Identifier: MIT
"""
import sys, re, collections

class Node:
    __slots__ = ('sym', 'cnt', 'kids', 'parent')
    def __init__(self, sym, cnt, parent):
        self.sym, self.cnt, self.kids, self.parent = sym, cnt, [], parent

def parse(path):
    threads = []
    stack = []
    for l in open(path, errors='replace').read().split('\n')[2:]:
        if l.startswith('Total number in stack'):
            break
        m = re.match(r'^( +)(\d+) (.*)$', l)
        if not m:
            continue
        ind, cnt, sym = len(m.group(1)), int(m.group(2)), m.group(3).strip()
        if ind == 4:
            n = Node(sym, cnt, None)
            threads.append(n)
            stack = [(ind, n)]
            continue
        while stack and stack[-1][0] >= ind:
            stack.pop()
        n = Node(sym, cnt, stack[-1][1])
        stack[-1][1].kids.append(n)
        stack.append((ind, n))
    return threads

def walk(n):
    yield n
    for k in n.kids:
        yield from walk(k)

def stats(root):
    self_, incl, callers, under = (collections.Counter(), collections.Counter(),
                                   collections.defaultdict(collections.Counter),
                                   collections.defaultdict(collections.Counter))
    for n in walk(root):
        s = n.cnt - sum(k.cnt for k in n.kids)
        self_[n.sym] += s
        # inclusive, once per stack
        p, seen = n.parent, False
        while p:
            if p.sym == n.sym:
                seen = True
                break
            p = p.parent
        if not seen:
            incl[n.sym] += n.cnt
        if n.parent:
            callers[n.sym][n.parent.sym] += n.cnt
            under[n.parent.sym][n.sym] += n.cnt
        if s:
            under[n.sym]['(self)'] += s
    return self_, incl, callers, under

args = sys.argv[1:]
path = args.pop(0)
only, top, cs, us = None, 40, [], []
while args:
    a = args.pop(0)
    if a == '-t': only = int(args.pop(0))
    elif a == '-n': top = int(args.pop(0))
    elif a == '-c': cs.append(args.pop(0))
    elif a == '-u': us.append(args.pop(0))
threads = parse(path)
for i, t in enumerate(threads):
    if only is not None and i != only:
        continue
    self_, incl, callers, under = stats(t)
    tot = t.cnt
    print('== thread %d %s: %d samples' % (i, t.sym, tot))
    if not cs and not us:
        print('-- self')
        for s, c in self_.most_common(top):
            print('  %6d %5.1f%%  %s' % (c, 100.0 * c / tot, s[:100]))
        print('-- inclusive')
        for s, c in incl.most_common(top):
            print('  %6d %5.1f%%  %s' % (c, 100.0 * c / tot, s[:100]))
    for s in cs:
        print('-- callers of %s (%d incl, %.1f%%)' % (s, incl[s], 100.0 * incl[s] / tot))
        for p, c in callers[s].most_common(top):
            print('  %6d  %s' % (c, p[:100]))
    for s in us:
        print('-- under %s (%d incl, %.1f%%)' % (s, incl[s], 100.0 * incl[s] / tot))
        for p, c in under[s].most_common(top):
            print('  %6d  %s' % (c, p[:100]))
