/*
 * apcopy: how fast the CPU writes into video memory through the aperture.
 *
 * Maps the aperture the way the OpenGL bundle does (write combining) and
 * times several ways of copying into it, next to the same copies into
 * ordinary memory: the C library's memcpy in pieces of several sizes (it
 * takes different routes for long operands), and plain loops of 4, 8 and
 * 16 byte stores.
 *
 *   apcopy [megabytes per test]
 *
 * Build on the Mac, next to a copy of hw/rdn_user.h:
 *   gcc -O2 -faltivec -Wall -o apcopy apcopy.c -framework IOKit -framework CoreFoundation
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>

#include "rdn_user.h"

#define AREA (2u << 20)

static double now(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
}

static void copy_memcpy(void *dst, const void *src, size_t n, size_t piece)
{
	size_t done;

	for (done = 0; done < n; done += piece)
		memcpy((char *)dst + done, (const char *)src + done, piece);
}

static void copy_words(void *dst, const void *src, size_t n, size_t piece)
{
	volatile uint32_t *d = dst;
	const uint32_t *s = src;
	size_t i;

	(void)piece;
	for (i = 0; i < n / 4; i++)
		d[i] = s[i];
}

static void copy_doubles(void *dst, const void *src, size_t n, size_t piece)
{
	volatile double *d = dst;
	const double *s = src;
	size_t i;

	(void)piece;
	for (i = 0; i < n / 8; i++)
		d[i] = s[i];
}

static void copy_vectors(void *dst, const void *src, size_t n, size_t piece)
{
	size_t i;

	(void)piece;
	for (i = 0; i < n; i += 16)
		vec_st(vec_ld(i, (const unsigned char *)src), i, (unsigned char *)dst);
}

static void run(const char *what, void (*copy)(void *, const void *, size_t, size_t),
		void *dst, const void *src, size_t piece, unsigned megabytes)
{
	unsigned rounds = megabytes / (AREA >> 20), i;
	double t0 = now(), t;

	for (i = 0; i < rounds; i++)
		copy(dst, src, AREA, piece);
	t = now() - t0;
	printf("  %-28s %7.0f MB/s\n", what, megabytes / t);
}

static void all(const char *where, void *dst, const void *src, unsigned megabytes)
{
	printf("%s:\n", where);
	run("memcpy, 256 KB pieces", copy_memcpy, dst, src, 256 << 10, megabytes);
	run("memcpy, 64 KB pieces", copy_memcpy, dst, src, 64 << 10, megabytes);
	run("memcpy, 4 KB pieces", copy_memcpy, dst, src, 4 << 10, megabytes);
	run("memcpy, 256 byte pieces", copy_memcpy, dst, src, 256, megabytes);
	run("4 byte stores", copy_words, dst, src, 0, megabytes);
	run("8 byte stores", copy_doubles, dst, src, 0, megabytes);
	run("16 byte stores (AltiVec)", copy_vectors, dst, src, 0, megabytes);
}

static void *map(io_connect_t conn, IOOptionBits how)
{
	vm_address_t addr = 0;
	vm_size_t len = 0;

	if (IOConnectMapMemory(conn, RDN_UC_MEMORY_APERTURE, mach_task_self(),
			       &addr, &len, kIOMapAnywhere | how))
		return NULL;
	return (void *)addr;
}

int main(int argc, char **argv)
{
	unsigned megabytes = argc > 1 ? atoi(argv[1]) : 64;
	io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault,
		IOServiceMatching(RDN_UC_SERVICE_CLASS));
	io_connect_t conn;
	uint8_t *wc, *plain, *src, *ram;
	int offset = 0;
	unsigned i;

	if (!service || IOServiceOpen(service, mach_task_self(), RDN_UC_TYPE, &conn)) {
		fprintf(stderr, "no accelerator\n");
		return 1;
	}
	wc = map(conn, kIOMapWriteCombineCache);
	plain = map(conn, kIOMapInhibitCache);
	if (!wc || !plain ||
	    IOConnectMethodScalarIScalarO(conn, RDN_UC_ALLOC, 2, 1, (int)AREA, 4096, &offset)) {
		fprintf(stderr, "no video memory\n");
		return 1;
	}
	/* Page aligned, as the buffers in the aperture are. */
	src = valloc(AREA);
	ram = valloc(AREA);
	for (i = 0; i < AREA; i++)
		src[i] = (uint8_t)(i * 7);
	memset(ram, 0, AREA);

	all("ordinary memory", ram, src, megabytes * 4);
	all("aperture, write combining", wc + offset, src, megabytes);
	if (memcmp(wc + offset, src, AREA))
		printf("  what was read back differs\n");
	all("aperture, uncached and guarded", plain + offset, src, megabytes / 4);

	IOConnectMethodScalarIScalarO(conn, RDN_UC_FREE, 1, 0, offset);
	return 0;
}
