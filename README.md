# RadeonNI-OSX

RadeonNI-OSX is a project aiming to enable the usage of modern(-ish) cards on OS X 10.4 PowerPC machines. Specifically, the Radeon HD 7570 (Turks PRO-L, TeraScale 2) on the PowerMac G5.

This project is vibecoded, meatproxied and any other AI slur you can think of. I am not responsible if your G5 explodes, your HD 7570 breaks your display, or any other property or bodily harm arising from the usage of this project.

# Usage

## What you need

- A PowerPC Mac with a PCI Express slot and Mac OS X 10.4.11.
- A Radeon HD 7570. Currently hardcoded to PCI ID `1002:675d` and the DVI-I output. IMPORTANT: The Radeon HD must be inserted on an x8 slot. It hates the x16 slot on the Mac for some reason, it fails to initialize on Linux as well.
- Recommended: enable ssh (Remote Login) in case installation fails..

## Install

1. Download and extract the zip on your mac, then enter the folder
2. Run the installer, with your password when asked:
   ```
   sudo sh ./install.sh --accel --hwcursor
   ```
   It should say `Found the Radeon HD 7570 (1002:675d); its ROM has an address.` If it instead refuses because Open Firmware gave the ROM no address, you need a VBIOS image of your own card: `sudo sh ./install.sh --accel --hwcursor /path/to/vbios.rom`. (TBA: how to extract)

3. Optional, Power Mac G5 PowerMac11,2 only: `sudo sh ./of-install.sh` puts the card on Open Firmware's console and shows Apple's boot logo on the Radeon. It adds a block to the `nvramrc` NVRAM variable (the old text is saved; `sudo sh ./of-uninstall.sh` takes it out) and does nothing on other Boot ROMs. `INSTALL.txt` has the details.

4. Restart the Mac.

5. Check what the driver did:
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

1. A hardware interface for the Radeon HD 7570 (could be compatible with other Turks and TeraScale 2 cards after a bit of fiddling) that implements card initialization, power management, command processing, video memory handling, and output framebuffer + a .kext that consumes it and provides an IOFramebuffer and an IOAccelerator. Ported from Linux's radeon driver [1].
2. Patches for Mesa, a winsys that replaces Linux DRM and shims for Tiger compatibility that allow the r600 driver [2] to run under OS X and communicate with the GPU using the aforementioned hardware interface.
3. An OpenGL Driver bundle that dispatches every OpenGL call to Mesa.
4. A 2D accelerator plugin that enables Quartz Extreme. Currently a CPU-only stub.

# Development stages

The project has been developed in a staged manner:

## Stage 1 (Research and display bringup)

This was not the first attempt at Radeon HD cards on PowerPC Macs: an earlier effort to port osx86-driver-radeonhd to PowerPC Leopard [16] got as far as detecting the card but never reached a modeset.

Using an HD 7570 bridged through VFIO into a QEMU virtual machine [4][5][6][7], Claude Opus 5.5 traced a barebones initialization of this card from the Linux kernel [1] and reproduced it to where it could perform basic tasks:

* Initialize/POST the card using AtomBIOS (currently VBIOS is provided through a file, reading VBIOS from the ROM is a future step)
* Retrieve the EDID of the connected display
* Initialize the card's display engine and draw a test image (rainbow color bars).

After this was possible from within emulated OS X, the next step was to develop a barebones IOFramebuffer [3] that allowed for unaccelerated display output, and that worked perfectly.

## Stage 2 (Mesa port and acceleration)

Once display output was working, the next step was to ask Claude to port Mesa over to it. Why Mesa? It's MIT, uses well-isolated modules, and most importantly has the r600 driver [2] with proven support for this card, which I'd already used under ArchPOWER on a big endian system.

Claude took the r600 driver from Mesa, patched it for Tiger compatibility, and developed two ends to this integration, a hardware interface that allows r600 to communicate with the GPU through PCIe (for command submission, etc) and a (currently in development) IOAccelerator implementation that passes through every OpenGL to Mesa. This worked, but did not support Quartz Extreme and essentially functioned by using OSMesa to render and then asking the CPU to copy over to the framebuffer. After some development Quartz Extreme seems to work correctly (no trails, OpenGL windowed and fullscreen works). A couple of games have been tested:

|   Game      |  State  | Known Issues |
| ----------- | ------- | ------------ |
| Quake 3     | Working | Sometimes the intro video doesn't work |
| Sauerbraten | Working | |
| Tux Racer   | Working | |
| Doom 3      | Working, performance about 2x 6600LE | |
| World of Warcraft | Working, required custom extension implementation for high speed | |

It is also now being tested on the G5 with no major issues. A test on a new monitor showed that the HDMI infoframes were not 100% accurate (which the other monitor was way more tolerant of). It should now work with most 1080p HDMI or DVI-D monitors. The output topology is hardcoded, so it's likely to only work on the DVI-I output of specificially the HD 7570.

## Stage 2.5 (Optimization)

Testing games like Quake 3 and Doom 3, performance seemed to be equivalent or sometimes worse than the original NVidia 6600LE which is ~10x worse in terms of raw power. After a bit of investigation, the firmware blob was initializing the card at very low core and memory clocks (100MHz/100MHz). Increasing this to expected levels (600MHz/850MHz) provided a small but noticeable improvement, however the big improvement of about 100% came when GART size was reported correctly (GART=0 made Mesa stop flushing after every draw command), and by flushing asynchronously on swapping buffers instead of waiting.

At this point performance was slightly below 6600LE on Doom 3, and about equivalent on Quake 3. Profiling by Claude revealed that most of the time was CPU bound within the driver. A couple of optimizations were developed (replacing Tiger's subpar emulation of Thread-local storage, force-enabling glthread to utilize both cores of the G5) which allowed us to reach and sometimes surpass the 6600LE.

After some investigation, it turned out the issue (specifically in Doom 3) was that a type-size mismatch between Mesa's expectations and OS X Tiger (bool is 4 bytes on OS X Tiger, Mesa expects 1 byte) caused the OpenGL Extension list to be wrong. As Doom 3 found none of the extensions it expected (ARB2 rendering path) it fell back to a nearly fixed-path rendering mode where the CPU took up more responsability. Once this was fixed, some shadowing and lighting issues were resolved but more importantly framerate rose from ~22FPS to 48-50FPS. This has no effect on Quake 3 which was already using all of the extensions it supported, but newer games (id Tech 4, etc) should see performance closer to a true CPU bottleneck.

World of Warcraft is a special case, in that the original OpenGL renderer used fixed-pipeline extensions that were never or barely adopted outside the Apple ecosystem (ARB_vertex_blend [14]), so an implementation was cobbled together. Additionally it reuses parts of buffers multiple times, which Apple had a propietary extension for (GL_APPLE_flush_buffer_range [15]), also now implemented and gets the framerate from ~33 to ~110.

Call of Duty 2 also depended on a couple of Apple-specific extensions, with that + optimizations it runs at about 40 to 70FPS on the demo mission.

## Stage 3 (OpenFirmware integration)

Something we expected might be impossible is the integration of the GPU drivers into OpenFirmware [13], the PowerMac equivalent of UEFI/BIOS. The way that GPU initialization (normally) works is that the card has a ROM containing code to be executed by the computer's bootloader in order to initialize the display before the drivers come into play (i.e. for displaying at boot time and in case the driver is not available in the system). As you might know or expect, this code is bootloader specific. UEFI-compatible cards have a .efi executable, BIOS-compatible cards have a raw block of x86 instructions while OpenFirmware-compatible cards (as used in new-world Macs) store FCode [8][11], a byte-compiled expression of a Forth program that is architecture-independent for higher compatibility.

Cards of the era only came with either BIOS-compatible code or FCode, which meant that there were PC-compatible cards and Mac-compatible cards. Most cards could be flashed from one to the other, sometimes due to ROM chip sizes Mac-specific ROM would have to be modified to fit in PC cards [11][12]. Flashing this card was out of the question: first, there was no Mac rom for this card ever due to being released, and even if Claude could generate FCode for this card I don't have a programmer to re-flash it if something went wrong.

I would've thought this was the end of it, but OpenFirmware contains a variable called `nvramrc`, which allows the user to define a string of Forth commands [9] to be executed at boot time, before `boot-command` or Cmd+Opt+O+F are evaluated (`nvramrc` was actually used by Apple themselves for certain firmware updates [10]). `nvramrc` only stores about 8000 characters, which is not nearly enough for even a barebones driver. Claude was able to generate a PowerPC32 client program in C that initializes the card, declares it in the device tree and returns control to OpenFirmware, however `nvramrc` also runs so early in the boot process that the disk drives are not yet initialized, so there was nowhere to read this program from. The solution was to use `nvramrc` to modify the Forth words that normal boot and Cmd+Opt+O+F execute (`mac-boot` and `quit` respectively) so that they first execute the client and then their original code. The new installer now asks if you want to enable it, since messing with OpenFirmware *could* be dangerous (nothing that would survive a PRAM zap though). With this, we have access to the OpenFirmware console, and the hand-over to the OS X kext works without an issue.

## Future steps

- At some point, I should try with other cards of the same family/model to see if anything works or if this is too HD 7570 specific.

# Sources

[1] Linux `radeon` driver: https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/radeon  
[2] Mesa `r600` Gallium driver: https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/gallium/drivers/r600  
[3] Apple IOGraphics (`IOFramebuffer`): https://github.com/apple-oss-distributions/IOGraphics  
[4] QEMU VFIO: https://github.com/qemu/qemu/tree/master/hw/vfio  
[5] PCI passthrough to `qemu-system-ppc` (MacRumors): https://forums.macrumors.com/threads/qemu-system-ppc-vga-passthrough.2229861/  
[6] QEMU PowerMac (`mac99`) documentation: https://www.qemu.org/docs/master/system/ppc/powermac.html  
[7] Mac OS X PPC in QEMU: https://www.emaculation.com/doku.php/ppc-osx-on-qemu-for-osx  
[8] Testing FCode ROMs before you flash (MacRumors): https://forums.macrumors.com/threads/testing-fcode-roms-before-you-flash.2123070/  
[9] 68kMLA, "updated firmware for new world ppc32 macs that allows booting from usb from the boot picker" (Open Firmware patches persisted in `nvramrc`): https://68kmla.org/bb/goto/post?id=546149  
[10] NetBSD/macppc, System Disk tutorial (Apple's System Disk utility writing Open Firmware patches to NVRAM): https://ludd.ltu.se/~ragge/htdocs/Ports/macppc/SystemDisk-tutorial  
[11] Apple, Designing PCI Cards and Drivers for Power Macintosh (Open Firmware drivers as FCode in the card's ROM): https://developer.apple.com/library/archive/documentation/Hardware/DeviceManagers/pci_srvcs/pci_cards_drivers/PCI_BOOK.3e.html  
[12] 68kMLA, "Radeon 7000 Flashing Woes" (PC card flash chips too small for Mac ROMs): https://68kmla.org/bb/threads/radeon-7000-flashing-woes.47042/post-526975  
[13] MacRumors, "Just how open is Apple's Open Firmware (particularly on the G5)": https://forums.macrumors.com/threads/just-how-open-is-apples-open-firmware-particularly-on-the-g5.2455988/  
[14] Khronos OpenGL Registry, ARB_vertex_blend: https://registry.khronos.org/OpenGL/extensions/ARB/ARB_vertex_blend.txt  
[15] Khronos OpenGL Registry, APPLE_flush_buffer_range: https://registry.khronos.org/OpenGL/extensions/APPLE/APPLE_flush_buffer_range.txt  
[16] MacRumors, "Radeon HD cards on PowerPC Leopard using osx86-driver-radeonhd framebuffer driver" (pc297): https://forums.macrumors.com/threads/radeon-hd-cards-on-powerpc-leopard-using-osx86-driver-radeonhd-framebuffer-driver.2344940/
