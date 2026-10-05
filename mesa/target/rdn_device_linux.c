/*
 * The device on Linux: the hardware library in this process, on the card's
 * sysfs resources. For development on the x86 host; it needs root, a card
 * no kernel driver is bound to, and `rdn_tool post` and `modeset` done.
 *
 * Environment: RDN_PCI_ADDR (default 0000:10:00.0), RDN_FIRMWARE_DIR
 * (default "firmware"), RDN_TRACE (report every submission and fence wait).
 *
 * Video memory while this runs: the scanout surface at 0 (untouched), the
 * ring at 32 MB, everything Mesa allocates from 34 MB to the end of the
 * aperture.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "rdn_device.h"
#include "hw/rdn_accel.h"
#include "hw/rdn_mem.h"

#define MMIO_SIZE	0x20000
#define RING_OFFSET	(32u << 20)
#define RING_BYTES	(1u << 20)
#define HEAP_OFFSET	(34u << 20)

struct linux_device {
	struct rdn_device base;
	struct rdn_os os;
	struct rdn_card card;
	struct rdn_accel accel;
	struct rdn_mem mem;
	volatile uint8_t *mmio;
	bool accel_up;
	/* RDN_TRACE=1: report every submission and fence wait. */
	bool trace;
};

static uint32_t os_mmio_read32(void *c, uint32_t off)
{
	struct linux_device *d = c;

	return rdn_swap_le32(*(volatile uint32_t *)(d->mmio + off));
}

static void os_mmio_write32(void *c, uint32_t off, uint32_t v)
{
	struct linux_device *d = c;

	*(volatile uint32_t *)(d->mmio + off) = rdn_swap_le32(v);
}

static void os_delay_us(void *c, uint32_t usec)
{
	struct timespec ts = { usec / 1000000, (long)(usec % 1000000) * 1000 };

	(void)c;
	/* Short waits are polls of the card: do not give up the CPU. */
	if (usec < 50) {
		struct timespec a, b;

		clock_gettime(CLOCK_MONOTONIC, &a);
		do
			clock_gettime(CLOCK_MONOTONIC, &b);
		while ((b.tv_sec - a.tv_sec) * 1000000000ll + b.tv_nsec - a.tv_nsec <
		       usec * 1000ll);
		return;
	}
	nanosleep(&ts, NULL);
}

static uint64_t os_time_ms(void *c)
{
	struct timespec ts;

	(void)c;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void *os_alloc(void *c, size_t size)
{
	(void)c;
	return calloc(1, size);
}

static void os_free(void *c, void *ptr)
{
	(void)c;
	free(ptr);
}

static void os_log(void *c, enum rdn_log_level level, const char *fmt,
		   va_list ap)
{
	(void)c;
	if (level == RDN_LOG_DEBUG && !getenv("RDN_DEBUG"))
		return;
	fprintf(stderr, "rdn: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
}

static void *map_resource(const char *addr, const char *name, size_t *size)
{
	char path[256];
	struct stat st;
	void *p;
	int fd;

	snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/%s", addr, name);
	fd = open(path, O_RDWR);
	if (fd < 0 || fstat(fd, &st)) {
		perror(path);
		return NULL;
	}
	p = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (p == MAP_FAILED) {
		perror(path);
		return NULL;
	}
	*size = st.st_size;
	return p;
}

static uint8_t *read_fw(const char *name, size_t *size)
{
	const char *dir = getenv("RDN_FIRMWARE_DIR");
	uint8_t *buf = malloc(1 << 16);
	char path[512];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir ? dir : "firmware", name);
	f = fopen(path, "rb");
	if (!f || !buf) {
		perror(path);
		return NULL;
	}
	*size = fread(buf, 1, 1 << 16, f);
	fclose(f);
	return buf;
}

static int dev_alloc(struct rdn_device *dev, uint64_t size, uint64_t align,
		     uint64_t *offset)
{
	return rdn_mem_alloc(&((struct linux_device *)dev)->mem, size, align, offset);
}

static void dev_free(struct rdn_device *dev, uint64_t offset)
{
	rdn_mem_free(&((struct linux_device *)dev)->mem, offset);
}

static int dev_submit(struct rdn_device *dev, uint64_t offset, uint32_t words,
		      bool swap, uint32_t *fence)
{
	struct linux_device *d = (struct linux_device *)dev;
	int r;

	r = rdn_ib_submit(&d->accel, rdn_vram_addr(&d->accel, (uint32_t)offset),
			  words, swap, fence);
	if (d->trace) {
		const uint8_t *p = (const uint8_t *)dev->aperture + offset;
		uint32_t i;

		/* The first words, as the bytes lie in video memory. */
		fprintf(stderr, "rdn: submit %u words at 0x%llx, swap %d -> %d, fence %u\n   ",
			(unsigned)words, (unsigned long long)offset, swap, r,
			(unsigned)*fence);
		for (i = 0; i < 32 && i < words * 4; i++)
			fprintf(stderr, "%02x%s", p[i], (i & 3) == 3 ? " " : "");
		fprintf(stderr, "\n");
	}
	return r;
}

static bool dev_fence_done(struct rdn_device *dev, uint32_t fence)
{
	return rdn_fence_done(&((struct linux_device *)dev)->accel, fence);
}

static int dev_fence_wait(struct rdn_device *dev, uint32_t fence,
			  uint64_t timeout_ns)
{
	struct linux_device *d = (struct linux_device *)dev;
	uint64_t ms = timeout_ns / 1000000;
	int r;

	/* "Forever" still ends: a hung GPU must not hang the process. */
	if (ms > 10000)
		ms = 10000;
	r = rdn_fence_wait(&d->accel, fence, (uint32_t)ms);
	if (d->trace)
		fprintf(stderr, "rdn: wait for fence %u -> %d, GRBM_STATUS %08x\n",
			(unsigned)fence, r, (unsigned)rdn_rreg(&d->card, 0x8010));
	return r;
}

static void dev_sync_for_cpu(struct rdn_device *dev)
{
	rdn_hdp_flush(&((struct linux_device *)dev)->accel);
}

static void dev_destroy(struct rdn_device *dev)
{
	struct linux_device *d = (struct linux_device *)dev;

	if (d->accel_up)
		rdn_accel_fini(&d->accel);
	rdn_mem_fini(&d->mem);
	free(d);
}

struct rdn_device *rdn_device_open(void)
{
	const char *addr = getenv("RDN_PCI_ADDR");
	struct linux_device *d = calloc(1, sizeof(*d));
	struct rdn_accel_fw fw;
	size_t mmio_size, aperture_size;
	void *aperture;

	if (!d)
		return NULL;
	if (!addr)
		addr = "0000:10:00.0";
	d->trace = getenv("RDN_TRACE") != NULL;

	d->mmio = map_resource(addr, "resource2", &mmio_size);
	/* Not the write-combining mapping: Mesa reads its buffers back. */
	aperture = map_resource(addr, "resource0", &aperture_size);
	if (!d->mmio || !aperture || mmio_size < MMIO_SIZE)
		goto fail;

	d->os.cookie = d;
	d->os.mmio_read32 = os_mmio_read32;
	d->os.mmio_write32 = os_mmio_write32;
	d->os.delay_us = os_delay_us;
	d->os.time_ms = os_time_ms;
	d->os.alloc = os_alloc;
	d->os.free = os_free;
	d->os.log = os_log;
	/* Only register access is used; the card is already posted. */
	d->card.os = &d->os;

	fw.pfp = read_fw("TURKS_pfp.bin", &fw.pfp_size);
	fw.me = read_fw("TURKS_me.bin", &fw.me_size);
	if (!fw.pfp || !fw.me)
		goto fail;
	if (rdn_accel_init(&d->accel, &d->card, aperture, (uint32_t)aperture_size,
			   &fw, RING_OFFSET, RING_BYTES)) {
		fprintf(stderr, "rdn: the command processor did not start; "
			"is the card posted (rdn_tool post, modeset)?\n");
		goto fail;
	}
	d->accel_up = true;
	if (rdn_mem_init(&d->mem, &d->os, HEAP_OFFSET, aperture_size - HEAP_OFFSET))
		goto fail;

	d->base.info.pci_device_id = 0x675d;
	d->base.info.vram_gpu_base = d->accel.vram_base;
	d->base.info.vram_size = aperture_size - HEAP_OFFSET;
	d->base.info.tile_config = d->accel.cfg.tile_config;
	d->base.info.backend_map = d->accel.cfg.backend_map;
	d->base.info.max_backends = d->accel.cfg.max_backends;
	d->base.info.max_tile_pipes = d->accel.cfg.max_tile_pipes;
	d->base.info.max_pipes = d->accel.cfg.max_pipes;
	d->base.info.num_ses = d->accel.cfg.num_ses;
	d->base.aperture = aperture;
	d->base.aperture_size = aperture_size;
	d->base.alloc = dev_alloc;
	d->base.free = dev_free;
	d->base.submit = dev_submit;
	d->base.fence_done = dev_fence_done;
	d->base.fence_wait = dev_fence_wait;
	d->base.sync_for_cpu = dev_sync_for_cpu;
	d->base.destroy = dev_destroy;
	return &d->base;

fail:
	free(d);
	return NULL;
}
