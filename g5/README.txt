RadeonNI: a graphics driver for the AMD Radeon HD 7570 (PCI ID 1002:675d)
on Mac OS X 10.4.11 PowerPC: a framebuffer, and with --accel OpenGL and
Quartz Extreme through Mesa's r600 driver.

STATUS: experimental. It has run on exactly one machine: a Power Mac G5
Late 2005 (PowerMac11,2) with one HD 7570 (a Dell card, subsystem
1028:2b20), one monitor on the DVI-I connector at 1920x1080. Any other
Mac, card or monitor is untested. The kext only loads for 1002:675d.

Contents
  RadeonNI.kext   the driver, built for PowerPC with Apple gcc 4.0.1
  RadeonNIGLDriver.bundle   the OpenGL driver (Mesa 26.2.4's r600 inside)
  RadeonNIGA.plugin         the 2D plug-in the window server asks for
  install.sh      installs them into /System/Library/Extensions
  uninstall.sh    removes them
  vbios.rom       the card's VBIOS image, if the package was built with it
                  (a fallback only, see below)
  TURKS_*.bin     the card's microcode, (C) Advanced Micro Devices, Inc.,
                  distributed unmodified under LICENSE.radeon
  LICENSE.radeon  the microcode's licence; keep it with those files

Install
  It needs Mac OS X 10.4.11 and nothing else: no developer tools.
  1. Until you know the driver works on your Mac, have another way in:
     turn on Remote Login (System Preferences, Sharing) so that you can
     reach the Mac over ssh if the screen stays dark.
  2. Copy this folder to the Mac and open Terminal in it.
  3. sudo sh ./install.sh --accel --hwcursor
     (plain "sudo sh ./install.sh" installs the framebuffer alone, without
     OpenGL, Quartz Extreme or the hardware cursor)
  4. Restart.

Open Firmware (optional, Power Mac G5 PowerMac11,2 with Open Firmware 5.2.7f1)
  sudo sh ./of-install.sh
  Adds a block to the nvramrc variable so that Open Firmware sets the card up
  before Mac OS X starts: the Apple logo shows on the Radeon, and Cmd-Opt-O-F
  (held from power-on, on a wired keyboard plugged in beforehand) gives the
  Open Firmware console on the Radeon. Other nvramrc text is kept; the old
  text is saved in /Library/RadeonNI/OpenFirmware. If the file cannot be
  loaded the Mac boots as before. sudo sh ./of-uninstall.sh removes it. A
  reset of the NVRAM removes the block; run the installer again. Without it
  the driver still works (it sets the card up itself, after a test pattern),
  and says so once, a minute after start-up; rdn_ofhook=0 in boot-args
  silences that.

The driver takes the VBIOS from the card's own ROM; no file is needed. A
VBIOS image file is a fallback: given to install.sh, or present in this
folder as vbios.rom, it is kept inside the driver and used only if the ROM
cannot be read. It must be the image of the card that is in the Mac. The
system log says where the VBIOS came from and, when there is a file too,
whether the two are the same:  grep RadeonNI /var/log/system.log
install.sh refuses to install without a file on a Mac whose Open Firmware
gave the card's ROM no address.

Acceleration
  --accel starts the card's 3D engine at boot, for OpenGL and Quartz
  Extreme, and installs RadeonNIGLDriver.bundle and RadeonNIGA.plugin
  from this folder. It needs TURKS_pfp.bin and TURKS_me.bin in this
  folder. --hwcursor (with --accel) uses the card's hardware cursor. To go
  back to the framebuffer alone: sudo sh ./install.sh, and restart.

What the driver does
  It initialises the card from cold (no x86 BIOS runs on a Mac), reads the
  monitor's EDID from the DVI-I connector, and offers the EDID's detailed
  timings at 256 colours, thousands and millions. With --accel, programs'
  OpenGL and the window server's compositing run on the card.

What it does not do
  DisplayPort, a second monitor, sleep and wake, display hot-plug, any card
  other than 1002:675d.

Check after restart
  grep RadeonNI /var/log/system.log

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

To make the driver leave the card's ROM alone and use the VBIOS image
install.sh was given (it does not start if there was none):
  rdn_rom=0          do not read the card's ROM

Mesa's work for each OpenGL call is done on the second processor (Mesa's
"glthread") for every program except the window server. To change that
for one program, name it on a line of the text file
  /Library/Application Support/RadeonNI/glthread
by the name the system has for it (as in Activity Monitor: Quake3,
Doom 3 Demo, WindowServer): the name alone turns it on, the name with a
minus before it (-Quake3) turns it off; * and -* stand for every program
the file does not name. RDN_GLTHREAD=1 or =0 in a program's environment
overrides the file.
