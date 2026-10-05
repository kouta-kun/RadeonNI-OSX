/*
 * Range allocator for video memory.
 *
 * Hands out byte ranges of one contiguous region, first fit, with
 * power-of-two alignment. It knows nothing about the card; the caller
 * decides what the region is.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_MEM_H
#define RDN_MEM_H

#include "rdn_os.h"

struct rdn_mem_block;

struct rdn_mem {
	struct rdn_os *os;
	struct rdn_mem_block *head;
	uint64_t base, size;
};

int rdn_mem_init(struct rdn_mem *mem, struct rdn_os *os, uint64_t base,
		 uint64_t size);
void rdn_mem_fini(struct rdn_mem *mem);

/* `align` is a power of two. Returns 0 and the offset, or -ENOMEM. */
int rdn_mem_alloc(struct rdn_mem *mem, uint64_t size, uint64_t align,
		  uint64_t *offset);
/* Free the range that starts at `offset`; unknown offsets are ignored. */
void rdn_mem_free(struct rdn_mem *mem, uint64_t offset);
/* Bytes not handed out. */
uint64_t rdn_mem_available(struct rdn_mem *mem);

#endif /* RDN_MEM_H */
