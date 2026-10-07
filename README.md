# RadeonNI-OSX

RadeonNI-OSX is a project aiming to enable the usage of modern(-ish) cards on OS X 10.4 PowerPC machines. Specifically, the Radeon HD 7570 (Turks PRO-L, TeraScale 2) on the PowerMac G5.

This project is vibecoded, meatproxied and any other AI slur you can think of. I am not responsible if your G5 explodes, your HD 7570 breaks your display, or any other property or bodily harm arising from the usage of this project.

# Usage

## What you need

- A PowerPC Mac with a PCI Express slot and Mac OS X 10.4.11.
- A Radeon HD 7570. Currently hardcoded to PCI ID `1002:675d` and the DVI-I output.
- Recommended: enable ssh (Remote Login) in case installation fails..

## Install

1. Download and extract the zip on your mac, then enter the folder
2. Run the installer, with your password when asked:
   ```
   sudo sh ./install.sh --accel --hwcursor
   ```
  It should say `Found the Radeon HD 7570 (1002:675d); its ROM has an address.` If it instead refuses because Open Firmware gave the ROM no address, you need a VBIOS image of your own card: `sudo sh ./install.sh --accel --hwcursor /path/to/vbios.rom`. (TBA: how to extract)
3. Restart the Mac.
4. Check what the driver did:
   ```
   grep RadeonNI /var/log/system.log
   ```
   A good start has `VBIOS, 65536 bytes from the card's ROM`, a mode line for your monitor, `framebuffer started` and `3D engine up`.

## If the Mac does not come up

- Over ssh: `cd` into the folder, `sudo sh ./uninstall.sh`, `sudo reboot`.
- Hold Shift while it starts (Safe Boot does not load third-party drivers), then uninstall.
- `INSTALL.txt` in the zip lists the boot arguments that switch single features off (`rdn_gart=0`, `rdn_bootclocks=1`, `rdn_rom=0`, ...).

## Building the zip yourself

`scripts/make-dist.sh` builds everything and writes `build/RadeonNI-<date>-<commit>.zip`. It needs this repository's whole setup on a Linux host: a QEMU guest running OS X 10.4.11 with Xcode 2.5 (the kext and the 2D plug-in are built there) and the cross toolchain container (`scripts/darwin.sh image`) for Mesa.

# What it actually is

RadeonNI-OSX is a project that is composed of a couple of things:

1. A hardware interface for the Radeon HD 7570 (could be compatible with other Turks and TeraScale 2 cards after a bit of fiddling) that implements card initialization, power management, command processing, video memory handling, and output framebuffer + a .kext that consumes it and provides an IOFramebuffer and an IOAccelerator. Ported from Linux's radeon driver.
2. Patches for Mesa, a winsys that replaces Linux DRM and shims for Tiger compatibility that allow the r600 driver to run under OS X and communicate with the GPU using the aforementioned hardware interface.
3. An OpenGL Driver bundle that dispatches every OpenGL call to Mesa.
4. A 2D accelerator plugin that enables Quartz Extreme. Currently a CPU-only stub.

# Development stages

The project has been developed in a staged manner:

## Stage 1 (Research and display bringup)

Using an HD 7570 bridged through VFIO into a QEMU virtual machine, Claude Opus 5.5 traced a barebones initialization of this card from the Linux kernel and reproduced it to where it could perform basic tasks:

* Initialize/POST the card using AtomBIOS (currently VBIOS is provided through a file, reading VBIOS from the ROM is a future step)
* Retrieve the EDID of the connected display
* Initialize the card's display engine and draw a test image (rainbow color bars).

After this was possible from within emulated OS X, the next step was to develop a barebones IOFramebuffer that allowed for unaccelerated display output, and that worked perfectly.

## Stage 2 (Mesa port and acceleration)

Once display output was working, the next step was to ask Claude to port Mesa over to it. Why Mesa? It's MIT, uses well-isolated modules, and most importantly has the r600 driver with proven support for this card, which I'd already used under ArchPOWER on a big endian system.

Claude took the r600 driver from Mesa, patched it for Tiger compatibility, and developed two ends to this integration, a hardware interface that allows r600 to communicate with the GPU through PCIe (for command submission, etc) and a (currently in development) IOAccelerator implementation that passes through every OpenGL to Mesa. This worked, but did not support Quartz Extreme and essentially functioned by using OSMesa to render and then asking the CPU to copy over to the framebuffer. After some development Quartz Extreme seems to work correctly (no trails, OpenGL windowed and fullscreen works). A couple of games have been tested:

|   Game      |  State  |
| ----------- | ------- |
| Quake 3     | Working in full screen, non-responsive input in windowed mode |
| Sauerbraten | Working in full screen, glitchy lower half in windowed mode |
| Tux Racer   | Broken (window only updates when moved, has no full-screen mode) |
| Doom 3      | Working in full screen, performance about 2x 6600LE |

It is also now being tested on the G5 with no major issues. A test on a new monitor showed that the HDMI infoframes were not 100% accurate (which the other monitor was way more tolerant of). It should now work with most 1080p HDMI or DVI-D monitors. The output topology is hardcoded, so it's likely to only work on the DVI-I output of specificially the HD 7570.

## Stage 2.5 (Optimization)

Testing games like Quake 3 and Doom 3, performance seemed to be equivalent or sometimes worse than the original NVidia 6600LE which is ~10x worse in terms of raw power. After a bit of investigation, the firmware blob was initializing the card at very low core and memory clocks (100MHz/100MHz). Increasing this to expected levels (600MHz/850MHz) provided a small but noticeable improvement, however the big improvement of about 100% came when GART size was reported correctly (GART=0 made Mesa stop flushing after every draw command), and by flushing asynchronously on swapping buffers instead of waiting.

At this point performance was slightly below 6600LE on Doom 3, and about equivalent on Quake 3. Profiling by Claude revealed that most of the time was CPU bound within the driver. A couple of optimizations were developed (replacing Tiger's subpar emulation of Thread-local storage, force-enabling glthread to utilize both cores of the G5) which allowed us to reach and sometimes surpass the 6600LE.

After some investigation, it turned out the issue (specifically in Doom 3) was that a type-size mismatch between Mesa's expectations and OS X Tiger (bool is 4 bytes on OS X Tiger, Mesa expects 1 byte) caused the OpenGL Extension list to be wrong. As Doom 3 found none of the extensions it expected (ARB2 rendering path) it fell back to a nearly fixed-path rendering mode where the CPU took up more responsability. Once this was fixed, some shadowing and lighting issues were resolved but more importantly framerate rose from ~22FPS to 48-50FPS. This has no effect on Quake 3 which was already using all of the extensions it supported, but newer games (id Tech 4, etc) should see performance closer to a true CPU bottleneck.

## Future steps

- G5 testing worked, so the next step is to try to read the VBIOS from the ROM and get a compatibility list working.
- At some point, I should try with other cards of the same family/model to see if anything works or if this is too HD 7570 specific.
