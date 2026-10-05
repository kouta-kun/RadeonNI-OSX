/*
 * The accelerator service of the Radeon HD 7570 driver, and its user
 * client.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <IOKit/IOLib.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>

#include "RadeonNI.h"
#include "RadeonNIAccel.h"
#include "RadeonNISurface.h"

OSDefineMetaClassAndStructors(RadeonNIAGPShim, IOAGPDevice)

/* No driver is to be matched against the shim. */
bool RadeonNIAGPShim::matchPropertyTable(OSDictionary *table, SInt32 *score)
{
	return false;
}

bool RadeonNIAGPShim::matchPropertyTable(OSDictionary *table)
{
	return false;
}

bool RadeonNIAGPShim::compareName(OSString *name, OSString **matched) const
{
	return false;
}

IOReturn RadeonNIAGPShim::getResources(void)
{
	return kIOReturnSuccess;
}

#define super IOAccelerator
OSDefineMetaClassAndStructors(RadeonNIAccel, IOAccelerator)

/* The bundle OpenGL loads from /System/Library/Extensions. */
#define GL_BUNDLE_NAME		"RadeonNIGLDriver"

/*
 * Video memory: the screen's surfaces at the bottom (the framebuffer
 * driver's), the ring at 32 MB, a megabyte of scratch space for the
 * self-test after it, and from 34 MB to the end of the aperture the heap
 * user clients allocate from.
 */
#define RING_OFFSET		(32u << 20)
#define RING_BYTES		(1u << 20)
#define SELFTEST_OFFSET		(33u << 20)
#define HEAP_OFFSET		(34u << 20)

RadeonNIAccel *RadeonNIAccel::withFramebuffer(RadeonNI *fb, IOService *provider)
{
	RadeonNIAccel *accel = new RadeonNIAccel;
	char path[512];
	int len = sizeof(path);

	IOService *parent = provider;

	if (!accel)
		return 0;
	if (!accel->init()) {
		accel->release();
		return 0;
	}
	OSNumber *shim = OSDynamicCast(OSNumber, fb->getProperty("AGPShim"));
	UInt32 which = shim ? shim->unsigned32BitValue() : 0;

	/* Bit 0: the ancestor. Bit 1: the registered shim. */
	if (which & 1) {
		OSDictionary *none = OSDictionary::withCapacity(1);

		accel->fAncestor = new IOAGPDevice;
		/*
		 * Into the registry only: IOPCIDevice::attach() expects a
		 * PCI bridge for a provider and panics on anything else.
		 */
		if (accel->fAncestor && accel->fAncestor->init(none) &&
		    accel->fAncestor->attachToParent(provider, gIOServicePlane)) {
			accel->fAncestor->setName("RadeonNIAGPAncestor");
			parent = accel->fAncestor;
		}
		if (none)
			none->release();
	}
	if (which & 2) {
		static const char model[] = "RadeonNI";
		OSDictionary *none = OSDictionary::withCapacity(1);

		accel->fShim = new RadeonNIAGPShim;
		if (accel->fShim && accel->fShim->init(none) &&
		    accel->fShim->attachToParent(provider, gIOServicePlane)) {
			accel->fShim->setName("RadeonNIAGPShim");
			accel->fShim->setProperty("model", (void *)model, sizeof(model));
			accel->fShim->registerService();
		}
		if (none)
			none->release();
	}
	if (!accel->attach(parent)) {
		accel->release();
		return 0;
	}
	accel->fFramebuffer = fb;
	accel->fLock = IOLockAlloc();
	accel->setName("RadeonNIAccel");
	accel->setProperty("IOMatchCategory", "IOAccelerator");
	accel->setProperty("IOGLBundleName", GL_BUNDLE_NAME);
	accel->setProperty(kIOAccelRevisionKey,
			   (UInt64)kCurrentGraphicsInterfaceRevision, 32);

	/*
	 * What the window server reads to decide about Quartz Extreme, as
	 * VMsvga2 sets it; only when the personality asks, while this is
	 * being found out.
	 */
	OSNumber *caps = OSDynamicCast(OSNumber, fb->getProperty("AccelCaps"));
	if (caps)
		accel->setProperty("AccelCaps", caps);

	accel->fSurfaces = fb->getProperty("Surfaces") == kOSBooleanTrue;
	accel->fEngineUp = accel->startEngine();
	accel->setProperty("RadeonNIEngine", accel->fEngineUp);

	/*
	 * The 2D accelerator plug-in the system loads for the framebuffer
	 * (ga/), when the personality asks for it.
	 */
	if (fb->getProperty("GAPlugin") == kOSBooleanTrue) {
		OSDictionary *types = OSDictionary::withCapacity(1);
		OSString *plugin = OSString::withCString("RadeonNIGA.plugin");

		if (types && plugin) {
			types->setObject("ACCF0000-0000-0000-0000-000a2789904e", plugin);
			fb->setProperty("IOCFPlugInTypes", types);
		}
		if (plugin)
			plugin->release();
		if (types)
			types->release();
	}

	/* The framebuffer points at its accelerator by registry path. */
	if (accel->getPath(path, &len, gIOServicePlane)) {
		fb->setProperty(kIOAccelTypesKey, path);
		fb->setProperty(kIOAccelIndexKey, (UInt64)0, 32);
		fb->setProperty(kIOAccelRevisionKey,
				(UInt64)kCurrentGraphicsInterfaceRevision, 32);
		IOLog("RadeonNI: accelerator at %s\n", path);
	}
	accel->registerService();
	return accel;
}

/* The microcode comes as data properties of the personality, like the VBIOS. */
void *RadeonNIAccel::copyFirmware(const char *key, UInt32 *size)
{
	OSData *data = OSDynamicCast(OSData, fFramebuffer->getProperty(key));
	void *copy;

	if (!data || !data->getLength())
		return 0;
	*size = data->getLength();
	copy = IOMalloc(*size);
	if (copy)
		bcopy(data->getBytesNoCopy(), copy, *size);
	return copy;
}

bool RadeonNIAccel::startEngine(void)
{
	struct rdn_accel_fw fw;
	UInt32 pfpSize = 0, meSize = 0, apertureSize = fFramebuffer->apertureSize();
	int r;

	fPfp = copyFirmware("FW_PFP", &pfpSize);
	fMe = copyFirmware("FW_ME", &meSize);
	if (!fPfp || !fMe) {
		IOLog("RadeonNI: no microcode in the personality; no 3D engine\n");
		return false;
	}
	fw.pfp = (const uint8_t *)fPfp;
	fw.pfp_size = pfpSize;
	fw.me = (const uint8_t *)fMe;
	fw.me_size = meSize;

	if (!fFramebuffer->aperture() || apertureSize <= HEAP_OFFSET)
		return false;
	r = rdn_accel_init(&fAccel, fFramebuffer->card(), fFramebuffer->aperture(),
			   apertureSize, &fw, RING_OFFSET, RING_BYTES,
			   RDN_BIG_ENDIAN);
	if (r) {
		IOLog("RadeonNI: the 3D engine did not start (%d)\n", r);
		return false;
	}
	if (rdn_mem_init(&fMem, fFramebuffer->os(), HEAP_OFFSET,
			 apertureSize - HEAP_OFFSET))
		return false;
	IOLog("RadeonNI: 3D engine up; %lu MB of video memory for clients\n",
	      (unsigned long)((apertureSize - HEAP_OFFSET) >> 20));

	if (fFramebuffer->getProperty("AccelSelfTest") == kOSBooleanTrue) {
		struct rdn_selftest_target t;

		if (fFramebuffer->selftestTarget(&fAccel, &t)) {
			r = rdn_accel_selftest(&fAccel, &t, SELFTEST_OFFSET);
			IOLog("RadeonNI: drawing self-test on %lux%lu returned %d\n",
			      (unsigned long)t.width, (unsigned long)t.height, r);
		} else {
			IOLog("RadeonNI: drawing self-test needs a 32-bit mode\n");
		}
	}
	return true;
}

void RadeonNIAccel::retire(IOService *provider)
{
	if (fFramebuffer) {
		fFramebuffer->removeProperty(kIOAccelTypesKey);
		fFramebuffer->removeProperty(kIOAccelIndexKey);
		fFramebuffer->removeProperty(kIOAccelRevisionKey);
	}
	terminate(kIOServiceRequired | kIOServiceSynchronous);
	if (fEngineUp) {
		IOLockLock(fLock);
		rdn_accel_fini(&fAccel);
		rdn_mem_fini(&fMem);
		fEngineUp = false;
		IOLockUnlock(fLock);
	}
	if (fAncestor) {
		detach(fAncestor);
		fAncestor->detachFromParent(provider, gIOServicePlane);
		fAncestor->release();
		fAncestor = 0;
	} else {
		detach(provider);
	}
	if (fShim) {
		fShim->terminate(kIOServiceRequired | kIOServiceSynchronous);
		fShim->detachFromParent(provider, gIOServicePlane);
		fShim->release();
		fShim = 0;
	}
	fFramebuffer = 0;
	if (fLock) {
		IOLockFree(fLock);
		fLock = 0;
	}
}

IOReturn RadeonNIAccel::newUserClient(task_t owningTask, void *securityID,
				      UInt32 type, IOUserClient **handler)
{
	RadeonNIUserClient *client;

	/* The public surface client the window server asks for. */
	if (type == kIOAccelSurfaceClientType && fSurfaces) {
		RadeonNISurfaceClient *surface = new RadeonNISurfaceClient;

		if (!surface)
			return kIOReturnNoMemory;
		if (!surface->initWithTask(owningTask, securityID, type) ||
		    !surface->attach(this)) {
			surface->release();
			return kIOReturnError;
		}
		if (!surface->start(this)) {
			surface->detach(this);
			surface->release();
			return kIOReturnError;
		}
		*handler = surface;
		return kIOReturnSuccess;
	}
	if (type != RDN_UC_TYPE) {
		IOLog("RadeonNI: accelerator user client type %lu requested, not ours\n",
		      (unsigned long)type);
		return kIOReturnUnsupported;
	}
	if (!fEngineUp)
		return kIOReturnNotReady;

	client = new RadeonNIUserClient;
	if (!client)
		return kIOReturnNoMemory;
	if (!client->initWithTask(owningTask, securityID, type) ||
	    !client->attach(this)) {
		client->release();
		return kIOReturnError;
	}
	if (!client->start(this)) {
		client->detach(this);
		client->release();
		return kIOReturnError;
	}
	*handler = client;
	return kIOReturnSuccess;
}

void RadeonNIAccel::getInfo(struct rdn_user_info *info)
{
	struct rdn_fb fb;

	bzero(info, sizeof(*info));
	IOLockLock(fLock);
	info->version = RDN_USER_VERSION;
	info->pci_device_id = 0x675d;
	info->vram_gpu_base_hi = (uint32_t)(fAccel.vram_base >> 32);
	info->vram_gpu_base_lo = (uint32_t)fAccel.vram_base;
	info->aperture_size = fAccel.aperture_size;
	info->heap_offset = HEAP_OFFSET;
	info->heap_size = fAccel.aperture_size - HEAP_OFFSET;
	info->tile_config = fAccel.cfg.tile_config;
	info->backend_map = fAccel.cfg.backend_map;
	info->max_backends = fAccel.cfg.max_backends;
	info->max_tile_pipes = fAccel.cfg.max_tile_pipes;
	info->max_pipes = fAccel.cfg.max_pipes;
	info->num_ses = fAccel.cfg.num_ses;
	if (fFramebuffer && fFramebuffer->screen(&fb)) {
		info->fb_offset = fb.aperture_offset;
		info->fb_width = fb.width;
		info->fb_height = fb.height;
		info->fb_pitch_pixels = fb.pitch_pixels;
		info->fb_bits_per_pixel = fb.bpp;
	}
	IOLockUnlock(fLock);
}

IOReturn RadeonNIAccel::allocVram(UInt32 size, UInt32 align, UInt32 *offset)
{
	uint64_t off = 0;
	int r;

	if (!size || (align & (align - 1)))
		return kIOReturnBadArgument;
	if (align < 4096)
		align = 4096;
	IOLockLock(fLock);
	r = rdn_mem_alloc(&fMem, size, align, &off);
	IOLockUnlock(fLock);
	if (r)
		return kIOReturnNoMemory;
	*offset = (UInt32)off;
	return kIOReturnSuccess;
}

void RadeonNIAccel::freeVram(UInt32 offset)
{
	IOLockLock(fLock);
	rdn_mem_free(&fMem, offset);
	IOLockUnlock(fLock);
}

IOReturn RadeonNIAccel::submit(UInt32 offset, UInt32 words, UInt32 *fence)
{
	uint32_t seq = 0;
	int r;

	if (offset < HEAP_OFFSET || (offset & 3) || !words ||
	    words > (fAccel.aperture_size - offset) / 4)
		return kIOReturnBadArgument;
	IOLockLock(fLock);
	r = rdn_ib_submit(&fAccel, rdn_vram_addr(&fAccel, offset), words, &seq);
	IOLockUnlock(fLock);
	if (r)
		return kIOReturnIOError;
	*fence = seq;
	return kIOReturnSuccess;
}

/*
 * Polls, giving up the CPU between looks: there is no interrupt yet. A
 * timeout of zero only asks.
 */
bool RadeonNIAccel::fenceWait(UInt32 fence, UInt32 timeoutMs)
{
	UInt32 waited = 0;
	bool done;

	for (;;) {
		IOLockLock(fLock);
		done = rdn_fence_done(&fAccel, fence);
		if (done)
			rdn_hdp_flush(&fAccel);
		IOLockUnlock(fLock);
		if (done || waited >= timeoutMs)
			return done;
		IOSleep(1);
		waited++;
	}
}

void RadeonNIAccel::syncForCPU(void)
{
	IOLockLock(fLock);
	rdn_hdp_flush(&fAccel);
	IOLockUnlock(fLock);
}

void RadeonNIAccel::setSurfaceRegion(UInt32 wid, const IOAccelDeviceRegion *rgn,
				     UInt32 rects)
{
	int i, slot = -1;
	UInt32 r;

	IOLockLock(fLock);
	for (i = 0; i < kMaxSurfaces; i++) {
		if (fShapes[i].used && fShapes[i].wid == wid) {
			slot = i;
			break;
		}
		if (!fShapes[i].used && slot < 0)
			slot = i;
	}
	if (slot >= 0) {
		struct rdn_user_region *region = &fShapes[slot].region;

		fShapes[slot].used = true;
		fShapes[slot].wid = wid;
		region->count = rects;
		region->bounds[0] = rgn->bounds.x;
		region->bounds[1] = rgn->bounds.y;
		region->bounds[2] = rgn->bounds.w;
		region->bounds[3] = rgn->bounds.h;
		for (r = 0; r < rects && r < RDN_USER_REGION_RECTS; r++) {
			region->rects[r][0] = rgn->rect[r].x;
			region->rects[r][1] = rgn->rect[r].y;
			region->rects[r][2] = rgn->rect[r].w;
			region->rects[r][3] = rgn->rect[r].h;
		}
	}
	IOLockUnlock(fLock);
}

bool RadeonNIAccel::getSurfaceRegion(UInt32 wid, struct rdn_user_region *region)
{
	bool found = false;
	int i;

	IOLockLock(fLock);
	for (i = 0; i < kMaxSurfaces && !found; i++) {
		if (!fShapes[i].used || fShapes[i].wid != wid)
			continue;
		*region = fShapes[i].region;
		found = true;
	}
	IOLockUnlock(fLock);
	return found;
}

IOMemoryDescriptor *RadeonNIAccel::apertureMemory(void)
{
	return fFramebuffer ? fFramebuffer->apertureDescriptor() : 0;
}

/*
 * The user client
 */

#undef super
#define super IOUserClient
OSDefineMetaClassAndStructors(RadeonNIUserClient, IOUserClient)

bool RadeonNIUserClient::initWithTask(task_t owningTask, void *securityID,
				      UInt32 type)
{
	if (!super::initWithTask(owningTask, securityID, type))
		return false;
	fOffsets = (UInt32 *)IOMalloc(kMaxAllocations * sizeof(UInt32));
	fCount = 0;
	return fOffsets != 0;
}

bool RadeonNIUserClient::start(IOService *provider)
{
	fAccel = OSDynamicCast(RadeonNIAccel, provider);
	if (!fAccel || !super::start(provider))
		return false;
	return true;
}

void RadeonNIUserClient::freeAll(void)
{
	UInt32 i;

	if (!fAccel || !fOffsets)
		return;
	/* Whatever the client still has queued must be done with the memory. */
	fAccel->syncForCPU();
	for (i = 0; i < fCount; i++)
		fAccel->freeVram(fOffsets[i]);
	fCount = 0;
}

IOReturn RadeonNIUserClient::clientClose(void)
{
	freeAll();
	terminate();
	return kIOReturnSuccess;
}

IOReturn RadeonNIUserClient::clientDied(void)
{
	return clientClose();
}

void RadeonNIUserClient::stop(IOService *provider)
{
	freeAll();
	if (fOffsets) {
		IOFree(fOffsets, kMaxAllocations * sizeof(UInt32));
		fOffsets = 0;
	}
	super::stop(provider);
}

IOExternalMethod *RadeonNIUserClient::getTargetAndMethodForIndex(
	IOService **target, UInt32 index)
{
	static const IOExternalMethod methods[RDN_UC_METHOD_COUNT] = {
		{ 0, (IOMethod)&RadeonNIUserClient::methodGetInfo,
		  kIOUCScalarIStructO, 0, sizeof(struct rdn_user_info) },
		{ 0, (IOMethod)&RadeonNIUserClient::methodAlloc,
		  kIOUCScalarIScalarO, 2, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodFree,
		  kIOUCScalarIScalarO, 1, 0 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodSubmit,
		  kIOUCScalarIScalarO, 2, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodFenceWait,
		  kIOUCScalarIScalarO, 2, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodSyncForCPU,
		  kIOUCScalarIScalarO, 0, 0 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodSurfaceRegion,
		  kIOUCScalarIStructO, 1, sizeof(struct rdn_user_region) },
	};

	if (index >= RDN_UC_METHOD_COUNT)
		return 0;
	*target = this;
	return (IOExternalMethod *)&methods[index];
}

IOReturn RadeonNIUserClient::clientMemoryForType(UInt32 type,
						 IOOptionBits *options,
						 IOMemoryDescriptor **memory)
{
	IOMemoryDescriptor *aperture;

	if (type != RDN_UC_MEMORY_APERTURE || !fAccel)
		return kIOReturnBadArgument;
	aperture = fAccel->apertureMemory();
	if (!aperture)
		return kIOReturnNotReady;
	/* The caller releases what it is handed. */
	aperture->retain();
	*memory = aperture;
	return kIOReturnSuccess;
}

IOReturn RadeonNIUserClient::methodGetInfo(struct rdn_user_info *info,
					   IOByteCount *size)
{
	if (*size < sizeof(*info))
		return kIOReturnBadArgument;
	fAccel->getInfo(info);
	*size = sizeof(*info);
	return kIOReturnSuccess;
}

IOReturn RadeonNIUserClient::methodAlloc(UInt32 size, UInt32 align,
					 UInt32 *offset)
{
	IOReturn r;

	if (fCount >= kMaxAllocations)
		return kIOReturnNoResources;
	r = fAccel->allocVram(size, align, offset);
	if (r == kIOReturnSuccess)
		fOffsets[fCount++] = *offset;
	return r;
}

IOReturn RadeonNIUserClient::methodFree(UInt32 offset)
{
	UInt32 i;

	/* Only what this client allocated. */
	for (i = 0; i < fCount; i++)
		if (fOffsets[i] == offset) {
			fOffsets[i] = fOffsets[--fCount];
			fAccel->freeVram(offset);
			return kIOReturnSuccess;
		}
	return kIOReturnBadArgument;
}

IOReturn RadeonNIUserClient::methodSubmit(UInt32 offset, UInt32 words,
					  UInt32 *fence)
{
	return fAccel->submit(offset, words, fence);
}

IOReturn RadeonNIUserClient::methodFenceWait(UInt32 fence, UInt32 timeoutMs,
					     UInt32 *reached)
{
	*reached = fAccel->fenceWait(fence, timeoutMs) ? 1 : 0;
	return kIOReturnSuccess;
}

IOReturn RadeonNIUserClient::methodSyncForCPU(void)
{
	fAccel->syncForCPU();
	return kIOReturnSuccess;
}

IOReturn RadeonNIUserClient::methodSurfaceRegion(UInt32 wid,
	struct rdn_user_region *region, IOByteCount *size)
{
	if (*size < sizeof(*region))
		return kIOReturnBadArgument;
	if (!fAccel->getSurfaceRegion(wid, region))
		return kIOReturnNotFound;
	*size = sizeof(*region);
	return kIOReturnSuccess;
}
