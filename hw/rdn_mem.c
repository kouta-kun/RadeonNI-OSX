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
		b->start = start;
		b->size = size;
		b->used = used;
	}
	return b;
}

int rdn_mem_init(struct rdn_mem *mem, struct rdn_os *os, uint64_t base,
		 uint64_t size)
{
	memset(mem, 0, sizeof(*mem));
	mem->os = os;
	mem->base = base;
	mem->size = size;
	mem->head = block_new(mem, base, size, false);
	return mem->head ? 0 : -ENOMEM;
}

void rdn_mem_fini(struct rdn_mem *mem)
{
	struct rdn_mem_block *b = mem->head, *next;

	for (; b; b = next) {
		next = b->next;
		mem->os->free(mem->os->cookie, b);
	}
	mem->head = NULL;
}

int rdn_mem_alloc(struct rdn_mem *mem, uint64_t size, uint64_t align,
		  uint64_t *offset)
{
	struct rdn_mem_block *b, *tail;
	uint64_t start, pad;

	if (!size)
		size = 1;
	if (!align)
		align = 1;
	for (b = mem->head; b; b = b->next) {
		if (b->used)
			continue;
		start = (b->start + align - 1) & ~(align - 1);
		pad = start - b->start;
		if (pad > b->size || b->size - pad < size)
			continue;
		/* Split off what is left behind the allocation ... */
		if (b->size - pad > size) {
			tail = block_new(mem, start + size, b->size - pad - size,
					 false);
			if (!tail)
				return -ENOMEM;
			tail->next = b->next;
			b->next = tail;
			b->size = pad + size;
		}
		/* ... and the alignment gap in front of it. */
		if (pad) {
			tail = block_new(mem, start, size, true);
			if (!tail)
				return -ENOMEM;
			tail->next = b->next;
			b->next = tail;
			b->size = pad;
		} else {
			b->used = true;
		}
		*offset = start;
		return 0;
	}
	return -ENOMEM;
}

void rdn_mem_free(struct rdn_mem *mem, uint64_t offset)
{
	struct rdn_mem_block *b, *prev = NULL, *next;

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
	struct rdn_mem_block *b;
	uint64_t total = 0;

	for (b = mem->head; b; b = b->next)
		if (!b->used)
			total += b->size;
	return total;
}
