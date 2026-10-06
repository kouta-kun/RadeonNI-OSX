/*
 * The device on Mac OS X: the kext's accelerator user client
 * (hw/rdn_user.h). The kext must be loaded with its 3D engine started
 * (RDN_ACCEL=1 scripts/kext.sh up).
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>

#include "rdn_device.h"
#include "hw/rdn_user.h"

struct darwin_device {
	struct rdn_device base;
	io_connect_t conn;
};

static int dev_alloc(struct rdn_device *dev, uint64_t size, uint64_t align,
		     uint64_t *offset)
{
	struct darwin_device *d = (struct darwin_device *)dev;
	int off = 0;

	if (size > 0xffffffffull || align > 0x10000000ull ||
	    IOConnectMethodScalarIScalarO(d->conn, RDN_UC_ALLOC, 2, 1,
					  (int)size, (int)align, &off))
		return -1;
	*offset = (uint32_t)off;
	return 0;
}

static void dev_free(struct rdn_device *dev, uint64_t offset)
{
	struct darwin_device *d = (struct darwin_device *)dev;

	IOConnectMethodScalarIScalarO(d->conn, RDN_UC_FREE, 1, 0, (int)offset);
}

static int dev_submit(struct rdn_device *dev, uint64_t offset, uint32_t words,
		      uint32_t *fence)
{
	struct darwin_device *d = (struct darwin_device *)dev;
	int seq = 0;

	if (IOConnectMethodScalarIScalarO(d->conn, RDN_UC_SUBMIT, 2, 1,
					  (int)offset, (int)words, &seq))
		return -1;
	*fence = (uint32_t)seq;
	return 0;
}

static bool fence_wait_ms(struct darwin_device *d, uint32_t fence, uint32_t ms)
{
	int reached = 0;

	if (IOConnectMethodScalarIScalarO(d->conn, RDN_UC_FENCE_WAIT, 2, 1,
					  (int)fence, (int)ms, &reached))
		return false;
	return reached != 0;
}

static bool dev_fence_done(struct rdn_device *dev, uint32_t fence)
{
	return fence_wait_ms((struct darwin_device *)dev, fence, 0);
}

/*
 * RDN_FENCE_SPIN=microseconds: look that long without sleeping before the
 * kext's wait, which sleeps between looks. RDN_STATS=1: say at exit how
 * many waits there were and how long they took.
 */
static long env_number(const char *name)
{
	const char *s = getenv(name);

	return s ? atol(s) : 0;
}

static uint64_t now_us(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static struct {
	unsigned waits, spun, slept;
	uint64_t us, max_us;
	unsigned hist[8];	/* under 0.25, 0.5, 1, 2, 4, 8, 16 ms, more */
} wait_stats;

static void wait_stats_print(void)
{
	fprintf(stderr, "rdn stats: %u fence waits (%u ended while spinning, %u slept), "
		"%llu ms in all, longest %llu us; under 0.25/0.5/1/2/4/8/16 ms and over: "
		"%u %u %u %u %u %u %u %u\n",
		wait_stats.waits, wait_stats.spun, wait_stats.slept,
		(unsigned long long)(wait_stats.us / 1000),
		(unsigned long long)wait_stats.max_us,
		wait_stats.hist[0], wait_stats.hist[1], wait_stats.hist[2],
		wait_stats.hist[3], wait_stats.hist[4], wait_stats.hist[5],
		wait_stats.hist[6], wait_stats.hist[7]);
}

static int dev_fence_wait(struct rdn_device *dev, uint32_t fence,
			  uint64_t timeout_ns)
{
	static long spin_us = -1, stats;
	struct darwin_device *d = (struct darwin_device *)dev;
	uint64_t ms = timeout_ns / 1000000;
	uint64_t start, us, limit;
	bool reached = false;
	unsigned i;

	if (spin_us < 0) {
		spin_us = env_number("RDN_FENCE_SPIN");
		stats = env_number("RDN_STATS");
		if (stats)
			atexit(wait_stats_print);
	}
	/* "Forever" still ends: a hung GPU must not hang the process. */
	if (ms > 10000)
		ms = 10000;
	if (!ms)
		ms = 1;
	start = (spin_us || stats) ? now_us() : 0;
	if (spin_us) {
		do
			reached = fence_wait_ms(d, fence, 0);
		while (!reached && now_us() - start < (uint64_t)spin_us);
		if (reached)
			wait_stats.spun++;
	}
	if (!reached) {
		reached = fence_wait_ms(d, fence, (uint32_t)ms);
		wait_stats.slept++;
	}
	if (stats) {
		us = now_us() - start;
		wait_stats.waits++;
		wait_stats.us += us;
		if (us > wait_stats.max_us)
			wait_stats.max_us = us;
		for (i = 0, limit = 250; i < 7 && us >= limit; i++, limit *= 2)
			;
		wait_stats.hist[i]++;
	}
	return reached ? 0 : -1;
}

static void dev_sync_for_cpu(struct rdn_device *dev)
{
	struct darwin_device *d = (struct darwin_device *)dev;

	IOConnectMethodScalarIScalarO(d->conn, RDN_UC_SYNC_FOR_CPU, 0, 0);
}

static bool dev_surface_region(struct rdn_device *dev, uint32_t id,
			       struct rdn_region *region)
{
	struct darwin_device *d = (struct darwin_device *)dev;
	static struct rdn_user_region r;
	IOByteCount size = sizeof(r);
	uint32_t n;

	if (IOConnectMethodScalarIStructureO(d->conn, RDN_UC_SURFACE_REGION, 1,
					     &size, (int)id, &r))
		return false;
	region->count = r.count;
	memcpy(region->bounds, r.bounds, sizeof(region->bounds));
	n = r.count < RDN_USER_REGION_RECTS ? r.count : RDN_USER_REGION_RECTS;
	if (n > RDN_REGION_RECTS)
		n = RDN_REGION_RECTS;
	memcpy(region->rects, r.rects, n * sizeof(r.rects[0]));
	return true;
}

static bool dev_surface_buffer(struct rdn_device *dev, uint32_t id,
			       uint64_t offset, uint32_t row_bytes,
			       uint32_t width, uint32_t height)
{
	struct darwin_device *d = (struct darwin_device *)dev;

	return IOConnectMethodScalarIScalarO(d->conn, RDN_UC_SURFACE_BUFFER, 5, 0,
					     (int)id, (int)offset, (int)row_bytes,
					     (int)width, (int)height) == 0;
}

static uint32_t dev_surface_list(struct rdn_device *dev, struct rdn_surface *list,
				 uint32_t max)
{
	struct darwin_device *d = (struct darwin_device *)dev;
	static struct rdn_user_surfaces s;
	IOByteCount size = sizeof(s);
	uint32_t i;

	if (IOConnectMethodScalarIStructureO(d->conn, RDN_UC_SURFACE_LIST, 0,
					     &size, &s))
		return 0;
	for (i = 0; i < s.count && i < max && i < RDN_USER_SURFACES; i++) {
		list[i].id = s.surface[i].id;
		list[i].offset = s.surface[i].offset;
		list[i].row_bytes = s.surface[i].row_bytes;
		list[i].width = s.surface[i].width;
		list[i].height = s.surface[i].height;
	}
	return i;
}

static uint32_t dev_surface_locked(struct rdn_device *dev)
{
	struct darwin_device *d = (struct darwin_device *)dev;
	int id = 0;

	if (IOConnectMethodScalarIScalarO(d->conn, RDN_UC_SURFACE_LOCKED, 0, 1, &id))
		return 0;
	return (uint32_t)id;
}

static void dev_destroy(struct rdn_device *dev)
{
	struct darwin_device *d = (struct darwin_device *)dev;

	/* Closing frees what this process allocated and unmaps the aperture. */
	IOServiceClose(d->conn);
	free(d);
}

struct rdn_device *rdn_device_open(void)
{
	struct darwin_device *d = calloc(1, sizeof(*d));
	struct rdn_user_info info;
	IOByteCount size = sizeof(info);
	io_service_t service;
	vm_address_t addr = 0;
	vm_size_t len = 0;
	kern_return_t kr;

	if (!d)
		return NULL;
	service = IOServiceGetMatchingService(kIOMasterPortDefault,
			IOServiceMatching(RDN_UC_SERVICE_CLASS));
	if (!service) {
		fprintf(stderr, "rdn: no %s service\n", RDN_UC_SERVICE_CLASS);
		goto fail;
	}
	kr = IOServiceOpen(service, mach_task_self(), RDN_UC_TYPE, &d->conn);
	IOObjectRelease(service);
	if (kr) {
		fprintf(stderr, "rdn: cannot open the accelerator (0x%x)\n", kr);
		goto fail;
	}
	kr = IOConnectMethodScalarIStructureO(d->conn, RDN_UC_GET_INFO, 0, &size,
					      &info);
	if (kr || info.version != RDN_USER_VERSION) {
		fprintf(stderr, "rdn: accelerator interface mismatch (0x%x, version %u)\n",
			kr, (unsigned)info.version);
		goto fail_close;
	}
	kr = IOConnectMapMemory(d->conn, RDN_UC_MEMORY_APERTURE, mach_task_self(),
				&addr, &len, kIOMapAnywhere);
	if (kr) {
		fprintf(stderr, "rdn: cannot map video memory (0x%x)\n", kr);
		goto fail_close;
	}

	d->base.info.pci_device_id = info.pci_device_id;
	d->base.info.vram_gpu_base = ((uint64_t)info.vram_gpu_base_hi << 32) |
				     info.vram_gpu_base_lo;
	d->base.info.vram_size = info.heap_size;
	d->base.info.tile_config = info.tile_config;
	d->base.info.backend_map = info.backend_map;
	d->base.info.max_backends = info.max_backends;
	d->base.info.max_tile_pipes = info.max_tile_pipes;
	d->base.info.max_pipes = info.max_pipes;
	d->base.info.num_ses = info.num_ses;
	d->base.aperture = (void *)addr;
	d->base.aperture_size = len;
	if (info.fb_bits_per_pixel == 32) {
		d->base.screen.offset = info.fb_offset;
		d->base.screen.width = info.fb_width;
		d->base.screen.height = info.fb_height;
		d->base.screen.pitch_pixels = info.fb_pitch_pixels;
	}
	d->base.alloc = dev_alloc;
	d->base.free = dev_free;
	d->base.submit = dev_submit;
	d->base.fence_done = dev_fence_done;
	d->base.fence_wait = dev_fence_wait;
	d->base.sync_for_cpu = dev_sync_for_cpu;
	d->base.destroy = dev_destroy;
	d->base.surface_region = dev_surface_region;
	d->base.surface_buffer = dev_surface_buffer;
	d->base.surface_list = dev_surface_list;
	d->base.surface_locked = dev_surface_locked;
	return &d->base;

fail_close:
	IOServiceClose(d->conn);
fail:
	free(d);
	return NULL;
}
