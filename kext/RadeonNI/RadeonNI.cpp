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

#define super IOService
OSDefineMetaClassAndStructors(RadeonNI, IOService)

/* Aperture: 64-bit memory BAR at 0x10. Registers: 64-bit memory BAR at 0x18. */
#define FB_BAR			kIOPCIConfigBaseAddress0
#define REG_BAR			kIOPCIConfigBaseAddress2

/* DDC line of the DVI-I connector (AtomBIOS i2c id). */
#define DVI_DDC_ID		0x93

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

/* POST if needed, read the EDID, set its preferred mode, show the pattern. */
bool RadeonNI::bringUp()
{
	static UInt8 edid[RDN_EDID_MAX_SIZE];
	struct rdn_i2c_bus bus;
	struct rdn_mode mode;
	struct rdn_fb fb;
	int len, r;

	r = rdn_card_post(&fCard);
	if (r) {
		IOLog("RadeonNI: POST failed (%d)\n", r);
		return false;
	}
	IOLog("RadeonNI: CONFIG_MEMSIZE %lu MB\n",
	      (unsigned long)readReg(0x5428));

	rdn_i2c_bus_by_id(&fCard, DVI_DDC_ID, &bus);
	len = rdn_edid_read(&fCard, &bus, edid);
	if (len < 0 || !rdn_edid_preferred_mode(edid, &mode)) {
		IOLog("RadeonNI: no EDID on the DVI connector (%d)\n", len);
		return false;
	}
	IOLog("RadeonNI: EDID %d bytes, preferred %ux%u at %lu kHz\n", len,
	      mode.hdisplay, mode.vdisplay, (unsigned long)mode.clock);

	fFbMap = fDevice->mapDeviceMemoryWithRegister(FB_BAR);
	if (!fFbMap) {
		IOLog("RadeonNI: cannot map the aperture\n");
		return false;
	}

	bzero(&fb, sizeof(fb));
	fb.width = mode.hdisplay;
	fb.height = mode.vdisplay;
	fb.pitch_pixels = (mode.hdisplay + 63) & ~63;
	fb.big_endian_pixels = true;

	rdn_pattern_draw((volatile uint32_t *)fFbMap->getVirtualAddress(),
			 fb.width, fb.height, fb.pitch_pixels);

	r = rdn_display_init(&fCard);
	if (!r)
		r = rdn_modeset(&fCard, &mode, &fb,
				rdn_edid_is_hdmi(edid, len));
	IOLog("RadeonNI: modeset returned %d, CRTC0_CONTROL %08lx\n", r,
	      (unsigned long)readReg(0x6e70));
	return r == 0;
}

bool RadeonNI::start(IOService *provider)
{
	if (!super::start(provider))
		return false;

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
		stop(provider);
		return false;
	}
	fCardReady = true;

	if (!bringUp()) {
		stop(provider);
		return false;
	}

	registerService();
	return true;
}

void RadeonNI::stop(IOService *provider)
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
	IOLog("RadeonNI: stopped\n");
	super::stop(provider);
}
