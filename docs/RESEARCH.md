# Prior research: Radeon HD 7570 on PowerPC Mac OS X

Date: 2026-10-04. Compiled by four research agents from web sources and cloned source code. Maintained alongside the project since; corrections are noted inline with their date.

Markers: **[V]** verified in the linked source. **[I]** inference or memory, unchecked. Inferences are hypotheses to validate.

## 1. Conclusions

- An unaccelerated framebuffer for the HD 7570 is feasible. Nobody has published one for PPC Mac OS X.
- Acceleration (Quartz Extreme, Core Image, OpenGL) depends on private Apple interfaces and is a project of a different magnitude.
- The RX 7600 is not viable: mandatory signed firmware, no big-endian precedent.
- VFIO passthrough works with cross-architecture emulation (PPC guest on x86 host), with limits.

## 2. The card

- **Identity [V]:** "Turks", Northern Islands, OEM rebrand of the HD 6570/6670. PCIe 2.1 x16. DCE 5.0 display engine. Modesetting through AtomBIOS.
- **PCI IDs [V]:** `1002:6759` (HD 6570/7570/8550) and `1002:675D` (Turks PRO, HD 7570). Full `CHIP_TURKS` list in [drm_pciids.h](https://github.com/torvalds/linux/blob/master/include/drm/drm_pciids.h).
- **Ambiguity [V]:** the name "HD 7570" also appears on Redwood PRO (Evergreen). The ID must be confirmed.
- **Microcode [V, Linux source]:** `ni.c` declares `TURKS_pfp.bin`, `TURKS_me.bin`, `BTC_rlc.bin`, `TURKS_mc.bin`, `TURKS_smc.bin`.
  - PFP, ME and RLC are only needed for acceleration. SMC only for power management.
  - Linux aborts without MC ("MC ucode required for NI+"), but `ni_mc_load_microcode()` only loads it if the memory is GDDR5 and the sequencer is not running.
  - Haiku `radeon_hd` loads no microcode and supports these IDs.
  - **[I]** A DDR3 card should do modesetting with no blobs.
  - **Correction (2026-10-04):** this card is GDDR5 (VBIOS string and `MC_SEQ_MISC0`), so it is the case where Linux does load `TURKS_mc.bin`. Whether modesetting works without it is untested.
  - The blobs are big-endian (`be32_to_cpup`) and their licence only allows binary redistribution: [LICENSE.radeon](https://github.com/cernekee/linux-firmware/blob/master/LICENSE.radeon).
- **Power [I]:** 45–60 W, no auxiliary connector.

### Why not the RX 7600 (Navi 33)

- **[V]** Mandatory firmware: PSP, SMU, GC (pfp/me/mec/rlc/imu/mes), SDMA, DMCUB, VCN. Without it there is no usable driver: [Ubuntu bug](https://bugs.launchpad.net/ubuntu/+source/linux-firmware/+bug/2003846).
- **[V]** `drivers/gpu/drm/amd` is over 1.2 million lines excluding generated headers; the display code (DC) uses kernel floating point.
- **[V]** DCN was disabled on PPC64 and re-enabled for little-endian only: [LKML](https://lkml.iu.edu/hypermail/linux/kernel/2207.2/07725.html).
- **[V]** amdgpu requests a 44-bit DMA mask. 165 W with an 8-pin connector.

## 3. Mac OS X 10.4/10.5 PPC graphics stack

| Layer | What it is | Public status |
|---|---|---|
| FCode ROM + NDRV | Open Firmware creates the `display` node and leaves a PEF driver in `driver,AAPL,MacOS,PowerPC` | Open support in IONDRVSupport [V] |
| Native `IOFramebuffer` | Kext with its own modeset | Base class open [V] |
| IOAccelerator | Kernel side of acceleration | Only a skeleton is open (366 lines) [V] |
| GA plugin | Userspace 2D acceleration | Interface in `IOGraphicsInterface.h` [V] |
| GLD (`*GLDriver.bundle`) | Userspace OpenGL driver | Headers not published |

- **[V]** Pure virtual methods of `IOFramebuffer`: `getApertureRange`, `getPixelFormats`, `getDisplayModeCount`, `getDisplayModes`, `getInformationForDisplayMode`, `getPixelInformation`, `getCurrentDisplayMode`, among others.
- **[V]** IONDRVSupport has a catch-all on the PCI display class with score 0; without an NDRV it falls back to `IOBootNDRV`, which on PPC requires `AAPL,boot-display` on the node.
- **[I]** For an unaccelerated desktop, the `IOFramebuffer` subclass with modeset and EDID is enough.
- **[V]** Documented Quartz Extreme requirements: OpenGL, AGP 2X or faster, arbitrary-size textures, 16 MB VRAM.
- **[V]** In 10.4/10.5 Apple's ATI framebuffer is ATINDRV with per-board personalities: [MattKC's notes](https://forum.mattkc.com/viewtopic.php?t=218).

### Apple open source versions [V]

| Project | 10.4.11 PPC | 10.5.8 |
|---|---|---|
| IOGraphics | 179.2.1 | 305.14 |
| xnu | 792.24.17 | 1228.15.4 |
| IOPCIFamily | 34 | 110 |
| IOKitUser | 277.8 | 388.53.30 |
| kext_tools | 65.2.1 | 117.4 |
| gcc | 5250 | 5465 |
| gdb | 437 | 768 |

Repos: [IOGraphics](https://github.com/apple-oss-distributions/IOGraphics), [xnu](https://github.com/apple-oss-distributions/xnu), [IOPCIFamily](https://github.com/apple-oss-distributions/IOPCIFamily), [release index](https://opensource.apple.com/releases/). The old opensource.apple.com URLs return 404.

## 4. A card with no FCode ROM

- **[V]** IOPCIFamily-34 supports `IOPCIMatch`, `IOPCIPrimaryMatch` and `IOPCIClassMatch` on config space.
- **[I]** Without FCode, the node is named `pciVVVV,DDDD`, with no `device_type=display`: no boot screen and no console on that card.
- **[V]** On PPC, IOPCIBridge takes device memory from the Open Firmware `assigned-addresses` property. **[I]** If the firmware does not assign the 64-bit BARs, the kext would have to deal with it.
- **[V]** An `lspci` dump from a G5 11,2 shows Open Firmware assigning BARs and the expansion ROM (128 KB) to an unflashed Evergreen card: [morph.zone](https://morph.zone/modules/newbb_plus/viewtopic.php?topic_id=13632&forum=11).
- **[V]** An FCode ROM can be loaded from disk in Open Firmware without flashing, and the Open Firmware console can be used over telnet: [MacRumors](https://forums.macrumors.com/threads/testing-fcode-roms-before-you-flash.2123070/).

### POST without x86 [V, Linux source]

1. `radeon_get_bios()` tries several sources; the relevant one is `pci_map_rom`. It validates the 0x55AA signature and looks for "ATOM". For NI there is also `ni_read_disabled_bios()`.
2. `radeon_card_posted()` checks whether any CRTC is active or `CONFIG_MEMSIZE` is non-zero.
3. If the card is not posted, `atom_asic_init()` runs the ASIC_Init table with the default clocks from FirmwareInfo.

No x86 code is executed. Haiku does the same in `bios.cpp`.

## 5. Reference code

| Project | Licence | Use |
|---|---|---|
| [Linux radeon](https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/radeon) | MIT headers on the relevant files [V] | Complete reference, with big-endian paths |
| [Haiku radeon_hd](https://github.com/haiku/haiku/tree/master/src/add-ons/accelerants/radeon_hd) | MIT [V] | Smallest template, modesetting only; almost no big-endian handling |
| [osx86-driver-radeonhd](https://github.com/AustinSMU/osx86-driver-radeonhd) | No visible licence | Real `IOFramebuffer` for 10.5/10.6 x86, up to HD 4000 |
| [VMQemuVGA](https://github.com/ivanagui2/VMQemuVGA) | MIT [V] | Small `IOFramebuffer` skeleton |
| [VMsvga2](https://github.com/mirror/VMsvga2) | MIT [V] | Open framebuffer, IOAccelerator, GA and GLD shim (x86) |
| [QemuMacDrivers](https://github.com/ozbenh/QemuMacDrivers) | GPL-2.0 [V] | The only complete open NDRV; do not copy |
| [atiradeonx1000-kext-source](https://github.com/SamBushman/atiradeonx1000-kext-source) | Derived from an Apple binary | Map of the acceleration ABI on Tiger PPC; do not copy |
| [Mesa r600](https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/gallium/drivers/r600) | MIT | 3D reference for later phases |

Linux files that matter for modesetting [V]:

| File | Lines | Purpose |
|---|---|---|
| `atom.c` | 1,439 | AtomBIOS interpreter |
| `atombios.h` | 7,980 | Table structures |
| `radeon_atombios.c` | 4,473 | Connectors, PLL |
| `atombios_crtc.c` | 2,231 | CRTC and PLL |
| `atombios_encoders.c` | 2,791 | Encoders |
| `atombios_dp.c` | 853 | DisplayPort |
| `radeon_bios.c` | 758 | ROM acquisition |
| `radeon_device.c` | 1,859 | Init and POST |
| `evergreen.c` / `ni.c` | 5,546 / 2,721 | ASIC init, firmware |

Documentation:
- [Haiku technical notes](https://dev.haiku-os.org/wiki/HardwareInfo/video/RadeonHD_tech): DCE generations and the modeset sequence.
- [AMD Evergreen/Cayman programming guide](https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/programmer-references/evergreen_cayman_programming_guide.pdf) and a [mirror of AMD documents](https://github.com/olvaffe/gpu-docs/tree/master/amd-open-gpu-docs).
- **[V]** No AMD documentation describes AtomBIOS; you have to read the interpreter.

## 6. Big-endian

- **[V]** The Linux radeon driver has explicit support: 42 occurrences of `__BIG_ENDIAN` in 16 files; `atom.c` reads with `get_unaligned_le32`; `atombios.h` requires `ATOM_BIG_ENDIAN` and has bitfield variants; `EVERGREEN_GRPH_ENDIAN_SWAP` for the framebuffer.
- **[I]** Classic trap: command table arguments must be converted to little-endian before `atom_execute_table`. Linux does it field by field.
- **[V]** Hans de Ruiter (AmigaOS 4) found a bitfield bug in the SetPixelClock parameters, and had to disable VGA and wait for the memory controller before ASICInit: [HD 4350 post](http://hdrlab.org.nz/projects/amiga-os-4-projects/radeonhd-driver/radeonhd-development-log/radeonhd-chip-radeon-hd-4350-pci-vga-output-is-working/), [full log](http://hdrlab.org.nz/projects/amiga-os-4-projects/radeonhd-driver/radeonhd-development-log).
- **[V]** OpenBSD 5.5 fixed endian issues in DisplayPort and in the POST check for radeondrm on macppc: [changelog](https://www.openbsd.org/plus55.html).
- Three layers to keep in mind: little-endian MMIO, little-endian AtomBIOS tables with reversed bitfields, and big-endian microcode.

### Precedents on the PCIe G5

- **Linux:** HD 6570 (Turks) working on a G5 Quad; HD 5770 hung: [debian-powerpc](https://groups.google.com/g/linux.debian.ports.powerpc/c/-7huwbCGG-s). Read only through an automatic summary; one agent found no first-hand report. **Confirm.**
- **[V]** HD 6970 on a G5 with kernel 6.10: console fine, X with artefacts: [debian-powerpc](https://www.mail-archive.com/debian-powerpc@lists.debian.org/msg71675.html).
- **[V]** MorphOS on PowerMac11,2: 1 GB HD 6570, HD 5550 and HD 5450 give a black screen; X1300, X1950 XT and HD 4550 work. Two HD 6450 also fail: [thread 13632](https://morph.zone/modules/newbb_plus/viewtopic.php?topic_id=13632&forum=11), [thread 13622](https://morph.zone/modules/newbb_plus/viewtopic.php?forum=11&topic_id=13622). The developer notes they work on machines whose firmware runs the BIOS first.
- **[I]** Cold POST of Evergreen/NI is the highest-risk step. Follow the Linux order.

## 7. The direct prior attempt

pc297, MacRumors, 2022–2025: [thread](https://forums.macrumors.com/threads/radeon-hd-cards-on-powerpc-leopard-using-osx86-driver-radeonhd-framebuffer-driver.2344940/) **[V]**

- Built RadeonHD.kext for ppc with Xcode 3.1.4 and llvm-gcc on 10.5.8, G5 Quad, with PC HD 4650/4870 cards. Plain gcc gave missing symbols.
- The kext loads, detects the card and creates the nubs; the screen stays black.
- Log: "Getting BIOS copy from legacy address", "No AtomBios signature found". The `RHDReadPCIBios` fallback also fails.
- Advice in the thread: load the VBIOS from a file or plist; the ROM size in the device tree may be truncated; audit byte-swapping across all MMIO.
- No repository was published and modeset was never reached.

## 8. Harness

### Cross-architecture VFIO

- **[V]** `VFIO_PCI` depends on neither KVM nor architecture; KVM paths sit behind `kvm_enabled()`: [hw/vfio](https://github.com/qemu/qemu/tree/master/hw/vfio).
- **[V]** `region.c` declares regions little-endian; the big-endian guest does the swaps, as on real hardware.
- **[V]** A PCI Rage 128 passed to `qemu-system-ppc` on a Ryzen host; 10.4.11 worked; it needed a patched OpenBIOS and loading the ROM/NDRV by hand: [MacRumors](https://forums.macrumors.com/threads/qemu-system-ppc-vga-passthrough.2229861/).
- **[V]** With 2D/3D acceleration the screen corrupts; DMA is suspected: [qemu-ppc](https://lists.gnu.org/archive/html/qemu-ppc/2020-03/msg00520.html).
- **[V]** An aarch64 TCG guest on an x86 host fails with 4 GB RAM because of overlap with reserved IOVA regions: [bug 1869006](https://bugs.launchpad.net/qemu/+bug/1869006).
- **[V]** Trace events `vfio_region_read`, `vfio_region_write`, `vfio_pci_read_config`, `vfio_pci_write_config`, and the `x-no-mmap` property. With `x-no-mmap=on` you get a complete register trace, including config space, the I/O BAR and every framebuffer write [V, 2026-10-04, x86 guest].

| Aspect | Status |
|---|---|
| Config space, MMIO BARs, INTx | Works [V] |
| 64-bit BARs | OpenBIOS always maps them in 32-bit space [V]; `mac99` PCI hole is 0x80000000 + 0x70000000 [V]; 256 MB untested |
| PCIe | `mac99` only has UniNorth PCI/AGP bridges [V] |
| ROM | Without FCode, OpenBIOS does not initialise it [I]; it can be supplied with `romfile=` |
| `x-vga` | Designed for PCs; do not use [I] |
| Host IOMMU grouping | On the development host the whole X370 chipset is one group including the NIC; only the CPU x16 slot is usable for passthrough [V, 2026-10-04] |
| Reset after a panic | QEMU's reset of the `vfio-pci` device at guest start returns this card to the un-POSTed state [V, 2026-10-04] |
| DMA | Known failure area; not needed for modesetting [I] |

### Mac OS X PPC in QEMU

- **[V]** `mac99`: default CPU 7400, one CPU upstream, 2 GB RAM maximum, ships `qemu_vga.ndrv`: [mac_newworld.c](https://github.com/qemu/qemu/blob/master/hw/ppc/mac_newworld.c), [documentation](https://www.qemu.org/docs/master/system/ppc/powermac.html).
- **[V]** 10.0–10.4 documented: [emaculation](https://www.emaculation.com/doku.php/ppc-osx-on-qemu-for-osx), [gist](https://gist.github.com/cellularmitosis/63b2914711f9ee32053d3c1f48d5c89a). 10.5 with `-cpu G4` and `via=pmu` [I].
- **[V]** `ati-vga` only emulates Rage 128 Pro and RV100. No Evergreen/NI model exists in any emulator.
- **[V]** [OpenBIOS drivers/pci.c](https://github.com/openbios/openbios/blob/master/drivers/pci.c): BAR assignment.
- **[V]** `mmiotrace` only exists on x86. **[I]** While active it takes all CPUs but one offline on the host; a VFIO trace from an x86 guest avoids that and matches the format used for the kext.
- Ruled out: KVM-PR on a G5 (no VFIO backend), DingusPPC, PearPC, SheepShaver.

### Real G5 (later phase)

- **[V]** PowerMac11,2: one x16 slot and three more (two x4, one x8), PCIe Gen1: [Apple developer note](https://developer.apple.com/library/archive/documentation/Hardware/Conceptual/PowerMac_G5_05Oct/Articles/PwrMacG5-0510_archi.html).
- **[V]** KDP over Ethernet: `nvram boot-args="debug=0x144 -v"`; symbols with `kextload -s`: [Apple tutorial](https://developer.apple.com/library/archive/documentation/Darwin/Conceptual/KEXTConcept/KEXTConceptDebugger/debug_tutorial.html), [TN2063](https://developer.apple.com/library/archive/technotes/tn2063/_index.html), [Debugging Drivers](https://developer.apple.com/library/archive/documentation/DeviceDrivers/Conceptual/WritingDeviceDriver/DebuggingDrivers/DebuggingDrivers.html).
- **[V]** FireWire KDP needs `AppleFireWireKDP.kext` from the FireWire SDK before 10.6: [release notes](https://developer.apple.com/library/archive/releasenotes/Darwin/RN-FireWire/index.html).
- **[I]** Keep the 6600 LE as the console and put the 7570 in another slot; load the kext by hand.

## 9. Toolchain

- **[V]** Xcode 2.5 is the last for 10.4; Xcode 3.1.4 works on 10.5.8 PPC.
- **[V]** The reconstructed X1000 kext builds with Apple gcc 4.0.1 on a Tiger G5, over ssh.
- **[I]** The PPC kernel is 32-bit even on a G5; kexts are ppc32.
- **[V]** Modern cross-compilation is only verified for userland: [gcc-powerpc-apple-darwin8](https://github.com/VariantXYZ/gcc-powerpc-apple-darwin8), [cctools-port](https://github.com/Wohlstand/cctools-port). Kexts untested.
- **[V]** Checklist for porting x86 kexts to PPC (endianness in buffers, byte-swapping accessors, DMA limits): [Envy24HT](https://github.com/ITzTravelInTime/Envy24HT).
- Kernel Debug Kits: [collection](https://www.macintoshrepository.org/25003-mac-os-x-kernel-debug-kits); not confirmed to include the PPC ones.

## 10. Acceleration (later phases)

- **[V]** Apple states the GLD interface supports third-party drivers, but never published the headers: [OpenGL Programming Guide](https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/OpenGL-MacProgGuide/opengl_pg_concepts/opengl_pg_concepts.html).
- **[V]** VMsvga2's GLD is a shim that forwards to Apple drivers; it is useful as a list of entry points.
- **[V]** There is no ATIRadeonX2000 for PPC: [kext table](https://forums.macrumors.com/threads/snow-leopard-on-unsupported-powerpc-macs.2232031/).
- **[V]** Mesa r600 on big-endian has historical bugs: [2011 patch](https://lists.freedesktop.org/archives/dri-devel/2011-April/010170.html), [bug 93727](https://bugs.freedesktop.org/show_bug.cgi?id=93727).

## 11. Still to verify

- ~~PCI ID and memory type of this specific card.~~ `1002:675d` Turks PRO, GDDR5 (2026-10-04).
- Whether GDDR5 VRAM is usable after `asic_init` without the MC microcode.
- The report of an HD 6570 under Linux on a G5 Quad, first-hand.
- Whether OpenBIOS assigns the 7570's BARs under `mac99`.
- Whether the G5's Open Firmware assigns large BARs to a card with no FCode.
- Whether an `IOFramebuffer` loaded with `kextload` after boot is picked up by WindowServer without logging out.
- The licence of osx86-driver-radeonhd.
