/*
 * The accelerator service of the Radeon HD 7570 driver, and its user
 * client.
 *
 * The service names the OpenGL driver bundle and links the framebuffer to
 * itself the way OpenGL and the window server expect. It starts the 3D
 * engine and the command processor (hw/rdn_accel.h) on the framebuffer's
 * card and owns the allocator for the video memory behind the screen.
 * User space reaches all of that through RadeonNIUserClient (hw/rdn_user.h).
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RADEONNIACCEL_H
#define RADEONNIACCEL_H

#include <IOKit/IOLocks.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/graphics/IOAccelerator.h>
#include <IOKit/pci/IOAGPDevice.h>

#include <IOKit/graphics/IOAccelSurfaceConnect.h>

extern "C" {
#include "rdn_accel.h"
#include "rdn_mem.h"
#include "rdn_user.h"
}

class RadeonNI;

/*
 * What Tiger's window server wants to see before it considers compositing
 * with OpenGL (Quartz Extreme), found by reading its checks:
 *  - some registered service that is an IOAGPDevice, with a "model"
 *    property that does not start with "ATY,Rage128";
 *  - an object whose class name is exactly IOAGPDevice among the
 *    accelerator's ancestors in the registry (the stock configuration's
 *    GLCompositorRequiredClasses).
 * A PCI or PCI Express card has neither. RadeonNIAGPShim is the first: a
 * registered nub that no driver can match. The second is a bare
 * IOAGPDevice the accelerator is attached under; it is never registered,
 * so nothing ever probes or calls it.
 */
class RadeonNIAGPShim : public IOAGPDevice
{
	OSDeclareDefaultStructors(RadeonNIAGPShim)

public:
	/*
	 * Everything IOKit's generic code calls on a registered nub that
	 * IOPCIDevice would pass to its PCI bridge. The shim has none.
	 */
	virtual bool matchPropertyTable(OSDictionary *table, SInt32 *score);
	virtual bool matchPropertyTable(OSDictionary *table);
	virtual bool compareName(OSString *name, OSString **matched = 0) const;
	virtual IOReturn getResources(void);
};

class RadeonNIAccel : public IOAccelerator
{
	OSDeclareDefaultStructors(RadeonNIAccel)

public:
	/* Create, attach under the PCI device and publish. */
	static RadeonNIAccel *withFramebuffer(RadeonNI *fb, IOService *provider);
	void retire(IOService *provider);

	virtual IOReturn newUserClient(task_t owningTask, void *securityID,
				       UInt32 type, IOUserClient **handler);

	/* For the user client. All of these take the lock themselves. */
	void getInfo(struct rdn_user_info *info);
	IOReturn allocVram(UInt32 size, UInt32 align, UInt32 *offset);
	void freeVram(UInt32 offset);
	IOReturn submit(UInt32 offset, UInt32 words, UInt32 *fence);
	bool fenceWait(UInt32 fence, UInt32 timeoutMs);
	void syncForCPU(void);
	IOMemoryDescriptor *apertureMemory(void);
	/* The shapes of the window server's surfaces, by surface ID. */
	void setSurfaceRegion(UInt32 wid, const IOAccelDeviceRegion *rgn,
			      UInt32 rects);
	bool getSurfaceRegion(UInt32 wid, struct rdn_user_region *region);
	void forgetSurface(UInt32 wid);

private:
	RadeonNI *fFramebuffer;
	IOLock *fLock;
	struct rdn_accel fAccel;
	struct rdn_mem fMem;
	void *fPfp, *fMe;
	bool fEngineUp;
	/* Hand out surface clients (RadeonNISurface.h)? */
	bool fSurfaces;
	enum { kMaxSurfaces = 32 };
	struct {
		UInt32 wid;
		bool used;
		struct rdn_user_region region;
	} fShapes[kMaxSurfaces];
	/* See RadeonNIAGPShim. */
	IOAGPDevice *fAncestor;
	RadeonNIAGPShim *fShim;

	bool startEngine(void);
	void *copyFirmware(const char *key, UInt32 *size);
};

class RadeonNIUserClient : public IOUserClient
{
	enum { kMaxAllocations = 4096 };

	OSDeclareDefaultStructors(RadeonNIUserClient)

public:
	virtual bool initWithTask(task_t owningTask, void *securityID, UInt32 type);
	virtual bool start(IOService *provider);
	virtual IOReturn clientClose(void);
	virtual IOReturn clientDied(void);
	virtual void stop(IOService *provider);
	virtual IOExternalMethod *getTargetAndMethodForIndex(IOService **target,
							     UInt32 index);
	virtual IOReturn clientMemoryForType(UInt32 type, IOOptionBits *options,
					     IOMemoryDescriptor **memory);

	/* Methods, in the order of hw/rdn_user.h. */
	IOReturn methodGetInfo(struct rdn_user_info *info, IOByteCount *size);
	IOReturn methodAlloc(UInt32 size, UInt32 align, UInt32 *offset);
	IOReturn methodFree(UInt32 offset);
	IOReturn methodSubmit(UInt32 offset, UInt32 words, UInt32 *fence);
	IOReturn methodFenceWait(UInt32 fence, UInt32 timeoutMs, UInt32 *reached);
	IOReturn methodSyncForCPU(void);
	IOReturn methodSurfaceRegion(UInt32 wid, struct rdn_user_region *region,
				     IOByteCount *size);

private:
	RadeonNIAccel *fAccel;
	/* What this client allocated, so that it can be freed when it goes. */
	UInt32 *fOffsets;
	UInt32 fCount;

	void freeAll(void);
};

#endif /* RADEONNIACCEL_H */
