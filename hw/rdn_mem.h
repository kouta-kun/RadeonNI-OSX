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

/* Zero size/alignment mean 1; otherwise align is a power of two. */
int rdn_mem_alloc(struct rdn_mem *mem, uint64_t size, uint64_t align,
		  uint64_t *offset);
/* Inside [lo, hi). Failure returns -ENOMEM without changing offset or the heap. */
int rdn_mem_alloc_range(struct rdn_mem *mem, uint64_t size, uint64_t align,
			uint64_t lo, uint64_t hi, uint64_t *offset);
/* Strict variant: size and alignment must be nonzero. */
int rdn_mem_alloc_in_range(struct rdn_mem *mem, uint64_t size, uint64_t align,
			  uint64_t minimum, uint64_t maximum, uint64_t *offset);
/* Free the range that starts at `offset`; unknown offsets are ignored. */
void rdn_mem_free(struct rdn_mem *mem, uint64_t offset);
uint64_t rdn_mem_allocation_size(struct rdn_mem *mem, uint64_t offset);
/* Bytes not handed out. */
uint64_t rdn_mem_available(struct rdn_mem *mem);
/* Bytes not handed out inside [lo, hi), and (if `largest` is not NULL) the largest free piece there. */
uint64_t rdn_mem_available_range(struct rdn_mem *mem, uint64_t lo, uint64_t hi,
				 uint64_t *largest);
uint64_t rdn_mem_largest(struct rdn_mem *mem);

#endif /* RDN_MEM_H */
