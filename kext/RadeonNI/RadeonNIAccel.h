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

extern "C" {
#include "rdn_accel.h"
#include "rdn_mem.h"
#include "rdn_user.h"
}

class RadeonNI;

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

private:
	RadeonNI *fFramebuffer;
	IOLock *fLock;
	struct rdn_accel fAccel;
	struct rdn_mem fMem;
	void *fPfp, *fMe;
	bool fEngineUp;

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

private:
	RadeonNIAccel *fAccel;
	/* What this client allocated, so that it can be freed when it goes. */
	UInt32 *fOffsets;
	UInt32 fCount;

	void freeAll(void);
};

#endif /* RADEONNIACCEL_H */
