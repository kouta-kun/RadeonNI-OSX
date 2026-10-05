/*
 * Radeon HD 7570 ("Turks", Northern Islands) driver for Mac OS X 10.4 PPC.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>

#include "RadeonNI.h"

#define super IOService
OSDefineMetaClassAndStructors(RadeonNI, IOService)

/* Register BAR: 64-bit memory BAR at config offset 0x18. */
#define REG_BAR			kIOPCIConfigBaseAddress2

static const UInt32 kCrtcControl[6] = {
	0x6e70, 0x7a70, 0x10670, 0x11270, 0x11e70, 0x12a70
};
#define CONFIG_MEMSIZE		0x5428
#define MC_SEQ_MISC0		0x2a00

/* The card's registers are little-endian; the CPU is not. */
UInt32 RadeonNI::readReg(UInt32 offset)
{
	return OSReadLittleInt32(fRegs, offset);
}

bool RadeonNI::start(IOService *provider)
{
	UInt32 i;

	if (!super::start(provider))
		return false;

	fDevice = OSDynamicCast(IOPCIDevice, provider);
	if (!fDevice)
		return false;

	IOLog("RadeonNI: device %04x:%04x, command %04x, %u memory ranges\n",
	      fDevice->configRead16(kIOPCIConfigVendorID),
	      fDevice->configRead16(kIOPCIConfigDeviceID),
	      fDevice->configRead16(kIOPCIConfigCommand),
	      (unsigned)fDevice->getDeviceMemoryCount());

	fDevice->setMemoryEnable(true);

	fRegMap = fDevice->mapDeviceMemoryWithRegister(REG_BAR);
	if (!fRegMap) {
		IOLog("RadeonNI: cannot map the register BAR\n");
		return false;
	}
	fRegs = (volatile UInt8 *)fRegMap->getVirtualAddress();
	IOLog("RadeonNI: registers at physical %08lx, length %lx\n",
	      (unsigned long)fRegMap->getPhysicalAddress(),
	      (unsigned long)fRegMap->getLength());

	for (i = 0; i < 6; i++)
		IOLog("RadeonNI: CRTC%u_CONTROL  %08lx\n", (unsigned)i,
		      (unsigned long)readReg(kCrtcControl[i]));
	IOLog("RadeonNI: CONFIG_MEMSIZE %08lx\n",
	      (unsigned long)readReg(CONFIG_MEMSIZE));
	IOLog("RadeonNI: MC_SEQ_MISC0   %08lx\n",
	      (unsigned long)readReg(MC_SEQ_MISC0));

	registerService();
	return true;
}

void RadeonNI::stop(IOService *provider)
{
	if (fRegMap) {
		fRegMap->release();
		fRegMap = 0;
	}
	fRegs = 0;
	IOLog("RadeonNI: stopped\n");
	super::stop(provider);
}
