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

static int dev_fence_wait(struct rdn_device *dev, uint32_t fence,
			  uint64_t timeout_ns)
{
	uint64_t ms = timeout_ns / 1000000;

	/* "Forever" still ends: a hung GPU must not hang the process. */
	if (ms > 10000)
		ms = 10000;
	if (!ms)
		ms = 1;
	return fence_wait_ms((struct darwin_device *)dev, fence, (uint32_t)ms) ? 0 : -1;
}

static void dev_sync_for_cpu(struct rdn_device *dev)
{
	struct darwin_device *d = (struct darwin_device *)dev;

	IOConnectMethodScalarIScalarO(d->conn, RDN_UC_SYNC_FOR_CPU, 0, 0);
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
	return &d->base;

fail_close:
	IOServiceClose(d->conn);
fail:
	free(d);
	return NULL;
}
