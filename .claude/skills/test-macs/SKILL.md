---
name: test-macs
description: How to reach and run things on this project's two test Macs, the Power Mac G5 with the Radeon in it and the Tiger guest under QEMU, with scripts/mac.sh. Use this before the first ssh, scp, sudo, file copy, screen grab, install, benchmark or log read on either machine, and whenever a task says "on the G5", "on the Mac", "in the guest", "run it on the real hardware", "check the kernel log", "install the bundle/kext" or "grab the screen". Do not look up the G5's address, build an ssh command line by hand, scan the network or write an ssh wrapper script; this skill replaces all of that, and the address in old notes may be wrong.
---

# The test Macs

Two machines run Mac OS X 10.4.11 for this project. One script reaches both,
and it is the only thing that needs to know where they are.

| | `g5` | `guest` |
|---|---|---|
| What | Power Mac G5 (PowerMac11,2), the Radeon HD 7570 is in it | Tiger under QEMU on this host (PowerMac3,1) |
| For | Anything that needs the card: OpenGL, Quartz Extreme, speed, the screen | Building the kext and tools (it has Xcode and the OpenGL headers); the card is not attached unless someone passes it through |
| Whose | The user's, with their monitor on it | Ours |

## The one command

```
scripts/mac.sh <g5|guest> ssh [command...]     one command, or a shell
scripts/mac.sh <g5|guest> sudo '<command>'     as root, through sh -c
scripts/mac.sh <g5|guest> put <local> <remote>
scripts/mac.sh <g5|guest> get <remote> <local>
scripts/mac.sh <g5|guest> grab <out.png> [scale]   the card's screen, as a PNG
scripts/mac.sh <g5|guest> wait [seconds]       until it answers again
scripts/mac.sh <g5|guest> addr                 where it is
```

It works from the main checkout and from any git worktree (the ssh key is
not in the repository; the script takes it from the main checkout). The
command's exit status is the remote command's.

The G5's address comes from the network and has changed before. The script
remembers the last one that worked and, when that stops answering, finds the
G5 again by itself and says so on stderr. So:

- Never write an IP address for the G5 into a command, a script or a note.
  If you need it (a VNC client, say), ask `scripts/mac.sh g5 addr`.
- If the script says the G5 does not answer anywhere, it is off, asleep or
  unplugged. That needs the user; say so instead of searching further.

The reason for all of this: every hand-built ssh line needs eight options for
Tiger's old OpenSSH, and sessions kept rediscovering them, and the address,
at the cost of many calls each time. One call to the script does it.

`scripts/tiger.sh ssh` still works for the guest; it calls this script.

## Using it well

Put a whole step into one call instead of one call per line. The remote
shell is bash:

```
scripts/mac.sh g5 ssh 'cd ~/gl && ./qe | tail -1 && ./fences | tail -1'
```

Quote the remote command with single quotes so that `$`, `~` and `*` are the
Mac's, not the host's. For anything longer, or with quotes of its own, send
a script on standard input:

```
scripts/mac.sh g5 ssh 'sh -s' < build/tmp/check.sh
scripts/mac.sh g5 ssh 'sh -s' <<'EOF'
cd ~/gl
for s in bench bench2; do ./d3save.sh $s | tail -1; done
EOF
```

`sudo` takes one string and needs no password handling from you:

```
scripts/mac.sh g5 sudo 'dmesg | grep RadeonNI | tail -20'
```

`put` and `get` copy one file; a remote path may start with `~/` and may
contain spaces. For a directory, pipe tar:

```
tar -C build -cf - RadeonNI-g5 | scripts/mac.sh g5 ssh 'tar -C ~ -xf -'
```

To see what is on the card's screen, grab it and Read the PNG. This is the
only way to look at the G5's display; say "by readback" when reporting what
you saw, because a still picture shows no motion and the user's eyes outrank
it.

```
scripts/mac.sh g5 grab build/tmp/screen.png 0.5
```

## Tiger is from 2007

Its command line tools are old. The ones that trip sessions up:

- `md5 -q file`, not `md5sum`. No `seq`, no `timeout`, no `readlink -f`.
- `sed -i` needs a suffix argument (`sed -i.bak ...`).
- `grep -a` for logs: the console and system logs hold bytes grep takes for
  binary.
- `ps -axc -o pid,command` gives bare program names; `ps -axww` the whole
  command line. Without `-c` or `-ww` the command is cut at 80 columns and a
  grep for a program's name finds nothing.
- `killall Name` uses the bare program name (`"Doom 3 Demo"`, `Quake3`).
- The G5's clock and time zone are not the host's. Compare times taken on
  the same machine.
- The G5 has gcc but no OpenGL headers. A test program that includes
  `<OpenGL/...>` or `<GLUT/...>` is built in the guest with
  `-isysroot /Developer/SDKs/MacOSX10.4u.sdk` and the binary copied over
  (`get` from the guest, `put` to the G5). A program that only needs IOKit
  builds on the G5 itself.

## The G5 is the user's machine

They sit in front of its monitor, or watch it over VNC. That shapes how to
work there:

- Before anything that takes over the screen or runs for more than a few
  seconds (a game, a benchmark, an install), say in a line what will run and
  for about how long. Games starting by themselves with no word is what they
  see otherwise.
- Ask before restarting the G5 or its window server. Both end their login
  session. Everything else here is yours to do.
- Leave it as you found it: quit what you started (`killall`), and keep the
  file you replace (the installed bundle, the kext) under a name that says
  what it is, and tell the user the name.
- A program started from ssh can open windows and go full screen, because
  the same user is logged in at the console.
