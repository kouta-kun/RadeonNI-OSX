/*
 * Test of the video memory range allocator: a fixed pseudo-random
 * sequence of allocations and frees, checked for overlap, alignment and
 * bounds, ending with everything freed and one block left.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hw/rdn_mem.h"

#define SLOTS	64
#define BASE	0x2000000ull
#define SIZE	0x8000000ull

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

int main(void)
{
	struct { uint64_t off, size; int used; } slot[SLOTS];
	struct rdn_mem mem;
	struct rdn_os os;
	uint32_t seed = 12345, digest = 2166136261u;
	uint64_t off;
	int i, j, step, failures = 0, allocs = 0;

	memset(&os, 0, sizeof(os));
	os.alloc = os_alloc;
	os.free = os_free;
	memset(slot, 0, sizeof(slot));
	if (rdn_mem_init(&mem, &os, BASE, SIZE))
		return 2;

	for (step = 0; step < 20000; step++) {
		seed = seed * 1664525u + 1013904223u;
		i = (seed >> 8) % SLOTS;
		if (slot[i].used) {
			rdn_mem_free(&mem, slot[i].off);
			slot[i].used = 0;
			continue;
		}
		seed = seed * 1664525u + 1013904223u;
		slot[i].size = 1 + (seed >> 4) % (4u << 20);
		seed = seed * 1664525u + 1013904223u;
		if (rdn_mem_alloc(&mem, slot[i].size, 1ull << ((seed >> 12) % 17),
				  &off)) {
			failures++;
			continue;
		}
		if (off & ((1ull << ((seed >> 12) % 17)) - 1) || off < BASE ||
		    off + slot[i].size > BASE + SIZE) {
			printf("FAIL: bad range at step %d\n", step);
			return 1;
		}
		for (j = 0; j < SLOTS; j++)
			if (slot[j].used && off < slot[j].off + slot[j].size &&
			    slot[j].off < off + slot[i].size) {
				printf("FAIL: overlap at step %d\n", step);
				return 1;
			}
		slot[i].off = off;
		slot[i].used = 1;
		allocs++;
		digest = (digest ^ (uint32_t)off) * 16777619u;
	}
	for (i = 0; i < SLOTS; i++)
		if (slot[i].used)
			rdn_mem_free(&mem, slot[i].off);
	if (rdn_mem_available(&mem) != SIZE ||
	    rdn_mem_alloc(&mem, SIZE, 1, &off) || off != BASE) {
		printf("FAIL: memory not whole after freeing everything\n");
		return 1;
	}
	rdn_mem_fini(&mem);
	printf("PASS: %d allocations, %d refused for lack of room, digest %08x\n",
	       allocs, failures, (unsigned)digest);
	return 0;
}
