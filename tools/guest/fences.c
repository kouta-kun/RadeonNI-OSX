/*
 * fences: is the GPU executing commands right now?
 *
 *   fences [seconds]
 *   fences now
 *
 * Asks the kext's accelerator which fence its command processor has
 * reached, waits (default 5 s) and asks again. Every command buffer a
 * client submits ends in one fence, so the difference is how many the
 * card completed meanwhile: zero when nothing draws with it.
 *
 * "now" prints the fence reached at this moment, a bare number, and does
 * not wait: for a script that takes the difference over a time of its own
 * (tools/guest/qebench.sh).
 *
 * Build in the guest, next to a copy of hw/rdn_user.h:
 *   gcc -Wall -o fences fences.c -framework IOKit -framework CoreFoundation
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

#include "rdn_user.h"

static io_connect_t conn;

static unsigned reached(unsigned fence)
{
	int r = 0;

	IOConnectMethodScalarIScalarO(conn, RDN_UC_FENCE_WAIT, 2, 1, (int)fence, 0, &r);
	return (unsigned)r;
}

/* The newest fence reached, by bisection: the interface only answers yes or no. */
static unsigned current(void)
{
	unsigned lo = 0, hi = 1;

	while (reached(hi) && hi < 0x40000000)
		hi *= 2;
	while (hi - lo > 1) {
		unsigned mid = lo + (hi - lo) / 2;

		if (reached(mid))
			lo = mid;
		else
			hi = mid;
	}
	return lo;
}

int main(int argc, char **argv)
{
	io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault,
		IOServiceMatching(RDN_UC_SERVICE_CLASS));
	int seconds = argc > 1 ? atoi(argv[1]) : 5;
	unsigned a, b;

	if (!service || IOServiceOpen(service, mach_task_self(), RDN_UC_TYPE, &conn)) {
		fprintf(stderr, "cannot open the accelerator\n");
		return 1;
	}
	if (argc > 1 && !strcmp(argv[1], "now")) {
		printf("%u\n", current());
		IOServiceClose(conn);
		return 0;
	}
	a = current();
	sleep(seconds);
	b = current();
	printf("command buffers the GPU completed in %d s: %u (fence %u -> %u)\n",
	       seconds, b - a, a, b);
	IOServiceClose(conn);
	return 0;
}
