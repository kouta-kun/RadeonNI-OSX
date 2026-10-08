/*
 * Radeon HD 7570 ("Turks", Northern Islands) driver for Mac OS X 10.4 PPC.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <libkern/libkern.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>

#include "RadeonNI.h"
#include "RadeonNIAccel.h"

extern "C" {
/* osfmk/ppc/machine_routines.h; exported, but not in Kernel.framework. */
boolean_t ml_probe_read(vm_offset_t paddr, unsigned int *val);
}

#define super IOFramebuffer
OSDefineMetaClassAndStructors(RadeonNI, IOFramebuffer)

/* Aperture: 64-bit memory BAR at 0x10. Registers: 64-bit memory BAR at 0x18. */
#define FB_BAR			kIOPCIConfigBaseAddress0
#define REG_BAR			kIOPCIConfigBaseAddress2

/* DDC line of the DVI-I connector (AtomBIOS i2c id). */
/*
 * Where the hardware cursor's picture is kept in video memory: above
 * every screen surface, below the accelerator's ring (RadeonNIAccel.cpp).
 */
#define CURSOR_OFFSET		(31u << 20)

/*
 * Display mode IDs are 1 + the index of the EDID detailed timing. Depth
 * indices follow the usual order.
 */
enum {
	kDepth8 = 0,
	kDepth16 = 1,
	kDepth32 = 2,
	kDepthCount = 3
};

static const UInt32 kDepthBits[kDepthCount] = { 8, 16, 32 };

/*
 * OS layer for the hardware library. The card's registers are
 * little-endian and the CPU is not, so every access swaps.
 */

static uint32_t os_mmio_read32(void *cookie, uint32_t offset)
{
	return ((RadeonNI *)cookie)->readReg(offset);
}

static void os_mmio_write32(void *cookie, uint32_t offset, uint32_t value)
{
	((RadeonNI *)cookie)->writeReg(offset, value);
}

static uint32_t os_cfg_read32(void *cookie, uint32_t offset)
{
	return ((RadeonNI *)cookie)->device()->configRead32(offset);
}

static void os_cfg_write32(void *cookie, uint32_t offset, uint32_t value)
{
	((RadeonNI *)cookie)->device()->configWrite32(offset, value);
}

static void os_delay_us(void *cookie, uint32_t usec)
{
	/* Short waits spin; long ones give up the CPU. */
	if (usec < 1000)
		IODelay(usec);
	else
		IOSleep((usec + 999) / 1000);
}

static uint64_t os_time_ms(void *cookie)
{
	uint64_t abstime, nanos;

	clock_get_uptime(&abstime);
	absolutetime_to_nanoseconds(abstime, &nanos);
	return nanos / 1000000ULL;
}

/* IOFree needs the size back, so it is kept in front of the block. */
static void *os_alloc(void *cookie, size_t size)
{
	UInt32 *block = (UInt32 *)IOMalloc(size + 8);

	if (!block)
		return 0;
	bzero(block, size + 8);
	block[0] = size + 8;
	return block + 2;
}

static void os_free(void *cookie, void *ptr)
{
	UInt32 *block = (UInt32 *)ptr - 2;

	IOFree(block, block[0]);
}

static void os_log(void *cookie, enum rdn_log_level level, const char *fmt,
		   va_list ap)
{
	char line[256];

	if (level == RDN_LOG_DEBUG)
		return;
	vsnprintf(line, sizeof(line), fmt, ap);
	IOLog("RadeonNI: %s\n", line);
}

UInt32 RadeonNI::readReg(UInt32 offset)
{
	return OSReadLittleInt32(fRegs, offset);
}

void RadeonNI::writeReg(UInt32 offset, UInt32 value)
{
	OSWriteLittleInt32(fRegs, offset, value);
	OSSynchronizeIO();
}

/*
 * The VBIOS image from the card's expansion ROM. The firmware gives the ROM
 * an address and leaves its decoding off; turning that on for the read is a
 * configuration bit, and nothing is written to the ROM. Every word is read
 * with ml_probe_read(), which comes back with "false" where a plain read of
 * something that does not answer would be a machine check. The image has to
 * start with the option ROM signature, fit the BAR and sum to zero.
 *
 * False, with the reason in the log, when that gives no image: under QEMU
 * always (OpenBIOS assigns the ROM no address), and with the boot argument
 * rdn_rom=0.
 */
bool RadeonNI::biosFromRom()
{
	IODeviceMemory *mem;
	const char *why = 0;
	UInt8 *image = 0;
	UInt32 bar, phys, size = 0, i;
	unsigned int word = 0;
	UInt8 sum = 0;
	int on = 1;

	if (PE_parse_boot_arg("rdn_rom", &on) && !on) {
		IOLog("RadeonNI: the card's ROM is left alone (rdn_rom=0)\n");
		return false;
	}
	bar = fDevice->configRead32(kIOPCIConfigExpansionROMBase);
	mem = fDevice->getDeviceMemoryWithRegister(kIOPCIConfigExpansionROMBase);
	if (!(bar & 0xfffff800) || !mem) {
		IOLog("RadeonNI: the firmware gave the card's ROM no address\n");
		return false;
	}
	phys = mem->getPhysicalAddress();
	fDevice->configWrite32(kIOPCIConfigExpansionROMBase, bar | 1);
	IODelay(1000);

	if (!ml_probe_read(phys, &word)) {
		why = "does not answer";
	} else if ((word >> 16) != 0x55aa) {
		why = "has no option ROM signature";
	} else {
		size = ((word >> 8) & 0xff) * 512;
		if (!size || size > mem->getLength())
			why = "states a length that does not fit its BAR";
		else if (!(image = (UInt8 *)IOMalloc(size)))
			why = "could not be copied (no memory)";
	}
	for (i = 0; !why && i < size; i += 4) {
		if (!ml_probe_read(phys + i, &word)) {
			why = "stopped answering";
			break;
		}
		image[i] = word >> 24;
		image[i + 1] = word >> 16;
		image[i + 2] = word >> 8;
		image[i + 3] = word;
		sum += image[i] + image[i + 1] + image[i + 2] + image[i + 3];
	}
	if (!why && sum)
		why = "has a wrong checksum";
	fDevice->configWrite32(kIOPCIConfigExpansionROMBase, bar);

	if (why) {
		IOLog("RadeonNI: the card's ROM at %08lx %s (last word read %08x)\n",
		      (unsigned long)phys, why, word);
		if (image)
			IOFree(image, size);
		return false;
	}
	fBios = image;
	fBiosSize = size;
	IOLog("RadeonNI: VBIOS, %lu bytes from the card's ROM\n",
	      (unsigned long)fBiosSize);
	return true;
}

/*
 * The image a "VBIOS" data property of the personality carries, if
 * scripts/kext.sh or g5/install.sh put one there.
 */
bool RadeonNI::biosFromPersonality()
{
	OSData *data = OSDynamicCast(OSData, getProperty("VBIOS"));
	const UInt8 *bytes;

	if (!data || data->getLength() < 0x200) {
		IOLog("RadeonNI: no VBIOS property in the personality\n");
		return false;
	}
	bytes = (const UInt8 *)data->getBytesNoCopy();
	if (bytes[0] != 0x55 || bytes[1] != 0xaa ||
	    data->getLength() < (unsigned)bytes[2] * 512) {
		IOLog("RadeonNI: VBIOS property is not a valid ROM image\n");
		return false;
	}
	fBiosSize = data->getLength();
	fBios = IOMalloc(fBiosSize);
	if (!fBios)
		return false;
	bcopy(bytes, fBios, fBiosSize);
	IOLog("RadeonNI: VBIOS, %lu bytes from the personality\n",
	      (unsigned long)fBiosSize);
	return true;
}

/*
 * Where the ROM's image is the one in use and the personality carries one
 * too, say whether they are the same: a file that belongs to another card
 * should not go unnoticed until the day it is needed.
 */
void RadeonNI::compareWithPersonality()
{
	OSData *data = OSDynamicCast(OSData, getProperty("VBIOS"));
	const UInt8 *rom = (const UInt8 *)fBios, *file;
	UInt32 size, i, differ = 0, first = 0;

	if (!data)
		return;
	file = (const UInt8 *)data->getBytesNoCopy();
	size = data->getLength() < fBiosSize ? data->getLength() : fBiosSize;
	for (i = 0; i < size; i++)
		if (rom[i] != file[i] && !differ++)
			first = i;
	if (differ)
		IOLog("RadeonNI: the personality's VBIOS differs from the ROM in %lu of %lu bytes, the first at %lx\n",
		      (unsigned long)differ, (unsigned long)size,
		      (unsigned long)first);
	else
		IOLog("RadeonNI: the personality's VBIOS is the same as the ROM\n");
}

/*
 * The VBIOS comes from the card's ROM, and from the personality where that
 * fails, also when the ROM answers with something the AtomBIOS parser
 * rejects. On success the card structure is set up from the image.
 */
bool RadeonNI::loadBios()
{
	int source;

	for (source = 0; source < 2; source++) {
		if (!(source ? biosFromPersonality() : biosFromRom()))
			continue;
		if (!rdn_card_init(&fCard, &fOS, fBios)) {
			if (!source)
				compareWithPersonality();
			return true;
		}
		IOLog("RadeonNI: the image from %s is not a usable VBIOS\n",
		      source ? "the personality" : "the card's ROM");
		IOFree(fBios, fBiosSize);
		fBios = 0;
	}
	return false;
}

const struct rdn_mode *RadeonNI::modeForID(IODisplayModeID id)
{
	if (id < 1 || (UInt32)id > fModeCount)
		return 0;
	return &fModes[id - 1];
}

/* The surface for a mode and depth: at the start of the aperture. */
void RadeonNI::describeFb(const struct rdn_mode *mode, IOIndex depth,
			  struct rdn_fb *fb)
{
	bzero(fb, sizeof(*fb));
	fb->width = mode->hdisplay;
	fb->height = mode->vdisplay;
	/* Linux pads the pitch to 64 pixels; do the same. */
	fb->pitch_pixels = (mode->hdisplay + 63) & ~63;
	fb->bpp = kDepthBits[depth];
	fb->big_endian_pixels = true;
}

/*
 * POST if needed, read the EDID, collect the modes and set the preferred
 * one. The pattern is drawn so that there is something to see until the
 * window server takes the screen over.
 */
bool RadeonNI::bringUp()
{
	UInt32 i;
	int r;

	r = rdn_card_post(&fCard);
	if (r) {
		IOLog("RadeonNI: POST failed (%d)\n", r);
		return false;
	}
	IOLog("RadeonNI: CONFIG_MEMSIZE %lu MB\n",
	      (unsigned long)readReg(0x5428));

	/*
	 * Load the memory controller's microcode, as Linux does on this
	 * card, before anything is kept in video memory: the memory clock
	 * can only be raised with it. It is in the personality when the
	 * installer found the file. rdn_mc=0 as a boot argument skips it.
	 */
	if (!PE_parse_boot_arg("rdn_mc", &i))
		i = 1;
	if (i) {
		OSData *mc = OSDynamicCast(OSData, getProperty("FW_MC"));

		if (!mc) {
			IOLog("RadeonNI: no memory controller microcode in the personality\n");
		} else {
			r = rdn_mc_load_microcode(&fCard, mc->getBytesNoCopy(),
						  mc->getLength());
			IOLog("RadeonNI: memory controller microcode: %d (0 loaded and trained, 1 not needed), "
			      "MC_SEQ_SUP_CNTL 0x%08lx, CONFIG_MEMSIZE %lu MB\n", r,
			      (unsigned long)readReg(0x28c8),
			      (unsigned long)readReg(0x5428));
		}
	}

	/* The first connector with a display on it: DVI-I, then DisplayPort. */
	fEdidLen = rdn_output_detect(&fCard, fEdid);
	if (fEdidLen < 0) {
		IOLog("RadeonNI: no EDID on any connector (%d)\n", fEdidLen);
		return false;
	}
	/* For ioreg: what the monitor said, when a mode is refused. */
	setProperty("EDID", fEdid, fEdidLen);
	setProperty("Output", fCard.output->name);
	fForceDVI = PE_parse_boot_arg("rdn_dvi", &i) && i;
	IOLog("RadeonNI: %s: EDID %d bytes, %s input, %s signalling%s\n",
	      fCard.output->name, fEdidLen,
	      (fEdid[20] & 0x80) ? "digital" : "analog",
	      fCard.output->displayport ? "DisplayPort" : useHDMI() ? "HDMI" : "DVI",
	      fForceDVI ? " (rdn_dvi)" : "");
	/* The monitor's timings, the preferred one first (mode 1), and the sizes programs expect. */
	{
		int pref = 0;

		fModeCount = (UInt32)rdn_edid_modes(fEdid, fEdidLen, fModes, kMaxModes, &pref);
		fPreferred = (UInt32)pref + 1;
	}
	for (i = 0; i < fModeCount; i++)
		IOLog("RadeonNI: mode %lu: %ux%u at %lu kHz\n", (unsigned long)i + 1,
		      fModes[i].hdisplay, fModes[i].vdisplay, (unsigned long)fModes[i].clock);
	if (!fModeCount) {
		IOLog("RadeonNI: the EDID has no detailed timing\n");
		return false;
	}

	/* Room for the largest mode at 32 bpp, rounded up to a megabyte. */
	fSurfaceBytes = 0;
	for (i = 0; i < fModeCount; i++) {
		struct rdn_fb fb;
		UInt32 bytes;

		describeFb(&fModes[i], kDepth32, &fb);
		bytes = fb.pitch_pixels * 4 * fb.height;
		if (bytes > fSurfaceBytes)
			fSurfaceBytes = bytes;
	}
	fSurfaceBytes = (fSurfaceBytes + 0xfffff) & ~0xfffff;
	IOLog("RadeonNI: surface memory %lu MB\n",
	      (unsigned long)fSurfaceBytes >> 20);

	fFbMap = fDevice->mapDeviceMemoryWithRegister(FB_BAR);
	if (!fFbMap) {
		IOLog("RadeonNI: cannot map the aperture\n");
		return false;
	}

	/* Linear ramps until the system loads its own tables. */
	for (i = 0; i < 256; i++) {
		fGamma[i].red = fGamma[i].green = fGamma[i].blue = i << 2;
		fClut[i] = fGamma[i];
	}

	describeFb(&fModes[fPreferred - 1], kDepth32, &fFb);
	rdn_pattern_draw((volatile uint32_t *)fFbMap->getVirtualAddress(),
			 fFb.width, fFb.height, fFb.pitch_pixels);

	r = rdn_display_init(&fCard);
	if (r) {
		IOLog("RadeonNI: display init failed (%d)\n", r);
		return false;
	}
	return programMode(fPreferred, kDepth32) == kIOReturnSuccess;
}

bool RadeonNI::useHDMI()
{
	return !fForceDVI && !fCard.output->displayport &&
	       rdn_edid_is_hdmi(fEdid, fEdidLen);
}

IOReturn RadeonNI::programMode(IODisplayModeID id, IOIndex depth)
{
	IOReturn r;

	if (fLock)
		IOLockLock(fLock);
	r = programModeLocked(id, depth);
	if (fLock)
		IOLockUnlock(fLock);
	return r;
}

IOReturn RadeonNI::programModeLocked(IODisplayModeID id, IOIndex depth)
{
	const struct rdn_mode *mode = modeForID(id);
	int r;

	if (!mode || depth < 0 || depth >= kDepthCount)
		return kIOReturnUnsupportedMode;

	describeFb(mode, depth, &fFb);
	r = rdn_modeset(&fCard, mode, &fFb, useHDMI());
	IOLog("RadeonNI: mode %ld depth %ld (%ux%u, %lu bpp): %d\n", (long)id,
	      (long)depth, mode->hdisplay, mode->vdisplay,
	      (unsigned long)fFb.bpp, r);
	if (r)
		return kIOReturnIOError;
	fCurrentMode = id;
	fCurrentDepth = depth;
	fModeSet = true;
	fOutputOn = true;
	/* The mode set leaves the cursor's registers to us. */
	if (fHWCursor && fCursorLoaded)
		rdn_cursor_set(&fCard, CURSOR_OFFSET, fCursorX, fCursorY,
			       fCursorVisible);
	loadColors();
	return kIOReturnSuccess;
}

/* 8 bpp looks colours up in the table; the other depths use it for gamma. */
void RadeonNI::loadColors()
{
	rdn_lut_set(&fCard, 0, 256, fCurrentDepth == kDepth8 ? fClut : fGamma);
}

bool RadeonNI::start(IOService *provider)
{
	fDevice = OSDynamicCast(IOPCIDevice, provider);
	if (!fDevice)
		return false;

	fLock = IOLockAlloc();
	fOutputOn = true;
	fConnected = true;
	fPowerMax = 0;

	IOLog("RadeonNI: device %04x:%04x, command %04x\n",
	      fDevice->configRead16(kIOPCIConfigVendorID),
	      fDevice->configRead16(kIOPCIConfigDeviceID),
	      fDevice->configRead16(kIOPCIConfigCommand));

	fDevice->setMemoryEnable(true);

	fRegMap = fDevice->mapDeviceMemoryWithRegister(REG_BAR);
	if (!fRegMap) {
		IOLog("RadeonNI: cannot map the register BAR\n");
		return false;
	}
	fRegs = (volatile UInt8 *)fRegMap->getVirtualAddress();

	bzero(&fOS, sizeof(fOS));
	fOS.cookie = this;
	fOS.mmio_read32 = os_mmio_read32;
	fOS.mmio_write32 = os_mmio_write32;
	/* No I/O BAR: Open Firmware may not assign it, and MMIO is enough. */
	fOS.cfg_read32 = os_cfg_read32;
	fOS.cfg_write32 = os_cfg_write32;
	fOS.delay_us = os_delay_us;
	fOS.time_ms = os_time_ms;
	fOS.alloc = os_alloc;
	fOS.free = os_free;
	fOS.log = os_log;

	if (!loadBios()) {
		IOLog("RadeonNI: no usable VBIOS\n");
		cleanUp();
		return false;
	}
	fCardReady = true;

	/* The hardware has to be up before IOFramebuffer starts asking. */
	if (!bringUp()) {
		cleanUp();
		return false;
	}

	if (!super::start(provider)) {
		IOLog("RadeonNI: IOFramebuffer::start failed\n");
		cleanUp();
		return false;
	}
	IOLog("RadeonNI: framebuffer started\n");

	{
		int arg = 1;

		fHotplug = !(PE_parse_boot_arg("rdn_hotplug", &arg) && !arg);
		arg = 1;
		fDpms = !(PE_parse_boot_arg("rdn_dpms", &arg) && !arg);
		IOLog("RadeonNI: display power management %s, hot-plug polling %s\n",
		      fDpms ? "on" : "off (rdn_dpms=0)", fHotplug ? "on" : "off (rdn_hotplug=0)");
	}
	startHotplug();

	fHWCursor = getProperty("HWCursor") == kOSBooleanTrue;
	/*
	 * The accelerator is announced only when the personality asks for
	 * it: the window server and OpenGL act on it as soon as it is there.
	 */
	if (getProperty("Accelerator") == kOSBooleanTrue)
		fAccel = RadeonNIAccel::withFramebuffer(this, provider);
	return true;
}

void RadeonNI::cleanUp()
{
	if (fCardReady) {
		rdn_card_fini(&fCard);
		fCardReady = false;
	}
	if (fBios) {
		IOFree(fBios, fBiosSize);
		fBios = 0;
	}
	if (fFbMap) {
		fFbMap->release();
		fFbMap = 0;
	}
	if (fRegMap) {
		fRegMap->release();
		fRegMap = 0;
	}
	fRegs = 0;
	if (fLock) {
		IOLockFree(fLock);
		fLock = 0;
	}
}

void RadeonNI::stop(IOService *provider)
{
	stopHotplug();
	if (fAccel) {
		fAccel->retire(provider);
		fAccel->release();
		fAccel = 0;
	}
	super::stop(provider);
	cleanUp();
	IOLog("RadeonNI: stopped\n");
}

/*
 * For the accelerator
 */

/*
 * The hardware cursor. IOFramebuffer draws the cursor itself, with the
 * CPU, unless the driver says it has one; it then hands over every new
 * cursor picture and every move.
 */

IOReturn RadeonNI::getAttribute(IOSelect attribute, UInt32 *value)
{
	if (attribute == kIOHardwareCursorAttribute && fHWCursor) {
		if (value)
			*value = 1;
		return kIOReturnSuccess;
	}
	return super::getAttribute(attribute, value);
}

IOReturn RadeonNI::setCursorImage(void *cursorImage)
{
	IOHardwareCursorDescriptor desc;
	IOHardwareCursorInfo info;
	volatile UInt32 *dst;
	UInt32 x, y, w, h;

	if (!fHWCursor || !aperture())
		return kIOReturnUnsupported;
	bzero(&desc, sizeof(desc));
	desc.majorVersion = kHardwareCursorDescriptorMajorVersion;
	desc.minorVersion = kHardwareCursorDescriptorMinorVersion;
	desc.height = RDN_CURSOR_SIZE;
	desc.width = RDN_CURSOR_SIZE;
	desc.bitDepth = 32;
	bzero(&info, sizeof(info));
	info.majorVersion = kHardwareCursorInfoMajorVersion;
	info.minorVersion = kHardwareCursorInfoMinorVersion;
	info.hardwareCursorData = (UInt8 *)fCursorData;
	if (!convertCursorImage(cursorImage, &desc, &info)) {
		fCursorLoaded = false;
		rdn_cursor_set(&fCard, CURSOR_OFFSET, 0, 0, false);
		return kIOReturnUnsupported;
	}
	w = info.cursorWidth;
	h = info.cursorHeight;
	if (w > RDN_CURSOR_SIZE || h > RDN_CURSOR_SIZE)
		return kIOReturnUnsupported;
	if (!fCursorLogged) {
		fCursorLogged = true;
		IOLog("RadeonNI: hardware cursor %lux%lu, first pixels %08lx %08lx %08lx %08lx\n",
		      (unsigned long)w, (unsigned long)h,
		      (unsigned long)fCursorData[0], (unsigned long)fCursorData[1],
		      (unsigned long)fCursorData[w], (unsigned long)fCursorData[w + 1]);
	}

	/* The card reads little-endian words; the rest is transparent. */
	dst = (volatile UInt32 *)((volatile UInt8 *)aperture() + CURSOR_OFFSET);
	for (y = 0; y < RDN_CURSOR_SIZE; y++)
		for (x = 0; x < RDN_CURSOR_SIZE; x++)
			dst[y * RDN_CURSOR_SIZE + x] = (x < w && y < h) ?
				OSSwapHostToLittleInt32(fCursorData[y * w + x]) : 0;
	fCursorLoaded = true;
	rdn_cursor_set(&fCard, CURSOR_OFFSET, fCursorX, fCursorY, fCursorVisible);
	return kIOReturnSuccess;
}

IOReturn RadeonNI::setCursorState(SInt32 x, SInt32 y, bool visible)
{
	if (!fHWCursor)
		return kIOReturnUnsupported;
	fCursorX = x;
	fCursorY = y;
	fCursorVisible = visible;
	if (fCursorLoaded)
		rdn_cursor_set(&fCard, CURSOR_OFFSET, x, y, visible);
	return kIOReturnSuccess;
}

volatile void *RadeonNI::aperture()
{
	return fFbMap ? (volatile void *)fFbMap->getVirtualAddress() : 0;
}

UInt32 RadeonNI::apertureSize()
{
	return fFbMap ? (UInt32)fFbMap->getLength() : 0;
}

IOMemoryDescriptor *RadeonNI::apertureDescriptor()
{
	return fDevice->getDeviceMemoryWithRegister(FB_BAR);
}

bool RadeonNI::screen(struct rdn_fb *fb)
{
	if (!fModeSet)
		return false;
	*fb = fFb;
	if (!fb->bpp)
		fb->bpp = 32;
	return true;
}

bool RadeonNI::selftestTarget(struct rdn_accel *accel,
			      struct rdn_selftest_target *target)
{
	if (!fModeSet || fFb.bpp != 32)
		return false;
	bzero(target, sizeof(*target));
	target->gpu_addr = rdn_vram_addr(accel, fFb.aperture_offset);
	target->width = fFb.width;
	target->height = fFb.height;
	target->pitch_pixels = fFb.pitch_pixels;
	target->big_endian_pixels = fFb.big_endian_pixels;
	return true;
}

/*
 * IOFramebuffer
 */

IOReturn RadeonNI::enableController(void)
{
	return fModeSet ? kIOReturnSuccess : programMode(fPreferred, kDepth32);
}

/*
 * The memory the visible surface can occupy, at the start of the aperture.
 * It has the same size in every mode, large enough for the biggest one: the
 * window server maps it once and keeps using that mapping across mode
 * changes, so a range that only fits the current mode makes it fault as
 * soon as a larger mode is selected.
 */
IODeviceMemory *RadeonNI::getApertureRange(IOPixelAperture aperture)
{
	IODeviceMemory *bar;

	if (aperture != kIOFBSystemAperture)
		return 0;
	bar = fDevice->getDeviceMemoryWithRegister(FB_BAR);
	if (!bar)
		return 0;
	return IODeviceMemory::withSubRange(bar, 0, fSurfaceBytes);
}

IODeviceMemory *RadeonNI::getVRAMRange(void)
{
	IODeviceMemory *bar;

	/*
	 * With the accelerator, all of the aperture is video memory the
	 * system can count on; the window server wants to see at least
	 * 16 MB before it considers Quartz Extreme. Without it, only the
	 * screen's surfaces are ours.
	 */
	if (getProperty("Accelerator") != kOSBooleanTrue)
		return getApertureRange(kIOFBSystemAperture);
	bar = fDevice->getDeviceMemoryWithRegister(FB_BAR);
	if (!bar)
		return 0;
	bar->retain();
	return bar;
}

const char *RadeonNI::getPixelFormats(void)
{
	static const char formats[] =
		IO8BitIndexedPixels "\0"
		IO16BitDirectPixels "\0"
		IO32BitDirectPixels "\0";

	return formats;
}

IOItemCount RadeonNI::getDisplayModeCount(void)
{
	return fModeCount;
}

IOReturn RadeonNI::getDisplayModes(IODisplayModeID *allDisplayModes)
{
	UInt32 i;

	for (i = 0; i < fModeCount; i++)
		allDisplayModes[i] = i + 1;
	return kIOReturnSuccess;
}

IOReturn RadeonNI::getInformationForDisplayMode(IODisplayModeID displayMode,
	IODisplayModeInformation *info)
{
	const struct rdn_mode *mode = modeForID(displayMode);
	UInt64 hz1616;

	if (!mode)
		return kIOReturnUnsupportedMode;

	bzero(info, sizeof(*info));
	info->nominalWidth = mode->hdisplay;
	info->nominalHeight = mode->vdisplay;
	/* Refresh rate in 16.16 fixed point: clock / (htotal * vtotal). */
	hz1616 = ((UInt64)mode->clock * 1000ULL << 16) /
		((UInt64)mode->htotal * mode->vtotal);
	info->refreshRate = (IOFixed1616)hz1616;
	info->maxDepthIndex = kDepthCount - 1;
	info->flags = kDisplayModeValidFlag | kDisplayModeSafeFlag;
	/* The EDID's first detailed timing is the preferred mode. */
	if ((UInt32)displayMode == fPreferred)
		info->flags |= kDisplayModeDefaultFlag;
	return kIOReturnSuccess;
}

UInt64 RadeonNI::getPixelFormatsForDisplayMode(IODisplayModeID displayMode,
	IOIndex depth)
{
	return 0;
}

IOReturn RadeonNI::getPixelInformation(IODisplayModeID displayMode,
	IOIndex depth, IOPixelAperture aperture, IOPixelInformation *pixelInfo)
{
	const struct rdn_mode *mode = modeForID(displayMode);
	struct rdn_fb fb;

	if (!mode || depth < 0 || depth >= kDepthCount ||
	    aperture != kIOFBSystemAperture)
		return kIOReturnUnsupportedMode;

	describeFb(mode, depth, &fb);
	bzero(pixelInfo, sizeof(*pixelInfo));
	pixelInfo->bytesPerRow = fb.pitch_pixels * (fb.bpp / 8);
	pixelInfo->bytesPerPlane = 0;
	pixelInfo->bitsPerPixel = fb.bpp;
	pixelInfo->activeWidth = fb.width;
	pixelInfo->activeHeight = fb.height;

	switch (depth) {
	case kDepth8:
		pixelInfo->pixelType = kIOCLUTPixels;
		pixelInfo->componentCount = 1;
		pixelInfo->bitsPerComponent = 8;
		pixelInfo->componentMasks[0] = 0xff;
		strncpy(pixelInfo->pixelFormat, IO8BitIndexedPixels,
			sizeof(pixelInfo->pixelFormat));
		break;
	case kDepth16:
		pixelInfo->pixelType = kIORGBDirectPixels;
		pixelInfo->componentCount = 3;
		pixelInfo->bitsPerComponent = 5;
		pixelInfo->componentMasks[0] = 0x7c00;
		pixelInfo->componentMasks[1] = 0x03e0;
		pixelInfo->componentMasks[2] = 0x001f;
		strncpy(pixelInfo->pixelFormat, IO16BitDirectPixels,
			sizeof(pixelInfo->pixelFormat));
		break;
	default:
		pixelInfo->pixelType = kIORGBDirectPixels;
		pixelInfo->componentCount = 3;
		pixelInfo->bitsPerComponent = 8;
		pixelInfo->componentMasks[0] = 0x00ff0000;
		pixelInfo->componentMasks[1] = 0x0000ff00;
		pixelInfo->componentMasks[2] = 0x000000ff;
		strncpy(pixelInfo->pixelFormat, IO32BitDirectPixels,
			sizeof(pixelInfo->pixelFormat));
		break;
	}
	return kIOReturnSuccess;
}

IOReturn RadeonNI::getCurrentDisplayMode(IODisplayModeID *displayMode,
	IOIndex *depth)
{
	if (displayMode)
		*displayMode = fCurrentMode;
	if (depth)
		*depth = fCurrentDepth;
	return kIOReturnSuccess;
}

IOReturn RadeonNI::setDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
	if (fModeSet && displayMode == fCurrentMode && depth == fCurrentDepth)
		return kIOReturnSuccess;
	return programMode(displayMode, depth);
}

IOReturn RadeonNI::getStartupDisplayMode(IODisplayModeID *displayMode,
	IOIndex *depth)
{
	if (displayMode)
		*displayMode = fPreferred;
	if (depth)
		*depth = kDepth32;
	return kIOReturnSuccess;
}

/* The colour table for 8 bpp. Components arrive as 16-bit values. */
IOReturn RadeonNI::setCLUTWithEntries(IOColorEntry *colors, UInt32 index,
	UInt32 numEntries, IOOptionBits options)
{
	UInt32 i, slot;

	for (i = 0; i < numEntries; i++) {
		slot = (options & kSetCLUTByValue) ? colors[i].index : index + i;
		if (slot > 255)
			continue;
		fClut[slot].red = colors[i].red >> 6;
		fClut[slot].green = colors[i].green >> 6;
		fClut[slot].blue = colors[i].blue >> 6;
	}
	if (fModeSet && fCurrentDepth == kDepth8)
		loadColors();
	return kIOReturnSuccess;
}

/*
 * The gamma ramp for the direct depths: `channelCount` tables of
 * `dataCount` entries, each `dataWidth` bits wide, red first.
 */
IOReturn RadeonNI::setGammaTable(UInt32 channelCount, UInt32 dataCount,
	UInt32 dataWidth, void *data)
{
	UInt32 i, ch, src, v;

	if (!data || !dataCount || (channelCount != 1 && channelCount != 3) ||
	    dataWidth < 8 || dataWidth > 16)
		return kIOReturnBadArgument;

	for (i = 0; i < 256; i++) {
		UInt16 out[3];

		src = i * dataCount / 256;
		for (ch = 0; ch < 3; ch++) {
			UInt32 table = (channelCount == 3 ? ch : 0) * dataCount;

			if (dataWidth == 8)
				v = ((UInt8 *)data)[table + src];
			else
				v = ((UInt16 *)data)[table + src];
			/* To 10 bits. */
			if (dataWidth >= 10)
				v >>= dataWidth - 10;
			else
				v <<= 10 - dataWidth;
			out[ch] = v;
		}
		fGamma[i].red = out[0];
		fGamma[i].green = out[1];
		fGamma[i].blue = out[2];
	}
	if (fModeSet && fCurrentDepth != kDepth8)
		loadColors();
	return kIOReturnSuccess;
}

IOReturn RadeonNI::getTimingInfoForDisplayMode(IODisplayModeID displayMode,
	IOTimingInformation *info)
{
	const struct rdn_mode *mode = modeForID(displayMode);
	IODetailedTimingInformationV2 *t;

	if (!mode)
		return kIOReturnUnsupportedMode;

	bzero(info, sizeof(*info));
	info->appleTimingID = kIOTimingIDInvalid;
	info->flags = kIODetailedTimingValid;

	t = &info->detailedInfo.v2;
	t->signalConfig = kIODigitalSignal;
	t->pixelClock = (UInt64)mode->clock * 1000ULL;
	t->minPixelClock = t->pixelClock;
	t->maxPixelClock = t->pixelClock;
	t->horizontalActive = mode->hdisplay;
	t->horizontalBlanking = mode->htotal - mode->hdisplay;
	t->horizontalSyncOffset = mode->hsync_start - mode->hdisplay;
	t->horizontalSyncPulseWidth = mode->hsync_end - mode->hsync_start;
	t->verticalActive = mode->vdisplay;
	t->verticalBlanking = mode->vtotal - mode->vdisplay;
	t->verticalSyncOffset = mode->vsync_start - mode->vdisplay;
	t->verticalSyncPulseWidth = mode->vsync_end - mode->vsync_start;
	t->horizontalSyncConfig = (mode->flags & RDN_MODE_NHSYNC) ?
		0 : kIOSyncPositivePolarity;
	t->verticalSyncConfig = (mode->flags & RDN_MODE_NVSYNC) ?
		0 : kIOSyncPositivePolarity;
	t->numLinks = 1;
	return kIOReturnSuccess;
}

IOItemCount RadeonNI::getConnectionCount(void)
{
	return 1;
}

/*
 * What the OS sends is logged: this is the part of IOGraphics that is least
 * documented (docs/HOTPLUG.md). Display sleep arrives as the power
 * attribute, or as the syncs of the connection switched off (DPMS), or as
 * the connection's power; each ends in setOutputPower().
 */
IOReturn RadeonNI::setAttributeForConnection(IOIndex connectIndex,
	IOSelect attribute, UInt32 value)
{
	switch (attribute) {
	case kConnectionSyncEnable:
		IOLog("RadeonNI: connection syncs 0x%lx\n", (unsigned long)value);
		/* The bits name the syncs that are off. */
		if (fDpms)
			setOutputPower(!(value & (kIOHSyncDisable | kIOVSyncDisable)));
		return kIOReturnSuccess;
	case kConnectionPower:
		IOLog("RadeonNI: connection power %lu\n", (unsigned long)value);
		if (fDpms)
			setOutputPower(value != 0);
		return kIOReturnSuccess;
	case kConnectionPostWake:
		/* The system woke: the monitor may have changed. */
		IOLog("RadeonNI: connection post-wake\n");
		fSenseSteady = 0;
		return kIOReturnSuccess;
	default:
		return super::setAttributeForConnection(connectIndex, attribute, value);
	}
}

IOReturn RadeonNI::setAttribute(IOSelect attribute, UInt32 value)
{
	switch (attribute) {
	case kIOPowerAttribute:
		/*
		 * IOFramebuffer's power states: the highest is "on", every other
		 * is a display that is not used. The value of "on" is learned as
		 * the largest seen, so that a first request is never taken for off.
		 */
		if (value > fPowerMax)
			fPowerMax = value;
		IOLog("RadeonNI: power attribute %lu (on is %lu)\n", (unsigned long)value,
		      (unsigned long)fPowerMax);
		if (!fDpms)
			return super::setAttribute(attribute, value);
		if (value >= fPowerMax) {
			setOutputPower(true);
			handleEvent(kIOFBNotifyDidPowerOn);
		} else {
			handleEvent(kIOFBNotifyWillPowerOff);
			setOutputPower(false);
		}
		return kIOReturnSuccess;
	case kIOCapturedAttribute:
		fCaptured = value != 0;
		return kIOReturnSuccess;
	default:
		return super::setAttribute(attribute, value);
	}
}

IOReturn RadeonNI::getAttributeForConnection(IOIndex connectIndex,
	IOSelect attribute, UInt32 *value)
{
	switch (attribute) {
	case kConnectionEnable:
		/* The display answered over DDC when we started. */
		if (value)
			*value = 1;
		return kIOReturnSuccess;
	case kConnectionFlags:
		if (value)
			*value = 0;
		return kIOReturnSuccess;
	default:
		return super::getAttributeForConnection(connectIndex, attribute,
							value);
	}
}

IOReturn RadeonNI::connectFlags(IOIndex connectIndex,
	IODisplayModeID displayMode, IOOptionBits *flags)
{
	if (!modeForID(displayMode))
		return kIOReturnUnsupportedMode;
	*flags = kDisplayModeValidFlag | kDisplayModeSafeFlag;
	return kIOReturnSuccess;
}

bool RadeonNI::hasDDCConnect(IOIndex connectIndex)
{
	return fConnected && fEdidLen > 0;
}

/* Blocks are numbered from 1. The EDID was read when the driver started. */
IOReturn RadeonNI::getDDCBlock(IOIndex connectIndex, UInt32 blockNumber,
	IOSelect blockType, IOOptionBits options, UInt8 *data,
	IOByteCount *length)
{
	UInt32 offset;
	IOByteCount n;

	if (blockType != kIODDCBlockTypeEDID || blockNumber < 1)
		return kIOReturnUnsupported;
	offset = (blockNumber - 1) * RDN_EDID_BLOCK_SIZE;
	if (fEdidLen <= 0 || offset + RDN_EDID_BLOCK_SIZE > (UInt32)fEdidLen)
		return kIOReturnUnsupported;
	n = *length < RDN_EDID_BLOCK_SIZE ? *length : RDN_EDID_BLOCK_SIZE;
	bcopy(fEdid + offset, data, n);
	*length = n;
	return kIOReturnSuccess;
}


/*
 * Display power management and hot-plug.
 */

/* The picture and the signal off, or back (a mode set, with link training). */
void RadeonNI::setOutputPower(bool on)
{
	if (!fLock)
		return;
	IOLockLock(fLock);
	if (fModeSet && on != fOutputOn) {
		if (on) {
			IOReturn r = programModeLocked(fCurrentMode, fCurrentDepth);

			IOLog("RadeonNI: output back on: %d\n", (int)r);
		} else {
			int r = rdn_output_disable(&fCard, modeForID(fCurrentMode), useHDMI());

			fOutputOn = false;
			IOLog("RadeonNI: output off: %d\n", r);
		}
	}
	IOLockUnlock(fLock);
}

void RadeonNI::pollTimerFired(OSObject *owner, IOTimerEventSource *sender)
{
	RadeonNI *self = OSDynamicCast(RadeonNI, owner);

	if (!self)
		return;
	self->pollHotplug();
	sender->setTimeoutMS(500);
}

void RadeonNI::startHotplug()
{
	if (!fHotplug || fPollTimer)
		return;
	/* The lines of both connectors, as the first thing the poll reads. */
	rdn_output_hpd_enable(&fCard);
	IOSleep(100);
	/*
	 * A connector whose line reads low while the display answered over DDC
	 * has no usable hot-plug line (an analog cable, an adapter without it):
	 * polling it would call the display unplugged.
	 */
	if (!rdn_output_connected(&fCard)) {
		IOLog("RadeonNI: %s: the hot-plug line reads low although the display answered: "
		      "not polled\n", fCard.output->name);
		return;
	}
	fSenseLast = fConnected ? 1 : 0;
	fSenseSteady = 0;
	fPollLoop = IOWorkLoop::workLoop();
	if (!fPollLoop)
		return;
	fPollTimer = IOTimerEventSource::timerEventSource(this, pollTimerFired);
	if (!fPollTimer || fPollLoop->addEventSource(fPollTimer) != kIOReturnSuccess) {
		if (fPollTimer) {
			fPollTimer->release();
			fPollTimer = 0;
		}
		fPollLoop->release();
		fPollLoop = 0;
		return;
	}
	fPollTimer->setTimeoutMS(500);
}

void RadeonNI::stopHotplug()
{
	if (fPollTimer) {
		fPollTimer->cancelTimeout();
		if (fPollLoop)
			fPollLoop->removeEventSource(fPollTimer);
		fPollTimer->release();
		fPollTimer = 0;
	}
	if (fPollLoop) {
		fPollLoop->release();
		fPollLoop = 0;
	}
}

/*
 * Every half second: has the hot-plug line of the selected output changed,
 * and stayed so for a second (a monitor that wakes pulls the line low for a
 * moment). Unplugged: nothing is torn down, the desktop stays and the
 * connection says it has no DDC. Plugged in: read the EDID again.
 */
void RadeonNI::pollHotplug()
{
	int sense;

	if (!fLock || !fModeSet)
		return;
	IOLockLock(fLock);
	sense = rdn_output_connected(&fCard) ? 1 : 0;
	if (sense != fSenseLast) {
		fSenseLast = sense;
		fSenseSteady = 0;
	} else if (sense != (fConnected ? 1 : 0) && ++fSenseSteady >= 2) {
		fSenseSteady = 0;
		if (sense) {
			monitorReturned();
		} else {
			fConnected = false;
			IOLog("RadeonNI: %s: the monitor was unplugged\n", fCard.output->name);
			if (fConnectProc && fConnectOn && !fCaptured)
				(*fConnectProc)(fConnectTarget, fConnectRef);
		}
	}
	IOLockUnlock(fLock);
}

/*
 * A monitor is on the line again, the same one or another, and maybe on the
 * other connector: find it, read its EDID, make the mode list, and set the
 * mode of the same size (or the preferred one) with the link trained anew.
 * Called with fLock held.
 */
void RadeonNI::monitorReturned()
{
	UInt8 edid[RDN_EDID_MAX_SIZE];
	struct rdn_mode modes[kMaxModes];
	UInt32 count, i, bytes, keep = 0, id = 1, pref_id = 1;
	int pref = 0;
	int len;
	bool changed;

	IOLog("RadeonNI: the monitor is back\n");
	len = rdn_output_detect(&fCard, edid);
	if (len < 0) {
		IOLog("RadeonNI: no EDID yet (%d); will try again\n", len);
		return;
	}
	changed = len != fEdidLen || bcmp(edid, fEdid, len) != 0;
	if (changed) {
		count = (UInt32)rdn_edid_modes(edid, len, modes, kMaxModes, &pref);
		for (i = 0; i < count; i++) {
			struct rdn_fb fb;

			/* The window server maps the surface once, at its size. */
			describeFb(&modes[i], kDepth32, &fb);
			bytes = fb.pitch_pixels * 4 * fb.height;
			if (bytes <= fSurfaceBytes) {
				if ((int)i == pref)
					pref_id = keep + 1;
				modes[keep++] = modes[i];
			}
		}
		if (!keep) {
			IOLog("RadeonNI: the new monitor has no mode that fits\n");
			return;
		}
		id = pref_id;
		/* The mode of the size on screen now, else the preferred one. */
		for (i = 0; i < keep; i++)
			if (fModeSet && modes[i].hdisplay == fModes[fCurrentMode - 1].hdisplay &&
			    modes[i].vdisplay == fModes[fCurrentMode - 1].vdisplay) {
				id = i + 1;
				break;
			}
		bcopy(edid, fEdid, len);
		fEdidLen = len;
		bcopy(modes, fModes, keep * sizeof(modes[0]));
		fModeCount = keep;
		fPreferred = pref_id;
		setProperty("EDID", fEdid, fEdidLen);
		setProperty("Output", fCard.output->name);
		IOLog("RadeonNI: another monitor: %lu modes, %s\n", (unsigned long)keep,
		      fCard.output->name);
	} else {
		id = fCurrentMode;
	}
	fConnected = true;
	if (programModeLocked(id, fCurrentDepth) != kIOReturnSuccess)
		IOLog("RadeonNI: the mode set after the monitor came back failed\n");
	if (fConnectProc && fConnectOn && !fCaptured)
		(*fConnectProc)(fConnectTarget, fConnectRef);
}

IOReturn RadeonNI::registerForInterruptType(IOSelect interruptType,
	IOFBInterruptProc proc, OSObject *target, void *ref, void **interruptRef)
{
	if (interruptType == kIOFBConnectInterruptType) {
		fConnectProc = proc;
		fConnectTarget = target;
		fConnectRef = ref;
		fConnectOn = true;
		*interruptRef = (void *)&fConnectProc;
		IOLog("RadeonNI: the OS wants connection changes\n");
		return kIOReturnSuccess;
	}
	return super::registerForInterruptType(interruptType, proc, target, ref,
					       interruptRef);
}

IOReturn RadeonNI::unregisterInterrupt(void *interruptRef)
{
	if (interruptRef == (void *)&fConnectProc) {
		fConnectProc = 0;
		fConnectOn = false;
		return kIOReturnSuccess;
	}
	return super::unregisterInterrupt(interruptRef);
}

IOReturn RadeonNI::setInterruptState(void *interruptRef, UInt32 state)
{
	if (interruptRef == (void *)&fConnectProc) {
		fConnectOn = state == kEnabledInterruptState;
		return kIOReturnSuccess;
	}
	return super::setInterruptState(interruptRef, state);
}
