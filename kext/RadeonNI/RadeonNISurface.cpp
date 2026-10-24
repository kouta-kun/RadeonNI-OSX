/*
 * The accelerator's surface user client.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <IOKit/IOLib.h>
#include <libkern/libkern.h>
#include <stdarg.h>
#include <stddef.h>

#include "RadeonNIAccel.h"
#include "RadeonNISurface.h"

#define super IOUserClient
OSDefineMetaClassAndStructors(RadeonNISurfaceClient, IOUserClient)

/* Enough of the log to see the pattern, not every frame for ever. */
#define MAX_NOTES 400

/* The counts as text: room for every table full. */
#define COUNTS_TEXT 2048

/* In the order of eIOAccelSurfaceMethods. */
static const char *const methodNames[kIOAccelNumSurfaceMethods] = {
	"readLockOptions", "readUnlockOptions", "getState", "writeLockOptions",
	"writeUnlockOptions", "read", "setShapeBacking", "setIDMode", "setScale",
	"setShape", "flush", "queryLock", "readLock", "readUnlock", "writeLock",
	"writeUnlock", "control", "setShapeBackingAndLength",
};

/* Add to the text at *len, never beyond size; what does not fit is left out. */
static void append(char *text, UInt32 size, UInt32 *len, const char *fmt, ...)
{
	va_list ap;

	if (*len + 1 >= size)
		return;
	va_start(ap, fmt);
	vsnprintf(text + *len, size - *len, fmt, ap);
	va_end(ap);
	while (text[*len])
		(*len)++;
}

bool RadeonNISurfaceClient::initWithTask(task_t owningTask, void *securityID,
					 UInt32 type)
{
	if (!super::initWithTask(owningTask, securityID, type))
		return false;
	fTask = owningTask;
	fBackingLock = IOLockAlloc();
	return fBackingLock != NULL;
}

/*
 * Reading a surface: its owner has told the accelerator where the picture
 * is in video memory (RDN_UC_SURFACE_BUFFER); that part of the aperture is
 * mapped, read-only, into the task that asks. The window server does this
 * when it has to draw a program's OpenGL window itself.
 */
IOReturn RadeonNISurfaceClient::lockForRead(IOAccelSurfaceInformation *info,
					    IOByteCount *size)
{
	UInt32 offset, rowBytes, width, height, start, length;
	uint64_t end;
	IOReturn ret;
	IOMemoryDescriptor *aperture;

	if (!info || !size || *size < sizeof(*info))
		return kIOReturnBadArgument;
	if (!fAccel->getSurfaceBuffer(fWid, &offset, &rowBytes, &width, &height))
		return kIOReturnUnsupported;
	aperture = fAccel->apertureMemory();
	if (!aperture)
		return kIOReturnNotReady;
	end = (uint64_t)offset + (uint64_t)rowBytes * height;
	if (!width || !height || (uint64_t)width * 4 > rowBytes ||
	    end > aperture->getLength() || end > 0xffffffffULL - PAGE_SIZE + 1)
		return kIOReturnBadArgument;
	ret = fAccel->waitForCPU();
	if (ret != kIOReturnSuccess)
		return ret;
	unlockRead();
	start = offset & ~(UInt32)(PAGE_SIZE - 1);
	length = ((UInt32)(end + PAGE_SIZE - 1) & ~(UInt32)(PAGE_SIZE - 1)) - start;
	if ((uint64_t)start + length > aperture->getLength())
		return kIOReturnBadArgument;
	fReadMap = aperture->map(fTask, 0, kIOMapAnywhere | kIOMapReadOnly,
				 start, length);
	if (!fReadMap)
		return kIOReturnNoMemory;
	bzero(info, sizeof(*info));
	info->address[0] = fReadMap->getVirtualAddress() + (offset - start);
	info->rowBytes = rowBytes;
	info->width = width;
	info->height = height;
	/*
	 * The window server reads this as a surface colour depth
	 * (kIOAccelSurfaceModeColorDepth8888); anything it does not know
	 * makes it give the surface up and draw white.
	 */
	info->pixelFormat = kIOAccelSurfaceModeColorDepth8888;
	fAccel->setReadLocked(fWid);
	*size = sizeof(*info);
	return kIOReturnSuccess;
}

void RadeonNISurfaceClient::unlockRead(void)
{
	if (fAccel && fAccel->readLocked() == fWid)
		fAccel->setReadLocked(0);
	if (fReadMap) {
		fReadMap->release();
		fReadMap = 0;
	}
}

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
	publishCounts(true);
    if (fAccel) fAccel->clearSurfaceBackingOwner(fWid, this);
	unlockRead();
	IOLockLock(fBackingLock);
	releaseBacking();
	IOLockUnlock(fBackingLock);
	/*
	 * The window server's and the program's clients share the surface
	 * by its ID; only when the window server lets go is it forgotten.
	 */
	if (fAccel && fWid && (fSetShape || fBackingMode))
		fAccel->forgetSurface(fWid);
	terminate();
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::clientDied(void)
{
	return clientClose();
}

void RadeonNISurfaceClient::note(UInt32 method, UInt32 a, UInt32 b, UInt32 c,
				 UInt32 d)
{
	UInt32 n;

	fMethodCalls[method]++;
	if (fCalls++ < MAX_NOTES)
		IOLog("RadeonNI: surface %lu: %s(0x%lx, 0x%lx, 0x%lx, 0x%lx)\n",
		      (unsigned long)fWid, methodNames[method], (unsigned long)a,
		      (unsigned long)b, (unsigned long)c, (unsigned long)d);
	/*
	 * The properties are also written here, in case nothing reads them
	 * the way serializeProperties() sees: at every power of two calls up
	 * to 256, then every 256.
	 */
	n = fCalls;
	if (n <= 256 ? !(n & (n - 1)) : !(n & 255))
		publishCounts(false);
}

void RadeonNISurfaceClient::noteRegion(UInt32 method, IOAccelDeviceRegion *rgn,
				       IOByteCount size)
{
	if (fCalls < MAX_NOTES && rgn && size >= sizeof(IOAccelDeviceRegion))
		IOLog("RadeonNI: surface %lu:   %s region: %lu rects, bounds %d,%d %dx%d\n",
		      (unsigned long)fWid, methodNames[method],
		      (unsigned long)rgn->num_rects,
		      rgn->bounds.x, rgn->bounds.y, rgn->bounds.w, rgn->bounds.h);
}

/*
 * The tables only grow, and a count is one word: two threads of a client
 * in here at once can lose a count or enter a value twice, nothing worse.
 */
void RadeonNISurfaceClient::countControl(UInt32 selector, UInt32 arg)
{
	UInt32 i, j, n = fControlCount;

	for (i = 0; i < n && fControl[i].selector != selector; i++)
		;
	if (i == n) {
		if (n >= kControlSelectors) {
			fControlOther++;
			return;
		}
		fControl[i].selector = selector;
		fControlCount = n + 1;
	}
	fControl[i].calls++;
	n = fControl[i].argCount;
	for (j = 0; j < n && fControl[i].args[j].arg != arg; j++)
		;
	if (j == n) {
		if (n >= kControlArgs) {
			fControl[i].otherArgs++;
			return;
		}
		fControl[i].args[j].arg = arg;
		fControl[i].argCount = n + 1;
	}
	fControl[i].args[j].calls++;
}

void RadeonNISurfaceClient::countFlush(UInt32 mask, UInt32 options)
{
	UInt32 i, n = fFlushCount;

	for (i = 0; i < n && (fFlush[i].mask != mask || fFlush[i].options != options); i++)
		;
	if (i == n) {
		if (n >= kFlushKinds) {
			fFlushOther++;
			return;
		}
		fFlush[i].mask = mask;
		fFlush[i].options = options;
		fFlushCount = n + 1;
	}
	fFlush[i].calls++;
}

/*
 * Write the counts as three strings, into the registry and, when the
 * client closes, into the log. Nothing is done when nothing was counted
 * since the last time.
 */
void RadeonNISurfaceClient::publishCounts(bool log)
{
	UInt32 all = fCalls + fPrivateAsked, len, i, j;
	char *text;

	if (!all || (!log && all == fPublished))
		return;
	text = (char *)IOMalloc(COUNTS_TEXT);
	if (!text)
		return;
	fPublished = all;

	len = 0;
	text[0] = 0;
	append(text, COUNTS_TEXT, &len, "window %lu: %lu in all",
	       (unsigned long)fWid, (unsigned long)fCalls);
	for (i = 0; i < kIOAccelNumSurfaceMethods; i++)
		if (fMethodCalls[i])
			append(text, COUNTS_TEXT, &len, ", %s %lu", methodNames[i],
			       (unsigned long)fMethodCalls[i]);
	if (fPrivateAsked)
		append(text, COUNTS_TEXT, &len, ", private methods asked for %lu",
		       (unsigned long)fPrivateAsked);
	setProperty("RadeonNICalls", text);
	if (log)
		IOLog("RadeonNI: surface calls: %s\n", text);

	if (fControlCount) {
		len = 0;
		text[0] = 0;
		append(text, COUNTS_TEXT, &len, "window %lu:", (unsigned long)fWid);
		for (i = 0; i < fControlCount; i++) {
			append(text, COUNTS_TEXT, &len, "%s selector %lu (%lu):",
			       i ? ";" : "", (unsigned long)fControl[i].selector,
			       (unsigned long)fControl[i].calls);
			for (j = 0; j < fControl[i].argCount; j++)
				append(text, COUNTS_TEXT, &len, "%s 0x%lx %lu",
				       j ? "," : "",
				       (unsigned long)fControl[i].args[j].arg,
				       (unsigned long)fControl[i].args[j].calls);
			if (fControl[i].otherArgs)
				append(text, COUNTS_TEXT, &len, ", other arguments %lu",
				       (unsigned long)fControl[i].otherArgs);
		}
		if (fControlOther)
			append(text, COUNTS_TEXT, &len, "; other selectors %lu",
			       (unsigned long)fControlOther);
		setProperty("RadeonNIControl", text);
		if (log)
			IOLog("RadeonNI: surface control: %s\n", text);
	}

	if (fFlushCount) {
		len = 0;
		text[0] = 0;
		append(text, COUNTS_TEXT, &len, "window %lu:", (unsigned long)fWid);
		for (i = 0; i < fFlushCount; i++)
			append(text, COUNTS_TEXT, &len, "%s mask 0x%lx options 0x%lx: %lu",
			       i ? ";" : "", (unsigned long)fFlush[i].mask,
			       (unsigned long)fFlush[i].options,
			       (unsigned long)fFlush[i].calls);
		if (fFlushOther)
			append(text, COUNTS_TEXT, &len, "; other %lu",
			       (unsigned long)fFlushOther);
		setProperty("RadeonNIFlush", text);
		if (log)
			IOLog("RadeonNI: surface flush: %s\n", text);
	}
	IOFree(text, COUNTS_TEXT);
}

/*
 * ioreg and IORegistryEntryCreateCFProperties() come through here, so the
 * counts they show are the counts of that moment.
 */
bool RadeonNISurfaceClient::serializeProperties(OSSerialize *s) const
{
	((RadeonNISurfaceClient *)this)->publishCounts(false);
	return super::serializeProperties(s);
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
		fPrivateAsked++;
		IOLog("RadeonNI: surface %lu: private method %lu asked for\n",
		      (unsigned long)fWid, (unsigned long)index);
		return 0;
	}
	*target = this;
	return (IOExternalMethod *)&methods[index];
}

/* Backed surfaces synchronize the caller's buffer with GPU storage. */
IOReturn RadeonNISurfaceClient::readLockOptions(UInt32 options,
	IOAccelSurfaceInformation *info, IOByteCount *size)
{
	IOReturn ret = fBackingMode ? lockBacking(info, size) : lockForRead(info, size);

	note(kIOAccelSurfaceReadLockOptions, options, ret, 0, 0);
	return ret;
}

IOReturn RadeonNISurfaceClient::readUnlockOptions(UInt32 options)
{
	note(kIOAccelSurfaceReadUnlockOptions, options, 0, 0, 0);
	unlockRead();
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::getState(UInt32 *state)
{
	note(kIOAccelSurfaceGetState, 0, 0, 0, 0);
	*state = kIOAccelSurfaceStateIdleBit;
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::writeLockOptions(UInt32 options,
	IOAccelSurfaceInformation *info, IOByteCount *size)
{
	note(kIOAccelSurfaceWriteLockOptions, options, 0, 0, 0);
	return fBackingMode ? lockBacking(info, size) : kIOReturnUnsupported;
}

IOReturn RadeonNISurfaceClient::writeUnlockOptions(UInt32 options)
{
	note(kIOAccelSurfaceWriteUnlockOptions, options, 0, 0, 0);
	if (!fBackingMode) return kIOReturnSuccess;
	IOLockLock(fBackingLock);
	IOReturn ret = syncBacking(false);
	unlockRead();
	IOLockUnlock(fBackingLock);
	return ret;
}

IOReturn RadeonNISurfaceClient::read(IOAccelSurfaceReadData *data, void *out,
				     IOByteCount inSize, IOByteCount *outSize)
{
	note(kIOAccelSurfaceRead, data ? data->x : 0, data ? data->y : 0, data ? data->w : 0,
	     data ? data->h : 0);
	return kIOReturnUnsupported;
}

IOReturn RadeonNISurfaceClient::setShapeBacking(UInt32 options, UInt32 fbIndex,
	UInt32 backing, UInt32 rowBytes, IOAccelDeviceRegion *rgn, IOByteCount size)
{
	note(kIOAccelSurfaceSetShapeBacking, options, fbIndex, backing, rowBytes);
	noteRegion(kIOAccelSurfaceSetShapeBacking, rgn, size);
	return configureBacking(options, fbIndex, backing, rowBytes, 0, rgn, size);
}

IOReturn RadeonNISurfaceClient::setIDMode(UInt32 wid, UInt32 mode)
{
	bool backing = fAccel->getProperty("BackingSurfaces") == kOSBooleanTrue &&
        fAccel->surfaceGpuReady();

	/*
	 * Surface 1 is the window server's screen compositor, not an app buffer; it is let through
	 * the gate only with BackingSurfaces. Without that key (the installers' default) the gate
	 * refuses every surface that is not a GL one, surface 1 included, as it always did: letting
	 * it through alone made the window server composite GL windows (QuickTime Player's video)
	 * from the wrong data on 10.5, bisected on the G5 (2026-10-22).
	 */
	if (fAccel->surfaceGate() && (wid != 1 || !backing) && !fAccel->isGLSurface(wid)) {
		if (mode != (kIOAccelSurfaceModeWindowedBit | kIOAccelSurfaceModeColorDepth8888) || !backing) {
			fRefused = true;
			note(kIOAccelSurfaceSetIDMode, wid, mode, 1, 0);
			return kIOReturnUnsupported;
		}
		fBackingMode = true;
	}
	fWid = wid;
	fMode = mode;
	note(kIOAccelSurfaceSetIDMode, wid, mode, 0, 0);
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::setScale(UInt32 options,
	IOAccelSurfaceScaling *scaling, IOByteCount size)
{
	note(kIOAccelSurfaceSetScale, options, size, 0, 0);
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::setShape(UInt32 options, UInt32 fbIndex,
	IOAccelDeviceRegion *rgn, IOByteCount size)
{
	note(kIOAccelSurfaceSetShape, options, fbIndex, size, 0);
	noteRegion(kIOAccelSurfaceSetShape, rgn, size);
	if (fRefused)
		return kIOReturnUnsupported;
	const UInt32 header = offsetof(IOAccelDeviceRegion, rect);
	if (!rgn || size < header ||
	    rgn->num_rects > (size - header) / sizeof(IOAccelBounds))
		return kIOReturnBadArgument;
	if (!fAccel->setSurfaceRegion(fWid, rgn, rgn->num_rects))
		return kIOReturnNoResources;
	fSetShape = true;
	return kIOReturnSuccess;
}

/*
 * The window server's way of showing a surface's new picture when it need
 * not draw the window again itself. There is one screen: bit 0 of the mask.
 */
IOReturn RadeonNISurfaceClient::flush(UInt32 fbMask, UInt32 options)
{
	IOReturn ret = kIOReturnSuccess;

	if (fBackingMode) {
		IOLockLock(fBackingLock);
		ret = syncBacking(true);
		IOLockUnlock(fBackingLock);
	} else if (fAccel && fWid && (fbMask & 1))
		ret = fAccel->flushSurface(fWid);
	countFlush(fbMask, options);
	note(kIOAccelSurfaceFlush, fbMask, options, ret, 0);
	return ret;
}

IOReturn RadeonNISurfaceClient::queryLock(void)
{
	note(kIOAccelSurfaceQueryLock, 0, 0, 0, 0);
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::readLock(IOAccelSurfaceInformation *info,
					 IOByteCount *size)
{
	IOReturn ret = fBackingMode ? lockBacking(info, size) : lockForRead(info, size);

	note(kIOAccelSurfaceReadLock, ret, 0, 0, 0);
	return ret;
}

IOReturn RadeonNISurfaceClient::readUnlock(void)
{
	note(kIOAccelSurfaceReadUnlock, 0, 0, 0, 0);
	unlockRead();
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::writeLock(IOAccelSurfaceInformation *info,
					  IOByteCount *size)
{
	return writeLockOptions(kIOAccelSurfaceLockInBacking, info, size);
}

IOReturn RadeonNISurfaceClient::writeUnlock(void)
{
	return writeUnlockOptions(kIOAccelSurfaceLockInBacking);
}

IOReturn RadeonNISurfaceClient::control(UInt32 selector, UInt32 arg,
					UInt32 *result)
{
	countControl(selector, arg);
	note(kIOAccelSurfaceControl, selector, arg, 0, 0);
	*result = 0;
	if (fBackingMode) {
		if (selector != 1 && selector != 4 && selector != 5)
			return kIOReturnUnsupported;
		/* The backing is pinned and retained; making it nonvolatile synchronizes it. */
		if (selector == 4 || selector == 5) *result = 1;
		if (selector == 1 || (selector == 5 && !arg)) {
			IOLockLock(fBackingLock);
			IOReturn ret = syncBacking(true);
			IOLockUnlock(fBackingLock);
			return ret;
		}
	}
	return kIOReturnSuccess;
}

IOReturn RadeonNISurfaceClient::setShapeBackingAndLength(UInt32 options,
	UInt32 fbIndex, UInt32 backing, UInt32 rowBytes, UInt32 length,
	IOAccelDeviceRegion *rgn, IOByteCount size)
{
	note(kIOAccelSurfaceSetShapeBackingAndLength, options, fbIndex, backing, rowBytes);
	noteRegion(kIOAccelSurfaceSetShapeBackingAndLength, rgn, size);
	return configureBacking(options, fbIndex, backing, rowBytes, length, rgn, size);
}

void RadeonNISurfaceClient::releaseBacking(void)
{
    if (fBackingGpuBound) {
        if (fBackingDmaFailed) fAccel->retainClientMemory(0, 0, this);
        else fAccel->gartUnbind(fBackingGart, this);
        fBackingGpuBound = false;
    }
	if (fBackingMap) { fBackingMap->release(); fBackingMap = NULL; }
	if (fBackingMemory) {
		fBackingMemory->complete();
		fBackingMemory->release();
		fBackingMemory = NULL;
	}
	fBackingAddress = fBackingStride = fBackingWidth = fBackingHeight = 0;
}

void RadeonNISurfaceClient::free(void)
{
    if (fAccel) fAccel->clearSurfaceBackingOwner(fWid, this);
	releaseBacking();
	if (fBackingLock) IOLockFree(fBackingLock);
	super::free();
}

IOReturn RadeonNISurfaceClient::configureBacking(UInt32 options, UInt32 fbIndex,
	UInt32 address, UInt32 stride, UInt32 length,
	IOAccelDeviceRegion *region, IOByteCount size)
{
	if (fRefused || !fWid || fWid == 1 ||
	    fMode != (kIOAccelSurfaceModeWindowedBit | kIOAccelSurfaceModeColorDepth8888))
		return kIOReturnUnsupported;
	if (fbIndex) return kIOReturnBadArgument;
	const UInt32 header = offsetof(IOAccelDeviceRegion, rect);
	if (!region || size < header ||
	    region->num_rects > (size - header) / sizeof(IOAccelBounds) ||
	    !address || region->bounds.w <= 0 || region->bounds.h <= 0 ||
	    region->bounds.w > 16384 || region->bounds.h > 16384 ||
	    stride < (UInt32)region->bounds.w * 4)
		return kIOReturnBadArgument;
	UInt64 bytes = (UInt64)stride * region->bounds.h;
	if (bytes > 0x7fffffffU || (UInt64)address + bytes > 0x100000000ULL ||
	    (length && bytes > length)) return kIOReturnBadArgument;
	/* WindowServer also publishes direct CPU edits by rebinding the backing. */
	IOLockLock(fBackingLock);
	bool unchanged = fBackingMap && fBackingAddress == address &&
		fBackingStride == stride && fBackingWidth == (UInt32)region->bounds.w &&
		fBackingHeight == (UInt32)region->bounds.h;
	if (unchanged) {
		IOReturn ret = kIOReturnNoResources;
		if (fAccel->setSurfaceRegion(fWid, region, region->num_rects))
			ret = (options & kIOAccelSurfaceShapeStaleBackingBit)
				? kIOReturnSuccess : syncBacking(false);
		IOLockUnlock(fBackingLock);
		return ret;
	}
	IOLockUnlock(fBackingLock);
	IOMemoryDescriptor *memory = IOMemoryDescriptor::withAddress(
		(vm_address_t)address, (IOByteCount)bytes, kIODirectionOutIn, fTask);
	if (!memory) return kIOReturnNoMemory;
	IOReturn ret = memory->prepare();
	if (ret != kIOReturnSuccess) { memory->release(); return ret; }
	IOMemoryMap *mapping = memory->map();
	if (!mapping) { memory->complete(); memory->release(); return kIOReturnNoMemory; }
	struct rdn_user_backing backing = {address, 0, stride,
		(UInt32)region->bounds.w, (UInt32)region->bounds.h};
	UInt32 gart = 0;
    UInt32 pageOffset = address & (PAGE_SIZE - 1);
    UInt32 pageBytes = (UInt32)((bytes + pageOffset + PAGE_SIZE - 1) & ~(UInt64)(PAGE_SIZE - 1));
    ret = fAccel->gartBind(fTask, address - pageOffset, pageBytes, this, &gart);
    if (ret != kIOReturnSuccess) {
        mapping->release(); memory->complete(); memory->release(); return ret;
    }
    bool adopted = false;
	IOLockLock(fBackingLock);
	ret = fBackingDmaFailed ? kIOReturnNotReady : fAccel->waitForCPU();
	if (ret == kIOReturnSuccess && !fAccel->setSurfaceBacking(fWid, fTask, &backing))
		ret = kIOReturnNoResources;
	if (ret == kIOReturnSuccess) {
		releaseBacking();
		adopted = true;
		fBackingMode = true;
		fBackingMemory = memory;
		fBackingMap = mapping;
		fBackingAddress = address;
		fBackingStride = stride;
		fBackingWidth = backing.width;
		fBackingHeight = backing.height;
        fBackingGart = gart;
        fBackingPageOffset = pageOffset;
        fBackingGpuBound = true;
        fAccel->setSurfaceBackingOwner(fWid, this);
		fSetShape = fAccel->setSurfaceRegion(fWid, region, region->num_rects);
		ret = fSetShape ? syncBacking(false) : kIOReturnNoResources;
	}
	IOLockUnlock(fBackingLock);
	if (!adopted) {
        fAccel->gartUnbind(gart, this);
		mapping->release(); memory->complete(); memory->release();
	}
	return ret;
}

IOReturn RadeonNISurfaceClient::syncBacking(bool toCPU)
{
    if (!fBackingMap || !fBackingGpuBound || fBackingDmaFailed) return kIOReturnNotReady;
    UInt64 ram = rdn_gart_addr(fBackingGart / PAGE_SIZE) + fBackingPageOffset;
    IOReturn result = fAccel->syncSurfaceGpu(fWid, ram, fBackingStride,
        fBackingWidth, fBackingHeight, toCPU);
    if (result == kIOReturnTimeout) fBackingDmaFailed = true;
    return result;
}

IOReturn RadeonNISurfaceClient::initializeGpu()
{
    IOLockLock(fBackingLock);
    IOReturn result = syncBacking(false);
    IOLockUnlock(fBackingLock);
    return result;
}

IOReturn RadeonNISurfaceClient::gpuInfo(struct rdn_user_surface_info *info)
{
    IOLockLock(fBackingLock);
    IOReturn result = kIOReturnNotReady;
    if (fBackingMap && fBackingGpuBound && !fBackingDmaFailed) {
        bzero(info, sizeof(*info));
        info->id = fWid; info->row_bytes = fBackingStride;
        info->width = fBackingWidth; info->height = fBackingHeight;
        result = kIOReturnSuccess;
    }
    IOLockUnlock(fBackingLock);
    return result;
}

IOReturn RadeonNISurfaceClient::gpuCopy(UInt32 offset, UInt32 stride,
    UInt32 width, UInt32 height)
{
    IOLockLock(fBackingLock);
    IOReturn result = kIOReturnNotReady;
    if (fBackingGpuBound && !fBackingDmaFailed && width == fBackingWidth && height == fBackingHeight) {
        UInt64 source = rdn_gart_addr(fBackingGart / PAGE_SIZE) + fBackingPageOffset;
        result = fAccel->surfaceGpuTransfer(source, fBackingStride,
            fAccel->surfaceVramAddress(offset), stride, width, height, fWid);
        if (result == kIOReturnTimeout) fBackingDmaFailed = true;
    }
    IOLockUnlock(fBackingLock);
    return result;
}

IOReturn RadeonNISurfaceClient::lockBacking(IOAccelSurfaceInformation *info,
                                           IOByteCount *size)
{
	if (!info || !size || *size < sizeof(*info)) return kIOReturnBadArgument;
	IOLockLock(fBackingLock);
	IOReturn ret = syncBacking(true);
	if (ret == kIOReturnSuccess) {
		bzero(info, sizeof(*info));
		info->address[0] = fBackingAddress;
		info->rowBytes = fBackingStride;
		info->width = fBackingWidth;
		info->height = fBackingHeight;
		info->pixelFormat = kIOAccelSurfaceModeColorDepth8888;
		*size = sizeof(*info);
		fAccel->setReadLocked(fWid);
	}
	IOLockUnlock(fBackingLock);
	return ret;
}
