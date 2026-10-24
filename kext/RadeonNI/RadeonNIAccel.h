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
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/graphics/IOAccelerator.h>
#include <IOKit/pci/IOAGPDevice.h>

#include <IOKit/graphics/IOAccelSurfaceConnect.h>

extern "C" {
#include "rdn_accel.h"
#include "rdn_uvd.h"
#include "rdn_gart.h"
#include "rdn_mem.h"
#include "rdn_pm.h"
#include "rdn_user.h"
#include "rdn_uvd.h"
}

class RadeonNI;
class RadeonNISurfaceClient;
class RadeonNIVideoUserClient;
struct RadeonNIVideoSession;
#include "rdn_video.h"

#define RDN_UVD_REBOOT_PROPERTY "RadeonNIUVDRebootRequired"

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
	virtual void free(void);
	void prepareToRetire(void);
	bool videoCharge(UInt32 bytes);
	void videoUncharge(UInt32 bytes);
	void videoQuery(bool administrator, struct rdn_video_query *out);
	IOReturn videoCreate(RadeonNIVideoUserClient *owner, bool administrator,
		const struct rdn_video_create *in, struct rdn_video_session *out);
	IOReturn videoDecode(RadeonNIVideoUserClient *owner,
		const struct rdn_video_decode *in, struct rdn_video_output *out);
	IOReturn videoDecodeGpu(RadeonNIVideoUserClient *owner,
		const struct rdn_video_decode *in, struct rdn_video_gpu_output *out);
	IOReturn videoAcquireGpu(RadeonNIVideoUserClient *owner,
		const struct rdn_video_gpu_acquire *in, struct rdn_video_gpu_source *out);
	IOReturn videoRetireGpu(RadeonNIVideoUserClient *owner,const struct rdn_video_gpu_retire *in);
	IOReturn videoRelease(RadeonNIVideoUserClient *owner,
		const struct rdn_video_release *in);
	IOReturn videoDestroy(RadeonNIVideoUserClient *owner,
		const struct rdn_video_destroy *in);
	void videoClose(RadeonNIVideoUserClient *owner);
	IOReturn videoMemory(RadeonNIVideoUserClient *owner, UInt32 type,
		IOOptionBits *options, IOMemoryDescriptor **memory);
	void retire(IOService *provider);

	virtual IOReturn newUserClient(task_t owningTask, void *securityID,
				       UInt32 type, IOUserClient **handler);

	/* For the user client. All of these take the lock themselves. */
	void getInfo(struct rdn_user_info *info);
	IOReturn allocVram(UInt32 size, UInt32 align, UInt32 *offset,
			   bool hidden = false, bool segment = false);
	void hiddenInfo(UInt32 *offset, UInt32 *size);
	/* A program's memory in the GART (rdn_user.h). */
	void gartInfo(UInt32 *on, UInt32 *gpuStart, UInt32 *pages);
	IOReturn gartBind(task_t task, UInt64 address, UInt32 size, void *owner,
			  UInt32 *offset);
	IOReturn gartUnbind(UInt32 offset, void *owner);
	void gartUnbindAll(void *owner);
	void retainClientMemory(const UInt32 *offsets,UInt32 count,void *owner);
	void releaseClientMemory(const UInt32 *offsets, UInt32 count, void *owner);
	void freeVram(UInt32 offset);
	IOReturn submit(UInt32 offset, UInt32 words, UInt32 *fence);
	bool fenceWait(UInt32 fence, UInt32 timeoutMs);
	void syncForCPU(void);
	IOReturn waitForCPU(void);
	IOReturn power(UInt32 state, UInt32 what, UInt32 *sclk, UInt32 *mclk,
		       UInt32 *temperature);
	IOReturn regRead(UInt32 offset, UInt32 *value);
	/* UVD, only with the personality's FW_UVD (hw/rdn_user.h). */
	IOReturn uvdInit(UInt32 flags, UInt32 *status);
	/*
	 * `handles` and `count`: the client's sessions (stream handles of the messages it has sent, not yet destroyed), made in
	 * the firmware epoch `*epoch`; if the firmware has been started again since, they are void (count goes to 0).
	 */
	IOReturn uvdSubmit(UInt32 offset, UInt32 words, UInt32 *fence, UInt32 *handles, UInt32 *count, UInt32 *epoch);
	/* Start the firmware again now, whatever runs; the new epoch. */
	IOReturn uvdReset(UInt32 *epoch);
	UInt32 uvdEpoch(void) { return fUvdEpoch; }
	/* End sessions a program made and left (it died, or ended without destroying them). */
	bool uvdDestroy(const UInt32 *handles, UInt32 count, UInt32 epoch);
	IOReturn uvdAcquireRaw(bool *claimed);
	bool uvdFenceWait(UInt32 fence, UInt32 timeoutMs);
	/* The clients that have decoded: the clocks stay up while there is one. */
	void uvdUser(bool add);
	/* Start the block again: the sessions programs left in the firmware (they end without destroying them) go. */
	void uvdRestart(UInt32 level);
	IOReturn uvdInfo(UInt32 *up, UInt32 *vcpuOffset, UInt32 *ringOffset);
	/* The decoder in a segment beyond the aperture (docs/UVD.md). */
	void uvdSegmentInfo(UInt32 *mode, UInt32 *first, UInt32 *size);
	IOReturn vramPut(task_t task, UInt32 offset, UInt32 address, UInt32 bytes);
	IOReturn vramGet(task_t task, UInt32 offset, UInt32 address, UInt32 bytes);
	IOReturn uvdSubmitWords(task_t task, UInt32 address, UInt32 words, UInt32 *fence, UInt32 *handles,
				UInt32 *count, UInt32 *epoch);
	/* Debugging (RDN_UC_IBLOG): kind '3' a command buffer, 'U' a decoder stream, 'F' 'C' a screen fill or copy, 'M' a mark. */
	void ibRecord(UInt32 kind, void *client, UInt32 offset, UInt32 words);
	IOReturn ibLogControl(UInt32 op, UInt32 *ms);
	IOReturn debugOp(UInt32 op, UInt32 *srbmStatus);
	void uvdDiagnosticInfo(struct rdn_user_uvd_diagnostic *info);
	IOReturn uvdDiagnosticInit(UInt32 *status);
	IOReturn uvdFixtureRun(struct rdn_user_uvd_fixture *result);
	IOReturn uvdFixtureRead(UInt32 generation, UInt32 chunk, void *bytes);
	IOMemoryDescriptor *apertureMemory(void);
	UInt32 apertureBytes(void);
	/* The shapes of the window server's surfaces, by surface ID. */
	bool setSurfaceRegion(UInt32 wid, const IOAccelDeviceRegion *rgn,
			      UInt32 rects);
	bool getSurfaceRegion(UInt32 wid, struct rdn_user_region *region);
	void forgetSurface(UInt32 wid);
	/*
	 * With the gate on (personality key SurfaceGate), setIDMode takes
	 * only surfaces whose owner has announced them with
	 * RDN_UC_SURFACE_BUFFER (width 0 is enough): the programs'
	 * OpenGL windows, not the window server's own windows of 10.5.
	 */
	bool surfaceGate(void) { return fSurfaceGate; }
    bool surfaceGpuReady(void) { return fGart.ready && fEngineUp && !fRetiring && !fSurfaceGpuStalled; }
	bool isGLSurface(UInt32 wid);
	bool setSurfaceBacking(UInt32 wid, task_t task, const struct rdn_user_backing *backing);
	bool getSurfaceBacking(UInt32 wid, task_t task, struct rdn_user_backing *backing);
    void setSurfaceBackingOwner(UInt32 wid, RadeonNISurfaceClient *owner);
    void clearSurfaceBackingOwner(UInt32 wid, RadeonNISurfaceClient *owner);
    RadeonNISurfaceClient *retainSurfaceBacking(UInt32 wid, task_t task, bool administrator);
    IOReturn surfaceGpuInfo(UInt32 wid, task_t task, bool administrator,
        struct rdn_user_surface_info *info);
    IOReturn surfaceGpuCopy(UInt32 wid, task_t task, bool administrator,
        UInt32 offset, UInt32 stride, UInt32 width, UInt32 height);
    IOReturn surfaceGpuTransfer(UInt64 source, UInt32 sourceStride,
        UInt64 destination, UInt32 destinationStride, UInt32 width, UInt32 height, UInt32 surface = 0, bool locked = false);
    IOReturn syncSurfaceGpu(UInt32 surface, UInt64 ram, UInt32 stride, UInt32 width, UInt32 height, bool toCPU);
    IOReturn refreshSurfaceGpuOwner(void *owner);
    IOReturn initializeSurfaceGpu(UInt32 wid, task_t task);
    bool surfaceDestination(UInt32 offset, UInt32 stride, UInt32 width, UInt32 height);
    UInt64 surfaceVramAddress(UInt32 offset) { return rdn_vram_addr(&fAccel, offset); }
	bool setSurfaceBuffer(UInt32 wid, UInt32 offset, UInt32 rowBytes,
			      UInt32 width, UInt32 height, void *owner, task_t producerTask = 0);
	bool getSurfaceBuffer(UInt32 wid, UInt32 *offset, UInt32 *rowBytes,
			      UInt32 *width, UInt32 *height);
	void listSurfaces(struct rdn_user_surfaces *list);
	void querySurface(UInt32 wid, struct rdn_user_surface_info *info);
	/* Put a surface's picture on the screen, inside its shape. */
	IOReturn flushSurface(UInt32 wid);
	/*
	 * Fill a rectangle of the screen, and copy one to another place on
	 * it, with the GPU (rdn_user.h). Queued; `fence` is what to wait for.
	 */
	IOReturn screenFill(SInt32 x, SInt32 y, SInt32 w, SInt32 h, UInt32 colour,
			    UInt32 *fence);
	IOReturn screenCopy(SInt32 sx, SInt32 sy, SInt32 dx, SInt32 dy,
			    SInt32 w, SInt32 h, UInt32 *fence);
	/* The surface that is read-locked now (0: none). */
	void setReadLocked(UInt32 wid) { fReadLocked = wid; }
	UInt32 readLocked(void) { return fReadLocked; }

private:
	RadeonNI *fFramebuffer;
	IOService *fUvdProvider;
	IOLock *fLock;
	IOLock *fUvdLock;
	RadeonNIVideoSession *fVideoSession;
	UInt64 fVideoEpoch;
	volatile UInt32 fVideoCpuBytes;
	bool fVideoBusy;
	bool videoReady(bool administrator, bool validation);
	int videoCommand(UInt32 type, const struct rdn_uvd_h264_picture *picture,
		UInt32 bitstreamBytes);
	IOReturn videoDecodeImpl(RadeonNIVideoUserClient *owner,const struct rdn_video_decode *in,
		struct rdn_video_output *out,struct rdn_video_gpu_output *gpu);
	void videoCleanup(bool destroy);
	int videoBindReadback(void);
	int videoUnbindReadback(void);
	int videoReadback(bool guardsOnly=false);
	int videoSubmitGfx(UInt32 words,bool poisonOnBusy);
	int videoInitializeVram(void);
	void videoPoison(int error);
	bool fRetiring;
	struct rdn_accel fAccel;
	struct rdn_mem fMem;
	/* Video memory beyond the aperture; fHiddenSize 0 if there is none. */
	struct rdn_mem fHidden;
	UInt32 fHiddenOffset, fHiddenSize;
	void *fPfp, *fMe;
	bool fEngineUp;
	/* UVD: set up by the first RDN_UC_UVD_INIT, never at start. */
	struct rdn_uvd fUvd;
	bool fUvdUp, fUvdFailed, fUvdClocksUp;
	UInt32 fUvdLastSeq, fUvdUsers;
	/* The sessions alive in the firmware (all programs'), and which start of the firmware this is. */
	UInt32 fUvdLive, fUvdEpoch;
	/* How the block was started (to start it again), and whether any program has used it since. */
	UInt32 fUvdFlags;
	uint32_t fUvdRef;
	bool fUvdDirty;
	/* How a restart of the block is done (RDN_UVD_RESET_*), set by RDN_UC_DEBUG 20 + bits for experiments. */
	UInt32 fUvdRestartBits;
	/* The command buffer recorder: a ring of what was submitted and the text of what the scan found. */
	struct IbEntry { UInt32 ms, client, pid, offset, words, kind, found; };
	bool fIbOn;
	IbEntry *fIbEntries;
	UInt32 fIbTotal;
	char *fIbText;
	UInt32 fIbTextLen, fIbFound;
	UInt32 ibScan(UInt32 ms, UInt32 pid, UInt32 client, UInt32 offset, UInt32 words);
	void ibNote(const char *fmt, ...);
	bool ibInUvd(UInt32 address);
	/* A page of video memory for messages of the kext's own. */
	UInt32 fUvdScratch;
	/* The session messages in a stream of commands: type (0 create, 2 destroy) and handle of each. */
	struct UvdMsg { UInt32 type, handle, size; };
	enum { kMaxUvdMsgs = 8 };
	UInt32 uvdFind(const volatile UInt32 *w, UInt32 words, UvdMsg *msgs);
	IOReturn uvdSubmitCommon(uint64_t ibAddress, UInt32 words, UvdMsg *msgs, UInt32 n, UInt32 *fence, UInt32 *handles,
				 UInt32 *count, UInt32 *epoch);
	/*
	 * The decoder in a 256 MB segment of the video memory beyond the aperture: fUvdSeg is its number (0: the block
	 * is in the aperture), [fUvdSegLo, fUvdSegHi) its bytes. The block's own memory (firmware window, ring,
	 * scratch page, indirect buffer slots) is in it; the shadows are the library's system memory copies of the first two.
	 */
	/* fUvdSplit: the block runs in the aperture and the decoder's own buffers are in this hidden segment (split mode; fUvdSeg is 0 then). */
	UInt32 fUvdSeg, fUvdSplit, fUvdSegLo, fUvdSegHi;
	/* Where the segment's memory comes from (fHidden; fMem for segment 7, the aperture's own, a test), and dumps made of a stalled stream. */
	struct rdn_mem *fUvdSegMem;
	UInt32 fUvdDumps;
	enum { kUvdOwn = 4, kUvdIbSlots = 16 };
	struct { UInt32 offset, size; } fUvdOwn[kUvdOwn];
	UInt8 *fUvdVcpuShadow, *fUvdRingShadow;
	UInt32 fUvdVcpuShadowBytes, fUvdRingShadowBytes;
	UInt32 fUvdIbBase, fUvdIbNext;
	/* The fence number to go on from when the block is started in the other place. */
	UInt32 fUvdModeFenceBase;
	/* How the next start of the block resets it: 0 for the first start, fUvdRestartBits when it moves between the aperture and a segment (the RLC runs already). */
	UInt32 fUvdStartLevel;
	UInt32 fUvdIbFence[kUvdIbSlots];
	/*
	 * Staging in the GART for the command processor's DMA into video memory the CPU cannot map: the register window's writes do
	 * not reach the decoder the way DMA does (docs/UVD.md). dmaPut() is called with fLock held.
	 */
	IOBufferMemoryDescriptor *fDmaMem;
	UInt8 *fDmaCpu;
	uint64_t fDmaGpu;
	UInt32 fDmaAt, fDmaBytes;
	bool dmaStageUp(void);
	/* Diagnosis: how many bytes of the firmware window in video memory differ from the file (the firmware changes a few of its own when it runs). */
	void uvdLogWindow(const char *when);
	int dmaPut(UInt32 offset, const void *src, UInt32 bytes);
	static int dmaPutHook(void *cookie, uint32_t offset, const void *src, uint32_t bytes);
	UInt32 uvdPickSegment(UInt32 force);
	void uvdSegmentFree(void);
	bool uvdSegmentCheck(UInt32 offset, UInt32 bytes);
	IOReturn uvdStartSegment(UInt32 flags, UInt32 force, const void *fw, UInt32 fwSize, uint32_t ref, UInt32 *status);
	bool uvdReadMessageHeader(uint64_t address, UInt8 *out);
	IOBufferMemoryDescriptor *fUvdGartMem;
	UInt32 fUvdGartAt;
	UInt32 fUvdState, fUvdOffset, fUvdBytes, fUvdStatus;
	SInt32 fUvdError;
	bool fUvdColdAttempted;
	struct rdn_user_uvd_fixture fUvdFixture;
	UInt32 fUvdFixtureOffset, fUvdFixtureBytes;
	UInt8 *fUvdFixtureSnapshot;
	/* The power state the card was last put in (hw/rdn_pm.h). */
	struct rdn_pm_state fPower;
	bool fPowerKnown;
	/* Hand out surface clients (RadeonNISurface.h)? */
	bool fSurfaces;
	bool fSurfaceGate;
    bool fSurfaceGpuStalled;
    UInt64 fSurfaceGpuCopies, fSurfaceGpuBytes;
	struct SurfaceMirror { UInt32 surface, offset, stride, width, height; };
    enum { kSurfaceMirrors = 1024 };
    SurfaceMirror fSurfaceMirrors[kSurfaceMirrors];
	struct SurfaceRecord {
		UInt32 wid;
		bool used;
		/* announced by its owner (RDN_UC_SURFACE_BUFFER) */
		bool gl;
		struct rdn_user_region region;
		/* The surface's picture, if its owner has said where it is. */
		UInt32 bufOffset, bufRowBytes, bufWidth, bufHeight;
		void *owner;
		task_t backingTask, producerTask;
        RadeonNISurfaceClient *backingClient;
		struct rdn_user_backing backing;
	};
	void clearSurfaceBufferRecord(SurfaceRecord *record);
	SurfaceRecord *fShapes;
	UInt32 fShapeCapacity;
	int surfaceSlot(UInt32 wid, bool create);
	UInt32 fReadLocked;
	/* flushSurface(): the last copy's fence, and its rectangles. */
	bool fBlitPending;
	UInt32 fBlitFence;
	struct rdn_blit_rect fBlitRects[RDN_BLIT_MAX_RECTS];
	/*
	 * screenFill() and screenCopy(): work areas used in turn, each with
	 * the fence that frees it, and the newest of those fences.
	 */
	enum { kScreenSlots = 3 };
	struct {
		bool pending;
		UInt32 fence;
	} fScreenSlot[kScreenSlots];
	UInt32 fScreenNext;
	bool fScreenPending;
	UInt32 fScreenFence;
	/* screenCopy()'s scratch surface; fMoveBytes 0 before the first copy. */
	UInt32 fMoveOffset, fMoveBytes;
	bool fMoveHidden;
	IOReturn screenBegin(struct rdn_selftest_target *screen, UInt32 *slot);
	void screenEnd(UInt32 slot);
	IOReturn moveScratch(const struct rdn_selftest_target *screen,
			     struct rdn_draw_surface *scratch);
	/* See RadeonNIAGPShim. */
	IOAGPDevice *fAncestor;
	RadeonNIAGPShim *fShim;

	bool startEngine(void);
	static void prepareUvdBeforeCP(void *cookie);
	IOReturn uvdDiagnosticInitImpl(UInt32 *status, bool prepareOnly);
	void releaseUvdFixtureSnapshot(void);
	/* rdn_gart=1: the page table for system memory, and a test of it. */
	void startGart(void);
	struct rdn_gart fGart;
	IOBufferMemoryDescriptor *fGartDummy, *fGartTest;
	UInt32 fGartTable;
	/* What programs have bound: byte offsets of the GART's range. */
	struct rdn_mem fGartSpace;
	enum { kMaxGartBindings = 512 };
	struct {
		IOMemoryDescriptor *memory;
		UInt32 offset, size;
		void *owner;
	} fGartBound[kMaxGartBindings];
	void *copyFirmware(const char *key, UInt32 *size);
};

class RadeonNIUserClient : public IOUserClient
{
	/* Mesa asks for every buffer by itself: a game has tens of thousands. */
	enum { kMaxAllocations = 65536 };

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
	IOReturn methodSurfaceBuffer(UInt32 wid, UInt32 offset, UInt32 rowBytes,
				     UInt32 width, UInt32 height);
	IOReturn methodSurfaceBacking(UInt32 wid, struct rdn_user_backing *backing, IOByteCount *size);
	IOReturn methodSurfaceQuery(UInt32 wid, struct rdn_user_surface_info *info, IOByteCount *size);
    IOReturn methodSurfaceGpuInfo(UInt32 wid, struct rdn_user_surface_info *info, IOByteCount *size);
    IOReturn methodSurfaceGpuCopy(UInt32 wid, UInt32 offset, UInt32 stride, UInt32 width, UInt32 height);
	IOReturn methodSurfaceList(struct rdn_user_surfaces *list, IOByteCount *size);
	IOReturn methodSurfaceLocked(UInt32 *wid);
	IOReturn methodRegRead(UInt32 offset, UInt32 *value);
	IOReturn methodUvdInit(UInt32 flags, UInt32 *status);
	IOReturn methodUvdSubmit(UInt32 offset, UInt32 words, UInt32 *fence);
	IOReturn methodUvdFenceWait(UInt32 fence, UInt32 timeoutMs, UInt32 *reached);
	IOReturn methodUvdInfo(UInt32 *up, UInt32 *vcpuOffset, UInt32 *ringOffset);
	IOReturn methodUvdReset(UInt32 *epoch);
	IOReturn methodUvdEpoch(UInt32 *epoch);
	IOReturn methodIbLog(UInt32 op, UInt32 *ms);
	IOReturn methodDebug(UInt32 op, UInt32 *status);
	IOReturn methodUvdDiagnosticInfo(struct rdn_user_uvd_diagnostic *info,
				       IOByteCount *size);
	IOReturn methodUvdDiagnosticInit(UInt32 reserved, UInt32 *status);
	IOReturn methodUvdFixtureRun(UInt32 reserved, struct rdn_user_uvd_fixture *result,
				    IOByteCount *size);
	IOReturn methodUvdFixtureRead(UInt32 generation, UInt32 chunk, void *bytes,
				     IOByteCount *size);
	IOReturn methodAllocHidden(UInt32 size, UInt32 align, UInt32 *offset);
	IOReturn methodUvdSegment(UInt32 *mode, UInt32 *first, UInt32 *size);
	IOReturn methodAllocUvd(UInt32 size, UInt32 align, UInt32 *offset);
	IOReturn methodVramPut(UInt32 offset, UInt32 address, UInt32 bytes);
	IOReturn methodVramGet(UInt32 offset, UInt32 address, UInt32 bytes);
	IOReturn methodUvdSubmitWords(UInt32 address, UInt32 words, UInt32 *fence);
	IOReturn methodHiddenInfo(UInt32 *offset, UInt32 *size);
	IOReturn methodGartInfo(UInt32 *on, UInt32 *gpuStart, UInt32 *pages);
	IOReturn methodGartBind(UInt32 address, UInt32 size, UInt32 *offset);
	IOReturn methodGartUnbind(UInt32 offset);
	IOReturn methodGartBind64(UInt32 low, UInt32 high, UInt32 size, UInt32 *offset);
	IOReturn methodPower(UInt32 state, UInt32 what, UInt32 *sclk,
			     UInt32 *mclk, UInt32 *temperature);
	IOReturn methodScreenFill(UInt32 x, UInt32 y, UInt32 width, UInt32 height,
				  UInt32 colour, UInt32 *fence);
	IOReturn methodScreenCopy(UInt32 sx, UInt32 sy, UInt32 dx, UInt32 dy,
				  UInt32 size, UInt32 *fence);

private:
	RadeonNIAccel *fAccel;
	task_t fTask;
	bool fUvdAdministrator;
    bool fSurfaceGpuFailed;
    bool fSurfaceCompositor;
    IOLock *fAllocationLock;
    IOReturn allocateMemory(UInt32 size, UInt32 align, UInt32 *offset, bool hidden, bool segment);
	/* What this client allocated, so that it can be freed when it goes. */
	UInt32 *fOffsets;
	UInt32 fCount;
	/* Has submitted UVD work: counted in RadeonNIAccel::uvdUser(). */
	bool fUvdUser;
	bool fProtocolV2;
	IOReturn claimUvd(void);
	/* The decoder's sessions this client has made and not destroyed (RadeonNIAccel::uvdScan). */
	enum { kMaxUvdSessions = 32 };
	UInt32 fUvdHandles[kMaxUvdSessions];
	UInt32 fUvdHandleCount;
	/* The firmware epoch they were made in. */
	UInt32 fUvdHandleEpoch;

	void freeAll(void);
};

#endif /* RADEONNIACCEL_H */
