/*
 * Radeon HD 7570 ("Turks", Northern Islands) driver for Mac OS X 10.4 PPC.
 *
 * At this stage the class attaches to the card, brings it up with the
 * hardware library and shows the test pattern. It becomes the IOFramebuffer
 * subclass in the next step of milestone 3.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RADEONNI_H
#define RADEONNI_H

#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>

extern "C" {
#include "rdn_card.h"
#include "rdn_i2c.h"
#include "rdn_mode.h"
#include "rdn_pattern.h"
}

class RadeonNI : public IOService
{
	OSDeclareDefaultStructors(RadeonNI)

public:
	virtual bool start(IOService *provider);
	virtual void stop(IOService *provider);

	/* Register access for the OS layer. */
	UInt32 readReg(UInt32 offset);
	void writeReg(UInt32 offset, UInt32 value);
	IOPCIDevice *device() { return fDevice; }

private:
	IOPCIDevice *fDevice;
	IOMemoryMap *fRegMap;
	IOMemoryMap *fFbMap;
	volatile UInt8 *fRegs;
	void *fBios;
	UInt32 fBiosSize;
	bool fCardReady;

	struct rdn_os fOS;
	struct rdn_card fCard;

	bool loadBios();
	bool bringUp();
};

#endif /* RADEONNI_H */
