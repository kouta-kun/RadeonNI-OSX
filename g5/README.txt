RadeonNI: unaccelerated framebuffer driver for the Radeon HD 7570
(PCI ID 1002:675d) on Mac OS X 10.4 PowerPC.

STATUS: this package has only ever run inside QEMU (Tiger 10.4.11 on an
emulated G4, with the real card passed through from a PC). It has never run
on a real Mac. Expect problems on first contact with real Open Firmware.

Contents
  RadeonNI.kext   the driver, built for PowerPC with Apple gcc 4.0.1
  RadeonNIGLDriver.bundle   the OpenGL driver (Mesa 26.2.4's r600 inside)
  RadeonNIGA.plugin         the 2D plug-in the window server asks for
  install.sh      installs them into /System/Library/Extensions
  uninstall.sh    removes it
  vbios.rom       the card's VBIOS image, if the package was built with it
  TURKS_*.bin     the card's microcode, (C) Advanced Micro Devices, Inc.,
                  distributed unmodified under LICENSE.radeon
  LICENSE.radeon  the microcode's licence; keep it with the two files

Install
  1. Keep the Mac's existing graphics card as the main display.
  2. Copy this folder to the Mac and open Terminal in it.
  3. sudo ./install.sh            (or: sudo ./install.sh /path/to/vbios.rom)
  4. Restart.

The VBIOS image is required because the driver does not read the card's own
ROM yet. It must be the image of the card that is in the Mac.

Acceleration (experimental)
  sudo ./install.sh --accel       (add --hwcursor for the hardware cursor)
  Restart. This starts the card's 3D engine at boot, for OpenGL and Quartz
  Extreme, and installs RadeonNIGLDriver.bundle and RadeonNIGA.plugin
  from this folder. It needs TURKS_pfp.bin and TURKS_me.bin in this
  folder. To go back: sudo ./install.sh, and restart.

What the driver does
  It initialises the card from cold (no x86 BIOS runs on a Mac), reads the
  monitor's EDID from the DVI-I connector, and offers the EDID's detailed
  timings at 256 colours, thousands and millions. There is no acceleration.

What it does not do
  DisplayPort, a second monitor, sleep and wake, display hot-plug, hardware
  cursor, any card other than 1002:675d.

Check after restart
  sudo dmesg | grep RadeonNI
  ioreg -p IODeviceTree -n pci1002,675d -w0 | grep -E "assigned-addresses|reg"

If the Mac does not boot with the driver installed
  Hold Shift at startup (Safe Boot skips third-party drivers) and run
  sudo ./uninstall.sh, or boot with Command-S and remove
  /System/Library/Extensions/RadeonNI.kext by hand (install.sh prints the
  commands), or take the card out.

With --accel the driver loads the memory controller's microcode
(TURKS_mc.bin) at start and raises the card's core voltage, engine clock
and memory clock from the slow state it boots in to the performance state
its own VBIOS lists. Boot arguments to change that, set with
sudo nvram boot-args="..." and a restart:
  rdn_bootclocks=1   keep the boot clocks and voltage
  rdn_mclk=0         raise the engine clock but not the memory clock
  rdn_mc=0           do not load the memory controller's microcode
                     (the memory clock then stays as it is)

The driver also lets the card read the computer's own memory (a "GART"):
programs keep there what they write all the time, and what no longer
fits in video memory goes there instead of stopping the program. At
start the driver has the card run a few commands from system memory and
leaves all this off if that fails. To leave it off yourself:
  rdn_gart=0         video memory only
If the desktop does not come up after an install, start the Mac with
that argument (in Open Firmware: setenv boot-args rdn_gart=0) and tell
us.

Mesa's work for each OpenGL call is done on the second processor (Mesa's
"glthread") for every program except the window server. To change that
for one program, name it on a line of the text file
  /Library/Application Support/RadeonNI/glthread
by the name the system has for it (as in Activity Monitor: Quake3,
Doom 3 Demo, WindowServer): the name alone turns it on, the name with a
minus before it (-Quake3) turns it off; * and -* stand for every program
the file does not name. RDN_GLTHREAD=1 or =0 in a program's environment
overrides the file.
