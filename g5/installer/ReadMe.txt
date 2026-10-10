# RadeonNI
## What is installed
- RadeonNI.kext, the driver, in /System/Library/Extensions
- RadeonNIGLDriver.bundle, the OpenGL driver, and RadeonNIGA.plugin, the 2D plug-in the window server asks for, in /System/Library/Extensions
- The card's microcode (inside the kext's property list), copyright Advanced Micro Devices, Inc., under the licence in the License pane
The kext extension caches are rebuilt at the next start-up, so restart when the installer asks you to.
## Optional: Open Firmware support
The card has no Open Firmware code of its own, so without help the monitor stays dark until Mac OS X's driver starts it; the driver then shows a test pattern before the desktop.
The optional item "Open Firmware support" fixes that on a Power Mac G5 with Open Firmware 5.2.7f1:
- it installs a small program, rdnk.elf, in /Library/RadeonNI/OpenFirmware;
- it adds a block of text to the Open Firmware variable nvramrc (the text already there is kept and a copy is saved in the same folder) and turns on use-nvramrc?. This writes to your Mac's NVRAM;
- at every start-up the block runs the program, which sets the card up and puts the Apple logo on the Radeon's monitor, then starts Mac OS X as usual;
- holding Command-Option-O-F at the chime (wired keyboard, plugged in before power-up) gives the Open Firmware console on the Radeon's monitor.
It checks the Open Firmware version first and does nothing on any other. If the program cannot be loaded the Mac starts as if nothing was installed. Resetting the NVRAM (Command-Option-P-R) removes the block; run this installer again to put it back. The driver works without it.
## Undo
Open Terminal and run:
- sudo sh /Library/RadeonNI/uninstall.sh removes the driver
- sudo sh /Library/RadeonNI/of-uninstall.sh removes only the Open Firmware block and restores the old settings
If the screen stays dark, start the Mac with Shift held (Safe Boot does not load third-party drivers) or log in over ssh, then uninstall.
