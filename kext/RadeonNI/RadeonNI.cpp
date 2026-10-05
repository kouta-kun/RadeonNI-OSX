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

#include "RadeonNI.h"

#define super IOFramebuffer
OSDefineMetaClassAndStructors(RadeonNI, IOFramebuffer)

/* Aperture: 64-bit memory BAR at 0x10. Registers: 64-bit memory BAR at 0x18. */
#define FB_BAR			kIOPCIConfigBaseAddress0
#define REG_BAR			kIOPCIConfigBaseAddress2

/* DDC line of the DVI-I connector (AtomBIOS i2c id). */
#define DVI_DDC_ID		0x93

/* The single display mode and its single depth. */
#define MODE_ID			1
#define DEPTH_32		0

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
 * The VBIOS image. Under QEMU, OpenBIOS does not assign the expansion ROM
 * BAR, so the image comes from a "VBIOS" data property that scripts/kext.sh
 * puts into the personality at load time. Reading the ROM BAR is deferred to
 * the real G5 (see docs/PLAN.md).
 */
bool RadeonNI::loadBios()
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
 * POST if needed, read the EDID and work out the mode and the surface. The
 * pattern is drawn so that there is something to see until the window
 * server takes the screen over.
 */
bool RadeonNI::bringUp()
{
	struct rdn_i2c_bus bus;
	int r;

	r = rdn_card_post(&fCard);
	if (r) {
		IOLog("RadeonNI: POST failed (%d)\n", r);
		return false;
	}
	IOLog("RadeonNI: CONFIG_MEMSIZE %lu MB\n",
	      (unsigned long)readReg(0x5428));

	rdn_i2c_bus_by_id(&fCard, DVI_DDC_ID, &bus);
	fEdidLen = rdn_edid_read(&fCard, &bus, fEdid);
	if (fEdidLen < 0 || !rdn_edid_preferred_mode(fEdid, &fMode)) {
		IOLog("RadeonNI: no EDID on the DVI connector (%d)\n", fEdidLen);
		return false;
	}
	IOLog("RadeonNI: EDID %d bytes, preferred %ux%u at %lu kHz\n", fEdidLen,
	      fMode.hdisplay, fMode.vdisplay, (unsigned long)fMode.clock);

	fFbMap = fDevice->mapDeviceMemoryWithRegister(FB_BAR);
	if (!fFbMap) {
		IOLog("RadeonNI: cannot map the aperture\n");
		return false;
	}

	bzero(&fFb, sizeof(fFb));
	fFb.width = fMode.hdisplay;
	fFb.height = fMode.vdisplay;
	fFb.pitch_pixels = (fMode.hdisplay + 63) & ~63;
	fFb.big_endian_pixels = true;

	rdn_pattern_draw((volatile uint32_t *)fFbMap->getVirtualAddress(),
			 fFb.width, fFb.height, fFb.pitch_pixels);

	r = rdn_display_init(&fCard);
	if (r) {
		IOLog("RadeonNI: display init failed (%d)\n", r);
		return false;
	}
	return programMode() == kIOReturnSuccess;
}

IOReturn RadeonNI::programMode()
{
	int r = rdn_modeset(&fCard, &fMode, &fFb,
			    rdn_edid_is_hdmi(fEdid, fEdidLen));

	IOLog("RadeonNI: modeset returned %d, CRTC0_CONTROL %08lx\n", r,
	      (unsigned long)readReg(0x6e70));
	if (r)
		return kIOReturnIOError;
	fModeSet = true;
	return kIOReturnSuccess;
}

bool RadeonNI::start(IOService *provider)
{
	fDevice = OSDynamicCast(IOPCIDevice, provider);
	if (!fDevice)
		return false;

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

	if (!loadBios() || rdn_card_init(&fCard, &fOS, fBios)) {
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
}

void RadeonNI::stop(IOService *provider)
{
	super::stop(provider);
	cleanUp();
	IOLog("RadeonNI: stopped\n");
}

/*
 * IOFramebuffer
 */

IOReturn RadeonNI::enableController(void)
{
	return fModeSet ? kIOReturnSuccess : programMode();
}

/* The visible surface, at the start of the aperture. */
IODeviceMemory *RadeonNI::getApertureRange(IOPixelAperture aperture)
{
	IODeviceMemory *bar;

	if (aperture != kIOFBSystemAperture)
		return 0;
	bar = fDevice->getDeviceMemoryWithRegister(FB_BAR);
	if (!bar)
		return 0;
	return IODeviceMemory::withSubRange(bar, 0,
		fFb.pitch_pixels * 4 * fFb.height);
}

const char *RadeonNI::getPixelFormats(void)
{
	static const char formats[] = IO32BitDirectPixels "\0";

	return formats;
}

IOItemCount RadeonNI::getDisplayModeCount(void)
{
	return 1;
}

IOReturn RadeonNI::getDisplayModes(IODisplayModeID *allDisplayModes)
{
	allDisplayModes[0] = MODE_ID;
	return kIOReturnSuccess;
}

IOReturn RadeonNI::getInformationForDisplayMode(IODisplayModeID displayMode,
	IODisplayModeInformation *info)
{
	UInt64 hz1616;

	if (displayMode != MODE_ID)
		return kIOReturnUnsupportedMode;

	bzero(info, sizeof(*info));
	info->nominalWidth = fMode.hdisplay;
	info->nominalHeight = fMode.vdisplay;
	/* Refresh rate in 16.16 fixed point: clock / (htotal * vtotal). */
	hz1616 = ((UInt64)fMode.clock * 1000ULL << 16) /
		((UInt64)fMode.htotal * fMode.vtotal);
	info->refreshRate = (IOFixed1616)hz1616;
	info->maxDepthIndex = DEPTH_32;
	info->flags = kDisplayModeValidFlag | kDisplayModeSafeFlag |
		kDisplayModeDefaultFlag;
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
	if (displayMode != MODE_ID || depth != DEPTH_32)
		return kIOReturnUnsupportedMode;
	if (aperture != kIOFBSystemAperture)
		return kIOReturnUnsupportedMode;

	bzero(pixelInfo, sizeof(*pixelInfo));
	pixelInfo->bytesPerRow = fFb.pitch_pixels * 4;
	pixelInfo->bytesPerPlane = 0;
	pixelInfo->bitsPerPixel = 32;
	pixelInfo->pixelType = kIORGBDirectPixels;
	pixelInfo->componentCount = 3;
	pixelInfo->bitsPerComponent = 8;
	pixelInfo->componentMasks[0] = 0x00ff0000;
	pixelInfo->componentMasks[1] = 0x0000ff00;
	pixelInfo->componentMasks[2] = 0x000000ff;
	strncpy(pixelInfo->pixelFormat, IO32BitDirectPixels,
		sizeof(pixelInfo->pixelFormat));
	pixelInfo->activeWidth = fFb.width;
	pixelInfo->activeHeight = fFb.height;
	return kIOReturnSuccess;
}

IOReturn RadeonNI::getCurrentDisplayMode(IODisplayModeID *displayMode,
	IOIndex *depth)
{
	if (displayMode)
		*displayMode = MODE_ID;
	if (depth)
		*depth = DEPTH_32;
	return kIOReturnSuccess;
}

IOReturn RadeonNI::setDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
	if (displayMode != MODE_ID || depth != DEPTH_32)
		return kIOReturnUnsupportedMode;
	/* There is one mode and it is already set. */
	return fModeSet ? kIOReturnSuccess : programMode();
}

IOReturn RadeonNI::getStartupDisplayMode(IODisplayModeID *displayMode,
	IOIndex *depth)
{
	return getCurrentDisplayMode(displayMode, depth);
}

/* Direct colour: there is no colour table to load. */
IOReturn RadeonNI::setCLUTWithEntries(IOColorEntry *colors, UInt32 index,
	UInt32 numEntries, IOOptionBits options)
{
	return kIOReturnSuccess;
}

/* The hardware table holds a linear ramp; gamma is not applied yet. */
IOReturn RadeonNI::setGammaTable(UInt32 channelCount, UInt32 dataCount,
	UInt32 dataWidth, void *data)
{
	return kIOReturnSuccess;
}

IOReturn RadeonNI::getTimingInfoForDisplayMode(IODisplayModeID displayMode,
	IOTimingInformation *info)
{
	IODetailedTimingInformationV2 *t;

	if (displayMode != MODE_ID)
		return kIOReturnUnsupportedMode;

	bzero(info, sizeof(*info));
	info->appleTimingID = kIOTimingIDInvalid;
	info->flags = kIODetailedTimingValid;

	t = &info->detailedInfo.v2;
	t->signalConfig = kIODigitalSignal;
	t->pixelClock = (UInt64)fMode.clock * 1000ULL;
	t->minPixelClock = t->pixelClock;
	t->maxPixelClock = t->pixelClock;
	t->horizontalActive = fMode.hdisplay;
	t->horizontalBlanking = fMode.htotal - fMode.hdisplay;
	t->horizontalSyncOffset = fMode.hsync_start - fMode.hdisplay;
	t->horizontalSyncPulseWidth = fMode.hsync_end - fMode.hsync_start;
	t->verticalActive = fMode.vdisplay;
	t->verticalBlanking = fMode.vtotal - fMode.vdisplay;
	t->verticalSyncOffset = fMode.vsync_start - fMode.vdisplay;
	t->verticalSyncPulseWidth = fMode.vsync_end - fMode.vsync_start;
	t->horizontalSyncConfig = (fMode.flags & RDN_MODE_NHSYNC) ?
		0 : kIOSyncPositivePolarity;
	t->verticalSyncConfig = (fMode.flags & RDN_MODE_NVSYNC) ?
		0 : kIOSyncPositivePolarity;
	t->numLinks = 1;
	return kIOReturnSuccess;
}

IOItemCount RadeonNI::getConnectionCount(void)
{
	return 1;
}

IOReturn RadeonNI::setAttributeForConnection(IOIndex connectIndex,
	IOSelect attribute, UInt32 value)
{
	return super::setAttributeForConnection(connectIndex, attribute, value);
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
	if (displayMode != MODE_ID)
		return kIOReturnUnsupportedMode;
	*flags = kDisplayModeValidFlag | kDisplayModeSafeFlag;
	return kIOReturnSuccess;
}

bool RadeonNI::hasDDCConnect(IOIndex connectIndex)
{
	return fEdidLen > 0;
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
