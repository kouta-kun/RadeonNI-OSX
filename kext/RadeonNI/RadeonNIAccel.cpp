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
 * driver's), the ring at 32 MB, a megabyte of scratch space after it (the
 * self-test's, then flushSurface()'s; after that, up to the heap, the work
 * areas of screenFill() and screenCopy()), and from 34 MB to the end of
 * the aperture the heap user clients allocate from.
 */
#define RING_OFFSET		(32u << 20)
#define RING_BYTES		(1u << 20)
#define SELFTEST_OFFSET		(33u << 20)
#define BLIT_OFFSET		(SELFTEST_OFFSET + (512u << 10))
/* RadeonNIAccel::kScreenSlots of these end 64 KB below the heap. */
#define SCREEN_OFFSET		(BLIT_OFFSET + (64u << 10))
#define SCREEN_SLOT_BYTES	(128u << 10)
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
	int r, keepBootClocks = 0, gart = 1;

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
	/*
	 * The card has more video memory than its aperture shows the CPU.
	 * The rest is for what only the GPU touches.
	 */
	if (fAccel.vram_size > apertureSize &&
	    !rdn_mem_init(&fHidden, fFramebuffer->os(), apertureSize,
			  fAccel.vram_size - apertureSize)) {
		fHiddenOffset = apertureSize;
		fHiddenSize = (UInt32)(fAccel.vram_size - apertureSize);
	}
	IOLog("RadeonNI: 3D engine up; %lu MB of video memory for clients, %lu MB more beyond the aperture\n",
	      (unsigned long)((apertureSize - HEAP_OFFSET) >> 20),
	      (unsigned long)(fHiddenSize >> 20));

	/*
	 * The GART: programs' own memory for what the CPU keeps writing, and
	 * somewhere to go when video memory is full. It tests itself and
	 * stays off if the GPU cannot run a command buffer from system
	 * memory. rdn_gart=0 as a boot argument does not start it.
	 */
	PE_parse_boot_arg("rdn_gart", &gart);
	if (gart)
		startGart();
	else
		IOLog("RadeonNI: GART: not started (rdn_gart=0)\n");

	/*
	 * ASIC_Init leaves the card at slow boot clocks. Go to the PowerPlay
	 * table's performance state: voltage and engine clock, and the
	 * memory clock if the memory controller's sequencer runs its
	 * microcode (without it the VBIOS's table does nothing).
	 * rdn_bootclocks=1 as a boot argument keeps the boot state,
	 * rdn_mclk=0 only the memory clock.
	 */
	fEngineUp = true;
	if (!PE_parse_boot_arg("rdn_bootclocks", &keepBootClocks) || !keepBootClocks) {
		UInt32 sclk = 0, mclk = 0, temperature = 0;
		UInt32 what = RDN_PM_VOLTAGE | RDN_PM_SCLK;
		int memoryClock = 1;

		if (!PE_parse_boot_arg("rdn_mclk", &memoryClock))
			memoryClock = 1;
		/* MC_SEQ_SUP_CNTL, RUN_MASK */
		if (memoryClock && (rdn_rreg(fAccel.card, 0x28c8) & 1))
			what |= RDN_PM_MCLK;
		power(RDN_UC_POWER_PERFORMANCE, what, &sclk, &mclk, &temperature);
		IOLog("RadeonNI: engine clock %lu0 kHz, memory clock %lu0 kHz\n",
		      sclk, mclk);
	}

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

/*
 * One page of system memory that stays where it is, and its address as
 * the card sees it on the bus (on a G5 that is not its physical address:
 * the memory descriptor goes through the machine's I/O mapper).
 */
static IOBufferMemoryDescriptor *gartPage(UInt32 *bus)
{
	IOBufferMemoryDescriptor *page =
		IOBufferMemoryDescriptor::withOptions(kIOMemoryPhysicallyContiguous,
						      RDN_GART_PAGE_SIZE,
						      RDN_GART_PAGE_SIZE);
	IOByteCount length = 0;

	if (!page)
		return 0;
	if (page->prepare() != kIOReturnSuccess) {
		page->release();
		return 0;
	}
	bzero(page->getBytesNoCopy(), RDN_GART_PAGE_SIZE);
	*bus = (UInt32)page->getPhysicalSegment(0, &length);
	return page;
}

void RadeonNIAccel::startGart(void)
{
	UInt32 dummyBus = 0, testBus = 0;
	uint64_t table = 0;
	int r;

	fGartDummy = gartPage(&dummyBus);
	fGartTest = gartPage(&testBus);
	if (!fGartDummy || !fGartTest || !dummyBus || !testBus ||
	    rdn_mem_alloc(&fMem, RDN_GART_TABLE_BYTES, RDN_GART_PAGE_SIZE, &table)) {
		IOLog("RadeonNI: GART: no memory for its pages or its table\n");
		return;
	}
	fGartTable = (UInt32)table;
	/* The card fetches the pages itself. */
	fFramebuffer->device()->setBusMasterEnable(true);

	IOLockLock(fLock);
	r = rdn_gart_enable(&fGart, &fAccel, fGartTable, dummyBus);
	if (!r) {
		rdn_gart_set_page(&fGart, 0, testBus);
		rdn_gart_flush(&fGart);
		r = rdn_ib_selftest(&fAccel, rdn_gart_addr(0),
				    (uint32_t *)fGartTest->getBytesNoCopy());
		if (r)
			rdn_gart_disable(&fGart);
	}
	/* Page 0 stays the test page; programs get the rest. */
	if (!r && rdn_mem_init(&fGartSpace, fFramebuffer->os(), RDN_GART_PAGE_SIZE,
			       (uint64_t)(RDN_GART_PAGES - 1) * RDN_GART_PAGE_SIZE)) {
		rdn_gart_disable(&fGart);
		r = -1;
	}
	IOLockUnlock(fLock);
	IOLog("RadeonNI: GART: table at 0x%lx, dummy page at bus 0x%lx, test page at bus 0x%lx: %s (%d)\n",
	      fGartTable, dummyBus, testBus,
	      r ? "FAILED, switched off" : "the GPU ran a command buffer from system memory", r);
}

void RadeonNIAccel::gartInfo(UInt32 *on, UInt32 *gpuStart, UInt32 *pages)
{
	*on = fGart.ready ? 1 : 0;
	*gpuStart = (UInt32)RDN_GART_GPU_START;
	*pages = RDN_GART_PAGES;
}

/*
 * Wire a piece of a program's memory and enter its pages in the table.
 * The memory stays the program's: no kernel address space is spent on it,
 * which on this 32-bit kernel would not reach far.
 */
IOReturn RadeonNIAccel::gartBind(task_t task, UInt32 address, UInt32 size,
				 void *owner, UInt32 *offset)
{
	IOMemoryDescriptor *memory;
	uint64_t at = 0;
	UInt32 slot, done;

	if (!fGart.ready)
		return kIOReturnNotReady;
	if (!size || (address & (RDN_GART_PAGE_SIZE - 1)) ||
	    (size & (RDN_GART_PAGE_SIZE - 1)))
		return kIOReturnBadArgument;
	memory = IOMemoryDescriptor::withAddress((vm_address_t)address, size,
						 kIODirectionOutIn, task);
	if (!memory)
		return kIOReturnNoMemory;
	if (memory->prepare() != kIOReturnSuccess) {
		memory->release();
		return kIOReturnVMError;
	}

	IOLockLock(fLock);
	for (slot = 0; slot < kMaxGartBindings && fGartBound[slot].memory; slot++)
		;
	if (slot == kMaxGartBindings ||
	    rdn_mem_alloc(&fGartSpace, size, RDN_GART_PAGE_SIZE, &at)) {
		IOLockUnlock(fLock);
		memory->complete();
		memory->release();
		return kIOReturnNoResources;
	}
	for (done = 0; done < size; done += RDN_GART_PAGE_SIZE) {
		IOByteCount length = 0;
		IOPhysicalAddress bus = memory->getPhysicalSegment(done, &length);

		if (!bus)
			break;
		rdn_gart_set_page(&fGart, (UInt32)((at + done) / RDN_GART_PAGE_SIZE), bus);
	}
	if (done < size) {
		/* A page without a bus address: take everything back. */
		while (done) {
			done -= RDN_GART_PAGE_SIZE;
			rdn_gart_clear_page(&fGart, (UInt32)((at + done) / RDN_GART_PAGE_SIZE));
		}
		rdn_gart_flush(&fGart);
		rdn_mem_free(&fGartSpace, at);
		IOLockUnlock(fLock);
		memory->complete();
		memory->release();
		return kIOReturnVMError;
	}
	rdn_gart_flush(&fGart);
	fGartBound[slot].memory = memory;
	fGartBound[slot].offset = (UInt32)at;
	fGartBound[slot].size = size;
	fGartBound[slot].owner = owner;
	IOLockUnlock(fLock);
	*offset = (UInt32)at;
	return kIOReturnSuccess;
}

IOReturn RadeonNIAccel::gartUnbind(UInt32 offset, void *owner)
{
	IOMemoryDescriptor *memory = 0;
	UInt32 slot, done;

	IOLockLock(fLock);
	for (slot = 0; slot < kMaxGartBindings; slot++)
		if (fGartBound[slot].memory && fGartBound[slot].offset == offset &&
		    fGartBound[slot].owner == owner)
			break;
	if (slot < kMaxGartBindings) {
		for (done = 0; done < fGartBound[slot].size; done += RDN_GART_PAGE_SIZE)
			rdn_gart_clear_page(&fGart, (offset + done) / RDN_GART_PAGE_SIZE);
		rdn_gart_flush(&fGart);
		rdn_mem_free(&fGartSpace, offset);
		memory = fGartBound[slot].memory;
		fGartBound[slot].memory = 0;
	}
	IOLockUnlock(fLock);
	if (!memory)
		return kIOReturnBadArgument;
	memory->complete();
	memory->release();
	return kIOReturnSuccess;
}

void RadeonNIAccel::gartUnbindAll(void *owner)
{
	UInt32 slot;

	for (slot = 0; slot < kMaxGartBindings; slot++)
		if (fGartBound[slot].memory && fGartBound[slot].owner == owner)
			gartUnbind(fGartBound[slot].offset, owner);
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
		if (fHiddenSize) {
			rdn_mem_fini(&fHidden);
			fHiddenSize = 0;
		}
		fMoveBytes = 0;
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

void RadeonNIAccel::hiddenInfo(UInt32 *offset, UInt32 *size)
{
	*offset = fHiddenOffset;
	*size = fHiddenSize;
}

IOReturn RadeonNIAccel::allocVram(UInt32 size, UInt32 align, UInt32 *offset,
				  bool hidden)
{
	uint64_t off = 0;
	int r;

	if (!size || (align & (align - 1)))
		return kIOReturnBadArgument;
	if (align < 4096)
		align = 4096;
	if (hidden && !fHiddenSize)
		return kIOReturnNoMemory;
	IOLockLock(fLock);
	r = rdn_mem_alloc(hidden ? &fHidden : &fMem, size, align, &off);
	IOLockUnlock(fLock);
	if (r)
		return kIOReturnNoMemory;
	*offset = (UInt32)off;
	return kIOReturnSuccess;
}

void RadeonNIAccel::freeVram(UInt32 offset)
{
	IOLockLock(fLock);
	if (fHiddenSize && offset >= fHiddenOffset)
		rdn_mem_free(&fHidden, offset);
	else
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

/*
 * Put the card in another power state, or only say what it runs at. The
 * lock keeps new command buffers out; what is already running gets half a
 * second to finish.
 */
IOReturn RadeonNIAccel::power(UInt32 state, UInt32 what, UInt32 *sclk,
			      UInt32 *mclk, UInt32 *temperature)
{
	struct rdn_card *card = fAccel.card;
	struct rdn_pm_state target;
	uint32_t s = 0, m = 0;
	int r = 0, tries;

	if (!fEngineUp || !card)
		return kIOReturnNotReady;
	IOLockLock(fLock);
	if (!fPowerKnown) {
		r = rdn_pm_boot_state(card, &fPower);
		fPowerKnown = !r;
	}
	if (!r && state != RDN_UC_POWER_QUERY) {
		if (state == RDN_UC_POWER_PERFORMANCE)
			r = rdn_pm_performance_state(card, &target);
		else if (state == RDN_UC_POWER_BOOT)
			r = rdn_pm_boot_state(card, &target);
		else
			r = -1;
		for (tries = 0; !r && tries < 500; tries++) {
			r = rdn_pm_set(card, &fPower, &target, what);
			if (r != -16)	/* -EBUSY: the GPU is drawing */
				break;
			r = 0;
			IOSleep(1);
		}
		if (tries == 500)
			r = -16;
		IOLog("RadeonNI: power state %lu (what 0x%lx): engine %lu0 kHz, memory %lu0 kHz, vddc %u mV, result %d\n",
		      state, what, (UInt32)target.sclk, (UInt32)target.mclk,
		      target.vddc, r);
		if (!r) {
			if (what & RDN_PM_VOLTAGE) {
				fPower.vddc = target.vddc;
				fPower.vddci = target.vddci;
			}
			if (what & RDN_PM_SCLK)
				fPower.sclk = target.sclk;
			if (what & RDN_PM_MCLK)
				fPower.mclk = target.mclk;
		}
	}
	rdn_pm_get_clocks(card, &s, &m);
	*sclk = s;
	*mclk = m;
	*temperature = (UInt32)(rdn_pm_temperature(card) + RDN_UC_TEMPERATURE_BIAS);
	IOLockUnlock(fLock);
	return r ? kIOReturnIOError : kIOReturnSuccess;
}

IOReturn RadeonNIAccel::regRead(UInt32 offset, UInt32 *value)
{
	struct rdn_card *card = fAccel.card;

	if (!card || (offset & 3) || offset >= RDN_MMIO_SIZE)
		return kIOReturnBadArgument;
	IOLockLock(fLock);
	*value = rdn_rreg(card, offset);
	IOLockUnlock(fLock);
	return kIOReturnSuccess;
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

		if (!fShapes[slot].used)
			fShapes[slot].bufWidth = 0;
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

void RadeonNIAccel::setSurfaceBuffer(UInt32 wid, UInt32 offset, UInt32 rowBytes,
				     UInt32 width, UInt32 height)
{
	int i, slot = -1;

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
		/* The owner may be first: the shape then comes later. */
		if (!fShapes[slot].used) {
			fShapes[slot].used = true;
			fShapes[slot].wid = wid;
			bzero(&fShapes[slot].region, sizeof(fShapes[slot].region));
		}
		fShapes[slot].bufOffset = offset;
		fShapes[slot].bufRowBytes = rowBytes;
		fShapes[slot].bufWidth = width;
		fShapes[slot].bufHeight = height;
	}
	IOLockUnlock(fLock);
}

bool RadeonNIAccel::getSurfaceBuffer(UInt32 wid, UInt32 *offset, UInt32 *rowBytes,
				     UInt32 *width, UInt32 *height)
{
	bool found = false;
	int i;

	IOLockLock(fLock);
	for (i = 0; i < kMaxSurfaces && !found; i++) {
		if (!fShapes[i].used || fShapes[i].wid != wid ||
		    !fShapes[i].bufWidth)
			continue;
		*offset = fShapes[i].bufOffset;
		*rowBytes = fShapes[i].bufRowBytes;
		*width = fShapes[i].bufWidth;
		*height = fShapes[i].bufHeight;
		found = true;
	}
	IOLockUnlock(fLock);
	return found;
}

void RadeonNIAccel::listSurfaces(struct rdn_user_surfaces *list)
{
	int i;

	list->count = 0;
	IOLockLock(fLock);
	for (i = 0; i < kMaxSurfaces && list->count < RDN_USER_SURFACES; i++) {
		if (!fShapes[i].used || !fShapes[i].bufWidth)
			continue;
		list->surface[list->count].id = fShapes[i].wid;
		list->surface[list->count].offset = fShapes[i].bufOffset;
		list->surface[list->count].row_bytes = fShapes[i].bufRowBytes;
		list->surface[list->count].width = fShapes[i].bufWidth;
		list->surface[list->count].height = fShapes[i].bufHeight;
		list->count++;
	}
	IOLockUnlock(fLock);
}

/*
 * The window server flushes a surface when its owner has drawn a new
 * picture and nothing else on the screen changed: it does not draw the
 * window again itself, the driver is to show the picture. The GPU copies
 * the surface's buffer to the screen, inside the shape the window server
 * gave the surface (what other windows cover is not in it). When the
 * window server does draw the window itself (it moves, something
 * translucent lies over it) it takes the picture from the same buffer.
 *
 * Not waited for: the copy is queued behind whatever the GPU still has to
 * do. Only the copy before it must be done, because both are written to
 * the same place.
 */
IOReturn RadeonNIAccel::flushSurface(UInt32 wid)
{
	struct rdn_selftest_target screen;
	struct rdn_draw_surface dst, src;
	struct rdn_user_region *region = 0;
	UInt32 width = 0, height = 0, n = 0, waited, r;
	uint32_t seq = 0;
	int i, ret;

	if (!fEngineUp || !fFramebuffer ||
	    !fFramebuffer->selftestTarget(&fAccel, &screen))
		return kIOReturnSuccess;
	bzero(&src, sizeof(src));
	IOLockLock(fLock);
	for (i = 0; i < kMaxSurfaces; i++) {
		if (!fShapes[i].used || fShapes[i].wid != wid ||
		    !fShapes[i].bufWidth)
			continue;
		region = &fShapes[i].region;
		width = fShapes[i].bufWidth;
		height = fShapes[i].bufHeight;
		src.gpu_addr = rdn_vram_addr(&fAccel, fShapes[i].bufOffset);
		src.width = width;
		src.height = height;
		src.pitch_pixels = fShapes[i].bufRowBytes / 4;
		break;
	}
	/* The surface's top left corner is that of the shape's box. */
	for (r = 0; region && r < region->count && r < RDN_USER_REGION_RECTS &&
		    n < RDN_BLIT_MAX_RECTS; r++) {
		SInt32 bx = region->bounds[0], by = region->bounds[1];
		SInt32 x0 = region->rects[r][0], y0 = region->rects[r][1];
		SInt32 x1 = x0 + region->rects[r][2], y1 = y0 + region->rects[r][3];

		if (x0 < bx)
			x0 = bx;
		if (y0 < by)
			y0 = by;
		if (x0 < 0)
			x0 = 0;
		if (y0 < 0)
			y0 = 0;
		if (x1 > bx + (SInt32)width)
			x1 = bx + (SInt32)width;
		if (y1 > by + (SInt32)height)
			y1 = by + (SInt32)height;
		if (x1 > (SInt32)screen.width)
			x1 = (SInt32)screen.width;
		if (y1 > (SInt32)screen.height)
			y1 = (SInt32)screen.height;
		if (x1 <= x0 || y1 <= y0)
			continue;
		fBlitRects[n].src_x = x0 - bx;
		fBlitRects[n].src_y = y0 - by;
		fBlitRects[n].dst_x = x0;
		fBlitRects[n].dst_y = y0;
		fBlitRects[n].width = x1 - x0;
		fBlitRects[n].height = y1 - y0;
		n++;
	}
	if (!n) {
		IOLockUnlock(fLock);
		return kIOReturnSuccess;
	}
	for (waited = 0; fBlitPending && waited < 500; waited++) {
		if (rdn_fence_done(&fAccel, fBlitFence))
			fBlitPending = false;
		else
			IOSleep(1);
	}
	if (fBlitPending) {
		/* The GPU is stuck or far behind; this picture is skipped. */
		IOLockUnlock(fLock);
		return kIOReturnBusy;
	}
	dst.gpu_addr = screen.gpu_addr;
	dst.width = screen.width;
	dst.height = screen.height;
	dst.pitch_pixels = screen.pitch_pixels;
	ret = rdn_blit(&fAccel, &dst, &src, fBlitRects, n, BLIT_OFFSET, &seq);
	if (!ret) {
		fBlitFence = seq;
		fBlitPending = true;
	}
	IOLockUnlock(fLock);
	return ret ? kIOReturnIOError : kIOReturnSuccess;
}

void RadeonNIAccel::forgetSurface(UInt32 wid)
{
	int i;

	IOLockLock(fLock);
	for (i = 0; i < kMaxSurfaces; i++)
		if (fShapes[i].used && fShapes[i].wid == wid)
			fShapes[i].used = false;
	IOLockUnlock(fLock);
}

/*
 * The 2D accelerator plug-in's fill and copy on the screen (ga/,
 * RDN_UC_SCREEN_FILL and RDN_UC_SCREEN_COPY): queued like flushSurface()'s
 * copy and not waited for. The caller gets the fence to wait for.
 *
 * The GPU reads a draw's work area until the draw's fence is reached, and
 * fills and copies come many in a row. So there are kScreenSlots work
 * areas, used in turn, and a call waits only when the GPU still has the
 * one that was used kScreenSlots calls ago.
 */

/* What a coordinate or a size on a screen can be; sums of two cannot overflow. */
static bool screenRange(SInt32 v)
{
	return v >= -32768 && v <= 32767;
}

/* Cut a rectangle to the screen; false if nothing is left of it. */
static bool screenClip(const struct rdn_selftest_target *screen, SInt32 *x,
		       SInt32 *y, SInt32 *w, SInt32 *h)
{
	if (*x < 0) {
		*w += *x;
		*x = 0;
	}
	if (*y < 0) {
		*h += *y;
		*y = 0;
	}
	if (*x + *w > (SInt32)screen->width)
		*w = (SInt32)screen->width - *x;
	if (*y + *h > (SInt32)screen->height)
		*h = (SInt32)screen->height - *y;
	return *w > 0 && *h > 0;
}

/*
 * The next work area, once the GPU is done with it, and the screen as it
 * is then. Called with the lock held and returns with it held, but lets go
 * of it while it sleeps, so that programs go on submitting. Refuses unless
 * the screen has 32 bits a pixel.
 */
IOReturn RadeonNIAccel::screenBegin(struct rdn_selftest_target *screen,
				    UInt32 *slot)
{
	UInt32 waited = 0, s;

	if (rdn_blit_move_work_bytes() > SCREEN_SLOT_BYTES)
		return kIOReturnUnsupported;
	for (;;) {
		if (!fEngineUp || !fFramebuffer)
			return kIOReturnNotReady;
		/* Another caller may have taken a turn meanwhile: look again. */
		s = fScreenNext;
		if (fScreenSlot[s].pending &&
		    rdn_fence_done(&fAccel, fScreenSlot[s].fence))
			fScreenSlot[s].pending = false;
		if (!fScreenSlot[s].pending)
			break;
		/* The GPU is stuck or far behind. */
		if (waited++ >= 500)
			return kIOReturnBusy;
		IOLockUnlock(fLock);
		IOSleep(1);
		IOLockLock(fLock);
	}
	if (!fFramebuffer->selftestTarget(&fAccel, screen))
		return kIOReturnUnsupported;
	*slot = s;
	return kIOReturnSuccess;
}

/*
 * After a draw with a work area, whether it succeeded or not: nothing the
 * GPU has been given reads the area once the newest fence is reached.
 */
void RadeonNIAccel::screenEnd(UInt32 slot)
{
	fScreenSlot[slot].fence = fAccel.fence_emitted;
	fScreenSlot[slot].pending = true;
	fScreenNext = (slot + 1) % kScreenSlots;
	fScreenFence = fAccel.fence_emitted;
	fScreenPending = true;
}

IOReturn RadeonNIAccel::screenFill(SInt32 x, SInt32 y, SInt32 w, SInt32 h,
				   UInt32 colour, UInt32 *fence)
{
	struct rdn_selftest_target screen;
	struct rdn_draw_surface dst;
	struct rdn_fill_rect rect;
	UInt32 slot = 0;
	uint32_t seq = 0;
	IOReturn ret;
	int r;

	if (!screenRange(x) || !screenRange(y) || !screenRange(w) || !screenRange(h))
		return kIOReturnBadArgument;
	if (!fEngineUp || !fFramebuffer)
		return kIOReturnUnsupported;
	IOLockLock(fLock);
	ret = screenBegin(&screen, &slot);
	if (ret != kIOReturnSuccess) {
		IOLockUnlock(fLock);
		return ret;
	}
	if (!screenClip(&screen, &x, &y, &w, &h)) {
		/* Nothing to draw. What is queued already is all there is to wait for. */
		*fence = fAccel.fence_emitted;
		IOLockUnlock(fLock);
		return kIOReturnSuccess;
	}
	dst.gpu_addr = screen.gpu_addr;
	dst.width = screen.width;
	dst.height = screen.height;
	dst.pitch_pixels = screen.pitch_pixels;
	rect.x = x;
	rect.y = y;
	rect.width = w;
	rect.height = h;
	/* The colour's bytes as this machine's programs store a pixel. */
	r = rdn_blit_fill(&fAccel, &dst, &rect, 1, colour, RDN_BIG_ENDIAN != 0,
			  SCREEN_OFFSET + slot * SCREEN_SLOT_BYTES, &seq);
	screenEnd(slot);
	*fence = fAccel.fence_emitted;
	IOLockUnlock(fLock);
	return r ? kIOReturnIOError : kIOReturnSuccess;
}

/*
 * screenCopy()'s scratch surface (rdn_blit_move()): video memory as large
 * as the screen, which only the GPU reads and writes, so it comes from
 * beyond the aperture when the card has memory there. Allocated at the
 * first copy and again when the screen has become larger. Called with the
 * lock held.
 */
IOReturn RadeonNIAccel::moveScratch(const struct rdn_selftest_target *screen,
				    struct rdn_draw_surface *scratch)
{
	UInt32 need = screen->pitch_pixels * 4 * ((screen->height + 7) & ~7u);
	uint64_t at = 0;
	UInt32 waited;

	if (fMoveBytes < need) {
		if (fMoveBytes) {
			/* Copies still queued use the old memory. */
			for (waited = 0; fScreenPending && waited < 500; waited++) {
				if (rdn_fence_done(&fAccel, fScreenFence))
					fScreenPending = false;
				else
					IOSleep(1);
			}
			if (fScreenPending)
				return kIOReturnBusy;
			rdn_mem_free(fMoveHidden ? &fHidden : &fMem, fMoveOffset);
			fMoveBytes = 0;
		}
		fMoveHidden = fHiddenSize && !rdn_mem_alloc(&fHidden, need, 4096, &at);
		if (!fMoveHidden && rdn_mem_alloc(&fMem, need, 4096, &at))
			return kIOReturnNoMemory;
		fMoveOffset = (UInt32)at;
		fMoveBytes = need;
		IOLog("RadeonNI: screen copies go through %lu KB of video memory at 0x%lx\n",
		      (unsigned long)(need >> 10), (unsigned long)fMoveOffset);
	}
	scratch->gpu_addr = rdn_vram_addr(&fAccel, fMoveOffset);
	scratch->width = screen->width;
	scratch->height = screen->height;
	scratch->pitch_pixels = screen->pitch_pixels;
	return kIOReturnSuccess;
}

IOReturn RadeonNIAccel::screenCopy(SInt32 sx, SInt32 sy, SInt32 dx, SInt32 dy,
				   SInt32 w, SInt32 h, UInt32 *fence)
{
	struct rdn_selftest_target screen;
	struct rdn_draw_surface surface, scratch;
	struct rdn_blit_rect rect;
	UInt32 slot = 0;
	uint32_t seq = 0;
	SInt32 cx, cy;
	IOReturn ret;
	bool some;
	int r;

	if (!screenRange(sx) || !screenRange(sy) || !screenRange(dx) ||
	    !screenRange(dy) || !screenRange(w) || !screenRange(h))
		return kIOReturnBadArgument;
	if (!fEngineUp || !fFramebuffer)
		return kIOReturnUnsupported;
	IOLockLock(fLock);
	ret = screenBegin(&screen, &slot);
	if (ret != kIOReturnSuccess) {
		IOLockUnlock(fLock);
		return ret;
	}
	/* Cut the destination and carry the source along, then the source. */
	cx = dx;
	cy = dy;
	some = screenClip(&screen, &cx, &cy, &w, &h);
	if (some) {
		sx += cx - dx;
		sy += cy - dy;
		dx = cx;
		dy = cy;
		cx = sx;
		cy = sy;
		some = screenClip(&screen, &cx, &cy, &w, &h);
	}
	if (some) {
		dx += cx - sx;
		dy += cy - sy;
		sx = cx;
		sy = cy;
	}
	if (!some || (sx == dx && sy == dy)) {
		/* Nothing to draw. What is queued already is all there is to wait for. */
		*fence = fAccel.fence_emitted;
		IOLockUnlock(fLock);
		return kIOReturnSuccess;
	}
	ret = moveScratch(&screen, &scratch);
	if (ret != kIOReturnSuccess) {
		IOLockUnlock(fLock);
		return ret;
	}
	surface.gpu_addr = screen.gpu_addr;
	surface.width = screen.width;
	surface.height = screen.height;
	surface.pitch_pixels = screen.pitch_pixels;
	rect.src_x = sx;
	rect.src_y = sy;
	rect.dst_x = dx;
	rect.dst_y = dy;
	rect.width = w;
	rect.height = h;
	r = rdn_blit_move(&fAccel, &surface, &scratch, &rect, 1,
			  SCREEN_OFFSET + slot * SCREEN_SLOT_BYTES, &seq);
	screenEnd(slot);
	*fence = fAccel.fence_emitted;
	IOLockUnlock(fLock);
	return r ? kIOReturnIOError : kIOReturnSuccess;
}

UInt32 RadeonNIAccel::apertureBytes(void)
{
	return fFramebuffer ? fFramebuffer->apertureSize() : 0;
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
	fTask = owningTask;
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
	fAccel->gartUnbindAll(this);
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
		{ 0, (IOMethod)&RadeonNIUserClient::methodSurfaceBuffer,
		  kIOUCScalarIScalarO, 5, 0 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodSurfaceList,
		  kIOUCScalarIStructO, 0, sizeof(struct rdn_user_surfaces) },
		{ 0, (IOMethod)&RadeonNIUserClient::methodSurfaceLocked,
		  kIOUCScalarIScalarO, 0, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodPower,
		  kIOUCScalarIScalarO, 2, 3 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodRegRead,
		  kIOUCScalarIScalarO, 1, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodAllocHidden,
		  kIOUCScalarIScalarO, 2, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodHiddenInfo,
		  kIOUCScalarIScalarO, 0, 2 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodGartInfo,
		  kIOUCScalarIScalarO, 0, 3 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodGartBind,
		  kIOUCScalarIScalarO, 2, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodGartUnbind,
		  kIOUCScalarIScalarO, 1, 0 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodScreenFill,
		  kIOUCScalarIScalarO, 5, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodScreenCopy,
		  kIOUCScalarIScalarO, 5, 1 },
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

IOReturn RadeonNIUserClient::methodAllocHidden(UInt32 size, UInt32 align,
					       UInt32 *offset)
{
	IOReturn r;

	if (fCount >= kMaxAllocations)
		return kIOReturnNoResources;
	r = fAccel->allocVram(size, align, offset, true);
	if (r == kIOReturnSuccess)
		fOffsets[fCount++] = *offset;
	return r;
}

IOReturn RadeonNIUserClient::methodHiddenInfo(UInt32 *offset, UInt32 *size)
{
	fAccel->hiddenInfo(offset, size);
	return kIOReturnSuccess;
}

IOReturn RadeonNIUserClient::methodGartInfo(UInt32 *on, UInt32 *gpuStart,
					    UInt32 *pages)
{
	fAccel->gartInfo(on, gpuStart, pages);
	return kIOReturnSuccess;
}

IOReturn RadeonNIUserClient::methodGartBind(UInt32 address, UInt32 size,
					    UInt32 *offset)
{
	return fAccel->gartBind(fTask, address, size, this, offset);
}

IOReturn RadeonNIUserClient::methodGartUnbind(UInt32 offset)
{
	/*
	 * The caller has waited for the GPU to be done with the memory. If
	 * it has not, the GPU reads the dummy page from here on: harmless.
	 */
	return fAccel->gartUnbind(offset, this);
}

IOReturn RadeonNIUserClient::methodScreenFill(UInt32 x, UInt32 y, UInt32 width,
					      UInt32 height, UInt32 colour,
					      UInt32 *fence)
{
	*fence = 0;
	return fAccel->screenFill((SInt32)x, (SInt32)y, (SInt32)width,
				  (SInt32)height, colour, fence);
}

/* Width and height share an argument: a method has six in all. */
IOReturn RadeonNIUserClient::methodScreenCopy(UInt32 sx, UInt32 sy, UInt32 dx,
					      UInt32 dy, UInt32 size,
					      UInt32 *fence)
{
	*fence = 0;
	return fAccel->screenCopy((SInt32)sx, (SInt32)sy, (SInt32)dx, (SInt32)dy,
				  (SInt32)(size & 0xffff), (SInt32)(size >> 16),
				  fence);
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

IOReturn RadeonNIUserClient::methodPower(UInt32 state, UInt32 what,
					 UInt32 *sclk, UInt32 *mclk,
					 UInt32 *temperature)
{
	return fAccel->power(state, what, sclk, mclk, temperature);
}

IOReturn RadeonNIUserClient::methodRegRead(UInt32 offset, UInt32 *value)
{
	return fAccel->regRead(offset, value);
}

IOReturn RadeonNIUserClient::methodSyncForCPU(void)
{
	fAccel->syncForCPU();
	return kIOReturnSuccess;
}

IOReturn RadeonNIUserClient::methodSurfaceLocked(UInt32 *wid)
{
	*wid = fAccel->readLocked();
	return kIOReturnSuccess;
}

IOReturn RadeonNIUserClient::methodSurfaceList(struct rdn_user_surfaces *list,
					       IOByteCount *size)
{
	if (*size < sizeof(*list))
		return kIOReturnBadArgument;
	fAccel->listSurfaces(list);
	*size = sizeof(*list);
	return kIOReturnSuccess;
}

IOReturn RadeonNIUserClient::methodSurfaceBuffer(UInt32 wid, UInt32 offset,
	UInt32 rowBytes, UInt32 width, UInt32 height)
{
	/* Inside the aperture, or nothing. */
	if (width && ((UInt64)offset + (UInt64)rowBytes * height >
		      fAccel->apertureBytes() || rowBytes < width * 4))
		return kIOReturnBadArgument;
	fAccel->setSurfaceBuffer(wid, offset, rowBytes, width, height);
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
