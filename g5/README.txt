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
  TURKS_*.bin     the card's microcode, if the package was built with it

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
