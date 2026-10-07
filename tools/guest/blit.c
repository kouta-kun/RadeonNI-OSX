/*
 * blit: run the library's surface copy (hw/rdn_blit.c) from user space.
 *
 *   blit [times [pause_ms]]
 *
 * For every window server surface whose owner has registered a buffer,
 * copies the buffer to the surface's place on the screen, inside the
 * surface's shape: what the kext does when the window server flushes a
 * surface. Here the library is compiled into the program and its command
 * buffer goes through the accelerator's user client, so the copy can be
 * tried on a card without loading another kext. `times` repeats it
 * (default 1) with `pause_ms` in between (default 20).
 *
 * Build on Tiger, next to a copy of hw/:
 *   gcc -Wall -Ihw -o blit blit.c hw/rdn_blit.c \
 *       -framework IOKit -framework CoreFoundation
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>

#include "rdn_accel.h"
#include "rdn_user.h"

static io_connect_t conn;
static struct rdn_user_info info;

/* The two calls the library's copy makes, through the user client. */
int rdn_ib_submit(struct rdn_accel *accel, uint64_t addr, uint32_t words,
		  uint32_t *seq)
{
	int fence = 0;

	if (IOConnectMethodScalarIScalarO(conn, RDN_UC_SUBMIT, 2, 1,
					  (int)(addr - accel->vram_base),
					  (int)words, &fence))
		return -1;
	*seq = (uint32_t)fence;
	return 0;
}

static int wait_fence(uint32_t seq)
{
	int done = 0;

	if (IOConnectMethodScalarIScalarO(conn, RDN_UC_FENCE_WAIT, 2, 1,
					  (int)seq, 2000, &done))
		return 0;
	return done;
}

/* One surface: its shape cut to the screen and to its buffer. */
static int copy_surface(struct rdn_accel *accel, uint32_t work,
			uint32_t id, uint32_t offset, uint32_t row_bytes,
			uint32_t width, uint32_t height)
{
	static struct rdn_blit_rect rects[RDN_BLIT_MAX_RECTS];
	struct rdn_draw_surface dst, src;
	struct rdn_user_region region;
	IOByteCount size = sizeof(region);
	uint32_t i, n = 0, seq = 0;
	int r;

	if (IOConnectMethodScalarIStructureO(conn, RDN_UC_SURFACE_REGION, 1,
					     &size, (int)id, &region) ||
	    !region.count || region.count > RDN_USER_REGION_RECTS)
		return 0;
	for (i = 0; i < region.count && n < RDN_BLIT_MAX_RECTS; i++) {
		int x0 = region.rects[i][0], y0 = region.rects[i][1];
		int x1 = x0 + region.rects[i][2], y1 = y0 + region.rects[i][3];
		int bx = region.bounds[0], by = region.bounds[1];

		if (x0 < bx)
			x0 = bx;
		if (y0 < by)
			y0 = by;
		if (x0 < 0)
			x0 = 0;
		if (y0 < 0)
			y0 = 0;
		if (x1 > bx + (int)width)
			x1 = bx + (int)width;
		if (y1 > by + (int)height)
			y1 = by + (int)height;
		if (x1 > (int)info.fb_width)
			x1 = (int)info.fb_width;
		if (y1 > (int)info.fb_height)
			y1 = (int)info.fb_height;
		if (x1 <= x0 || y1 <= y0)
			continue;
		rects[n].src_x = x0 - bx;
		rects[n].src_y = y0 - by;
		rects[n].dst_x = x0;
		rects[n].dst_y = y0;
		rects[n].width = x1 - x0;
		rects[n].height = y1 - y0;
		n++;
	}
	if (!n)
		return 0;
	dst.gpu_addr = rdn_vram_addr(accel, info.fb_offset);
	dst.width = info.fb_width;
	dst.height = info.fb_height;
	dst.pitch_pixels = info.fb_pitch_pixels;
	src.gpu_addr = rdn_vram_addr(accel, offset);
	src.width = width;
	src.height = height;
	src.pitch_pixels = row_bytes / 4;
	r = rdn_blit(accel, &dst, &src, rects, n, work, &seq);
	if (r || !wait_fence(seq)) {
		fprintf(stderr, "surface 0x%x: copy failed (%d)\n", (unsigned)id, r);
		return 0;
	}
	return (int)n;
}

int main(int argc, char **argv)
{
	io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault,
		IOServiceMatching(RDN_UC_SERVICE_CLASS));
	int times = argc > 1 ? atoi(argv[1]) : 1;
	int pause_ms = argc > 2 ? atoi(argv[2]) : 20;
	struct rdn_user_surfaces list;
	struct rdn_accel accel;
	IOByteCount size = sizeof(info);
	vm_address_t addr = 0;
	vm_size_t len = 0;
	int work = 0, t, copied = 0;
	uint32_t i;

	if (!service || IOServiceOpen(service, mach_task_self(), RDN_UC_TYPE, &conn) ||
	    IOConnectMethodScalarIStructureO(conn, RDN_UC_GET_INFO, 0, &size, &info) ||
	    info.version != RDN_USER_VERSION ||
	    IOConnectMapMemory(conn, RDN_UC_MEMORY_APERTURE, mach_task_self(),
			       &addr, &len, kIOMapAnywhere)) {
		fprintf(stderr, "cannot reach the accelerator\n");
		return 1;
	}
	if (info.fb_bits_per_pixel != 32) {
		fprintf(stderr, "the screen has %u bits a pixel; 32 needed\n",
			(unsigned)info.fb_bits_per_pixel);
		return 1;
	}
	if (IOConnectMethodScalarIScalarO(conn, RDN_UC_ALLOC, 2, 1,
					  (int)rdn_blit_work_bytes(), 4096, &work)) {
		fprintf(stderr, "no video memory for the work area\n");
		return 1;
	}
	memset(&accel, 0, sizeof(accel));
	accel.vram_base = ((uint64_t)info.vram_gpu_base_hi << 32) | info.vram_gpu_base_lo;
	accel.aperture = (volatile uint32_t *)addr;
	accel.aperture_size = info.aperture_size;
	accel.swapped = RDN_BIG_ENDIAN;

	for (t = 0; t < times; t++) {
		size = sizeof(list);
		if (IOConnectMethodScalarIStructureO(conn, RDN_UC_SURFACE_LIST, 0,
						     &size, &list))
			break;
		for (i = 0; i < list.count; i++) {
			int n = copy_surface(&accel, (uint32_t)work,
					     list.surface[i].id, list.surface[i].offset,
					     list.surface[i].row_bytes,
					     list.surface[i].width, list.surface[i].height);

			if (t == 0)
				printf("surface 0x%x, %ux%u at aperture offset 0x%x: %d rectangles copied\n",
				       (unsigned)list.surface[i].id,
				       (unsigned)list.surface[i].width,
				       (unsigned)list.surface[i].height,
				       (unsigned)list.surface[i].offset, n);
			copied += n;
		}
		if (t + 1 < times)
			usleep(pause_ms * 1000);
	}
	printf("%d rectangles copied in %d rounds\n", copied, times);
	IOServiceClose(conn);
	return 0;
}
