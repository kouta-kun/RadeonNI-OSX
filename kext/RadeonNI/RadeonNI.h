/*
 * Radeon HD 7570 ("Turks", Northern Islands) driver for Mac OS X 10.4 PPC.
 *
 * An unaccelerated IOFramebuffer: the hardware library in hw/ does the
 * work, this class presents it to IOGraphics. The display modes are the
 * detailed timings of the monitor's EDID, each at 8, 16 and 32 bits per
 * pixel. The cursor is drawn by IOGraphics in software.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RADEONNI_H
#define RADEONNI_H

#include <IOKit/graphics/IOFramebuffer.h>
#include <IOKit/pci/IOPCIDevice.h>

extern "C" {
#include "rdn_accel.h"
#include "rdn_card.h"
#include "rdn_cursor.h"
#include "rdn_i2c.h"
#include "rdn_mode.h"
#include "rdn_pattern.h"
}

class RadeonNIAccel;

class RadeonNI : public IOFramebuffer
{
	enum { kMaxModes = 4 };

	OSDeclareDefaultStructors(RadeonNI)

public:
	virtual bool start(IOService *provider);
	virtual void stop(IOService *provider);

	/* IOFramebuffer */
	virtual IOReturn enableController(void);
	virtual IODeviceMemory *getApertureRange(IOPixelAperture aperture);
	virtual IODeviceMemory *getVRAMRange(void);
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

	/* The hardware cursor, when the HWCursor property asks for it. */
	virtual IOReturn getAttribute(IOSelect attribute, UInt32 *value);
	virtual IOReturn setCursorImage(void *cursorImage);
	virtual IOReturn setCursorState(SInt32 x, SInt32 y, bool visible);

	/* Register access for the OS layer. */
	UInt32 readReg(UInt32 offset);
	void writeReg(UInt32 offset, UInt32 value);
	IOPCIDevice *device() { return fDevice; }

	/* For the accelerator. */
	struct rdn_card *card() { return &fCard; }
	struct rdn_os *os() { return &fOS; }
	volatile void *aperture();
	UInt32 apertureSize();
	IOMemoryDescriptor *apertureDescriptor();
	/* What is on screen now, at any depth; false before the first mode. */
	bool screen(struct rdn_fb *fb);
	/* The screen as a render target; false unless it is 32 bits deep. */
	bool selftestTarget(struct rdn_accel *accel,
			    struct rdn_selftest_target *target);

private:
	IOPCIDevice *fDevice;
	IOMemoryMap *fRegMap;
	IOMemoryMap *fFbMap;
	volatile UInt8 *fRegs;
	void *fBios;
	UInt32 fBiosSize;
	bool fCardReady;
	RadeonNIAccel *fAccel;
	bool fModeSet;

	struct rdn_os fOS;
	struct rdn_card fCard;

	/* The display's EDID and the modes taken from its detailed timings. */
	UInt8 fEdid[RDN_EDID_MAX_SIZE];
	int fEdidLen;
	/* DVI signalling even when the EDID asks for HDMI (boot-arg rdn_dvi=1). */
	bool fForceDVI;
	struct rdn_mode fModes[kMaxModes];
	UInt32 fModeCount;
	UInt32 fSurfaceBytes;

	/* The hardware cursor: what was last asked for. */
	bool fHWCursor, fCursorLoaded, fCursorVisible, fCursorLogged;
	SInt32 fCursorX, fCursorY;
	UInt32 fCursorData[RDN_CURSOR_SIZE * RDN_CURSOR_SIZE];

	/* What is on screen now. */
	IODisplayModeID fCurrentMode;
	IOIndex fCurrentDepth;
	struct rdn_fb fFb;

	/* Colour table for 8 bpp, and the gamma ramp for the direct depths. */
	struct rdn_lut_entry fClut[256];
	struct rdn_lut_entry fGamma[256];

	bool loadBios();
	bool bringUp();
	const struct rdn_mode *modeForID(IODisplayModeID id);
	void describeFb(const struct rdn_mode *mode, IOIndex depth,
			struct rdn_fb *fb);
	IOReturn programMode(IODisplayModeID id, IOIndex depth);
	bool useHDMI();
	void loadColors();
	void cleanUp();
};

#endif /* RADEONNI_H */
