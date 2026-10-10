RadeonNI Open Firmware support for Linux on the Power Mac G5
(AMD Radeon HD 7570, PCI ID 1002:675d)

STATUS: experimental. The client and the nvramrc block have run on one
machine, a PowerMac11,2, installed from Mac OS X. This Linux installer has
been tested against a mock of the NVRAM tool and a plain directory; it has
not yet been run on a real G5 under Linux.

What it gives you
  - Cmd-Opt-O-F at the chime (Win+Alt+O+F on a PC keyboard, held from power-on,
    keyboard plugged in before power-up): the Open Firmware "ok" prompt on the
    7570's monitor.
  - A normal boot: the card is POSTed and set to 1920x1080 before yaboot or
    GRUB starts, so the Linux radeon driver finds it already running.
  If the client file cannot be loaded or fails, the Mac boots as without it.

Contents
  rdnk.elf        the Open Firmware client (MIT; no VBIOS inside: it reads the
                  card's own ROM at run time)
  of-block.tmpl   the nvramrc text, with @OFDEV@ where the volume goes
  install.sh      puts rdnk.elf on the boot partition and the block in NVRAM
  uninstall.sh    takes both out again
  nvram-lib.sh    used by both

Needs
  - powerpc-utils (the "nvram" command), or nvsetenv; perl; blkid; mount.
  - An HFS or HFS+ partition that Open Firmware can read: the Apple_Bootstrap
    partition yaboot or GRUB booted from (Open Firmware cannot read ext4 or
    btrfs). It must be mounted writable: a journaled HFS+ volume mounts
    read-only on Linux, and the installer stops there.
  - The disk must have an Apple partition map and be the one Open Firmware's
    "hd" alias points to (the internal boot disk). Otherwise give --of-device.

Install
  sudo ./install.sh --partition /dev/sda2
  It shows what it will write and asks. Then restart.
  --print-only writes nothing and prints the text, to paste at the Open
  Firmware prompt by hand if the nvram command does not work on your system.

Undo
  sudo ./uninstall.sh
  If something goes wrong with NVRAM, Cmd-Opt-P-R at the chime resets it
  (blind: the screen shows nothing). That also removes the block.
  After a hang expect two power cycles.

Copyright (c) 2026 kouta-kun and Claude. MIT licence (LICENSE).
