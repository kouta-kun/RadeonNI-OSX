/*
 * The GART: a page table in video memory through which the GPU reads and
 * writes pages of system memory.
 *
 * The table gives the GPU a range of its own address space (1 GB starting
 * at RDN_GART_GPU_START, where Linux puts it too) whose pages are whatever
 * bus addresses the OS layer's user binds there. Video memory stays where
 * it is; the "system aperture" registers are set to cover it so that it is
 * not looked up in the table. A page nobody has bound points at a dummy
 * page the caller provides.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_GART_H
#define RDN_GART_H

#include "rdn_accel.h"

#define RDN_GART_PAGE_SIZE	4096
#define RDN_GART_GPU_START	0x40000000ull
#define RDN_GART_PAGES		0x40000		/* 1 GB */
/* Bytes of video memory the table takes: eight a page. */
#define RDN_GART_TABLE_BYTES	(RDN_GART_PAGES * 8)

struct rdn_gart {
	struct rdn_accel *accel;
	uint32_t table_offset;		/* in the aperture */
	uint64_t dummy;			/* bus address of the dummy page */
	bool ready;
};

/*
 * Fill the table at `table_offset` (RDN_GART_TABLE_BYTES of the aperture,
 * page aligned) with the dummy page and switch address translation on.
 * `dummy` is the bus address of a page of system memory that stays
 * allocated. The device must be a bus master. Returns 0 or a negative
 * errno value.
 */
int rdn_gart_enable(struct rdn_gart *gart, struct rdn_accel *accel,
		    uint32_t table_offset, uint64_t dummy);
void rdn_gart_disable(struct rdn_gart *gart);

/* Point page `page` of the range at a bus address, or back at the dummy. */
void rdn_gart_set_page(struct rdn_gart *gart, uint32_t page, uint64_t bus_addr);
void rdn_gart_clear_page(struct rdn_gart *gart, uint32_t page);
/* After changing pages and before the GPU uses them. */
void rdn_gart_flush(struct rdn_gart *gart);

/* The GPU's address of a page of the range. */
static inline uint64_t rdn_gart_addr(uint32_t page)
{
	return RDN_GART_GPU_START + (uint64_t)page * RDN_GART_PAGE_SIZE;
}

#endif /* RDN_GART_H */
