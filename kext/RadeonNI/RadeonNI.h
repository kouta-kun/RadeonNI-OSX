/*
 * Radeon HD 7570 ("Turks", Northern Islands) driver for Mac OS X 10.4 PPC.
 *
 * At this stage the class is a probe: it attaches to the card, maps the
 * register BAR and reports what it reads. It becomes the IOFramebuffer
 * subclass in milestone 3.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RADEONNI_H
#define RADEONNI_H

#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>

class RadeonNI : public IOService
{
	OSDeclareDefaultStructors(RadeonNI)

public:
	virtual bool start(IOService *provider);
	virtual void stop(IOService *provider);

private:
	IOPCIDevice *fDevice;
	IOMemoryMap *fRegMap;
	volatile UInt8 *fRegs;

	UInt32 readReg(UInt32 offset);
};

#endif /* RADEONNI_H */
