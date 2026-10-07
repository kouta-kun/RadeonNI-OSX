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
	virtual bool initWithTask(task_t owningTask, void *securityID, UInt32 type);
	virtual bool start(IOService *provider);
	virtual IOReturn clientClose(void);
	virtual IOReturn clientDied(void);
	virtual IOExternalMethod *getTargetAndMethodForIndex(IOService **target,
							     UInt32 index);
	/* Brings the count properties up to date for whoever reads them. */
	virtual bool serializeProperties(OSSerialize *s) const;

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
	/* Whose client this is, and its view of the surface while read-locked. */
	task_t fTask;
	bool fSetShape;
	IOMemoryMap *fReadMap;

	IOReturn lockForRead(IOAccelSurfaceInformation *info, IOByteCount *size);
	void unlockRead(void);
	UInt32 fCalls;

	/* method: one of eIOAccelSurfaceMethods. */
	void note(UInt32 method, UInt32 a, UInt32 b, UInt32 c, UInt32 d);
	void noteRegion(UInt32 method, IOAccelDeviceRegion *rgn, IOByteCount size);

	/*
	 * What is asked of this client, counted for its whole life: every
	 * method, and for control and flush each distinct set of arguments,
	 * as far as the tables go (what does not fit is counted as "other").
	 * Shown as the properties RadeonNICalls, RadeonNIControl and
	 * RadeonNIFlush of this object (ioreg -c RadeonNISurfaceClient -l -w0)
	 * and logged when the client closes.
	 */
	enum { kControlSelectors = 8, kControlArgs = 4, kFlushKinds = 8 };
	UInt32 fMethodCalls[kIOAccelNumSurfaceMethods];
	UInt32 fPrivateAsked;
	struct {
		UInt32 selector, calls, argCount, otherArgs;
		struct {
			UInt32 arg, calls;
		} args[kControlArgs];
	} fControl[kControlSelectors];
	UInt32 fControlCount, fControlOther;
	struct {
		UInt32 mask, options, calls;
	} fFlush[kFlushKinds];
	UInt32 fFlushCount, fFlushOther;
	/* fCalls + fPrivateAsked when the properties were last written. */
	UInt32 fPublished;

	void countControl(UInt32 selector, UInt32 arg);
	void countFlush(UInt32 mask, UInt32 options);
	void publishCounts(bool log);
};

#endif /* RADEONNISURFACE_H */
