/*
 * The accelerator's surface user client.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <IOKit/IOLib.h>

#include "RadeonNIAccel.h"
#include "RadeonNISurface.h"

#define super IOUserClient
OSDefineMetaClassAndStructors(RadeonNISurfaceClient, IOUserClient)

/* Enough of the log to see the pattern, not every frame for ever. */
#define MAX_NOTES 400

bool RadeonNISurfaceClient::start(IOService *provider)
{
	fAccel = OSDynamicCast(RadeonNIAccel, provider);
	if (!fAccel || !super::start(provider))
		return false;
	IOLog("RadeonNI: surface client %p opened\n", this);
	return true;
}

IOReturn RadeonNISurfaceClient::clientClose(void)
{
	IOLog("RadeonNI: surface client %p (window %lu) closed after %lu calls\n",
	      this, (unsigned long)fWid, (unsigned long)fCalls);
	terminate();
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::clientDied(void)
{
	return clientClose();
}

void RadeonNISurfaceClient::note(const char *what, UInt32 a, UInt32 b, UInt32 c,
				 UInt32 d)
{
	if (fCalls++ < MAX_NOTES)
		IOLog("RadeonNI: surface %lu: %s(0x%lx, 0x%lx, 0x%lx, 0x%lx)\n",
		      (unsigned long)fWid, what, (unsigned long)a, (unsigned long)b,
		      (unsigned long)c, (unsigned long)d);
}

void RadeonNISurfaceClient::noteRegion(const char *what, IOAccelDeviceRegion *rgn,
				       IOByteCount size)
{
	if (fCalls < MAX_NOTES && rgn && size >= sizeof(IOAccelDeviceRegion))
		IOLog("RadeonNI: surface %lu:   %s region: %lu rects, bounds %d,%d %dx%d\n",
		      (unsigned long)fWid, what, (unsigned long)rgn->num_rects,
		      rgn->bounds.x, rgn->bounds.y, rgn->bounds.w, rgn->bounds.h);
}

IOExternalMethod *RadeonNISurfaceClient::getTargetAndMethodForIndex(
	IOService **target, UInt32 index)
{
	static const IOExternalMethod methods[kIOAccelNumSurfaceMethods] = {
		/* kIOAccelSurfaceReadLockOptions */
		{ 0, (IOMethod)&RadeonNISurfaceClient::readLockOptions,
		  kIOUCScalarIStructO, 1, sizeof(IOAccelSurfaceInformation) },
		/* kIOAccelSurfaceReadUnlockOptions */
		{ 0, (IOMethod)&RadeonNISurfaceClient::readUnlockOptions,
		  kIOUCScalarIScalarO, 1, 0 },
		/* kIOAccelSurfaceGetState */
		{ 0, (IOMethod)&RadeonNISurfaceClient::getState,
		  kIOUCScalarIScalarO, 0, 1 },
		/* kIOAccelSurfaceWriteLockOptions */
		{ 0, (IOMethod)&RadeonNISurfaceClient::writeLockOptions,
		  kIOUCScalarIStructO, 1, sizeof(IOAccelSurfaceInformation) },
		/* kIOAccelSurfaceWriteUnlockOptions */
		{ 0, (IOMethod)&RadeonNISurfaceClient::writeUnlockOptions,
		  kIOUCScalarIScalarO, 1, 0 },
		/* kIOAccelSurfaceRead */
		{ 0, (IOMethod)&RadeonNISurfaceClient::read,
		  kIOUCStructIStructO, sizeof(IOAccelSurfaceReadData), 0 },
		/* kIOAccelSurfaceSetShapeBacking */
		{ 0, (IOMethod)&RadeonNISurfaceClient::setShapeBacking,
		  kIOUCScalarIStructI, 4, 0xffffffff },
		/* kIOAccelSurfaceSetIDMode */
		{ 0, (IOMethod)&RadeonNISurfaceClient::setIDMode,
		  kIOUCScalarIScalarO, 2, 0 },
		/* kIOAccelSurfaceSetScale */
		{ 0, (IOMethod)&RadeonNISurfaceClient::setScale,
		  kIOUCScalarIStructI, 1, 0xffffffff },
		/* kIOAccelSurfaceSetShape */
		{ 0, (IOMethod)&RadeonNISurfaceClient::setShape,
		  kIOUCScalarIStructI, 2, 0xffffffff },
		/* kIOAccelSurfaceFlush */
		{ 0, (IOMethod)&RadeonNISurfaceClient::flush,
		  kIOUCScalarIScalarO, 2, 0 },
		/* kIOAccelSurfaceQueryLock */
		{ 0, (IOMethod)&RadeonNISurfaceClient::queryLock,
		  kIOUCScalarIScalarO, 0, 0 },
		/* kIOAccelSurfaceReadLock */
		{ 0, (IOMethod)&RadeonNISurfaceClient::readLock,
		  kIOUCScalarIStructO, 0, sizeof(IOAccelSurfaceInformation) },
		/* kIOAccelSurfaceReadUnlock */
		{ 0, (IOMethod)&RadeonNISurfaceClient::readUnlock,
		  kIOUCScalarIScalarO, 0, 0 },
		/* kIOAccelSurfaceWriteLock */
		{ 0, (IOMethod)&RadeonNISurfaceClient::writeLock,
		  kIOUCScalarIStructO, 0, sizeof(IOAccelSurfaceInformation) },
		/* kIOAccelSurfaceWriteUnlock */
		{ 0, (IOMethod)&RadeonNISurfaceClient::writeUnlock,
		  kIOUCScalarIScalarO, 0, 0 },
		/* kIOAccelSurfaceControl */
		{ 0, (IOMethod)&RadeonNISurfaceClient::control,
		  kIOUCScalarIScalarO, 2, 1 },
		/* kIOAccelSurfaceSetShapeBackingAndLength */
		{ 0, (IOMethod)&RadeonNISurfaceClient::setShapeBackingAndLength,
		  kIOUCScalarIStructI, 5, 0xffffffff },
	};

	if (index >= kIOAccelNumSurfaceMethods) {
		IOLog("RadeonNI: surface %lu: private method %lu asked for\n",
		      (unsigned long)fWid, (unsigned long)index);
		return 0;
	}
	*target = this;
	return (IOExternalMethod *)&methods[index];
}

/* Nothing can be locked yet: there is no surface memory behind this. */
IOReturn RadeonNISurfaceClient::readLockOptions(UInt32 options,
	IOAccelSurfaceInformation *info, IOByteCount *size)
{
	note("readLockOptions", options, 0, 0, 0);
	return kIOReturnUnsupported;
}

IOReturn RadeonNISurfaceClient::readUnlockOptions(UInt32 options)
{
	note("readUnlockOptions", options, 0, 0, 0);
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::getState(UInt32 *state)
{
	note("getState", 0, 0, 0, 0);
	*state = kIOAccelSurfaceStateIdleBit;
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::writeLockOptions(UInt32 options,
	IOAccelSurfaceInformation *info, IOByteCount *size)
{
	note("writeLockOptions", options, 0, 0, 0);
	return kIOReturnUnsupported;
}

IOReturn RadeonNISurfaceClient::writeUnlockOptions(UInt32 options)
{
	note("writeUnlockOptions", options, 0, 0, 0);
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::read(IOAccelSurfaceReadData *data, void *out,
				     IOByteCount inSize, IOByteCount *outSize)
{
	note("read", data ? data->x : 0, data ? data->y : 0, data ? data->w : 0,
	     data ? data->h : 0);
	return kIOReturnUnsupported;
}

IOReturn RadeonNISurfaceClient::setShapeBacking(UInt32 options, UInt32 fbIndex,
	UInt32 backing, UInt32 rowBytes, IOAccelDeviceRegion *rgn, IOByteCount size)
{
	note("setShapeBacking", options, fbIndex, backing, rowBytes);
	noteRegion("setShapeBacking", rgn, size);
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::setIDMode(UInt32 wid, UInt32 mode)
{
	fWid = wid;
	fMode = mode;
	note("setIDMode", wid, mode, 0, 0);
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::setScale(UInt32 options,
	IOAccelSurfaceScaling *scaling, IOByteCount size)
{
	note("setScale", options, size, 0, 0);
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::setShape(UInt32 options, UInt32 fbIndex,
	IOAccelDeviceRegion *rgn, IOByteCount size)
{
	note("setShape", options, fbIndex, size, 0);
	noteRegion("setShape", rgn, size);
	/* The structure is declared with one rectangle and holds num_rects. */
	if (rgn && size >= sizeof(IOAccelDeviceRegion) - sizeof(IOAccelBounds) &&
	    rgn->num_rects <= (size - (sizeof(IOAccelDeviceRegion) -
				       sizeof(IOAccelBounds))) / sizeof(IOAccelBounds))
		fAccel->setSurfaceRegion(fWid, rgn, rgn->num_rects);
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::flush(UInt32 fbMask, UInt32 options)
{
	note("flush", fbMask, options, 0, 0);
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::queryLock(void)
{
	note("queryLock", 0, 0, 0, 0);
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::readLock(IOAccelSurfaceInformation *info,
					 IOByteCount *size)
{
	note("readLock", 0, 0, 0, 0);
	return kIOReturnUnsupported;
}

IOReturn RadeonNISurfaceClient::readUnlock(void)
{
	note("readUnlock", 0, 0, 0, 0);
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::writeLock(IOAccelSurfaceInformation *info,
					  IOByteCount *size)
{
	note("writeLock", 0, 0, 0, 0);
	return kIOReturnUnsupported;
}

IOReturn RadeonNISurfaceClient::writeUnlock(void)
{
	note("writeUnlock", 0, 0, 0, 0);
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::control(UInt32 selector, UInt32 arg,
					UInt32 *result)
{
	note("control", selector, arg, 0, 0);
	*result = 0;
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::setShapeBackingAndLength(UInt32 options,
	UInt32 fbIndex, UInt32 backing, UInt32 rowBytes, UInt32 length,
	IOAccelDeviceRegion *rgn, IOByteCount size)
{
	note("setShapeBackingAndLength", options, fbIndex, backing, rowBytes);
	noteRegion("setShapeBackingAndLength", rgn, size);
	return kIOReturnSuccess;
}
