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

## Stage 2 (Mesa port and acceleration)

Once display output was working, the next step was to ask Claude to port Mesa over to it. Why Mesa? It's MIT, uses well-isolated modules, and most importantly has the r600 driver with proven support for this card, which I'd already used under ArchPOWER on a big endian system.

Claude took the r600 driver from Mesa, vendored it into the project, and developed two ends to this integration, a hardware interface that allows r600 to communicate with the GPU through PCIe (for command submission, etc) and a (currently in development) IOAccelerator implementation that passes through every OpenGL to Mesa. This worked, but did not support Quartz Extreme and essentially functioned by using OSMesa to render and then asking the CPU to copy over to the framebuffer. After some development Quartz Extreme seems to work correctly (no trails, OpenGL windowed and fullscreen works). A couple of games have been tested:

|   Game      |  State  |
| ----------- | ------- |
| Quake 3     | Working in full screen, non-responsive input in windowed mode |
| Sauerbraten | Working in full screen, glitchy lower half in windowed mode |
| Tux Racer   | Broken (window only updates when moved, has no full-screen mode) |
| Doom 3      | Working in full screen, performance about equivalent to 6600LE due to CPU bottleneck |

It is also now being tested on the G5 with no major issues. A test on a new monitor showed that the HDMI infoframes were not 100% accurate (which the other monitor was way more tolerant of). It should now work with most 1080p HDMI or DVI-D monitors. The output topology is hardcoded, so it's likely to only work on the DVI-1 output of specificially the HD 7570.

## Stage 2.5 (Optimization)

Testing games like Quake 3 and Doom 3, performance seemed to be equivalent or sometimes worse than the original NVidia 6600LE which is ~10x worse in terms of raw power. After a bit of investigation, the firmware blob was initializing the card at very low core and memory clocks (100MHz/100MHz). Increasing this to expected levels (600MHz/850MHz) provided a small but noticeable improvement, however the big improvement of about 100% came when GART size was reported correctly (GART=0 made Mesa stop flushing after every draw command), and by flushing asynchronously on swapping buffers instead of waiting.

At this point performance was slightly below 6600LE on Doom 3, and about equivalent on Quake 3. Profiling by Claude revealed that most of the time was CPU bound within the driver. A couple of optimizations were developed (replacing Tiger's subpar emulation of Thread-local storage, force-enabling glthread to utilize both cores of the G5) which allowed us to reach and sometimes surpass the 6600LE. Currently, it seems like the bottleneck for high-end games like Doom 3 is the CPU. This is supported by benchmarks run with graphic settings maxxed out, which have almost no effect on either GPU except for anti-aliasing, which drops the 6600LE to 5FPS while the Radeon (as expected) remains at 22FPS.

## Future steps

- Optimize glthread parameters to reduce cross-thread waiting
- G5 testing worked, so the next step is to try to read the VBIOS from the ROM and get a compatibility list working.
- At some point, I should try with other cards of the same family/model to see if anything works or if this is too HD 7570 specific.

# Usage

WIP, check g5/README.txt. Compilation requires an OS X 10.4.11 installation with XCode 2.5. I will provide a build at some point.