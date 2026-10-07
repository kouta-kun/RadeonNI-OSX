/*
 * gablit: the screen filled and copied by the GPU, without the window
 * server (the kext's RDN_UC_SCREEN_FILL and RDN_UC_SCREEN_COPY, which the
 * 2D plug-in uses with /tmp/rdnga.gpu).
 *
 *   gablit x y
 *
 * SCRIBBLES ON THE VISIBLE SCREEN, in the 320x200 pixels whose top left
 * corner is x,y. A dark grey ground; four squares 40 pixels a side at
 * x+10, +70, +130 and +190, y+10: red, green, blue, white. Then two
 * copies of 100x40 pixels that overlap themselves: the red and green pair
 * 30 right and 30 down, and the block at x+130,y+110 (the ground, with
 * nothing else in it) 30 left and 30 up, over the lower right corner of
 * the first copy. Prints each call's result; look at the picture with
 * ~/gl/rdnuc grab. The window server paints over the area when something
 * there changes.
 *
 * Build on the Mac:
 *   gcc -Wall -I. -o gablit gablit.c -framework IOKit -framework CoreFoundation
 * (with rdn_user.h, from hw/, beside it)
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>

#include "rdn_user.h"

static io_connect_t conn;

static UInt32 fill(int x, int y, int w, int h, UInt32 colour)
{
	UInt32 fence = 0;
	kern_return_t kr = IOConnectMethodScalarIScalarO(conn, RDN_UC_SCREEN_FILL,
		5, 1, x, y, w, h, colour, &fence);

	printf("fill %d,%d %dx%d with %08lx -> 0x%x, fence %lu\n", x, y, w, h,
	       (unsigned long)colour, kr, (unsigned long)fence);
	return fence;
}

static UInt32 copy(int sx, int sy, int dx, int dy, int w, int h)
{
	UInt32 fence = 0;
	kern_return_t kr = IOConnectMethodScalarIScalarO(conn, RDN_UC_SCREEN_COPY,
		5, 1, sx, sy, dx, dy, ((UInt32)h << 16) | (UInt32)w, &fence);

	printf("copy %d,%d to %d,%d %dx%d -> 0x%x, fence %lu\n", sx, sy, dx, dy,
	       w, h, kr, (unsigned long)fence);
	return fence;
}

int main(int argc, char **argv)
{
	static const UInt32 colours[4] = {
		0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff
	};
	io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault,
		IOServiceMatching(RDN_UC_SERVICE_CLASS));
	UInt32 fence, reached = 0;
	int x, y, i;

	if (argc < 3) {
		fprintf(stderr, "usage: gablit x y   (draws on the screen there, 320x200)\n");
		return 2;
	}
	x = atoi(argv[1]);
	y = atoi(argv[2]);
	if (!service || IOServiceOpen(service, mach_task_self(), RDN_UC_TYPE, &conn)) {
		fprintf(stderr, "no accelerator\n");
		return 1;
	}
	fill(x, y, 320, 200, 0xff404040);
	for (i = 0; i < 4; i++)
		fill(x + 10 + i * 60, y + 10, 40, 40, colours[i]);
	/* Right and down, then left and up, each over part of itself. */
	copy(x + 10, y + 10, x + 40, y + 40, 100, 40);
	fence = copy(x + 130, y + 110, x + 100, y + 80, 100, 40);
	if (IOConnectMethodScalarIScalarO(conn, RDN_UC_FENCE_WAIT, 2, 1, fence,
					  2000, &reached) || !reached)
		printf("the GPU did not reach fence %lu in 2 s\n", (unsigned long)fence);
	else
		printf("done, fence %lu reached\n", (unsigned long)fence);
	IOServiceClose(conn);
	return 0;
}
