/*
 * Radeon HD 7570 ("Turks", Northern Islands) driver for Mac OS X 10.4 PPC.
 *
 * An unaccelerated IOFramebuffer: the hardware library in hw/ does the
 * work, this class presents it to IOGraphics. One display mode for now, the
 * preferred timing of the monitor's EDID, at 32 bits per pixel.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RADEONNI_H
#define RADEONNI_H

#include <IOKit/graphics/IOFramebuffer.h>
#include <IOKit/pci/IOPCIDevice.h>

extern "C" {
#include "rdn_card.h"
#include "rdn_i2c.h"
#include "rdn_mode.h"
#include "rdn_pattern.h"
}

class RadeonNI : public IOFramebuffer
{
	OSDeclareDefaultStructors(RadeonNI)

public:
	virtual bool start(IOService *provider);
	virtual void stop(IOService *provider);

	/* IOFramebuffer */
	virtual IOReturn enableController(void);
	virtual IODeviceMemory *getApertureRange(IOPixelAperture aperture);
	virtual const char *getPixelFormats(void);
	virtual IOItemCount getDisplayModeCount(void);
	virtual IOReturn getDisplayModes(IODisplayModeID *allDisplayModes);
	virtual IOReturn getInformationForDisplayMode(IODisplayModeID displayMode,
		IODisplayModeInformation *info);
	virtual UInt64 getPixelFormatsForDisplayMode(IODisplayModeID displayMode,
		IOIndex depth);
	virtual IOReturn getPixelInformation(IODisplayModeID displayMode,
		IOIndex depth, IOPixelAperture aperture,
		IOPixelInformation *pixelInfo);
	virtual IOReturn getCurrentDisplayMode(IODisplayModeID *displayMode,
		IOIndex *depth);
	virtual IOReturn setDisplayMode(IODisplayModeID displayMode, IOIndex depth);
	virtual IOReturn getStartupDisplayMode(IODisplayModeID *displayMode,
		IOIndex *depth);
	virtual IOReturn setCLUTWithEntries(IOColorEntry *colors, UInt32 index,
		UInt32 numEntries, IOOptionBits options);
	virtual IOReturn setGammaTable(UInt32 channelCount, UInt32 dataCount,
		UInt32 dataWidth, void *data);
	virtual IOReturn getTimingInfoForDisplayMode(IODisplayModeID displayMode,
		IOTimingInformation *info);
	virtual IOItemCount getConnectionCount(void);
	virtual IOReturn setAttributeForConnection(IOIndex connectIndex,
		IOSelect attribute, UInt32 value);
	virtual IOReturn getAttributeForConnection(IOIndex connectIndex,
		IOSelect attribute, UInt32 *value);
	virtual IOReturn connectFlags(IOIndex connectIndex,
		IODisplayModeID displayMode, IOOptionBits *flags);
	virtual bool hasDDCConnect(IOIndex connectIndex);
	virtual IOReturn getDDCBlock(IOIndex connectIndex, UInt32 blockNumber,
		IOSelect blockType, IOOptionBits options, UInt8 *data,
		IOByteCount *length);

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
	bool fModeSet;

	struct rdn_os fOS;
	struct rdn_card fCard;

	/* The one mode: the display's preferred timing. */
	UInt8 fEdid[RDN_EDID_MAX_SIZE];
	int fEdidLen;
	struct rdn_mode fMode;
	struct rdn_fb fFb;

	bool loadBios();
	bool bringUp();
	IOReturn programMode();
	void cleanUp();
};

#endif /* RADEONNI_H */
