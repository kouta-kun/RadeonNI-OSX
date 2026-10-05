/*
 * RadeonNIGA: the 2D accelerator plug-in of the Radeon HD 7570 driver.
 *
 * Mac OS X loads this CFPlugIn for the framebuffer (the kext names it in
 * the framebuffer's IOCFPlugInTypes property) through the public
 * IOGraphicsAcceleratorInterface. The window server asks it for a fill
 * and a copy routine when a display starts, and only considers
 * compositing with OpenGL on a display that has them
 * (docs/QUARTZ-EXTREME.md).
 *
 * The routines work on the screen's surface in video memory, which the
 * kext's accelerator user client maps (hw/rdn_user.h), with the CPU. That
 * is no faster than what the system does without a plug-in; it is here
 * because it has to exist.
 *
 * The shape of a CFPlugIn for this interface was learned from VMsvga2's
 * (MIT, Zenith432); the code is this project's.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/graphics/IOGraphicsInterface.h>

#include "rdn_user.h"

/* 5D1E2B7A-93C4-4E0F-8A61-2F7B90C3D4E6, as in Info.plist. */
#define kRadeonNIGAFactoryID \
	CFUUIDGetConstantUUIDWithBytes(NULL, 0x5D, 0x1E, 0x2B, 0x7A, 0x93, 0xC4, \
		0x4E, 0x0F, 0x8A, 0x61, 0x2F, 0x7B, 0x90, 0xC3, 0xD4, 0xE6)

struct ga {
	IOGraphicsAcceleratorInterface *vtbl;
	CFUUIDRef factory;
	UInt32 refs;
	io_connect_t conn;
	uint8_t *aperture;
	/* The screen now; read again on Reset, which follows a mode change. */
	struct rdn_user_info info;
	int usable;
};

static IOGraphicsAcceleratorInterface interface;

static void ga_read_screen(struct ga *ga)
{
	IOByteCount size = sizeof(ga->info);

	ga->usable = 0;
	if (!ga->conn || !ga->aperture)
		return;
	if (IOConnectMethodScalarIStructureO(ga->conn, RDN_UC_GET_INFO, 0, &size,
					     &ga->info))
		return;
	ga->usable = ga->info.version == RDN_USER_VERSION && ga->info.fb_width &&
		     (ga->info.fb_bits_per_pixel == 8 ||
		      ga->info.fb_bits_per_pixel == 16 ||
		      ga->info.fb_bits_per_pixel == 32);
}

/* Address of a pixel of the screen. */
static uint8_t *ga_pixel(struct ga *ga, SInt32 x, SInt32 y)
{
	UInt32 bytes = ga->info.fb_bits_per_pixel / 8;

	return ga->aperture + ga->info.fb_offset +
	       ((UInt32)y * ga->info.fb_pitch_pixels + (UInt32)x) * bytes;
}

/* Clip a rectangle to the screen; false if nothing is left. */
static int ga_clip(struct ga *ga, SInt32 *x, SInt32 *y, SInt32 *w, SInt32 *h)
{
	if (*x < 0) { *w += *x; *x = 0; }
	if (*y < 0) { *h += *y; *y = 0; }
	if (*x + *w > (SInt32)ga->info.fb_width)
		*w = (SInt32)ga->info.fb_width - *x;
	if (*y + *h > (SInt32)ga->info.fb_height)
		*h = (SInt32)ga->info.fb_height - *y;
	return *w > 0 && *h > 0;
}

static void ga_fill(struct ga *ga, SInt32 x, SInt32 y, SInt32 w, SInt32 h,
		    UInt32 color)
{
	SInt32 row, col;

	if (!ga_clip(ga, &x, &y, &w, &h))
		return;
	for (row = 0; row < h; row++) {
		uint8_t *p = ga_pixel(ga, x, y + row);

		switch (ga->info.fb_bits_per_pixel) {
		case 8:
			memset(p, (int)color, (size_t)w);
			break;
		case 16:
			for (col = 0; col < w; col++)
				((uint16_t *)p)[col] = (uint16_t)color;
			break;
		default:
			for (col = 0; col < w; col++)
				((uint32_t *)p)[col] = color;
			break;
		}
	}
}

/* Screen to screen, overlapping or not. */
static void ga_copy(struct ga *ga, SInt32 sx, SInt32 sy, SInt32 dx, SInt32 dy,
		    SInt32 w, SInt32 h)
{
	UInt32 bytes = ga->info.fb_bits_per_pixel / 8;
	SInt32 row, cx, cy, cw, ch;

	/* Clip the destination, carry the source along, then the source. */
	cx = dx; cy = dy; cw = w; ch = h;
	if (!ga_clip(ga, &cx, &cy, &cw, &ch))
		return;
	sx += cx - dx; sy += cy - dy; dx = cx; dy = cy; w = cw; h = ch;
	cx = sx; cy = sy; cw = w; ch = h;
	if (!ga_clip(ga, &cx, &cy, &cw, &ch))
		return;
	dx += cx - sx; dy += cy - sy; sx = cx; sy = cy; w = cw; h = ch;

	if (dy < sy) {
		for (row = 0; row < h; row++)
			memmove(ga_pixel(ga, dx, dy + row), ga_pixel(ga, sx, sy + row),
				(size_t)w * bytes);
	} else {
		for (row = h - 1; row >= 0; row--)
			memmove(ga_pixel(ga, dx, dy + row), ga_pixel(ga, sx, sy + row),
				(size_t)w * bytes);
	}
}

/*
 * Blitters
 */

static IOReturn blit_fill(void *self, IOOptionBits options, IOBlitType type,
			  IOBlitSourceType sourceType, IOBlitOperation *operation,
			  void *source)
{
	struct ga *ga = self;
	IOBlitRectangles *rects = (IOBlitRectangles *)operation;
	IOItemCount i;

	if (!ga || !ga->usable || !rects)
		return kIOReturnNotReady;
	for (i = 0; i < rects->count; i++)
		ga_fill(ga, rects->rects[i].x, rects->rects[i].y,
			rects->rects[i].width, rects->rects[i].height,
			rects->operation.color0);
	return kIOReturnSuccess;
}

static IOReturn blit_copy(void *self, IOOptionBits options, IOBlitType type,
			  IOBlitSourceType sourceType, IOBlitOperation *operation,
			  void *source)
{
	struct ga *ga = self;
	IOBlitCopyRectangles *rects = (IOBlitCopyRectangles *)operation;
	IOItemCount i;

	if (!ga || !ga->usable || !rects)
		return kIOReturnNotReady;
	for (i = 0; i < rects->count; i++)
		ga_copy(ga, rects->rects[i].sourceX, rects->rects[i].sourceY,
			rects->rects[i].x, rects->rects[i].y,
			rects->rects[i].width, rects->rects[i].height);
	return kIOReturnSuccess;
}

/*
 * The region's rectangles are where the pixels are now; deltaX and deltaY
 * say where the region's bounds go.
 */
static IOReturn blit_copy_region(void *self, IOOptionBits options,
				 IOBlitType type, IOBlitSourceType sourceType,
				 IOBlitOperation *operation, void *source)
{
	struct ga *ga = self;
	IOBlitCopyRegion *copy = (IOBlitCopyRegion *)operation;
	IOAccelDeviceRegion *rgn;
	SInt32 dx, dy;
	UInt32 i, n;

	if (!ga || !ga->usable || !copy || !copy->region)
		return kIOReturnNotReady;
	rgn = copy->region;
	dx = copy->deltaX - rgn->bounds.x;
	dy = copy->deltaY - rgn->bounds.y;
	n = rgn->num_rects;
	if (!n) {
		ga_copy(ga, rgn->bounds.x, rgn->bounds.y, rgn->bounds.x + dx,
			rgn->bounds.y + dy, rgn->bounds.w, rgn->bounds.h);
		return kIOReturnSuccess;
	}
	/*
	 * Rectangles of a region come top to bottom, left to right. Going
	 * down or right, take them in reverse so that none is overwritten
	 * before it has been copied.
	 */
	for (i = 0; i < n; i++) {
		IOAccelBounds *r = &rgn->rect[(dy > 0 || (dy == 0 && dx > 0)) ? n - 1 - i : i];

		ga_copy(ga, r->x, r->y, r->x + dx, r->y + dy, r->w, r->h);
	}
	return kIOReturnSuccess;
}

/*
 * IOGraphicsAcceleratorInterface
 */

static HRESULT ga_query_interface(void *self, REFIID iid, LPVOID *out)
{
	struct ga *ga = self;
	CFUUIDRef id = CFUUIDCreateFromUUIDBytes(NULL, iid);
	HRESULT r = E_NOINTERFACE;

	if (CFEqual(id, kIOGraphicsAcceleratorInterfaceID) ||
	    CFEqual(id, kIOCFPlugInInterfaceID) || CFEqual(id, IUnknownUUID)) {
		ga->refs++;
		*out = ga;
		r = S_OK;
	} else {
		*out = NULL;
	}
	CFRelease(id);
	return r;
}

static ULONG ga_add_ref(void *self)
{
	return ++((struct ga *)self)->refs;
}

static IOReturn ga_stop(void *self);

static ULONG ga_release(void *self)
{
	struct ga *ga = self;
	ULONG refs = --ga->refs;

	if (!refs) {
		CFUUIDRef factory = ga->factory;

		ga_stop(ga);
		free(ga);
		CFPlugInRemoveInstanceForFactory(factory);
		CFRelease(factory);
	}
	return refs;
}

static IOReturn ga_probe(void *self, CFDictionaryRef properties,
			 io_service_t service, SInt32 *order)
{
	return kIOReturnSuccess;
}

static IOReturn ga_start(void *self, CFDictionaryRef properties,
			 io_service_t service)
{
	struct ga *ga = self;
	io_service_t accel = IOServiceGetMatchingService(kIOMasterPortDefault,
				IOServiceMatching(RDN_UC_SERVICE_CLASS));
	vm_address_t addr = 0;
	vm_size_t len = 0;
	kern_return_t kr;

	if (!accel)
		return kIOReturnNoDevice;
	kr = IOServiceOpen(accel, mach_task_self(), RDN_UC_TYPE, &ga->conn);
	IOObjectRelease(accel);
	if (kr) {
		ga->conn = 0;
		return kr;
	}
	kr = IOConnectMapMemory(ga->conn, RDN_UC_MEMORY_APERTURE, mach_task_self(),
				&addr, &len, kIOMapAnywhere);
	if (kr) {
		IOServiceClose(ga->conn);
		ga->conn = 0;
		return kr;
	}
	ga->aperture = (uint8_t *)addr;
	ga_read_screen(ga);
	return ga->usable ? kIOReturnSuccess : kIOReturnNotReady;
}

static IOReturn ga_stop(void *self)
{
	struct ga *ga = self;

	if (ga->conn) {
		IOServiceClose(ga->conn);
		ga->conn = 0;
	}
	ga->aperture = NULL;
	ga->usable = 0;
	return kIOReturnSuccess;
}

static IOReturn ga_reset(void *self, IOOptionBits options)
{
	ga_read_screen(self);
	return kIOReturnSuccess;
}

static IOReturn ga_copy_capabilities(void *self, FourCharCode select,
				     CFTypeRef *capabilities)
{
	return kIOReturnUnsupported;
}

static IOReturn ga_flush(void *self, IOOptionBits options)
{
	return kIOReturnSuccess;
}

static IOReturn ga_synchronize(void *self, UInt32 options, UInt32 x, UInt32 y,
			       UInt32 w, UInt32 h)
{
	return kIOReturnSuccess;
}

static IOReturn ga_get_beam_position(void *self, IOOptionBits options,
				     SInt32 *position)
{
	return kIOReturnUnsupported;
}

static IOReturn ga_no_surface(void *self, IOOptionBits options,
			      IOBlitSurface *surface)
{
	return kIOReturnUnsupported;
}

static IOReturn ga_allocate_surface(void *self, IOOptionBits options,
				    IOBlitSurface *surface, void *cgsSurfaceID)
{
	return kIOReturnUnsupported;
}

static IOReturn ga_lock_surface(void *self, IOOptionBits options,
				IOBlitSurface *surface, vm_address_t *address)
{
	return kIOReturnUnsupported;
}

static IOReturn ga_swap_surface(void *self, IOOptionBits options,
				IOBlitSurface *surface, IOOptionBits *swapFlags)
{
	return kIOReturnUnsupported;
}

/* Only the screen is a destination. */
static IOReturn ga_set_destination(void *self, IOOptionBits options,
				   IOBlitSurface *surface)
{
	return options == kIOBlitFramebufferDestination ? kIOReturnSuccess
							 : kIOReturnUnsupported;
}

static IOReturn ga_get_blitter(void *self, IOOptionBits options, IOBlitType type,
			       IOBlitSourceType sourceType, IOBlitterPtr *blitter)
{
	if (!blitter)
		return kIOReturnBadArgument;
	sourceType &= 0x7ffff000;
	switch (type & kIOBlitTypeVerbMask) {
	case kIOBlitTypeRects:
		if (sourceType == kIOBlitSourceSolid) {
			*blitter = blit_fill;
			return kIOReturnSuccess;
		}
		break;
	case kIOBlitTypeCopyRects:
		if (sourceType == kIOBlitSourceDefault ||
		    sourceType == kIOBlitSourceFramebuffer) {
			*blitter = blit_copy;
			return kIOReturnSuccess;
		}
		break;
	case kIOBlitTypeCopyRegion:
		if (sourceType == kIOBlitSourceDefault ||
		    sourceType == kIOBlitSourceFramebuffer) {
			*blitter = blit_copy_region;
			return kIOReturnSuccess;
		}
		break;
	}
	return kIOReturnUnsupported;
}

static IOReturn ga_wait_complete(void *self, IOOptionBits options)
{
	return kIOReturnSuccess;
}

static void build_interface(void)
{
	memset(&interface, 0, sizeof(interface));
	interface.QueryInterface = ga_query_interface;
	interface.AddRef = ga_add_ref;
	interface.Release = ga_release;
	interface.version = kCurrentGraphicsInterfaceVersion;
	interface.revision = kCurrentGraphicsInterfaceRevision;
	interface.Probe = ga_probe;
	interface.Start = ga_start;
	interface.Stop = ga_stop;
	interface.Reset = ga_reset;
	interface.CopyCapabilities = ga_copy_capabilities;
	interface.Flush = ga_flush;
	interface.Synchronize = ga_synchronize;
	interface.GetBeamPosition = ga_get_beam_position;
	interface.AllocateSurface = ga_allocate_surface;
	interface.FreeSurface = ga_no_surface;
	interface.LockSurface = ga_lock_surface;
	interface.UnlockSurface = ga_swap_surface;
	interface.SwapSurface = ga_swap_surface;
	interface.SetDestination = ga_set_destination;
	interface.GetBlitter = ga_get_blitter;
	interface.WaitComplete = ga_wait_complete;
}

/* The factory function named in Info.plist. */
void *RadeonNIGAFactory(CFAllocatorRef allocator, CFUUIDRef typeID);

void *RadeonNIGAFactory(CFAllocatorRef allocator, CFUUIDRef typeID)
{
	struct ga *ga;

	if (!CFEqual(typeID, kIOGraphicsAcceleratorTypeID))
		return NULL;
	ga = calloc(1, sizeof(*ga));
	if (!ga)
		return NULL;
	if (!interface.QueryInterface)
		build_interface();
	ga->vtbl = &interface;
	ga->factory = CFRetain(kRadeonNIGAFactoryID);
	CFPlugInAddInstanceForFactory(ga->factory);
	ga->refs = 1;
	return ga;
}
