/*
 * The accelerator service of the Radeon HD 7570 driver, and its user
 * client.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include <libkern/libkern.h>
#include <stdarg.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>
#include <sys/errno.h>
#ifdef RDN_LEOPARD
#include <sys/proc.h>
#include <sys/kauth.h>
#endif

#include "RadeonNI.h"
#include "RadeonNIAccel.h"
#include "RadeonNIVideo.h"
#include "RadeonNISurface.h"
#include "rdn_uvd_reg.h"
#include "rdn_mc.h"
extern "C" {
#include "rdn_cp_dma_readback.h"
}

/* sync: orders every earlier store, also to write-combined memory, before the next one (eieio orders only guarded I/O) */
static inline void rdnStoreBarrier(void)
{
#if defined(__ppc__) || defined(__ppc64__)
	__asm__ __volatile__("sync" : : : "memory");
#endif
}

extern "C" {
#include "rdn_mc_layout.h"
}

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
	if (provider->getProperty(RDN_UVD_REBOOT_PROPERTY))
		return 0;
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
			static const UInt32 noClass = 0;

			accel->fShim->setName("RadeonNIAGPShim");
			accel->fShim->setProperty("model", (void *)model, sizeof(model));
			/*
			 * The shim is an IOPCIDevice to anything that walks
			 * the PCI devices, and programs read "class-code" of
			 * each without checking (Sid Meier's Pirates!'s
			 * ZonicLib crashed on the lack of it). A class that
			 * is not a display, so that none takes it for a second
			 * graphics card, and the real card's ids.
			 */
			accel->fShim->setProperty("class-code", (void *)&noClass,
						  sizeof(noClass));
			static const char *const ids[] = { "vendor-id", "device-id" };
			for (unsigned i = 0; i < 2; i++) {
				OSData *id = OSDynamicCast(OSData,
						provider->getProperty(ids[i]));

				if (id)
					accel->fShim->setProperty(ids[i], id);
			}
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
	accel->fUvdProvider = provider;
	accel->fLock = IOLockAlloc();
	accel->fUvdLock = IOLockAlloc();
	accel->fVideoSession = 0;
	accel->fVideoEpoch = 0;
	accel->fVideoCpuBytes = 0;
	accel->fVideoBusy = false;
	if (!accel->fLock || !accel->fUvdLock) {
		accel->detach(parent);
		accel->release();
		return 0;
	}
	accel->fRetiring = false;
	accel->fUvdColdAttempted = false;
	bzero(&accel->fUvdFixture, sizeof(accel->fUvdFixture));
	accel->fUvdFixtureOffset = accel->fUvdFixtureBytes = 0;
	accel->fUvdFixtureSnapshot = 0;
	accel->fEngineUp = false;
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
	accel->fSurfaceGate = fb->getProperty("SurfaceGate") == kOSBooleanTrue;
	accel->setProperty("BackingSurfaces", fb->getProperty("BackingSurfaces") == kOSBooleanTrue);
	accel->fEngineUp = accel->startEngine();
	OSNumber *videoAPI = OSDynamicCast(OSNumber, fb->getProperty("UVDValidatedAPI"));
	if (accel->fEngineUp && videoAPI && videoAPI->unsigned32BitValue() == RDN_VIDEO_API_VERSION &&
	    fb->getProperty("EnableUVDSessions") == kOSBooleanTrue && accel->fUvd.prepared) {
		UInt32 status = 0;
		IOReturn result = accel->uvdDiagnosticInit(&status);
		IOLog("RadeonNI: video startup result 0x%x status 0x%lx\n", result, (unsigned long)status);
	}
	accel->setProperty("RadeonNIEngine", accel->fEngineUp);
	accel->setProperty("RadeonNIUVDDiagnosticVersion",
		(UInt64)RDN_UVD_DIAGNOSTIC_VERSION, 32);
	accel->setProperty("RadeonNIUVDFixtureVersion",
		(UInt64)RDN_UVD_FIXTURE_VERSION, 32);

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
    bzero(fSurfaceMirrors, sizeof(fSurfaceMirrors));
    fSurfaceGpuCopies = fSurfaceGpuBytes = 0;
    fSurfaceGpuStalled = false;
	typedef char ScratchAfterScreenSlots[
		SCREEN_OFFSET + kScreenSlots * SCREEN_SLOT_BYTES <=
		RDN_MC_LAYOUT_SCRATCH_OFFSET ? 1 : -1];
	typedef char ScratchBeforeHeap[
		RDN_MC_LAYOUT_SCRATCH_OFFSET + 4096u <= HEAP_OFFSET ? 1 : -1];
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
	if (rdn_mem_init(&fMem, fFramebuffer->os(), HEAP_OFFSET,
			 apertureSize - HEAP_OFFSET))
		return false;
	const bool coldUvd =
		fFramebuffer->getProperty("EnableUVDDiagnostics") == kOSBooleanTrue &&
		fFramebuffer->getProperty("UVDDiagnosticsBeforeCP") == kOSBooleanTrue;
	r = rdn_accel_init_hook(&fAccel, fFramebuffer->card(), fFramebuffer->aperture(),
			   apertureSize, &fw, RING_OFFSET, RING_BYTES,
			   RDN_BIG_ENDIAN, coldUvd ? prepareUvdBeforeCP : 0, this);
	if (r) {
		IOLog("RadeonNI: the 3D engine did not start (%d)\n", r);
		if (fUvd.prepared)
			rdn_uvd_fini(&fUvd);
		rdn_mem_fini(&fMem);
		return false;
	}
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
	if (fFramebuffer->getProperty("MCLayoutScratchVerified") == kOSBooleanTrue)
		r = rdn_gart_enable_with_scratch(&fGart, &fAccel, fGartTable,
						dummyBus, RDN_MC_LAYOUT_SCRATCH_OFFSET);
	else
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
IOReturn RadeonNIAccel::gartBind(task_t task, UInt64 address, UInt32 size,
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
#ifdef RDN_LEOPARD
	memory = IOMemoryDescriptor::withAddressRange(address, size, kIODirectionOutIn, task);
#else
	if (address > 0xffffffffULL)
		return kIOReturnUnsupported;
	memory = IOMemoryDescriptor::withAddress((vm_address_t)address, size,
	                                       kIODirectionOutIn, task);
#endif
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
		/* The G5 PCI bus needs IOMapper addresses, not raw physical pages. */
		IOPhysicalAddress bus = memory->getPhysicalSegment(done, &length);

		if (!bus || length < RDN_GART_PAGE_SIZE)
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
    if (fSurfaceGpuStalled) return kIOReturnNotReady;
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

void RadeonNIAccel::clearSurfaceBufferRecord(SurfaceRecord *record)
{
    if (record->backingClient) {
        record->owner = 0; record->producerTask = 0; record->gl = false;
        record->bufOffset = record->bufRowBytes = record->bufWidth = record->bufHeight = 0;
    } else bzero(record, sizeof(*record));
}

void RadeonNIAccel::retainClientMemory(const UInt32 *offsets,UInt32 count,void *owner)
{
 if(!owner) return;
 IOLockLock(fLock);
 for(UInt32 i=0;i<kMaxGartBindings;++i)
  if(fGartBound[i].memory && fGartBound[i].owner==owner) fGartBound[i].owner=0;
 for(UInt32 i=0;i<fShapeCapacity;++i) {
  if(fShapes[i].used && fShapes[i].owner==owner) clearSurfaceBufferRecord(&fShapes[i]);
  for(UInt32 j=0;fShapes[i].used && j<count;++j)
   if(fShapes[i].bufWidth && fShapes[i].bufOffset==offsets[j]) clearSurfaceBufferRecord(&fShapes[i]);
 }
 IOLockUnlock(fLock);
}

void RadeonNIAccel::releaseClientMemory(const UInt32 *offsets, UInt32 count,
                                         void *owner)
{
	UInt32 i, j, fence;
	bool hasMemory = count != 0;

	IOLockLock(fLock);
	for (i = 0; i < kMaxGartBindings; i++)
		if (fGartBound[i].memory && fGartBound[i].owner == owner)
			hasMemory = true;
	/* Reservations have no VRAM offset to match during client teardown. */
	for (i = 0; i < fShapeCapacity; i++)
		if (fShapes[i].used && fShapes[i].owner == owner)
			clearSurfaceBufferRecord(&fShapes[i]);
	if (!hasMemory) {
		IOLockUnlock(fLock);
		return;
	}
	/* Stop future compositor copies from referencing this client's stores. */
	for (i = 0; i < fShapeCapacity; i++)
		for (j = 0; fShapes[i].used && j < count; j++)
			if (fShapes[i].bufWidth && fShapes[i].bufOffset == offsets[j]) {
				clearSurfaceBufferRecord(&fShapes[i]);
				break;
			}
	fence = fAccel.fence_emitted;
	IOLockUnlock(fLock);

	if (fSurfaceGpuStalled || !fenceWait(fence, 10000)) {
		/* Keep GPU-visible memory pinned until reboot after a hang. */
		retainClientMemory(offsets,count,owner);
		IOLog("RadeonNI: client cleanup fence %lu timed out; retaining GPU memory\n", fence);
		return;
	}
	for (i = 0; i < count; i++)
		freeVram(offsets[i]);
	gartUnbindAll(owner);
}

void RadeonNIAccel::prepareToRetire(void)
{
	IOLockLock(fLock);
	fRetiring = true;
	IOLockUnlock(fLock);
}

void RadeonNIAccel::retire(IOService *provider)
{
	prepareToRetire();
	videoClose(0);
	releaseUvdFixtureSnapshot();
	if (fFramebuffer) {
		fFramebuffer->removeProperty(kIOAccelTypesKey);
		fFramebuffer->removeProperty(kIOAccelIndexKey);
		fFramebuffer->removeProperty(kIOAccelRevisionKey);
	}
	terminate(kIOServiceRequired | kIOServiceSynchronous);
	if (fEngineUp) {
		IOLockLock(fUvdLock);
		IOLockLock(fLock);
		if (fUvdBytes)
			rdn_uvd_fini(&fUvd);
		if (fUvdUp) {
			rdn_uvd_fini(&fUvd);
			fUvdUp = false;
		}
		if (fUvdVcpuShadow) {
			IOFree(fUvdVcpuShadow, fUvdVcpuShadowBytes);
			fUvdVcpuShadow = 0;
		}
		if (fUvdRingShadow) {
			IOFree(fUvdRingShadow, fUvdRingShadowBytes);
			fUvdRingShadow = 0;
		}
		fUvdSeg = 0;
		rdn_accel_fini(&fAccel);
		rdn_mem_fini(&fMem);
		if (fHiddenSize) {
			rdn_mem_fini(&fHidden);
			fHiddenSize = 0;
		}
		fMoveBytes = 0;
		fEngineUp = false;
		IOLockUnlock(fLock);
		IOLockUnlock(fUvdLock);
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
	if (fShapes) {
		IOFree(fShapes, fShapeCapacity * sizeof(*fShapes));
		fShapes = 0;
		fShapeCapacity = 0;
	}
	IOLockLock(fUvdLock);
	if (fLock) {
		IOLockFree(fLock);
		fLock = 0;
	}
	IOLockUnlock(fUvdLock);
}

void RadeonNIAccel::free(void)
{
	if (fLock) IOLockFree(fLock);
	if (fUvdLock) IOLockFree(fUvdLock);
	super::free();
}

void RadeonNIAccel::uvdDiagnosticInfo(struct rdn_user_uvd_diagnostic *info)
{
	IOLockLock(fUvdLock);
	IOLockLock(fLock);
	bzero(info, sizeof(*info));
	info->version = RDN_UVD_DIAGNOSTIC_VERSION;
	if (!fRetiring) {
		if (fFramebuffer->getProperty("EnableUVDDiagnostics") == kOSBooleanTrue)
			info->flags |= RDN_UVD_DIAGNOSTIC_ALLOWED;
		OSData *firmware = OSDynamicCast(OSData, fFramebuffer->getProperty("FW_UVD"));
		if (firmware && firmware->getLength())
			info->flags |= RDN_UVD_DIAGNOSTIC_FIRMWARE;
		OSData *rlc = OSDynamicCast(OSData, fFramebuffer->getProperty("FW_RLC"));
		if (rlc && rlc->getLength() == 3072)
			info->flags |= RDN_UVD_DIAGNOSTIC_RLC_FIRMWARE;
	}
	info->state = fUvdState;
	info->last_status = fUvdStatus;
	info->last_error = fUvdError;
	info->reserved_bytes = fUvdBytes;
	IOLockUnlock(fLock);
	IOLockUnlock(fUvdLock);
}

void RadeonNIAccel::prepareUvdBeforeCP(void *cookie)
{
	RadeonNIAccel *accel = static_cast<RadeonNIAccel *>(cookie);
	UInt32 status = 0;

	accel->fUvdColdAttempted = true;
	IOReturn result = accel->uvdDiagnosticInitImpl(&status, true);
	IOLog("RadeonNI: UVD preparation before CP: result 0x%x, prepared %u, reserved %lu\n",
	      (unsigned)result, accel->fUvd.prepared ? 1u : 0u,
	      (unsigned long)accel->fUvdBytes);
}

IOReturn RadeonNIAccel::uvdDiagnosticInit(UInt32 *status)
{
	return uvdDiagnosticInitImpl(status, false);
}

IOReturn RadeonNIAccel::uvdDiagnosticInitImpl(UInt32 *status, bool prepareOnly)
{
	const UInt32 ringBytes = 4096;
	UInt32 fwSize = 0, rlcSize = 0, vcpuBytes = 0, bytes, offset, waited;
	uint32_t refClock = 0, fence = 0;
	uint64_t allocationOffset = 0;
	void *firmware = 0;
	void *rlcFirmware = 0;
	IOReturn result = kIOReturnError;
	bool startPrepared = false;
	int r;

	*status = 0;
	IOLockLock(fUvdLock);
	IOLockLock(fLock);
	if (fRetiring || fVideoBusy || fUvdUsers || fUvdLive || (fUvdUp && !fUvd.guarded)) {
		result = kIOReturnNotReady;
		goto out;
	}
	if (fFramebuffer->getProperty("EnableUVDDiagnostics") != kOSBooleanTrue) {
		result = kIOReturnNotPermitted;
		goto out;
	}
	if ((!fEngineUp && !prepareOnly) || (fEngineUp && prepareOnly)) {
		result = kIOReturnNotReady;
		goto out;
	}
	if (fUvdState != RDN_UVD_DIAGNOSTIC_OFF) {
		*status = fUvdStatus;
		result = fUvdState == RDN_UVD_DIAGNOSTIC_RING_READY ?
			kIOReturnSuccess : kIOReturnNotReady;
		goto out;
	}
	if (!prepareOnly && fUvdColdAttempted) {
		if (!fUvd.prepared || !fUvd.started || fUvd.failed || fUvd.stopped ||
		    fUvdProvider->getProperty(RDN_UVD_REBOOT_PROPERTY) != kOSBooleanTrue) {
			result = kIOReturnNotReady;
			goto out;
		}
		startPrepared = true;
	}
	if (!startPrepared && fUvdProvider->getProperty(RDN_UVD_REBOOT_PROPERTY)) {
		result = kIOReturnNotReady;
		goto out;
	}
	if (startPrepared)
		goto drainGraphics;
	firmware = copyFirmware("FW_UVD", &fwSize);
	if (!firmware) {
		result = kIOReturnNotFound;
		goto out;
	}
	rlcFirmware = copyFirmware("FW_RLC", &rlcSize);
	if (!rlcFirmware) {
		result = kIOReturnNotFound;
		goto out;
	}
	if (rlcSize != 3072) {
		result = kIOReturnBadArgument;
		goto out;
	}
	vcpuBytes = rdn_uvd_vcpu_bytes(fwSize, RDN_UVD_DEFAULT_HANDLES);
	if (!vcpuBytes || fwSize > (1u << 20) ||
	    vcpuBytes > UINT32_MAX - ringBytes - RDN_UVD_RING_EXTRA) {
		result = kIOReturnBadArgument;
		goto out;
	}
	r = rdn_uvd_ref_clock(fAccel.card, &refClock);
	if (r) {
		fUvdError = r;
		goto out;
	}
drainGraphics:
	/* The cold callback runs with ME/PFP halted, before clients are published. */
	if (!prepareOnly) {
		r = rdn_fence_emit(&fAccel, &fence);
		if (r) {
			fUvdError = r;
			result = kIOReturnNotReady;
			goto out;
		}
		for (waited = 0; waited < 1000; waited++) {
			if (rdn_fence_done(&fAccel, fence))
				break;
			IOSleep(1);
		}
		if (waited == 1000 && !rdn_fence_done(&fAccel, fence)) {
			fUvdError = -ETIMEDOUT;
			result = kIOReturnTimeout;
			goto out;
		}
		rdn_hdp_flush(&fAccel);
	}
	if (startPrepared) {
		r = rdn_uvd_start_prepared(&fUvd);
		goto record;
	}
	/* Reserve internal storage while serialized; clients never own these offsets. */
	bytes = vcpuBytes + ringBytes + RDN_UVD_RING_EXTRA;
	if (rdn_mem_alloc(&fMem, bytes, 4096, &allocationOffset)) {
		result = kIOReturnNoMemory;
		goto out;
	}
	if (allocationOffset > fAccel.aperture_size ||
	    bytes > fAccel.aperture_size - allocationOffset) {
		rdn_mem_free(&fMem, allocationOffset);
		result = kIOReturnBadArgument;
		goto out;
	}
	offset = (UInt32)allocationOffset;
	/* VRAM may remain in use after shutdown; block reuse until reboot. */
	if (!fUvdProvider->setProperty(RDN_UVD_REBOOT_PROPERTY, true) ||
	    fUvdProvider->getProperty(RDN_UVD_REBOOT_PROPERTY) != kOSBooleanTrue) {
		rdn_mem_free(&fMem, offset);
		result = kIOReturnNoMemory;
		goto out;
	}
	fUvdOffset = offset;
	fUvdBytes = bytes;
	if (prepareOnly)
		r = rdn_uvd_prepare_rlc(&fUvd, &fAccel, refClock,
			(const uint8_t *)firmware, fwSize, RDN_UVD_DEFAULT_HANDLES,
			offset, offset + vcpuBytes, ringBytes, 0,
			(const uint8_t *)rlcFirmware, rlcSize);
	else
		r = rdn_uvd_init_rlc(&fUvd, &fAccel, refClock,
			(const uint8_t *)firmware, fwSize, RDN_UVD_DEFAULT_HANDLES,
			offset, offset + vcpuBytes, ringBytes, 0,
			(const uint8_t *)rlcFirmware, rlcSize);
record:
	fUvdError = r;
	fUvdStatus = fUvd.status_snapshot;
	*status = fUvdStatus;
	if (!r) {
		if (!prepareOnly) {
			fUvdState = RDN_UVD_DIAGNOSTIC_RING_READY;
			fUvdUp = true; fUvdEpoch = 1; fUvdFlags = 0; fUvdRef=fUvd.ref_freq;
		}
		result = kIOReturnSuccess;
	} else if (rdn_uvd_storage_releasable(&fUvd)) {
		rdn_mem_free(&fMem, fUvdOffset);
		fUvdOffset = fUvdBytes = 0;
		bzero(&fUvd, sizeof(fUvd));
		fUvdProvider->removeProperty(RDN_UVD_REBOOT_PROPERTY);
	} else {
		/* A stopped clock is not proof that DMA completed. Keep storage quarantined. */
		fUvdState = RDN_UVD_DIAGNOSTIC_FAULTED;
	}
	IOLog("RadeonNI: UVD diagnostic init: result %d, status 0x%lx, state %lu\n",
	      r, (unsigned long)fUvdStatus, (unsigned long)fUvdState);
out:
	if (rlcFirmware)
		IOFree(rlcFirmware, rlcSize);
	if (firmware)
		IOFree(firmware, fwSize);
	IOLockUnlock(fLock);
	IOLockUnlock(fUvdLock);
	return result;
}

IOReturn RadeonNIAccel::newUserClient(task_t owningTask, void *securityID,
				      UInt32 type, IOUserClient **handler)
{
	RadeonNIUserClient *client;

	if (type == RDN_VIDEO_UC_TYPE) {
		RadeonNIVideoUserClient *video = new RadeonNIVideoUserClient;
		if (!video) return kIOReturnNoMemory;
		if (!video->initWithTask(owningTask, securityID, type) || !video->attach(this)) {
			video->release(); return kIOReturnError;
		}
		if (!video->start(this)) {
			video->detach(this); video->release(); return kIOReturnError;
		}
		*handler = video; return kIOReturnSuccess;
	}
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
	if (type != RDN_UC_TYPE && type != RDN_UC_TYPE_V2) {
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
	if (fFramebuffer && fFramebuffer->device())
		info->pci_device_id = fFramebuffer->device()->configRead16(
			kIOPCIConfigDeviceID);
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
				  bool hidden, bool segment)
{
	uint64_t off = 0;
	int r;

	if (!size || (align & (align - 1)))
		return kIOReturnBadArgument;
	if (align < 4096)
		align = 4096;
	if ((hidden || segment) && !fHiddenSize)
		return kIOReturnNoMemory;
	IOLockLock(fLock);
	if (segment) {
		/* the decoder's own 256 MB (docs/UVD.md) */
		r = (fUvdSeg || fUvdSplit) ? rdn_mem_alloc_range(fUvdSegMem, size, align, fUvdSegLo, fUvdSegHi, &off) : -1;
	} else if (hidden && (fUvdSeg || fUvdSplit) && fUvdSegMem == &fHidden) {
		/* the segment is kept for the decoder while the rest of the hidden memory has room */
		r = rdn_mem_alloc_range(&fHidden, size, align, fHiddenOffset, fUvdSegLo, &off);
		if (r)
			r = rdn_mem_alloc_range(&fHidden, size, align, fUvdSegHi,
						(uint64_t)fHiddenOffset + fHiddenSize, &off);
		if (r)
			r = rdn_mem_alloc(&fHidden, size, align, &off);
	} else {
		r = rdn_mem_alloc(hidden ? &fHidden : &fMem, size, align, &off);
	}
	IOLockUnlock(fLock);
	if (r)
		return kIOReturnNoMemory;
	*offset = (UInt32)off;
	return kIOReturnSuccess;
}

void RadeonNIAccel::freeVram(UInt32 offset)
{
	IOLockLock(fLock);
    if (fSurfaceGpuStalled) { IOLockUnlock(fLock); return; }
    for (UInt32 i = 0; i < kSurfaceMirrors; ++i)
        if (fSurfaceMirrors[i].offset == offset) fSurfaceMirrors[i].surface = 0;
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
 * UVD. Nothing here runs unless a program asks (RDN_UC_UVD_INIT) and the
 * personality carries the firmware (FW_UVD); the block is not touched at
 * start. A failed bring-up may be tried again (with other flags).
 */
IOReturn RadeonNIAccel::uvdAcquireRaw(bool *claimed)
{
 IOLockLock(fUvdLock);
 IOLockLock(fLock);
 IOReturn result=kIOReturnSuccess;
 if(*claimed) goto done;
 if(fRetiring || fVideoBusy || fUvd.failed || fUvd.prepared || fUvdUsers==UINT32_MAX) {
  result=kIOReturnBusy; goto done;
 }
 if(fUvd.guarded && !fUvd.ready) { result=kIOReturnNotReady; goto done; }
 if(fUvd.guarded && !fUvdScratch) {
  uint64_t offset=0, low=fUvd.vcpu_gpu&~((1ull<<28)-1), high=low+(1ull<<28);
  low=low>fAccel.vram_base?low-fAccel.vram_base:0;
  high=high>fAccel.vram_base?high-fAccel.vram_base:0;
  if(high>fAccel.aperture_size) high=fAccel.aperture_size;
  if(rdn_mem_alloc_in_range(&fMem,4096,4096,low,high,&offset)) {
   result=kIOReturnNoMemory; goto done;
  }
  fUvdScratch=(UInt32)offset;
  volatile UInt32 *words=(volatile UInt32 *)((UInt8 *)fAccel.aperture+offset);
  for(unsigned i=0;i<1024;++i) words[i]=0;
  rdnStoreBarrier();
 }
 ++fUvdUsers; *claimed=true;
done:
 IOLockUnlock(fLock); IOLockUnlock(fUvdLock);
 return result;
}

IOReturn RadeonNIUserClient::claimUvd(void)
{
 return fAccel->uvdAcquireRaw(&fUvdUser);
}

IOReturn RadeonNIAccel::uvdInit(UInt32 flags, UInt32 *status)
{
	UInt32 requestedFlags=flags;
	/*
	 * No flags: the start that works (docs/UVD.md); other values select the older variants, for experiments. RDN_UVD_F_SEGMENT
	 * alone asks for that start with the block in a segment beyond the aperture.
	 */
	bool wantSegment = (flags & RDN_UVD_F_SEGMENT) != 0;
	bool wantAperture = (flags & RDN_UVD_F_APERTURE) != 0;
	bool wantSplit = (flags & RDN_UVD_F_SPLIT) != 0;

	UInt32 force = (flags >> RDN_UVD_F_SEGMENT_AT) & 7;

	UInt32 nibbles = (flags >> 24) & 0xf;

	/* tests: bits 24 to 27 say which nibbles of UVD_LMI_ADDR_EXT get the segment (0: all) */
	bool noDma = (flags & RDN_UVD_F_NO_DMA) != 0;

	flags &= ~(RDN_UVD_F_SEGMENT | RDN_UVD_F_APERTURE | (7u << RDN_UVD_F_SEGMENT_AT) | (0xfu << 24) | RDN_UVD_F_PUSH_APERTURE | RDN_UVD_F_NO_DMA | RDN_UVD_F_SPLIT);
	if (!flags)
		flags = RDN_UVD_F_MINIMAL | RDN_UVD_F_LMI | RDN_UVD_F_PLL | RDN_UVD_F_WINDOWS;
	UInt32 fwSize = 0, vcpuOff = 0, ringOff = 0;
	uint32_t ref = 0;
	UInt32 vcpuBytes;
	void *fw;
	IOReturn ret;
	int r;

	IOLockLock(fLock);
	if (fVideoBusy || fRetiring || fUvd.failed) { IOLockUnlock(fLock); return kIOReturnBusy; }
	if (fUvd.guarded) {
		*status=fUvdStatus;
		IOReturn result=requestedFlags?kIOReturnUnsupported:fUvd.ready?kIOReturnSuccess:kIOReturnNotReady;
		IOLockUnlock(fLock); return result;
	}
	rdn_uvd_set_ext_nibbles(nibbles ? nibbles : 0xf);
	rdn_uvd_set_push_via_aperture((requestedFlags & RDN_UVD_F_PUSH_APERTURE) != 0);

	if (fUvdUp && (wantSegment || wantSplit || wantAperture) &&
	    !(wantSegment ? fUvdSeg && (!force || force == fUvdSeg) :
	      wantSplit ? fUvdSplit && (!force || force == fUvdSplit) : !fUvdSeg && !fUvdSplit) &&
	    fUvdUsers <= 1 && !fUvdLive) {
		/*
		 * Asked for the other place and nothing runs: start again there. The fence numbers go on and the epoch changes,
		 * as at any restart; a program that kept buffers of the old place does not use them again (tests only).
		 */
		UInt32 fenceBase = fUvd.fence_emitted;

		IOLog("RadeonNI: UVD: from %s to %s\n", fUvdSeg ? "a segment" : fUvdSplit ? "split mode" : "the aperture",
		      wantSegment ? "a segment" : wantSplit ? "split mode" : "the aperture");
		rdn_uvd_fini(&fUvd);
		fUvdSplit = 0;
		rdn_uvd_set_decode_segment(-1);
		if (fUvdSeg) {
			uvdSegmentFree();
		} else {
			rdn_mem_free(&fMem, fUvd.vcpu_offset);
			rdn_mem_free(&fMem, fUvd.ring_offset);
			if (fUvdScratch)
				rdn_mem_free(&fMem, fUvdScratch);
			fUvdScratch = 0;
		}
		fUvdUp = false;
		fUvdClocksUp = false;
		fUvdDirty = false;
		fUvdEpoch++;
		fUvdModeFenceBase = fenceBase;
		/* the RLC runs since the first start: loading it again breaks the decoder (docs/UVD.md), as it does at a restart */
		fUvdStartLevel = fUvdRestartBits;
	}
	if (fUvdUp) {
		*status = rdn_rreg(fAccel.card, 0xf6bc);
		IOLockUnlock(fLock);
		return kIOReturnSuccess;
	}
	if (!fEngineUp) {
		IOLockUnlock(fLock);
		return kIOReturnNotReady;
	}
	fw = copyFirmware("FW_UVD", &fwSize);
	if (!fw) {
		IOLockUnlock(fLock);
		return kIOReturnNotReady;
	}
	{
		/* the RLC microcode for the minimal start (personality FW_RLC); optional, and kept for the life of the kext */
		static void *rlcFw;
		static UInt32 rlcSize;

		if (!rlcFw)
			rlcFw = copyFirmware("FW_RLC", &rlcSize);
		rdn_uvd_set_rlc_ucode(rlcFw, rlcSize);
	}
	vcpuBytes = rdn_uvd_vcpu_bytes(fwSize, RDN_UVD_DEFAULT_HANDLES);
	if (rdn_uvd_ref_clock(fAccel.card, &ref))
		ref = 0;
	IOLockUnlock(fLock);

	if (flags & RDN_UVD_F_GART) {
		/* Variant: the block's memory in system memory, reached through the GART. */
		UInt32 total = vcpuBytes + 4096 + RDN_UVD_RING_EXTRA, done;
		IOBufferMemoryDescriptor *mem;
		uint64_t at = 0;
		volatile uint8_t *cpu;
		uint64_t gpu;

		total = (total + 4095) & ~4095u;
		if (!fGart.ready || !ref) {
			IOFree(fw, fwSize);
			return kIOReturnNotReady;
		}
		mem = IOBufferMemoryDescriptor::withOptions(kIODirectionInOut, total, 4096);
		if (!mem || mem->prepare() != kIOReturnSuccess) {
			if (mem)
				mem->release();
			IOFree(fw, fwSize);
			return kIOReturnNoMemory;
		}
		bzero(mem->getBytesNoCopy(), total);
		IOLockLock(fLock);
		if (rdn_mem_alloc(&fGartSpace, total, 4096, &at)) {
			IOLockUnlock(fLock);
			mem->complete();
			mem->release();
			IOFree(fw, fwSize);
			return kIOReturnNoResources;
		}
		for (done = 0; done < total; done += 4096) {
			IOByteCount length = 0;
			IOPhysicalAddress bus = mem->getPhysicalSegment(done, &length);

			if (!bus)
				break;
			rdn_gart_set_page(&fGart, (UInt32)((at + done) / 4096), bus);
		}
		if (done < total) {
			while (done) {
				done -= 4096;
				rdn_gart_clear_page(&fGart, (UInt32)((at + done) / 4096));
			}
			rdn_gart_flush(&fGart);
			rdn_mem_free(&fGartSpace, at);
			IOLockUnlock(fLock);
			mem->complete();
			mem->release();
			IOFree(fw, fwSize);
			return kIOReturnVMError;
		}
		rdn_gart_flush(&fGart);
		cpu = (volatile uint8_t *)mem->getBytesNoCopy();
		gpu = rdn_gart_addr((UInt32)(at / 4096));
		IOLog("RadeonNI: UVD (GART): firmware %lu bytes, memory at GPU 0x%lx%08lx, reference clock %lu0 kHz; starting\n",
		      (unsigned long)fwSize, (unsigned long)(gpu >> 32), (unsigned long)gpu,
		      (unsigned long)ref);
		r = rdn_uvd_init_mem(&fUvd, &fAccel, ref, (const uint8_t *)fw, fwSize,
				     RDN_UVD_DEFAULT_HANDLES, cpu, gpu, cpu + vcpuBytes,
				     gpu + vcpuBytes, 4096, flags);
		*status = rdn_rreg(fAccel.card, 0xf6bc);
		if (r) {
			for (done = 0; done < total; done += 4096)
				rdn_gart_clear_page(&fGart, (UInt32)((at + done) / 4096));
			rdn_gart_flush(&fGart);
			rdn_mem_free(&fGartSpace, at);
			IOLockUnlock(fLock);
			mem->complete();
			mem->release();
			IOLog("RadeonNI: UVD (GART) did not start (%d), UVD_STATUS 0x%08lx\n", r,
			      (unsigned long)*status);
			IOFree(fw, fwSize);
			return kIOReturnIOError;
		}
		fUvdGartMem = mem;
		fUvdGartAt = (UInt32)at;
		fUvdUp = true;
		IOLockUnlock(fLock);
		IOLog("RadeonNI: UVD (GART) is up, UVD_STATUS 0x%08lx\n", (unsigned long)*status);
		IOFree(fw, fwSize);
		return kIOReturnSuccess;
	}

	if (wantSplit && !wantSegment && ref && fHiddenSize) {
		UInt32 seg;

		(void)dmaStageUp();
		IOLockLock(fLock);
		seg = uvdPickSegment(force == 7 ? 0 : force);
		if (seg) {
			fUvdSplit = seg;
			fUvdSegLo = seg << 28;
			fUvdSegHi = fUvdSegLo + (1u << 28);
			fUvdSegMem = &fHidden;
			rdn_uvd_set_decode_segment((int)seg);
			IOLog("RadeonNI: UVD: split mode: the firmware, ring and messages in the aperture, the decoder's buffers in segment %lu (0x%lx)\n",
			      (unsigned long)seg, (unsigned long)fUvdSegLo);
		} else {
			rdn_uvd_set_decode_segment(-1);
			IOLog("RadeonNI: UVD: split mode: no segment has room; in the aperture\n");
		}
		IOLockUnlock(fLock);
	} else if (!wantSegment) {
		rdn_uvd_set_decode_segment(-1);
	}
	if (wantSegment && ref && fHiddenSize) {
		if (noDma || !dmaStageUp()) {
			rdn_uvd_set_put_hook(0, 0);
			if (!noDma)
				IOLog("RadeonNI: UVD: no DMA staging; the register window writes big regions\n");
		} else {
			rdn_uvd_set_put_hook(dmaPutHook, this);
		}
		IOLockLock(fLock);
		ret = uvdStartSegment(flags, force, fw, fwSize, ref, status);
		IOLockUnlock(fLock);
		if (ret == kIOReturnSuccess) {
			IOFree(fw, fwSize);
			return kIOReturnSuccess;
		}
		IOLog("RadeonNI: UVD: not in a segment (%d); in the aperture\n", (int)ret);
	}
	ret = allocVram(vcpuBytes, 4096, &vcpuOff, false);
	if (ret == kIOReturnSuccess)
		ret = allocVram(4096 + RDN_UVD_RING_EXTRA, 4096, &ringOff, false);
	if (ret != kIOReturnSuccess || !ref) {
		IOLog("RadeonNI: UVD: no memory or no reference clock (ref %lu0 kHz)\n",
		      (unsigned long)ref);
		fUvdFailed = true;
		if (vcpuOff)
			freeVram(vcpuOff);
		IOFree(fw, fwSize);
		return kIOReturnNoMemory;
	}
	IOLog("RadeonNI: UVD: firmware %lu bytes, VCPU region at 0x%lx, ring at 0x%lx, reference clock %lu0 kHz; starting\n",
	      (unsigned long)fwSize, (unsigned long)vcpuOff, (unsigned long)ringOff,
	      (unsigned long)ref);
	IOLockLock(fLock);
	if (flags & RDN_UVD_F_BLANK)
		rdn_display_hold(fAccel.card, true);
	rdn_uvd_set_reset_level(fUvdStartLevel);
	r = rdn_uvd_init(&fUvd, &fAccel, ref, (const uint8_t *)fw, fwSize,
			 RDN_UVD_DEFAULT_HANDLES, vcpuOff, ringOff, 4096, flags);
	rdn_uvd_set_reset_level(0);
	fUvdStartLevel = 0;
	if (flags & RDN_UVD_F_BLANK)
		rdn_display_hold(fAccel.card, false);
	*status = rdn_rreg(fAccel.card, 0xf6bc);
	if (r) {
		IOLockUnlock(fLock);
		IOLog("RadeonNI: UVD did not start (%d), UVD_STATUS 0x%08lx\n", r,
		      (unsigned long)*status);
		fUvdSplit = 0;
		rdn_uvd_set_decode_segment(-1);
		freeVram(vcpuOff);
		freeVram(ringOff);
		IOFree(fw, fwSize);
		return kIOReturnIOError;
	}
	fUvdUp = true;
	fUvdRestartBits = RDN_UVD_RESET_SUB | RDN_UVD_RESET_NO_RLC;
	fUvdFlags = flags;
	fUvdRef = ref;
	if (fUvdModeFenceBase) {
		rdn_uvd_set_fence_base(&fUvd, fUvdModeFenceBase);
		fUvdLastSeq = fUvdModeFenceBase;
		fUvdModeFenceBase = 0;
	}
	IOLockUnlock(fLock);
	if (!fUvdScratch)
		(void)allocVram(4096, 4096, &fUvdScratch, false);
	IOLog("RadeonNI: UVD is up, UVD_STATUS 0x%08lx\n", (unsigned long)*status);
	IOFree(fw, fwSize);
	return kIOReturnSuccess;
}

/*
 * The firmware holds ten sessions, made by programs' messages, and it can stop answering a session's decode commands
 * (a long pause of its session with other programs busy on the machine did it, and a session nobody destroys stays):
 * the commands run and the fences come, but nothing is decoded. Starting the block again (about half a second) clears
 * all of that, so it is done whenever a new generation of decoders begins (the first session when none is alive), when
 * the last program that decoded is gone, and when a program whose decoder stopped asks for it. Each start is a new
 * epoch; sessions of an older one are gone. Called with fLock held; nothing happens if no program has used the block
 * since the last start (fUvdDirty).
 */
void RadeonNIAccel::uvdRestart(UInt32 level)
{
	if (fUvd.guarded || fVideoBusy) return;
	UInt32 fwSize = 0, fenceBase;
	void *fw;
	int r;

	if (!fUvdUp || !fUvdDirty || (fUvdFlags & RDN_UVD_F_GART))
		return;
	if (fUvdSeg) {
		/* the firmware window cannot be written again so that it boots (docs/UVD.md): no restarts with everything in a segment */
		static bool said;

		if (!said) {
			said = true;
			IOLog("RadeonNI: UVD: no restart of a block that runs wholly in a segment\n");
		}
		return;
	}
	fw = copyFirmware("FW_UVD", &fwSize);
	if (!fw)
		return;
	fUvdDirty = false;
	fenceBase = fUvd.fence_emitted;
	rdn_uvd_set_reset_level(level);
	rdn_uvd_fini(&fUvd);
	r = rdn_uvd_init(&fUvd, &fAccel, fUvdRef, (const uint8_t *)fw, fwSize, RDN_UVD_DEFAULT_HANDLES,
			 fUvd.vcpu_offset, fUvd.ring_offset, 4096, fUvdFlags);
	rdn_uvd_set_reset_level(0);
	IOFree(fw, fwSize);
	/* the fence numbers go on, for programs that remember the newest one they saw reached */
	if (!r) {
		rdn_uvd_set_fence_base(&fUvd, fenceBase);
		fUvdLastSeq = fenceBase;
	}
	/* the sessions are gone, whether it started again or not; a started block has its clocks on, the next command says so */
	fUvdLive = 0;
	fUvdEpoch++;
	fUvdClocksUp = false;
	if (r) {
		fUvdUp = false;
		IOLog("RadeonNI: UVD did not start again (%d)\n", r);
		return;
	}
	IOLog("RadeonNI: UVD restarted (epoch %lu, reset level %lu)\n", (unsigned long)fUvdEpoch, (unsigned long)level);
}

/*
 * The staging buffer: 1 MB of the kext's own memory in the GART, made when first wanted. Without the lock; takes it for the page table.
 */
bool RadeonNIAccel::dmaStageUp(void)
{
	const UInt32 total = 1u << 20;
	IOBufferMemoryDescriptor *mem;
	uint64_t at = 0;
	UInt32 done;

	if (fDmaMem)
		return true;
	if (!fGart.ready || !fEngineUp)
		return false;
	mem = IOBufferMemoryDescriptor::withOptions(kIODirectionInOut, total, 4096);
	if (!mem || mem->prepare() != kIOReturnSuccess) {
		if (mem)
			mem->release();
		return false;
	}
	bzero(mem->getBytesNoCopy(), total);
	IOLockLock(fLock);
	if (fDmaMem || rdn_mem_alloc(&fGartSpace, total, 4096, &at)) {
		bool have = fDmaMem != 0;

		IOLockUnlock(fLock);
		mem->complete();
		mem->release();
		return have;
	}
	for (done = 0; done < total; done += 4096) {
		IOByteCount length = 0;
		IOPhysicalAddress bus = mem->getPhysicalSegment(done, &length);

		if (!bus)
			break;
		rdn_gart_set_page(&fGart, (UInt32)((at + done) / 4096), bus);
	}
	if (done < total) {
		while (done) {
			done -= 4096;
			rdn_gart_clear_page(&fGart, (UInt32)((at + done) / 4096));
		}
		rdn_gart_flush(&fGart);
		rdn_mem_free(&fGartSpace, at);
		IOLockUnlock(fLock);
		mem->complete();
		mem->release();
		return false;
	}
	rdn_gart_flush(&fGart);
	fDmaMem = mem;
	fDmaCpu = (UInt8 *)mem->getBytesNoCopy();
	fDmaGpu = rdn_gart_addr((UInt32)(at / 4096));
	fDmaAt = (UInt32)at;
	fDmaBytes = total;
	IOLockUnlock(fLock);
	IOLog("RadeonNI: DMA staging of %lu KB in the GART at GPU 0x%lx\n", (unsigned long)(total >> 10), (unsigned long)fDmaGpu);
	return true;
}

/*
 * Write bytes to video memory (beyond the aperture, say) with the command processor's DMA from the staging buffer; the few bytes before
 * a dword boundary and after the last whole dword go through the register window. With fLock held.
 */
int RadeonNIAccel::dmaPut(UInt32 offset, const void *src, UInt32 bytes)
{
	const UInt8 *p = (const UInt8 *)src;
	UInt32 head = (4 - (offset & 3)) & 3, tail;
	int r = 0;

	if (!fDmaMem)
		return rdn_vram_put(fAccel.card, offset, src, bytes);
	if (head > bytes)
		head = bytes;
	if (head) {
		r = rdn_vram_put(fAccel.card, offset, p, head);
		if (r)
			return r;
		offset += head;
		p += head;
		bytes -= head;
	}
	tail = bytes & 3;
	bytes -= tail;
	while (bytes) {
		UInt32 n = bytes < fDmaBytes ? bytes : fDmaBytes;
		uint32_t seq = 0;

		bcopy(p, fDmaCpu, n);
		rdnStoreBarrier();
		r = rdn_cp_dma(&fAccel, fDmaGpu, rdn_vram_addr(&fAccel, offset), n, &seq);
		if (!r)
			r = rdn_fence_wait(&fAccel, seq, 2000);
		if (r)
			return r;
		offset += n;
		p += n;
		bytes -= n;
	}
	if (tail)
		r = rdn_vram_put(fAccel.card, offset, p, tail);
	return r;
}

void RadeonNIAccel::uvdLogWindow(const char *when)
{
	UInt32 fwSize = 0, at, n, bad = 0, first = 0, last = 0, i;
	void *fw;
	UInt8 chunk[256];

	if (!fUvdSeg)
		return;
	fw = copyFirmware("FW_UVD", &fwSize);
	if (!fw)
		return;
	for (at = 0; at < fwSize; at += n) {
		n = fwSize - at < sizeof(chunk) ? fwSize - at : (UInt32)sizeof(chunk);
		if (rdn_vram_get(fAccel.card, fUvdOwn[0].offset + at, chunk, n))
			break;
		for (i = 0; i < n; i++)
			if (chunk[i] != ((const UInt8 *)fw)[at + i]) {
				if (!bad)
					first = at + i;
				last = at + i;
				bad++;
			}
	}
	IOFree(fw, fwSize);
	IOLog("RadeonNI: UVD: firmware window %s: %lu of %lu bytes differ from the file (first at %lu, last at %lu)\n", when,
	      (unsigned long)bad, (unsigned long)fwSize, (unsigned long)first, (unsigned long)last);
}

int RadeonNIAccel::dmaPutHook(void *cookie, uint32_t offset, const void *src, uint32_t bytes)
{
	return ((RadeonNIAccel *)cookie)->dmaPut(offset, src, bytes);
}

/*
 * The decoder in a 256 MB segment of the video memory beyond the aperture (docs/UVD.md). The segment is the hidden
 * one with the most room that has at least 96 MB free in one piece, the highest on a tie (hidden allocations are first
 * fit from the bottom). Called with fLock held. 0 if there is none.
 */
UInt32 RadeonNIAccel::uvdPickSegment(UInt32 force)
{
	const uint64_t kMinFree = 96ull << 20;
	UInt32 n, best = 0;
	uint64_t bestFree = 0;

	if (force == 7) {
		/* a test: the aperture's own heap, but through the register window like the others */
		uint64_t big = 0;

		(void)rdn_mem_available_range(&fMem, HEAP_OFFSET, fAccel.aperture_size, &big);
		return big >= (32ull << 20) ? 7 : 0;
	}
	if (!fHiddenSize)
		return 0;
	for (n = 1; n <= 6; n++) {
		uint64_t lo = (uint64_t)n << 28, hi = lo + (1ull << 28), big = 0, avail;

		if (force && n != force)
			continue;
		if (lo < fHiddenOffset || hi > (uint64_t)fHiddenOffset + fHiddenSize)
			continue;
		avail = rdn_mem_available_range(&fHidden, lo, hi, &big);
		if (big >= kMinFree && avail >= bestFree) {
			best = n;
			bestFree = avail;
		}
	}
	return best;
}

/* Give back what a start in a segment took (fLock held). */
void RadeonNIAccel::uvdSegmentFree(void)
{
	UInt32 i;

	for (i = 0; i < kUvdOwn; i++)
		if (fUvdOwn[i].size) {
			rdn_mem_free(fUvdSegMem, fUvdOwn[i].offset);
			fUvdOwn[i].size = 0;
		}
	if (fUvdVcpuShadow) {
		IOFree(fUvdVcpuShadow, fUvdVcpuShadowBytes);
		fUvdVcpuShadow = 0;
	}
	if (fUvdRingShadow) {
		IOFree(fUvdRingShadow, fUvdRingShadowBytes);
		fUvdRingShadow = 0;
	}
	fUvdSeg = 0;
	fUvdScratch = 0;
}

/* May a program put or get these bytes? Inside the segment and outside the block's own memory (fLock held). */
bool RadeonNIAccel::uvdSegmentCheck(UInt32 offset, UInt32 bytes)
{
	UInt32 i;

	if (!(fUvdSeg || fUvdSplit) || offset < fUvdSegLo || (uint64_t)offset + bytes > fUvdSegHi || offset + bytes < offset)
		return false;
	for (i = 0; i < kUvdOwn; i++)
		if (fUvdOwn[i].size && offset < fUvdOwn[i].offset + fUvdOwn[i].size &&
		    fUvdOwn[i].offset < offset + bytes)
			return false;
	return true;
}

/*
 * Start the block with its firmware window, ring, scratch page and indirect buffer slots in a segment, the way
 * rdn_uvd_init_window() wants them. Called with fLock held; on failure nothing stays taken.
 */
IOReturn RadeonNIAccel::uvdStartSegment(UInt32 flags, UInt32 force, const void *fw, UInt32 fwSize, uint32_t ref,
					UInt32 *status)
{
	UInt32 seg = uvdPickSegment(force), i;
	UInt32 vcpuBytes = rdn_uvd_vcpu_bytes(fwSize, RDN_UVD_DEFAULT_HANDLES);
	UInt32 sizes[kUvdOwn];
	uint64_t lo, hi, at;
	int r;

	if (!seg)
		return kIOReturnNoMemory;
	rdn_uvd_set_decode_segment(-1);
	sizes[0] = vcpuBytes;
	sizes[1] = 8192;			/* the ring (4096 bytes and its fence word) */
	sizes[2] = 4096;			/* the scratch page */
	sizes[3] = kUvdIbSlots * 4096;	/* the indirect buffer slots */
	lo = (uint64_t)seg << 28;
	hi = lo + (1ull << 28);
	fUvdSegMem = &fHidden;
	if (seg == 7) {
		lo = HEAP_OFFSET;
		hi = fAccel.aperture_size;
		fUvdSegMem = &fMem;
	}
	for (i = 0; i < kUvdOwn; i++) {
		if (rdn_mem_alloc_range(fUvdSegMem, sizes[i], 4096, lo, hi, &at)) {
			uvdSegmentFree();
			return kIOReturnNoMemory;
		}
		fUvdOwn[i].offset = (UInt32)at;
		fUvdOwn[i].size = sizes[i];
	}
	fUvdVcpuShadowBytes = vcpuBytes;
	fUvdRingShadowBytes = 4096 + RDN_UVD_RING_EXTRA;
	fUvdVcpuShadow = (UInt8 *)IOMalloc(fUvdVcpuShadowBytes);
	fUvdRingShadow = (UInt8 *)IOMalloc(fUvdRingShadowBytes);
	if (!fUvdVcpuShadow || !fUvdRingShadow) {
		uvdSegmentFree();
		return kIOReturnNoMemory;
	}
	bzero(fUvdVcpuShadow, fUvdVcpuShadowBytes);
	bzero(fUvdRingShadow, fUvdRingShadowBytes);
	fUvdSeg = seg;
	fUvdSegLo = (UInt32)lo;
	fUvdSegHi = (UInt32)hi;
	IOLog("RadeonNI: UVD: firmware %lu bytes, in segment %lu of video memory (0x%lx), VCPU region at 0x%lx, ring at 0x%lx, reference clock %lu0 kHz; starting\n",
	      (unsigned long)fwSize, (unsigned long)seg, (unsigned long)fUvdSegLo, (unsigned long)fUvdOwn[0].offset,
	      (unsigned long)fUvdOwn[1].offset, (unsigned long)ref);
	rdn_uvd_set_reset_level(fUvdStartLevel);
	r = rdn_uvd_init_window(&fUvd, &fAccel, ref, (const uint8_t *)fw, fwSize, RDN_UVD_DEFAULT_HANDLES,
				fUvdVcpuShadow, fUvdOwn[0].offset, fUvdRingShadow, fUvdOwn[1].offset, 4096, flags);
	rdn_uvd_set_reset_level(0);
	fUvdStartLevel = 0;
	*status = rdn_rreg(fAccel.card, 0xf6bc);
	if (r) {
		IOLog("RadeonNI: UVD did not start in the segment (%d), UVD_STATUS 0x%08lx\n", r, (unsigned long)*status);
		/* the block must not run on in memory that is given back */
		rdn_uvd_fini(&fUvd);
		uvdSegmentFree();
		return kIOReturnIOError;
	}
	fUvdScratch = fUvdOwn[2].offset;
	fUvdIbBase = fUvdOwn[3].offset;
	fUvdIbNext = 0;
	for (i = 0; i < kUvdIbSlots; i++)
		fUvdIbFence[i] = 0;
	fUvdUp = true;
	fUvdRestartBits = RDN_UVD_RESET_SUB | RDN_UVD_RESET_NO_RLC;
	fUvdFlags = flags | RDN_UVD_F_EXT;
	fUvdRef = ref;
	if (fUvdModeFenceBase) {
		rdn_uvd_set_fence_base(&fUvd, fUvdModeFenceBase);
		fUvdLastSeq = fUvdModeFenceBase;
		fUvdModeFenceBase = 0;
	}
	IOLog("RadeonNI: UVD is up in segment %lu, UVD_STATUS 0x%08lx\n", (unsigned long)seg, (unsigned long)*status);
	uvdLogWindow("after the start");
	return kIOReturnSuccess;
}

void RadeonNIAccel::uvdSegmentInfo(UInt32 *mode, UInt32 *first, UInt32 *size)
{
	IOLockLock(fLock);
	*mode = fUvdUp ? (fUvdSeg ? 1 : fUvdSplit ? 2 : 0) : 0;
	*first = *mode ? fUvdSegLo : 0;
	*size = *mode ? fUvdSegHi - fUvdSegLo : 0;
	IOLockUnlock(fLock);
}

/*
 * Bytes between a program's memory and the segment, through the register window, 16 KB at a time with the lock let go of
 * between the pieces (the window is slow, and the 3D engine's users want the lock).
 */
static IOMemoryDescriptor *taskMemory(task_t task, UInt32 address, UInt32 bytes, bool toTask);

IOReturn RadeonNIAccel::vramPut(task_t task, UInt32 offset, UInt32 address, UInt32 bytes)
{
	const UInt32 kPiece = 16384;
	IOMemoryDescriptor *memory;
	UInt8 *piece;
	UInt32 done = 0;
	IOReturn ret = kIOReturnSuccess;
	bool ok;

	if (!bytes || bytes > (16u << 20))
		return kIOReturnBadArgument;
	IOLockLock(fLock);
	ok = fUvdUp && uvdSegmentCheck(offset, bytes);
	IOLockUnlock(fLock);
	if (!ok)
		return kIOReturnBadArgument;
	memory = taskMemory(task, address, bytes, false);
	if (!memory)
		return kIOReturnVMError;
	piece = (UInt8 *)IOMalloc(kPiece);
	if (!piece) {
		memory->complete();
		memory->release();
		return kIOReturnNoMemory;
	}
	while (done < bytes && ret == kIOReturnSuccess) {
		UInt32 n = bytes - done < kPiece ? bytes - done : kPiece;

		if (memory->readBytes(done, piece, n) != n) {
			ret = kIOReturnVMError;
			break;
		}
		IOLockLock(fLock);
		if (!fUvdUp || !uvdSegmentCheck(offset + done, n) ||
		    (n >= 4096 && fDmaMem ? dmaPut(offset + done, piece, n) : rdn_vram_put(fAccel.card, offset + done, piece, n)))
			ret = kIOReturnIOError;
		IOLockUnlock(fLock);
		done += n;
	}
	IOFree(piece, kPiece);
	memory->complete();
	memory->release();
	return ret;
}

IOReturn RadeonNIAccel::vramGet(task_t task, UInt32 offset, UInt32 address, UInt32 bytes)
{
	const UInt32 kPiece = 16384;
	IOMemoryDescriptor *memory;
	UInt8 *piece;
	UInt32 done = 0;
	IOReturn ret = kIOReturnSuccess;
	bool ok;

	if (!bytes || bytes > (16u << 20))
		return kIOReturnBadArgument;
	IOLockLock(fLock);
	/* reading the block's own memory is harmless, but the check is the same as for writing, less the own blocks */
	ok = fUvdUp && (fUvdSeg || fUvdSplit) && offset >= fUvdSegLo && (uint64_t)offset + bytes <= fUvdSegHi && offset + bytes > offset;
	IOLockUnlock(fLock);
	if (!ok)
		return kIOReturnBadArgument;
	memory = taskMemory(task, address, bytes, true);
	if (!memory)
		return kIOReturnVMError;
	piece = (UInt8 *)IOMalloc(kPiece);
	if (!piece) {
		memory->complete();
		memory->release();
		return kIOReturnNoMemory;
	}
	while (done < bytes && ret == kIOReturnSuccess) {
		UInt32 n = bytes - done < kPiece ? bytes - done : kPiece;

		IOLockLock(fLock);
		if (!fUvdUp || !(fUvdSeg || fUvdSplit) || rdn_vram_get(fAccel.card, offset + done, piece, n))
			ret = kIOReturnIOError;
		IOLockUnlock(fLock);
		if (ret == kIOReturnSuccess && memory->writeBytes(done, piece, n) != n)
			ret = kIOReturnVMError;
		done += n;
	}
	IOFree(piece, kPiece);
	memory->complete();
	memory->release();
	return ret;
}

/*
 * The first bytes of the message at a video memory address, for uvdFind(): through the aperture when it lies there, through the register
 * window when it lies in the decoder's segment. Called with fLock held.
 */
bool RadeonNIAccel::uvdReadMessageHeader(uint64_t address, UInt8 *out)
{
	uint64_t at;
	UInt32 i;

	if (address < fAccel.vram_base)
		return false;
	at = address - fAccel.vram_base;
	if (at + 12 <= fAccel.aperture_size) {
		volatile UInt8 *m = (volatile UInt8 *)fAccel.aperture + at;

		for (i = 0; i < 12; i++)
			out[i] = m[i];
		return true;
	}
	if (fUvdSeg && at >= fUvdSegLo && at + 12 <= fUvdSegHi)
		return rdn_vram_get(fAccel.card, (uint32_t)at, out, 12) == 0;
	return false;
}

/*
 * The session messages in a program's command stream. The stream is register writes: the message's address goes to
 * GPCOM_VCPU_DATA0 and DATA1, and a write of 0 to GPCOM_VCPU_CMD says "the message is there". Its first words,
 * little-endian, are its size, its type (0 create, 2 destroy) and the stream handle. Reads only; called with fLock held.
 * `w` is the stream's words (in the aperture, or a copy of the kext's own).
 */
UInt32 RadeonNIAccel::uvdFind(const volatile UInt32 *w, UInt32 words, UvdMsg *msgs)
{
	UInt32 i = 0, data0 = 0, data1 = 0, n = 0;

	while (i < words) {
		UInt32 pkt = w[i], type = pkt >> 30, count, reg, j;

		if (type == 2) {
			i++;
			continue;
		}
		if (type != 0)
			return n;
		count = ((pkt >> 16) & 0x3fff) + 1;
		reg = (pkt & 0xffff) << 2;
		if (i + 1 + count > words)
			return n;
		for (j = 0; j < count; j++, reg += 4) {
			UInt32 value = w[i + 1 + j];

			if (reg == UVD_GPCOM_VCPU_DATA0) {
				data0 = value;
			} else if (reg == UVD_GPCOM_VCPU_DATA1) {
				data1 = value;
			} else if (reg == UVD_GPCOM_VCPU_CMD && (value >> 1) == 0) {
				uint64_t addr = ((uint64_t)data1 << 32) | data0;
				UInt8 m[12];
				UInt32 type_;

				if (!uvdReadMessageHeader(addr, m))
					continue;
				type_ = m[4] | m[5] << 8 | m[6] << 16 | (UInt32)m[7] << 24;
				if ((type_ == 0 || type_ == 2) && n < kMaxUvdMsgs) {
					msgs[n].type = type_;
					msgs[n].size = m[0] | m[1] << 8 | m[2] << 16 | (UInt32)m[3] << 24;
					msgs[n].handle = m[8] | m[9] << 8 | m[10] << 16 | (UInt32)m[11] << 24;
					n++;
				}
			}
		}
		i += 1 + count;
	}
	return n;
}

/*
 * Linux's radeon_uvd_get_destroy_msg(): a message of 0xde4 bytes whose type is 2 and whose third word is the handle,
 * in a stream that only names it. Only for sessions of the epoch the firmware is in.
 */
bool RadeonNIAccel::uvdDestroy(const UInt32 *handles, UInt32 count, UInt32 epoch)
{
	UInt32 i;
	bool safe=true;
	IOLockLock(fUvdLock);

	IOLockLock(fLock);
	if (fVideoBusy || fUvd.failed || (count && (!fUvdUp || !fUvdScratch || (fUvdFlags & RDN_UVD_F_GART) || epoch != fUvdEpoch))) {
		IOLockUnlock(fLock);
		IOLockUnlock(fUvdLock);
		return false;
	}
	if (!fUvdClocksUp && count) {
		fUvdClocksUp = rdn_uvd_wake(&fUvd) == 0;
		if (!fUvdClocksUp) safe=false;
	}
	for (i = 0; safe && i < count; i++) {
		UInt8 message[64];
		UInt32 stream[16];
		uint64_t addr = rdn_vram_addr(&fAccel, fUvdScratch);
		UInt32 j, handle = handles[i];
		uint32_t seq = 0;

		for (j = 0; j < 64; j++)
			message[j] = 0;
		message[0] = 0xe4;
		message[1] = 0x0d;
		message[4] = 2;
		message[8] = handle;
		message[9] = handle >> 8;
		message[10] = handle >> 16;
		message[11] = handle >> 24;
		stream[0] = RDN_UVD_PACKET0(UVD_GPCOM_VCPU_DATA0, 0);
		stream[1] = (UInt32)addr;
		stream[2] = RDN_UVD_PACKET0(UVD_GPCOM_VCPU_DATA1, 0);
		stream[3] = (UInt32)(addr >> 32);
		stream[4] = RDN_UVD_PACKET0(UVD_GPCOM_VCPU_CMD, 0);
		stream[5] = 0;
		for (j = 6; j < 16; j++)
			stream[j] = RDN_UVD_PACKET0(UVD_NO_OP, 0);
		if (fUvdSeg) {
			/* beyond the aperture: through the register window */
			if (rdn_vram_put(fAccel.card, fUvdScratch, message, 64) ||
			    rdn_vram_put(fAccel.card, fUvdScratch + 1024, stream, 64)) {
				IOLog("RadeonNI: UVD: could not write a program's session end\n");
				safe=false; break;
			}
		} else {
			volatile UInt8 *m = (volatile UInt8 *)fAccel.aperture + fUvdScratch;
			volatile UInt32 *ib = (volatile UInt32 *)((UInt8 *)fAccel.aperture + fUvdScratch + 1024);

			for (j = 0; j < 64; j++)
				m[j] = message[j];
			for (j = 0; j < 16; j++)
				ib[j] = stream[j];
			rdnStoreBarrier();
		}
		if (rdn_uvd_ib_submit(&fUvd, rdn_vram_addr(&fAccel, fUvdScratch + 1024), 16, &seq)) {
			IOLog("RadeonNI: UVD: could not end a program's session\n");
			safe=false; break;
		}
		fUvdLastSeq = seq;
		fUvdDirty = true;
		IOLockUnlock(fLock);
		bool completed=uvdFenceWait(seq,1000);
		IOLockLock(fLock);
		if (!completed) { safe=false; break; }
		if (fUvdLive)
			fUvdLive--;
	}
	if (safe && fUvdLastSeq) {
		IOLockUnlock(fLock);
		safe=uvdFenceWait(fUvdLastSeq,1000);
		IOLockLock(fLock);
	}
	if (!safe) { fUvd.failed=true; fUvd.ready=false; fUvdState=RDN_UVD_DIAGNOSTIC_FAULTED; fUvdError=-ETIMEDOUT; }
	if (count && safe)
		IOLog("RadeonNI: UVD: ended %lu session(s) a program left\n", (unsigned long)count);
	IOLockUnlock(fLock);
	IOLockUnlock(fUvdLock);
	return safe;
}

IOReturn RadeonNIAccel::uvdSubmit(UInt32 offset, UInt32 words, UInt32 *fence, UInt32 *handles,
				  UInt32 *count, UInt32 *epoch)
{
	UvdMsg msgs[kMaxUvdMsgs];
	UInt32 n;

	if (offset < HEAP_OFFSET || offset >= fAccel.aperture_size || (offset & 3) || !words ||
	    words > (fAccel.aperture_size - offset) / 4)
		return kIOReturnBadArgument;
	IOLockLock(fLock);
	/* a block in a segment takes its streams by value (uvdSubmitWords) */
	if (!fUvdUp || fUvdSeg || fVideoBusy || fUvd.failed || fRetiring) {
		IOLockUnlock(fLock);
		return kIOReturnNotReady;
	}
	n = uvdFind((const volatile UInt32 *)((UInt8 *)fAccel.aperture + offset), words, msgs);
	return uvdSubmitCommon(rdn_vram_addr(&fAccel, offset), words, msgs, n, fence, handles, count, epoch);
}

/*
 * The rest of a submission, with fLock held (and released here): the stream is at `ibAddress` in video memory, `msgs`
 * its `n` session messages.
 */
IOReturn RadeonNIAccel::uvdSubmitCommon(uint64_t ibAddress, UInt32 words, UvdMsg *msgs, UInt32 n, UInt32 *fence,
					UInt32 *handles, UInt32 *count, UInt32 *epoch)
{
	uint32_t seq = 0;
	int r = 0;
	UInt32 i, k;
	bool creates = false;

	for (i = 0; i < n; i++)
		if (msgs[i].type == 0)
			creates = true;
	/* a new generation of decoders: nobody else's session is alive, so the firmware starts clean (and not in the middle of a command) */
	if (creates && !fUvdLive && fUvdDirty && rdn_uvd_fence_done(&fUvd, fUvdLastSeq))
		uvdRestart(fUvdRestartBits);
	if (!fUvdUp) {
		IOLockUnlock(fLock);
		return kIOReturnNotReady;
	}
	/* the program's sessions are void if the firmware was started again since it made them */
	if (*epoch != fUvdEpoch) {
		*count = 0;
		*epoch = fUvdEpoch;
	}
	if (!fUvdClocksUp) {
		r = rdn_uvd_wake(&fUvd);
		fUvdClocksUp = r == 0;
		if (fUvd.flags & RDN_UVD_F_PLL)
			IOLog("RadeonNI: UVD clocks %s\n", r ? "did not come up" : "up");
	}
	/* the program's stores to the write-combined window must be in the card before the doorbell (eieio does not order them) */
	rdnStoreBarrier();
	if (!r)
		r = rdn_uvd_ib_submit(&fUvd, ibAddress, words, &seq);
	if (!r) {
		fUvdLastSeq = seq;
		fUvdDirty = true;
		for (i = 0; i < n; i++) {
			for (k = 0; k < *count; k++)
				if (handles[k] == msgs[i].handle)
					break;
			if (msgs[i].type == 0) {
				if (k == *count && *count < 32) {	/* RadeonNIUserClient::kMaxUvdSessions */
					handles[(*count)++] = msgs[i].handle;
					fUvdLive++;
				}
			} else if (k < *count) {
				handles[k] = handles[--(*count)];
				if (fUvdLive)
					fUvdLive--;
			}
		}
	}
	IOLockUnlock(fLock);
	if (r)
		return kIOReturnIOError;
	*fence = seq;
	return kIOReturnSuccess;
}

/*
 * Copy to or from a program's memory (kIODirectionOut: the kext reads it). The pages are wired for the time.
 */
static IOMemoryDescriptor *taskMemory(task_t task, UInt32 address, UInt32 bytes, bool toTask)
{
	IOMemoryDescriptor *memory = IOMemoryDescriptor::withAddress((vm_address_t)address, bytes,
								     toTask ? kIODirectionIn : kIODirectionOut, task);

	if (!memory)
		return 0;
	if (memory->prepare() != kIOReturnSuccess) {
		memory->release();
		return 0;
	}
	return memory;
}

/*
 * A stream of decoder commands given by value, for a block in a segment: the words are copied into one of the kext's slots in
 * the segment and run from there.
 */
IOReturn RadeonNIAccel::uvdSubmitWords(task_t task, UInt32 address, UInt32 words, UInt32 *fence, UInt32 *handles,
				       UInt32 *count, UInt32 *epoch)
{
	UvdMsg msgs[kMaxUvdMsgs];
	IOMemoryDescriptor *memory;
	UInt32 *stream, n, slot, at;
	IOReturn ret = kIOReturnSuccess;
	bool copied;

	if (!words || words > 1024 || (address & 3))
		return kIOReturnBadArgument;
	stream = (UInt32 *)IOMalloc(words * 4);
	if (!stream)
		return kIOReturnNoMemory;
	memory = taskMemory(task, address, words * 4, false);
	copied = memory && memory->readBytes(0, stream, words * 4) == words * 4;
	if (memory) {
		memory->complete();
		memory->release();
	}
	if (!copied) {
		IOFree(stream, words * 4);
		return kIOReturnVMError;
	}
	IOLockLock(fLock);
	if (!fUvdUp || !fUvdSeg || fVideoBusy || fUvd.failed || fRetiring) {
		IOLockUnlock(fLock);
		IOFree(stream, words * 4);
		return kIOReturnNotReady;
	}
	n = uvdFind(stream, words, msgs);
	{
		static UInt32 said;

		if (said < 24) {
			said++;
			IOLog("RadeonNI: UVD: stream of %lu words, %lu session message(s)%s, live %lu, dirty %d, epoch %lu, clocks %s, UVD_STATUS 0x%08lx; message size %lu type %lu handle 0x%08lx\n",
			      (unsigned long)words, (unsigned long)n, n && msgs[0].type == 0 ? " (create)" : "", (unsigned long)fUvdLive,
			      fUvdDirty, (unsigned long)fUvdEpoch, fUvdClocksUp ? "up" : "down", (unsigned long)rdn_rreg(fAccel.card, 0xf6bc),
			      (unsigned long)(n ? msgs[0].size : 0), (unsigned long)(n ? msgs[0].type : 0), (unsigned long)(n ? msgs[0].handle : 0));
		}
	}
	/* the slot's last stream must have run before it is written over */
	slot = fUvdIbNext;
	if (fUvdIbFence[slot] && !rdn_uvd_fence_done(&fUvd, fUvdIbFence[slot]) &&
	    rdn_uvd_fence_wait(&fUvd, fUvdIbFence[slot], 200)) {
		IOLockUnlock(fLock);
		IOFree(stream, words * 4);
		return kIOReturnBusy;
	}
	at = fUvdIbBase + slot * 4096;
	if (rdn_vram_put(fAccel.card, at, stream, words * 4)) {
		IOLockUnlock(fLock);
		IOFree(stream, words * 4);
		return kIOReturnIOError;
	}
	fUvdIbNext = (slot + 1) % kUvdIbSlots;
	ret = uvdSubmitCommon(rdn_vram_addr(&fAccel, at), words, msgs, n, fence, handles, count, epoch);
	if (ret == kIOReturnSuccess)
		fUvdIbFence[slot] = *fence;
	IOFree(stream, words * 4);
	return ret;
}

/*
 * The command buffer recorder (RDN_UC_IBLOG). Off unless a program turns it on; then each command buffer submitted is listed (time, client, process,
 * size) and read once through the aperture and walked packet by packet. What is noted: packets of type 0 (register writes the 3D
 * engine's drivers do not make), SET_CONFIG_REG and SET_CONTEXT_REG outside their ranges, WRITE_DATA and COPY_DATA into registers, and any address
 * that points into the decoder's own memory (its firmware window, ring and scratch page) in a memory write, a render target or depth base,
 * or a resource descriptor.
 */
static const UInt32 kIbEntries = 512;
static const UInt32 kIbTextBytes = 24 * 1024;

static UInt32 kernelMs(void)
{
	uint64_t abstime, nanos;

	clock_get_uptime(&abstime);
	absolutetime_to_nanoseconds(abstime, &nanos);
	return (UInt32)(nanos / 1000000ULL);
}

void RadeonNIAccel::ibNote(const char *fmt, ...)
{
	va_list ap;
	char line[160];
	UInt32 n;

	if (!fIbText || fIbTextLen + 2 >= kIbTextBytes)
		return;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	for (n = 0; line[n] && fIbTextLen + 2 < kIbTextBytes; n++)
		fIbText[fIbTextLen++] = line[n];
	fIbText[fIbTextLen++] = '\n';
	fIbText[fIbTextLen] = 0;
}

/* Is this offset in the aperture one of the decoder's own pages? */
bool RadeonNIAccel::ibInUvd(UInt32 address)
{
	if (!fUvdUp)
		return false;
	if (address >= fUvd.vcpu_offset && address < fUvd.vcpu_offset + fUvd.vcpu_bytes)
		return true;
	if (address >= fUvd.ring_offset && address < fUvd.ring_offset + 4096 + RDN_UVD_RING_EXTRA)
		return true;
	if (fUvdScratch && address >= fUvdScratch && address < fUvdScratch + 4096)
		return true;
	return false;
}

UInt32 RadeonNIAccel::ibScan(UInt32 ms, UInt32 pid, UInt32 client, UInt32 offset, UInt32 words)
{
	volatile UInt32 *w = (volatile UInt32 *)((UInt8 *)fAccel.aperture + offset);
	UInt32 i = 0, found = 0;
	char who[64];

	snprintf(who, sizeof(who), "t=%lu task=%08lx cl=%08lx ib=0x%lx+%lu", (unsigned long)ms, (unsigned long)pid,
		 (unsigned long)client, (unsigned long)offset, (unsigned long)words);
	while (i < words) {
		UInt32 h = w[i], type = h >> 30, count, op, k, start;

		if (type == 2) {
			i++;
			continue;
		}
		if (type == 0) {
			count = ((h >> 16) & 0x3fff) + 1;
			ibNote("%s: type 0 packet, register 0x%lx, %lu dwords, at word %lu", who, (unsigned long)((h & 0x1fff) << 2),
			       (unsigned long)count, (unsigned long)i);
			found++;
			i += 1 + count;
			continue;
		}
		if (type != 3) {
			ibNote("%s: header 0x%08lx is no packet, at word %lu", who, (unsigned long)h, (unsigned long)i);
			found++;
			break;
		}
		op = (h >> 8) & 0xff;
		count = ((h >> 16) & 0x3fff) + 1;
		if (i + 1 + count > words) {
			ibNote("%s: packet 0x%02lx of %lu dwords runs past the buffer, at word %lu", who, (unsigned long)op,
			       (unsigned long)count, (unsigned long)i);
			found++;
			break;
		}
		{
			volatile UInt32 *d = w + i + 1;

			switch (op) {
			case 0x68:	/* SET_CONFIG_REG */
				start = 0x8000 + (d[0] << 2);
				if (start < 0x8000 || start + (count - 1) * 4 > 0xb000) {
					ibNote("%s: SET_CONFIG_REG 0x%lx, %lu regs, outside its range, at word %lu", who,
					       (unsigned long)start, (unsigned long)(count - 1), (unsigned long)i);
					found++;
				}
				break;
			case 0x69:	/* SET_CONTEXT_REG */
				start = 0x28000 + (d[0] << 2);
				if (start < 0x28000 || start + (count - 1) * 4 > 0x29000) {
					ibNote("%s: SET_CONTEXT_REG 0x%lx, %lu regs, outside its range, at word %lu", who,
					       (unsigned long)start, (unsigned long)(count - 1), (unsigned long)i);
					found++;
				}
				for (k = 1; k < count; k++)
					if (ibInUvd(d[k] << 8)) {
						ibNote("%s: SET_CONTEXT_REG 0x%lx = 0x%lx points into the decoder's memory (at 0x%lx), at word %lu",
						       who, (unsigned long)(start + (k - 1) * 4), (unsigned long)d[k],
						       (unsigned long)(d[k] << 8), (unsigned long)i);
						found++;
					}
				break;
			case 0x6d:	/* SET_RESOURCE */
				for (k = 1; k < count; k++)
					if (ibInUvd(d[k]) || ibInUvd(d[k] << 8)) {
						ibNote("%s: SET_RESOURCE dword %lu = 0x%lx points into the decoder's memory, at word %lu", who,
						       (unsigned long)k, (unsigned long)d[k], (unsigned long)i);
						found++;
					}
				break;
			case 0x37:	/* WRITE_DATA */
				if (count >= 3) {
					UInt32 dst = (d[0] >> 8) & 0xf;

					if (dst == 0) {
						ibNote("%s: WRITE_DATA into register 0x%lx, %lu dwords, at word %lu", who,
						       (unsigned long)(d[1] << 2), (unsigned long)(count - 3), (unsigned long)i);
						found++;
					} else if (ibInUvd(d[1] & ~(UInt32)3)) {
						ibNote("%s: WRITE_DATA to 0x%lx in the decoder's memory, at word %lu", who,
						       (unsigned long)d[1], (unsigned long)i);
						found++;
					}
				}
				break;
			case 0x40:	/* COPY_DATA */
				if (count >= 5) {
					UInt32 dst = (d[0] >> 8) & 0xf;

					if (dst == 0) {
						ibNote("%s: COPY_DATA into register 0x%lx, at word %lu", who, (unsigned long)(d[3] << 2),
						       (unsigned long)i);
						found++;
					} else if (ibInUvd(d[3] & ~(UInt32)3)) {
						ibNote("%s: COPY_DATA to 0x%lx in the decoder's memory, at word %lu", who,
						       (unsigned long)d[3], (unsigned long)i);
						found++;
					}
				}
				break;
			case 0x47:	/* EVENT_WRITE_EOP */
				if (count >= 5 && ibInUvd(d[1] & ~(UInt32)3)) {
					ibNote("%s: EVENT_WRITE_EOP to 0x%lx in the decoder's memory, at word %lu", who,
					       (unsigned long)d[1], (unsigned long)i);
					found++;
				}
				break;
			case 0x3d:	/* MEM_WRITE */
				if (count >= 3 && ibInUvd(d[0] & ~(UInt32)3)) {
					ibNote("%s: MEM_WRITE to 0x%lx in the decoder's memory, at word %lu", who,
					       (unsigned long)d[0], (unsigned long)i);
					found++;
				}
				break;
			default:
				break;
			}
		}
		i += 1 + count;
	}
	return found;
}

void RadeonNIAccel::ibRecord(UInt32 kind, void *client, UInt32 offset, UInt32 words)
{
	UInt32 ms, pid, found = 0;
	IbEntry *e;

	if (!fIbOn)
		return;
	ms = kernelMs();
	pid = (UInt32)(unsigned long)current_task();	/* the task's address stands for the process (no BSD interface in this kext) */
	if (kind == '3' && offset >= HEAP_OFFSET && words && words <= (fAccel.aperture_size - offset) / 4 && !(offset & 3))
		found = ibScan(ms, pid, (UInt32)(unsigned long)client, offset, words);
	fIbFound += found;
	e = &fIbEntries[fIbTotal % kIbEntries];
	e->ms = ms;
	e->client = (UInt32)(unsigned long)client;
	e->pid = pid;
	e->offset = offset;
	e->words = words;
	e->kind = kind;
	e->found = found;
	fIbTotal++;
}

/* Diagnostics on the memory controller's busy flags (RDN_UC_DEBUG); each returns SRBM_STATUS 20 ms later. */
IOReturn RadeonNIAccel::debugOp(UInt32 op, UInt32 *srbmStatus)
{
	IOReturn ret = kIOReturnSuccess;

	IOLockLock(fLock);
	if (fVideoBusy || fUvd.guarded || fRetiring) { IOLockUnlock(fLock); return kIOReturnBusy; }
	switch (op) {
	case 1:
		rdn_display_hold(fAccel.card, true);
		break;
	case 2:
		rdn_display_hold(fAccel.card, false);
		break;
	case 3:
		rdn_hdp_flush(&fAccel);
		break;
	case 4:
	case 5:
		if (fUvdUp && fUvdClocksUp)
			rdn_uvd_debug_reset(&fUvd, op - 3);
		else
			ret = kIOReturnNotReady;
		break;
	default:
		if (op >= 20 && op < 84)
			fUvdRestartBits = op - 20;
		else
			ret = kIOReturnBadArgument;
	}
	IOLockUnlock(fLock);
	IOSleep(20);
	*srbmStatus = rdn_rreg(fAccel.card, 0xe50);
	return ret;
}

IOReturn RadeonNIAccel::ibLogControl(UInt32 op, UInt32 *ms)
{
	*ms = kernelMs();
	if (op == 0) {
		fIbOn = false;
		return kIOReturnSuccess;
	}
	if (op == 1) {
		if (!fIbEntries)
			fIbEntries = (IbEntry *)IOMalloc(kIbEntries * sizeof(IbEntry));
		if (!fIbText)
			fIbText = (char *)IOMalloc(kIbTextBytes);
		if (!fIbEntries || !fIbText)
			return kIOReturnNoMemory;
		bzero(fIbEntries, kIbEntries * sizeof(IbEntry));
		fIbText[0] = 0;
		fIbTextLen = 0;
		fIbTotal = 0;
		fIbFound = 0;
		fIbOn = true;
		return kIOReturnSuccess;
	}
	if (!fIbEntries || !fIbText)
		return kIOReturnNotReady;
	if (op == 3) {
		ibRecord('M', 0, 0, 0);
		return kIOReturnSuccess;
	}
	if (op == 2) {
		/* the findings, then the last entries (oldest first) */
		static char text[32 * 1024];
		UInt32 len = 0, first, n;

		len += snprintf(text + len, sizeof(text) - len, "on=%d entries=%lu findings=%lu now=%lu\n", fIbOn ? 1 : 0,
				(unsigned long)fIbTotal, (unsigned long)fIbFound, (unsigned long)*ms);
		len += snprintf(text + len, sizeof(text) - len, "-- findings --\n%s", fIbText);
		len += snprintf(text + len, sizeof(text) - len, "-- last entries: time kind client task ib words findings --\n");
		first = fIbTotal > 160 ? fIbTotal - 160 : 0;
		for (n = first; n < fIbTotal && len + 80 < sizeof(text); n++) {
			IbEntry *e = &fIbEntries[n % kIbEntries];

			len += snprintf(text + len, sizeof(text) - len, "%lu %c %08lx %lu 0x%lx %lu %lu\n", (unsigned long)e->ms,
					(char)e->kind, (unsigned long)e->client, (unsigned long)e->pid, (unsigned long)e->offset,
					(unsigned long)e->words, (unsigned long)e->found);
		}
		setProperty("IBLog", text);
		return kIOReturnSuccess;
	}
	return kIOReturnBadArgument;
}

/*
 * A program's decoder stopped answering: start the firmware again now. Waits (a little) for what is running, then does
 * it whatever else is going on; the other programs' sessions are gone, and they find out by the epoch.
 */
IOReturn RadeonNIAccel::uvdReset(UInt32 *epoch)
{
	UInt32 waited = 0;

	IOLockLock(fLock);
	if (fVideoBusy || fUvd.guarded || fRetiring) { IOLockUnlock(fLock); return kIOReturnBusy; }
	if (!fUvdUp) {
		IOLockUnlock(fLock);
		return kIOReturnNotReady;
	}
	while (fUvdLastSeq && !rdn_uvd_fence_done(&fUvd, fUvdLastSeq) && waited++ < 200) {
		IOLockUnlock(fLock);
		IOSleep(1);
		IOLockLock(fLock);
	}
	fUvdDirty = true;
	uvdRestart(fUvdRestartBits);
	*epoch = fUvdEpoch;
	IOLockUnlock(fLock);
	return fUvdUp ? kIOReturnSuccess : kIOReturnIOError;
}

bool RadeonNIAccel::uvdFenceWait(UInt32 fence, UInt32 timeoutMs)
{
	UInt32 waited = 0;
	bool done;

	for (;;) {
		IOLockLock(fLock);
		done = fUvdUp && rdn_uvd_fence_done(&fUvd, fence);
		if (!done && waited >= timeoutMs && timeoutMs >= 1000 && fUvdUp && fUvdClocksUp && fUvdDumps < 3) {
			/* a stream that never finished: what the block says (the clocks are up, so the reads answer) */
			UInt8 word[4], sample[32];
			UInt32 at = fUvdSeg ? fUvdOwn[1].offset : fUvd.ring_offset;

			fUvdDumps++;
			rdn_uvd_dump(&fUvd, "a stream did not finish");
			uvdLogWindow("at the stall");
			if (fUvdSeg && !rdn_vram_get(fAccel.card, at + 4096, word, 4))
				IOLog("RadeonNI: UVD: fence word %02x%02x%02x%02x (waiting for %lu), wptr %lu\n", word[0], word[1], word[2], word[3],
				      (unsigned long)fence, (unsigned long)fUvd.wptr);
			if (fUvdSeg && !rdn_vram_get(fAccel.card, fUvdIbBase + ((fUvdIbNext + kUvdIbSlots - 1) % kUvdIbSlots) * 4096, sample, 32))
				IOLog("RadeonNI: UVD: last stream %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
				      (unsigned long)((sample[0] << 24) | (sample[1] << 16) | (sample[2] << 8) | sample[3]),
				      (unsigned long)((sample[4] << 24) | (sample[5] << 16) | (sample[6] << 8) | sample[7]),
				      (unsigned long)((sample[8] << 24) | (sample[9] << 16) | (sample[10] << 8) | sample[11]),
				      (unsigned long)((sample[12] << 24) | (sample[13] << 16) | (sample[14] << 8) | sample[15]),
				      (unsigned long)((sample[16] << 24) | (sample[17] << 16) | (sample[18] << 8) | sample[19]),
				      (unsigned long)((sample[20] << 24) | (sample[21] << 16) | (sample[22] << 8) | sample[23]),
				      (unsigned long)((sample[24] << 24) | (sample[25] << 16) | (sample[26] << 8) | sample[27]),
				      (unsigned long)((sample[28] << 24) | (sample[29] << 16) | (sample[30] << 8) | sample[31]));
		}
		IOLockUnlock(fLock);
		if (done || waited >= timeoutMs)
			return done;
		IOSleep(1);
		waited++;
	}
}

/*
 * The clocks go up with a client's first command stream and stay up while
 * a client that has submitted one is open: raising them takes a few
 * hundred milliseconds, and a decoder submits every frame. The last
 * client to go lowers them, once its work is done.
 */
void RadeonNIAccel::uvdUser(bool add)
{
	IOLockLock(fLock);
	if (add) {
		fUvdUsers++;
	} else if (fUvdUsers && !--fUvdUsers && fUvdUp && fUvdClocksUp && !fVideoBusy && !fUvd.failed) {
		UInt32 waited = 0;

		while (fUvdLastSeq && !rdn_uvd_fence_done(&fUvd, fUvdLastSeq) && waited++ < 200) {
			IOLockUnlock(fLock);
			IOSleep(1);
			IOLockLock(fLock);
		}
		if (!fUvdUsers && !fVideoBusy && (!fUvdLastSeq || rdn_uvd_fence_done(&fUvd, fUvdLastSeq))) {
			(void)rdn_uvd_idle(&fUvd);
			fUvdClocksUp = false;
			if (fUvd.flags & RDN_UVD_F_PLL)
				IOLog("RadeonNI: UVD clocks down\n");
			uvdRestart(fUvdRestartBits);
		}
	}
	IOLockUnlock(fLock);
}

IOReturn RadeonNIAccel::uvdInfo(UInt32 *up, UInt32 *vcpuOffset, UInt32 *ringOffset)
{
	*up = fUvdUp ? 1 : 0;
	*vcpuOffset = fUvdUp ? fUvd.vcpu_offset : 0;
	*ringOffset = fUvdUp ? fUvd.ring_offset : 0;
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
	if (fVideoBusy && state != RDN_UC_POWER_QUERY) {
		IOLockUnlock(fLock);
		return kIOReturnBusy;
	}
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

IOReturn RadeonNIAccel::waitForCPU(void)
{
	UInt32 fence;

	IOLockLock(fLock);
	if (!fEngineUp) {
		IOLockUnlock(fLock);
		return kIOReturnNotReady;
	}
	fence = fAccel.fence_emitted;
	IOLockUnlock(fLock);
	/* Surface imports do not yet carry a per-surface producer fence. */
	return fenceWait(fence, 2000) ? kIOReturnSuccess : kIOReturnTimeout;
}

void RadeonNIAccel::syncForCPU(void)
{
	IOLockLock(fLock);
	rdn_hdp_flush(&fAccel);
	IOLockUnlock(fLock);
}

/* fLock held. Existing records survive a failed expansion. */
int RadeonNIAccel::surfaceSlot(UInt32 wid, bool create)
{
	int empty = -1;
	for (UInt32 i = 0; i < fShapeCapacity; ++i) {
		if (fShapes[i].used && fShapes[i].wid == wid) return i;
		if (!fShapes[i].used && empty < 0) empty = i;
	}
	if (!create || empty >= 0) return create ? empty : -1;
	UInt32 capacity = fShapeCapacity ? fShapeCapacity * 2 : 32;
	if (capacity <= fShapeCapacity || capacity > 0x7fffffffU / sizeof(*fShapes))
		return -1;
	SurfaceRecord *records = (SurfaceRecord *)IOMalloc(capacity * sizeof(*records));
	if (!records) return -1;
	bzero(records, capacity * sizeof(*records));
	if (fShapes) {
		bcopy(fShapes, records, fShapeCapacity * sizeof(*records));
		IOFree(fShapes, fShapeCapacity * sizeof(*fShapes));
	}
	empty = fShapeCapacity;
	fShapes = records;
	fShapeCapacity = capacity;
	return empty;
}

void RadeonNIAccel::querySurface(UInt32 wid, struct rdn_user_surface_info *info)
{
	bzero(info, sizeof(*info));
	IOLockLock(fLock);
	int slot = surfaceSlot(wid, false);
	if (slot >= 0) {
		info->id = wid;
		info->offset = fShapes[slot].bufOffset;
		info->row_bytes = fShapes[slot].bufRowBytes;
		info->width = fShapes[slot].bufWidth;
		info->height = fShapes[slot].bufHeight;
	}
	IOLockUnlock(fLock);
}

bool RadeonNIAccel::setSurfaceRegion(UInt32 wid, const IOAccelDeviceRegion *rgn,
				     UInt32 rects)
{
	int slot;
	UInt32 r;

	IOLockLock(fLock);
	slot = surfaceSlot(wid, true);
	if (slot >= 0) {
		struct rdn_user_region *region = &fShapes[slot].region;

		if (!fShapes[slot].used) {
			bzero(&fShapes[slot], sizeof(fShapes[slot]));
		}
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
	return slot >= 0;
}

bool RadeonNIAccel::getSurfaceRegion(UInt32 wid, struct rdn_user_region *region)
{
	bool found = false;
	int i;

	IOLockLock(fLock);
	for (i = 0; i < fShapeCapacity && !found; i++) {
		if (!fShapes[i].used || fShapes[i].wid != wid)
			continue;
		*region = fShapes[i].region;
		found = true;
	}
	IOLockUnlock(fLock);
	return found;
}

bool RadeonNIAccel::setSurfaceBuffer(UInt32 wid, UInt32 offset, UInt32 rowBytes,
				     UInt32 width, UInt32 height, void *owner, task_t producerTask)
{
	int slot;

	IOLockLock(fLock);
	slot = surfaceSlot(wid, true);
	if (slot >= 0) {
		/* The owner may be first: the shape then comes later. */
		if (!fShapes[slot].used) {
			bzero(&fShapes[slot], sizeof(fShapes[slot]));
			fShapes[slot].used = true;
			fShapes[slot].wid = wid;
			bzero(&fShapes[slot].region, sizeof(fShapes[slot].region));
		}
		fShapes[slot].gl = true;
		if (!fShapes[slot].owner || width)
			fShapes[slot].owner = owner;
		fShapes[slot].bufOffset = offset;
		fShapes[slot].bufRowBytes = rowBytes;
		fShapes[slot].bufWidth = width;
		fShapes[slot].bufHeight = height;
        if (width) fShapes[slot].producerTask = producerTask;
	}
	IOLockUnlock(fLock);
	return slot >= 0;
}

bool RadeonNIAccel::getSurfaceBuffer(UInt32 wid, UInt32 *offset, UInt32 *rowBytes,
				     UInt32 *width, UInt32 *height)
{
	bool found = false;
	int i;

	IOLockLock(fLock);
	for (i = 0; i < fShapeCapacity && !found; i++) {
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

bool RadeonNIAccel::setSurfaceBacking(UInt32 wid, task_t task,
                                     const struct rdn_user_backing *backing)
{
	int slot = -1;
	IOLockLock(fLock);
	slot = surfaceSlot(wid, true);
	if (slot >= 0) {
		if (!fShapes[slot].used) bzero(&fShapes[slot], sizeof(fShapes[slot]));
		fShapes[slot].used = true;
		fShapes[slot].wid = wid;
		fShapes[slot].backingTask = task;
		fShapes[slot].backing = *backing;
	}
	IOLockUnlock(fLock);
	return slot >= 0;
}

bool RadeonNIAccel::getSurfaceBacking(UInt32 wid, task_t task,
                                     struct rdn_user_backing *backing)
{
	bool found = false;
	IOLockLock(fLock);
	for (int i = 0; i < fShapeCapacity; ++i) {
		if (fShapes[i].used && fShapes[i].wid == wid &&
		    fShapes[i].backingTask == task && fShapes[i].backing.width) {
			*backing = fShapes[i].backing;
			found = true;
			break;
		}
	}
	IOLockUnlock(fLock);
	return found;
}

bool RadeonNIAccel::isGLSurface(UInt32 wid)
{
	bool found = false;
	int i;

	IOLockLock(fLock);
	for (i = 0; i < fShapeCapacity && !found; i++)
		found = fShapes[i].used && fShapes[i].wid == wid && fShapes[i].gl;
	IOLockUnlock(fLock);
	return found;
}

void RadeonNIAccel::listSurfaces(struct rdn_user_surfaces *list)
{
	int i;

	list->count = 0;
	IOLockLock(fLock);
	for (i = 0; i < fShapeCapacity && list->count < RDN_USER_SURFACES; i++) {
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
	for (i = 0; i < fShapeCapacity; i++) {
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
	for (i = 0; i < fShapeCapacity; i++)
		if (fShapes[i].used && fShapes[i].wid == wid && !fShapes[i].backingClient)
			bzero(&fShapes[i], sizeof(fShapes[i]));
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
    fSurfaceGpuFailed = false;
	fProtocolV2 = type == RDN_UC_TYPE_V2;
	fUvdAdministrator = clientHasPrivilege(securityID,
		kIOClientPrivilegeAdministrator) == kIOReturnSuccess;
    fSurfaceCompositor = fUvdAdministrator;
#ifdef RDN_LEOPARD
    char processName[32];
    bzero(processName, sizeof(processName));
    proc_selfname(processName, sizeof(processName));
    fSurfaceCompositor = fUvdAdministrator ||
        (kauth_cred_getuid(kauth_cred_get()) == 88 && !strcmp(processName, "WindowServer"));
#endif
	fAllocationLock = IOLockAlloc();
    fOffsets = (UInt32 *)IOMalloc(kMaxAllocations * sizeof(UInt32));
    if (!fAllocationLock || !fOffsets) {
        if (fAllocationLock) IOLockFree(fAllocationLock);
        if (fOffsets) IOFree(fOffsets, kMaxAllocations * sizeof(UInt32));
        fAllocationLock = 0; fOffsets = 0;
        return false;
    }
	fCount = 0;
	fUvdUser = false;
	fUvdHandleCount = 0;
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
	if (!fAccel || !fOffsets)
		return;
    IOLockLock(fAllocationLock);
	bool uvdSafe = !fUvdUser || fAccel->uvdDestroy(fUvdHandles, fUvdHandleCount, fUvdHandleEpoch);
	if (uvdSafe && !fSurfaceGpuFailed)
		fAccel->releaseClientMemory(fOffsets, fCount, this);
	else
		fAccel->retainClientMemory(fOffsets, fCount, this);
	fUvdHandleCount = 0;
	fCount = 0;
	if (fUvdUser) {
		fUvdUser = false;
		fAccel->uvdUser(false);
	}
    IOLockUnlock(fAllocationLock);
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
    if (fAllocationLock) { IOLockFree(fAllocationLock); fAllocationLock = 0; }
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
		{ 0, (IOMethod)&RadeonNIUserClient::methodUvdInit,
		  kIOUCScalarIScalarO, 1, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodUvdSubmit,
		  kIOUCScalarIScalarO, 2, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodUvdFenceWait,
		  kIOUCScalarIScalarO, 2, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodUvdInfo,
		  kIOUCScalarIScalarO, 0, 3 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodUvdReset,
		  kIOUCScalarIScalarO, 0, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodUvdEpoch,
		  kIOUCScalarIScalarO, 0, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodIbLog,
		  kIOUCScalarIScalarO, 1, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodDebug,
		  kIOUCScalarIScalarO, 1, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodUvdSegment,
		  kIOUCScalarIScalarO, 0, 3 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodAllocUvd,
		  kIOUCScalarIScalarO, 2, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodVramPut,
		  kIOUCScalarIScalarO, 3, 0 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodVramGet,
		  kIOUCScalarIScalarO, 3, 0 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodUvdSubmitWords,
		  kIOUCScalarIScalarO, 2, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodGartBind64,
		  kIOUCScalarIScalarO, 3, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodSurfaceBacking,
		  kIOUCScalarIStructO, 1, sizeof(struct rdn_user_backing) },
		{ 0, (IOMethod)&RadeonNIUserClient::methodSurfaceQuery,
		  kIOUCScalarIStructO, 1, sizeof(struct rdn_user_surface_info) },
		{ 0, (IOMethod)&RadeonNIUserClient::methodUvdDiagnosticInfo,
		  kIOUCScalarIStructO, 0, sizeof(struct rdn_user_uvd_diagnostic) },
		{ 0, (IOMethod)&RadeonNIUserClient::methodUvdDiagnosticInit,
		  kIOUCScalarIScalarO, 1, 1 },
		{ 0, (IOMethod)&RadeonNIUserClient::methodUvdFixtureRun,
		  kIOUCScalarIStructO, 1, sizeof(struct rdn_user_uvd_fixture) },
		{ 0, (IOMethod)&RadeonNIUserClient::methodUvdFixtureRead,
		  kIOUCScalarIStructO, 2, RDN_UVD_FIXTURE_CHUNK_BYTES },
        { 0, (IOMethod)&RadeonNIUserClient::methodSurfaceGpuInfo,
          kIOUCScalarIStructO, 1, sizeof(struct rdn_user_surface_info) },
        { 0, (IOMethod)&RadeonNIUserClient::methodSurfaceGpuCopy,
          kIOUCScalarIScalarO, 5, 0 },
	};

	if (index >= (fProtocolV2 ? RDN_UC_METHOD_COUNT : RDN_UC_V1_METHOD_COUNT))
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

IOReturn RadeonNIUserClient::methodUvdDiagnosticInfo(
	struct rdn_user_uvd_diagnostic *info, IOByteCount *size)
{
	if (!fAccel || !info || !size || *size < sizeof(*info))
		return kIOReturnBadArgument;
	fAccel->uvdDiagnosticInfo(info);
	*size = sizeof(*info);
	return kIOReturnSuccess;
}

IOReturn RadeonNIUserClient::methodUvdDiagnosticInit(UInt32 reserved, UInt32 *status)
{
	if (!status || reserved || !fAccel)
		return kIOReturnBadArgument;
	*status = 0;
	if (!fUvdAdministrator)
		return kIOReturnNotPrivileged;
	return fAccel->uvdDiagnosticInit(status);
}

IOReturn RadeonNIUserClient::methodSurfaceQuery(UInt32 wid,
	struct rdn_user_surface_info *info, IOByteCount *size)
{
	if (!info || !size || *size < sizeof(*info)) return kIOReturnBadArgument;
	fAccel->querySurface(wid, info);
	*size = sizeof(*info);
	return kIOReturnSuccess;
}

IOReturn RadeonNIUserClient::allocateMemory(UInt32 size, UInt32 align, UInt32 *offset,
    bool hidden, bool segment)
{
    IOLockLock(fAllocationLock);
    IOReturn result = kIOReturnNoResources;
    if (fCount < kMaxAllocations && !fSurfaceGpuFailed) {
        result = fAccel->allocVram(size, align, offset, hidden, segment);
        if (result == kIOReturnSuccess) fOffsets[fCount++] = *offset;
    }
    IOLockUnlock(fAllocationLock);
    return result;
}

IOReturn RadeonNIUserClient::methodAlloc(UInt32 size, UInt32 align, UInt32 *offset)
{ return allocateMemory(size, align, offset, false, false); }

IOReturn RadeonNIUserClient::methodAllocHidden(UInt32 size, UInt32 align, UInt32 *offset)
{ return allocateMemory(size, align, offset, true, false); }

IOReturn RadeonNIUserClient::methodAllocUvd(UInt32 size, UInt32 align, UInt32 *offset)
{ return allocateMemory(size, align, offset, false, true); }

IOReturn RadeonNIUserClient::methodUvdSegment(UInt32 *mode, UInt32 *first, UInt32 *size)
{
	fAccel->uvdSegmentInfo(mode, first, size);
	return kIOReturnSuccess;
}

IOReturn RadeonNIUserClient::methodVramPut(UInt32 offset, UInt32 address, UInt32 bytes)
{
	return fAccel->vramPut(fTask, offset, address, bytes);
}

IOReturn RadeonNIUserClient::methodVramGet(UInt32 offset, UInt32 address, UInt32 bytes)
{
	return fAccel->vramGet(fTask, offset, address, bytes);
}

IOReturn RadeonNIUserClient::methodUvdSubmitWords(UInt32 address, UInt32 words, UInt32 *fence)
{
	IOReturn admission=claimUvd();
	if (admission!=kIOReturnSuccess) return admission;
	IOReturn ret = fAccel->uvdSubmitWords(fTask, address, words, fence, fUvdHandles, &fUvdHandleCount,
					      &fUvdHandleEpoch);

	return ret;
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

IOReturn RadeonNIUserClient::methodGartBind64(UInt32 low, UInt32 high,
                                                UInt32 size, UInt32 *offset)
{
	return fAccel->gartBind(fTask, ((UInt64)high << 32) | low, size, this, offset);
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
	fAccel->ibRecord('F', this, x, width);
	return fAccel->screenFill((SInt32)x, (SInt32)y, (SInt32)width,
				  (SInt32)height, colour, fence);
}

/* Width and height share an argument: a method has six in all. */
IOReturn RadeonNIUserClient::methodScreenCopy(UInt32 sx, UInt32 sy, UInt32 dx,
					      UInt32 dy, UInt32 size,
					      UInt32 *fence)
{
	*fence = 0;
	fAccel->ibRecord('C', this, sx, size);
	return fAccel->screenCopy((SInt32)sx, (SInt32)sy, (SInt32)dx, (SInt32)dy,
				  (SInt32)(size & 0xffff), (SInt32)(size >> 16),
				  fence);
}

IOReturn RadeonNIUserClient::methodFree(UInt32 offset)
{
    IOLockLock(fAllocationLock);
    IOReturn result = fSurfaceGpuFailed ? kIOReturnNotReady : kIOReturnBadArgument;
    for (UInt32 i = 0; !fSurfaceGpuFailed && i < fCount; ++i) {
        if (fOffsets[i] != offset) continue;
        fOffsets[i] = fOffsets[--fCount];
        fAccel->freeVram(offset);
        result = kIOReturnSuccess;
        break;
    }
    IOLockUnlock(fAllocationLock);
    return result;
}

IOReturn RadeonNIUserClient::methodSubmit(UInt32 offset, UInt32 words,
					  UInt32 *fence)
{
	fAccel->ibRecord('3', this, offset, words);
	IOReturn result = fAccel->submit(offset, words, fence);
    if (result == kIOReturnSuccess) result = fAccel->refreshSurfaceGpuOwner(this);
    return result;
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

IOReturn RadeonNIUserClient::methodUvdInit(UInt32 flags, UInt32 *status)
{
	IOReturn admission=claimUvd();
	if (admission!=kIOReturnSuccess) return admission;
	return fAccel->uvdInit(flags, status);
}

IOReturn RadeonNIUserClient::methodUvdSubmit(UInt32 offset, UInt32 words,
					     UInt32 *fence)
{
	IOReturn admission=claimUvd();
	if (admission!=kIOReturnSuccess) return admission;
	fAccel->ibRecord('U', this, offset, words);
	IOReturn ret = fAccel->uvdSubmit(offset, words, fence, fUvdHandles, &fUvdHandleCount, &fUvdHandleEpoch);

	return ret;
}

IOReturn RadeonNIUserClient::methodUvdReset(UInt32 *epoch)
{
	IOReturn admission=claimUvd();
	if (admission!=kIOReturnSuccess) return admission;
	IOReturn ret = fAccel->uvdReset(epoch);

	if (ret == kIOReturnSuccess) {
		/* this program's sessions went with the rest */
		fUvdHandleCount = 0;
		fUvdHandleEpoch = *epoch;
	}
	return ret;
}

IOReturn RadeonNIUserClient::methodDebug(UInt32 op, UInt32 *status)
{
	return fAccel->debugOp(op, status);
}

IOReturn RadeonNIUserClient::methodIbLog(UInt32 op, UInt32 *ms)
{
	return fAccel->ibLogControl(op, ms);
}

IOReturn RadeonNIUserClient::methodUvdEpoch(UInt32 *epoch)
{
	*epoch = fAccel->uvdEpoch();
	return kIOReturnSuccess;
}

IOReturn RadeonNIUserClient::methodUvdFenceWait(UInt32 fence, UInt32 timeoutMs,
						UInt32 *reached)
{
	*reached = fAccel->uvdFenceWait(fence, timeoutMs) ? 1 : 0;
	return kIOReturnSuccess;
}

IOReturn RadeonNIUserClient::methodUvdInfo(UInt32 *up, UInt32 *vcpuOffset,
					   UInt32 *ringOffset)
{
	return fAccel->uvdInfo(up, vcpuOffset, ringOffset);
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
	/* Release reservations even when the pbuffer never attached. */
	if (offset == 0xffffffffU && !rowBytes && !width && !height) {
		fAccel->forgetSurface(wid);
		return kIOReturnSuccess;
	}
	/* Inside the aperture, or nothing. */
	if (width && ((UInt64)offset + (UInt64)rowBytes * height >
		      fAccel->apertureBytes() || rowBytes < width * 4))
		return kIOReturnBadArgument;
    IOLockLock(fAllocationLock);
    bool owned = !width;
    for (UInt32 i = 0; i < fCount; ++i) if (fOffsets[i] == offset) owned = true;
    UInt32 oldOffset, oldStride, oldWidth, oldHeight;
    bool changed = width && (!fAccel->getSurfaceBuffer(wid, &oldOffset, &oldStride, &oldWidth, &oldHeight) ||
        offset != oldOffset || rowBytes != oldStride || width != oldWidth || height != oldHeight);
    IOReturn result = kIOReturnBadArgument;
    if (owned && (!width || fAccel->surfaceDestination(offset, rowBytes, width, height))) {
        result = fAccel->setSurfaceBuffer(wid, offset, rowBytes, width, height, this, fTask)
            ? kIOReturnSuccess : kIOReturnNoResources;
        if (result == kIOReturnSuccess && changed) {
            IOReturn initialized = fAccel->initializeSurfaceGpu(wid, fTask);
            if (initialized != kIOReturnNotFound) result = initialized;
            if (result == kIOReturnTimeout) fSurfaceGpuFailed = true;
        }
    }
    IOLockUnlock(fAllocationLock);
    return result;
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

IOReturn RadeonNIUserClient::methodSurfaceBacking(UInt32 wid,
	struct rdn_user_backing *backing, IOByteCount *size)
{
	if (!backing || !size || *size < sizeof(*backing)) return kIOReturnBadArgument;
	if (!fAccel->getSurfaceBacking(wid, fTask, backing)) return kIOReturnNotFound;
	*size = sizeof(*backing);
	return kIOReturnSuccess;
}

void RadeonNIAccel::setSurfaceBackingOwner(UInt32 wid, RadeonNISurfaceClient *owner)
{
    IOLockLock(fLock);
    int slot = surfaceSlot(wid, false);
    if (slot >= 0) fShapes[slot].backingClient = owner;
    IOLockUnlock(fLock);
}

void RadeonNIAccel::clearSurfaceBackingOwner(UInt32 wid, RadeonNISurfaceClient *owner)
{
    IOLockLock(fLock);
    int slot = surfaceSlot(wid, false);
    if (slot >= 0 && fShapes[slot].backingClient == owner) {
        for (UInt32 i = 0; i < kSurfaceMirrors; ++i)
            if (fSurfaceMirrors[i].surface == wid) fSurfaceMirrors[i].surface = 0;
        fShapes[slot].backingClient = 0;
        fShapes[slot].backingTask = 0;
        bzero(&fShapes[slot].backing, sizeof(fShapes[slot].backing));
    }
    IOLockUnlock(fLock);
}

RadeonNISurfaceClient *RadeonNIAccel::retainSurfaceBacking(UInt32 wid, task_t task, bool administrator)
{
    RadeonNISurfaceClient *client = 0;
    IOLockLock(fLock);
    int slot = surfaceSlot(wid, false);
    if (slot >= 0 && (administrator || fShapes[slot].backingTask == task ||
        (task && fShapes[slot].producerTask == task))) {
        client = fShapes[slot].backingClient;
        if (client) client->retain();
    }
    IOLockUnlock(fLock);
    return client;
}

IOReturn RadeonNIAccel::initializeSurfaceGpu(UInt32 wid, task_t task)
{
    RadeonNISurfaceClient *client = retainSurfaceBacking(wid, task, false);
    if (!client) return kIOReturnNotFound;
    IOReturn result = client->initializeGpu();
    client->release();
    return result;
}

IOReturn RadeonNIAccel::surfaceGpuInfo(UInt32 wid, task_t task, bool administrator,
    struct rdn_user_surface_info *info)
{
    RadeonNISurfaceClient *client = retainSurfaceBacking(wid, task, administrator);
    if (!client) return kIOReturnNotFound;
    IOReturn result = client->gpuInfo(info);
    client->release();
    return result;
}

bool RadeonNIAccel::surfaceDestination(UInt32 offset, UInt32 stride, UInt32 width, UInt32 height)
{
    if (!width || !height || width > 16384 || height > 16384 ||
        stride < width * 4 || (stride & 3)) return false;
    UInt64 bytes = (UInt64)stride * (height - 1) + width * 4;
    IOLockLock(fLock);
    bool valid = bytes <= rdn_mem_allocation_size(&fMem, offset) &&
        (UInt64)offset + bytes <= fAccel.aperture_size;
    IOLockUnlock(fLock);
    return valid;
}

IOReturn RadeonNIAccel::surfaceGpuCopy(UInt32 wid, task_t task, bool administrator,
    UInt32 offset, UInt32 stride, UInt32 width, UInt32 height)
{
    RadeonNISurfaceClient *client = retainSurfaceBacking(wid, task, administrator);
    if (!client) return kIOReturnNotFound;
    IOReturn result = client->gpuCopy(offset, stride, width, height);
    client->release();
    IOLockLock(fLock);
    if (result == kIOReturnSuccess) {
        ++fSurfaceGpuCopies;
        fSurfaceGpuBytes += (UInt64)width * height * 4;
        if (!(fSurfaceGpuCopies & (fSurfaceGpuCopies - 1)) || !(fSurfaceGpuCopies & 255)) {
            setProperty("RadeonNISurfaceGPUCopies", fSurfaceGpuCopies, 64);
            setProperty("RadeonNISurfaceGPUBytes", fSurfaceGpuBytes, 64);
        }
    }
    IOLockUnlock(fLock);
    return result;
}

IOReturn RadeonNIAccel::surfaceGpuTransfer(UInt64 source, UInt32 sourceStride,
    UInt64 destination, UInt32 destinationStride, UInt32 width, UInt32 height, UInt32 surface, bool locked)
{
    if (!width || !height || height > 16384) return kIOReturnBadArgument;
    UInt32 capacity = (height * 6 + 12) * 4;
    uint32_t words = 0, seq = 0;
    UInt32 *commands = (UInt32 *)IOMalloc(capacity);
    if (!commands) return kIOReturnNoMemory;
    if (!locked) IOLockLock(fLock);
    if (fRetiring || fSurfaceGpuStalled) {
        if (!locked) IOLockUnlock(fLock); IOFree(commands, capacity); return kIOReturnNotReady;
    }
    int mirror = -1;
    if (surface) {
        for (UInt32 i = 0; i < kSurfaceMirrors; ++i) {
            if (fSurfaceMirrors[i].surface == surface &&
                surfaceVramAddress(fSurfaceMirrors[i].offset) == destination) { mirror = i; break; }
            if (!fSurfaceMirrors[i].surface && mirror < 0) mirror = i;
        }
        if (mirror < 0) {
            if (!locked) IOLockUnlock(fLock);
            IOFree(commands, capacity); return kIOReturnNoResources;
        }
    }
    int slot = surface ? surfaceSlot(surface, false) : -1;
    if (slot >= 0 && fShapes[slot].bufWidth == width && fShapes[slot].bufHeight == height) {
        source = rdn_vram_addr(&fAccel, fShapes[slot].bufOffset);
        sourceStride = fShapes[slot].bufRowBytes;
    }
    if (source == destination && sourceStride == destinationStride) {
        int error = rdn_fence_wait(&fAccel, fAccel.fence_emitted, 500);
        if (error) fSurfaceGpuStalled = true;
        if (!locked) IOLockUnlock(fLock); IOFree(commands, capacity);
        return error ? kIOReturnTimeout : kIOReturnSuccess;
    }
    int error = rdn_cp_dma_surface_build(source, sourceStride, destination,
        destinationStride, width, height, commands, capacity, &words);
    UInt64 offset = 0;
    if (error || rdn_mem_alloc(&fMem, words * 4, 4096, &offset)) {
        if (!locked) IOLockUnlock(fLock); IOFree(commands, capacity);
        return error ? kIOReturnBadArgument : kIOReturnNoResources;
    }
    volatile UInt32 *ib = (volatile UInt32 *)((UInt8 *)fAccel.aperture + offset);
    for (UInt32 i = 0; i < words; ++i) ib[i] = commands[i];
    rdnStoreBarrier();
    error = rdn_ib_submit(&fAccel, rdn_vram_addr(&fAccel, offset), words, &seq);
    if (!error) error = rdn_fence_wait(&fAccel, seq, 500);
    if (!error) {
        rdn_mem_free(&fMem, offset);
        if (mirror >= 0) {
            SurfaceMirror *m = &fSurfaceMirrors[mirror];
            m->surface = surface; m->offset = (UInt32)(destination - rdn_vram_addr(&fAccel, 0));
            m->stride = destinationStride; m->width = width; m->height = height;
        }
    }
    else {
        fSurfaceGpuStalled = true;
        setProperty("RadeonNISurfaceGPURebootRequired", kOSBooleanTrue);
    }
    if (!locked) IOLockUnlock(fLock);
    IOFree(commands, capacity);
    return error ? kIOReturnTimeout : kIOReturnSuccess;
}

IOReturn RadeonNIAccel::syncSurfaceGpu(UInt32 surface, UInt64 ram, UInt32 stride,
    UInt32 width, UInt32 height, bool toCPU)
{
    IOLockLock(fLock);
    IOReturn result = kIOReturnSuccess;
    int slot = surfaceSlot(surface, false);
    if (slot >= 0) {
        SurfaceRecord *s = &fShapes[slot];
        if (s->bufWidth == width && s->bufHeight == height) {
            UInt64 bytes = (UInt64)s->bufRowBytes * (height - 1) + width * 4;
            if (s->bufRowBytes < width * 4 || s->bufOffset + bytes > fAccel.aperture_size)
                result = kIOReturnBadArgument;
            else {
                UInt64 gpu = surfaceVramAddress(s->bufOffset);
                result = surfaceGpuTransfer(toCPU ? gpu : ram, toCPU ? s->bufRowBytes : stride,
                    toCPU ? ram : gpu, toCPU ? stride : s->bufRowBytes, width, height, 0, true);
            }
        }
    }
    if (!toCPU) for (UInt32 i = 0; i < kSurfaceMirrors && result == kIOReturnSuccess; ++i) {
        SurfaceMirror *m = &fSurfaceMirrors[i];
        if (m->surface == surface && m->width == width && m->height == height)
            result = surfaceGpuTransfer(ram, stride, surfaceVramAddress(m->offset),
                m->stride, width, height, 0, true);
    }
    IOLockUnlock(fLock);
    return result;
}

IOReturn RadeonNIAccel::refreshSurfaceGpuOwner(void *owner)
{
    IOLockLock(fLock);
    IOReturn result = kIOReturnSuccess;
    for (UInt32 i = 0; i < kSurfaceMirrors && result == kIOReturnSuccess; ++i) {
        SurfaceMirror *m = &fSurfaceMirrors[i];
        int slot = m->surface ? surfaceSlot(m->surface, false) : -1;
        if (slot < 0) continue;
        SurfaceRecord *s = &fShapes[slot];
        if (s->owner == owner && s->bufWidth == m->width && s->bufHeight == m->height)
            result = surfaceGpuTransfer(surfaceVramAddress(s->bufOffset), s->bufRowBytes,
                surfaceVramAddress(m->offset), m->stride, m->width, m->height, 0, true);
    }
    IOLockUnlock(fLock);
    return result;
}

IOReturn RadeonNIUserClient::methodSurfaceGpuInfo(UInt32 wid,
    struct rdn_user_surface_info *info, IOByteCount *size)
{
    if (!info || !size || *size < sizeof(*info)) return kIOReturnBadArgument;
    IOReturn result = fAccel->surfaceGpuInfo(wid, fTask, fSurfaceCompositor, info);
    if (result == kIOReturnSuccess) *size = sizeof(*info);
    return result;
}

IOReturn RadeonNIUserClient::methodSurfaceGpuCopy(UInt32 wid, UInt32 offset,
    UInt32 stride, UInt32 width, UInt32 height)
{
    IOLockLock(fAllocationLock);
    IOReturn result = kIOReturnBadArgument;
    bool owned = false;
    for (UInt32 i = 0; i < fCount; ++i) if (fOffsets[i] == offset) owned = true;
    if (!fSurfaceGpuFailed && owned && fAccel->surfaceDestination(offset, stride, width, height)) {
        result = fAccel->surfaceGpuCopy(wid, fTask, fSurfaceCompositor,
            offset, stride, width, height);
        if (result == kIOReturnTimeout) fSurfaceGpuFailed = true;
    }
    IOLockUnlock(fAllocationLock);
    return result;
}
