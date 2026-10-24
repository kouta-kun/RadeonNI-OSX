/*
 * Range allocator for video memory.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <string.h>

#include "rdn_mem.h"

#ifndef ENOMEM
#define ENOMEM 12
#endif

/* The region as a list of blocks in address order, used and free. */
struct rdn_mem_block {
	struct rdn_mem_block *next;
	uint64_t start, size;
	bool used;
};

static struct rdn_mem_block *block_new(struct rdn_mem *mem, uint64_t start,
				       uint64_t size, bool used)
{
	struct rdn_mem_block *b = mem->os->alloc(mem->os->cookie, sizeof(*b));

	if (b) {
		b->next = NULL;
		b->start = start;
		b->size = size;
		b->used = used;
	}
	return b;
}

int rdn_mem_init(struct rdn_mem *mem, struct rdn_os *os, uint64_t base,
		 uint64_t size)
{
	if (!mem)
		return -ENOMEM;
	memset(mem, 0, sizeof(*mem));
	if (!os || !os->alloc || !os->free || base > UINT64_MAX - size)
		return -ENOMEM;
	mem->os = os;
	mem->base = base;
	mem->size = size;
	mem->head = block_new(mem, base, size, false);
	return mem->head ? 0 : -ENOMEM;
}

void rdn_mem_fini(struct rdn_mem *mem)
{
	struct rdn_mem_block *b, *next;

	if (!mem)
		return;
	b = mem->head;
	for (; b; b = next) {
		next = b->next;
		mem->os->free(mem->os->cookie, b);
	}
	mem->head = NULL;
}

static int alloc_range(struct rdn_mem *mem, uint64_t size, uint64_t align,
		       uint64_t lo, uint64_t hi, uint64_t *offset)
{
	struct rdn_mem_block *b, *allocation, *tail;
	uint64_t start, end, limit;

	if (!mem || !offset || !size || !align || (align & (align - 1)) ||
	    lo >= hi || size > hi - lo)
		return -ENOMEM;
	for (b = mem->head; b; b = b->next) {
		if (b->used || b->start > UINT64_MAX - b->size)
			continue;
		end = b->start + b->size;
		start = b->start > lo ? b->start : lo;
		limit = end < hi ? end : hi;
		if (start > UINT64_MAX - (align - 1))
			continue;
		start = (start + align - 1) & ~(align - 1);
		if (start >= limit || size > limit - start)
			continue;
		/* Allocate both split nodes before changing the free block. */
		allocation = start == b->start ? NULL : block_new(mem, start, size, true);
		if (start != b->start && !allocation)
			return -ENOMEM;
		tail = start + size == end ? NULL :
			block_new(mem, start + size, end - start - size, false);
		if (start + size != end && !tail) {
			if (allocation)
				mem->os->free(mem->os->cookie, allocation);
			return -ENOMEM;
		}
		if (tail)
			tail->next = b->next;
		if (allocation) {
			allocation->next = tail ? tail : b->next;
			b->next = allocation;
			b->size = start - b->start;
		} else {
			b->used = true;
			b->size = size;
			if (tail)
				b->next = tail;
		}
		*offset = start;
		return 0;
	}
	return -ENOMEM;
}

int rdn_mem_alloc_range(struct rdn_mem *mem, uint64_t size, uint64_t align,
			uint64_t lo, uint64_t hi, uint64_t *offset)
{
	return alloc_range(mem, size ? size : 1, align ? align : 1,
			   lo, hi, offset);
}

int rdn_mem_alloc_in_range(struct rdn_mem *mem, uint64_t size, uint64_t align,
			  uint64_t minimum, uint64_t maximum, uint64_t *offset)
{
	return alloc_range(mem, size, align, minimum, maximum, offset);
}

int rdn_mem_alloc(struct rdn_mem *mem, uint64_t size, uint64_t align,
		  uint64_t *offset)
{
	if (!mem || mem->base > UINT64_MAX - mem->size)
		return -ENOMEM;
	return rdn_mem_alloc_range(mem, size, align, mem->base,
				   mem->base + mem->size, offset);
}

void rdn_mem_free(struct rdn_mem *mem, uint64_t offset)
{
	struct rdn_mem_block *b, *prev = NULL, *next;

	if (!mem)
		return;
	for (b = mem->head; b; prev = b, b = b->next)
		if (b->used && b->start == offset)
			break;
	if (!b)
		return;
	b->used = false;
	next = b->next;
	if (next && !next->used) {
		b->size += next->size;
		b->next = next->next;
		mem->os->free(mem->os->cookie, next);
	}
	if (prev && !prev->used) {
		prev->size += b->size;
		prev->next = b->next;
		mem->os->free(mem->os->cookie, b);
	}
}

uint64_t rdn_mem_available(struct rdn_mem *mem)
{
	return rdn_mem_available_range(mem, 0, UINT64_MAX, NULL);
}

uint64_t rdn_mem_available_range(struct rdn_mem *mem, uint64_t lo, uint64_t hi,
				 uint64_t *largest)
{
	struct rdn_mem_block *b;
	uint64_t total = 0, big = 0;

	for (b = mem && lo < hi ? mem->head : NULL; b; b = b->next) {
		uint64_t start, end;

		if (b->used || b->start > UINT64_MAX - b->size)
			continue;
		start = b->start > lo ? b->start : lo;
		end = b->start + b->size < hi ? b->start + b->size : hi;
		if (start >= end)
			continue;
		total += end - start;
		if (end - start > big)
			big = end - start;
	}
	if (largest)
		*largest = big;
	return total;
}

uint64_t rdn_mem_largest(struct rdn_mem *mem)
{
	uint64_t largest;
	rdn_mem_available_range(mem, 0, UINT64_MAX, &largest);
	return largest;
}

uint64_t rdn_mem_allocation_size(struct rdn_mem *mem, uint64_t offset)
{
    struct rdn_mem_block *b;
    for (b = mem->head; b; b = b->next)
        if (b->start == offset && b->used) return b->size;
    return 0;
}
