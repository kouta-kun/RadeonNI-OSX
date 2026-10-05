# RadeonNI-OSX

RadeonNI-OSX is a project aiming to enable the usage of modern(-ish) cards on OS X 10.4 PowerPC machines. Specifically, the Radeon HD 7570 (Turks PRO-L, TeraScale 2) on the PowerMac G5.

This project is vibecoded, meatproxied and any other AI slur you can think of. I am not responsible if your G5 explores, your HD 7570 breaks your display, or any other property or bodily harm arising from the usage of this project.

# Development stages
The project has been developed in a staged manner:

## Stage 1 (Research and display bringup)

Using an HD 7570 bridged through VFIO into a QEMU virtual machine, Claude Opus 5.5 traced a barebones initialization of this card from the Linux kernel and reproduced it to where it could perform basic tasks:

* Initialize/POST the card using AtomBIOS (currently VBIOS is provided through a file, reading VBIOS from the ROM is a future step)
* Retrieve the EDID of the connected display
* Initialize the card's display engine and draw a test image (rainbow color bars).

After this was possible from within emulated OS X, the next step was to develop a barebones IOFramebuffer that allowed for unaccelerated display output, and that worked perfectly.

## Stage 2 (Mesa port and acceleration, currently WIP)

Once display output was working, the next step was to ask Claude to port Mesa over to it. Why Mesa? It's MIT, uses well-isolated modules, and most importantly has the r600 driver with proven support for this card, which I'd already used under ArchPOWER on a big endian system.

Claude took the r600 driver from Mesa, vendored it into the project, and developed two ends to this integration, a hardware interface that allows r600 to communicate with the GPU through PCIe (for command submission, etc) and a (currently in development) IOAccelerator implementation that passes through every OpenGL to Mesa. This worked, but did not support Quartz Extreme and essentially functioned by using OSMesa to render and then asking the CPU to copy over to the framebuffer. What is now in progress is implementing Quartz Extreme/compositing and handling some Apple extensions so that the driver interacts correctly with the Windowing System (i.e. no flickering, trails, etc).

## Future steps

Once the QEMU-harness experiment is working (OpenGL works perfectly, no graphical errors, applications and games work correctly) I will test this on my actual G5. I expect OpenFirmware to present some issues, although Claude's research implied that the G5's implementation might expose even more BARs by default than OpenBIOS did.

# Usage

WIP, check g5/README.txt. Compilation requires an OS X 10.4.11 installation with XCode 2.5. I will provide a build at some point.