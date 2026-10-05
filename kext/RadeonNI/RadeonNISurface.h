/*
 * The accelerator's surface user client: the public IOAccelSurface
 * interface (IOAccelSurfaceConnect.h) that the window server opens with
 * IOAccelCreateSurface.
 *
 * State of this file: it accepts every call, remembers what it is told
 * and logs it, so that what the window server does with a surface can be
 * learned (docs/QUARTZ-EXTREME.md). It does not put anything on screen.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RADEONNISURFACE_H
#define RADEONNISURFACE_H

#include <IOKit/IOUserClient.h>
#include <IOKit/graphics/IOAccelSurfaceConnect.h>

class RadeonNIAccel;

class RadeonNISurfaceClient : public IOUserClient
{
	OSDeclareDefaultStructors(RadeonNISurfaceClient)

public:
	virtual bool start(IOService *provider);
	virtual IOReturn clientClose(void);
	virtual IOReturn clientDied(void);
	virtual IOExternalMethod *getTargetAndMethodForIndex(IOService **target,
							     UInt32 index);

	/* The public methods, in the order of eIOAccelSurfaceMethods. */
	IOReturn readLockOptions(UInt32 options, IOAccelSurfaceInformation *info,
				 IOByteCount *size);
	IOReturn readUnlockOptions(UInt32 options);
	IOReturn getState(UInt32 *state);
	IOReturn writeLockOptions(UInt32 options, IOAccelSurfaceInformation *info,
				  IOByteCount *size);
	IOReturn writeUnlockOptions(UInt32 options);
	IOReturn read(IOAccelSurfaceReadData *data, void *out, IOByteCount inSize,
		      IOByteCount *outSize);
	IOReturn setShapeBacking(UInt32 options, UInt32 fbIndex, UInt32 backing,
				 UInt32 rowBytes, IOAccelDeviceRegion *rgn,
				 IOByteCount size);
	IOReturn setIDMode(UInt32 wid, UInt32 mode);
	IOReturn setScale(UInt32 options, IOAccelSurfaceScaling *scaling,
			  IOByteCount size);
	IOReturn setShape(UInt32 options, UInt32 fbIndex, IOAccelDeviceRegion *rgn,
			  IOByteCount size);
	IOReturn flush(UInt32 fbMask, UInt32 options);
	IOReturn queryLock(void);
	IOReturn readLock(IOAccelSurfaceInformation *info, IOByteCount *size);
	IOReturn readUnlock(void);
	IOReturn writeLock(IOAccelSurfaceInformation *info, IOByteCount *size);
	IOReturn writeUnlock(void);
	IOReturn control(UInt32 selector, UInt32 arg, UInt32 *result);
	IOReturn setShapeBackingAndLength(UInt32 options, UInt32 fbIndex,
					  UInt32 backing, UInt32 rowBytes,
					  UInt32 length, IOAccelDeviceRegion *rgn,
					  IOByteCount size);

private:
	RadeonNIAccel *fAccel;
	UInt32 fWid, fMode;
	UInt32 fCalls;

	void note(const char *what, UInt32 a, UInt32 b, UInt32 c, UInt32 d);
	void noteRegion(const char *what, IOAccelDeviceRegion *rgn, IOByteCount size);
};

#endif /* RADEONNISURFACE_H */
