/*
 * The GART, after the Linux radeon driver: evergreen_pcie_gart_enable(),
 * evergreen_pcie_gart_disable() and evergreen_pcie_gart_tlb_flush() in
 * evergreen.c, the page entries of rs600_gart_get_page_entry() in rs600.c
 * and radeon_gart_set_page(), and the system aperture part of
 * evergreen_mc_program(). Linux moves video memory to address 0 while the
 * displays are stopped; this library leaves it where ASIC_Init put it and
 * only declares that range the system aperture.
 *
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 * Copyright 2010 Advanced Micro Devices, Inc.
 * Copyright (c) 2026 kouta-kun and Claude
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "rdn_gart.h"
#include "rdn_accel_reg.h"
#include "linux/evergreend.h"

#define R600_PTE_VALID		(1 << 0)
#define R600_PTE_SYSTEM		(1 << 1)
#define R600_PTE_SNOOPED	(1 << 2)
#define R600_PTE_READABLE	(1 << 5)
#define R600_PTE_WRITEABLE	(1 << 6)

#define FLUSH_TIMEOUT_US	100000

/* An entry is 64 bits, little-endian, low word first. */
static void write_entry(struct rdn_gart *gart, uint32_t page, uint64_t bus_addr)
{
	uint64_t entry = (bus_addr & ~0xfffull) | R600_PTE_SYSTEM | R600_PTE_VALID |
			 R600_PTE_READABLE | R600_PTE_WRITEABLE | R600_PTE_SNOOPED;

	rdn_vram_write32(gart->accel, gart->table_offset + page * 8, (uint32_t)entry);
	rdn_vram_write32(gart->accel, gart->table_offset + page * 8 + 4,
			 (uint32_t)(entry >> 32));
}

void rdn_gart_set_page(struct rdn_gart *gart, uint32_t page, uint64_t bus_addr)
{
	if (page < RDN_GART_PAGES)
		write_entry(gart, page, bus_addr);
}

void rdn_gart_clear_page(struct rdn_gart *gart, uint32_t page)
{
	if (page < RDN_GART_PAGES)
		write_entry(gart, page, gart->dummy);
}

/* evergreen_pcie_gart_tlb_flush() */
void rdn_gart_flush(struct rdn_gart *gart)
{
	struct rdn_card *card = gart->accel->card;
	uint32_t tmp;
	int i;

	rdn_wreg(card, HDP_MEM_COHERENCY_FLUSH_CNTL, 0x1);

	rdn_wreg(card, VM_CONTEXT0_REQUEST_RESPONSE, REQUEST_TYPE(1));
	for (i = 0; i < FLUSH_TIMEOUT_US; i++) {
		tmp = rdn_rreg(card, VM_CONTEXT0_REQUEST_RESPONSE);
		tmp = (tmp & RESPONSE_TYPE_MASK) >> RESPONSE_TYPE_SHIFT;
		if (tmp == 2) {
			rdn_log(card->os, RDN_LOG_ERROR, "GART: flushing the TLB failed");
			return;
		}
		if (tmp)
			return;
		card->os->delay_us(card->os->cookie, 1);
	}
}

int rdn_gart_enable(struct rdn_gart *gart, struct rdn_accel *accel,
		    uint32_t table_offset, uint64_t dummy)
{
	struct rdn_card *card = accel->card;
	uint64_t vram_end = accel->vram_base + accel->vram_size - 1;
	uint32_t tmp, page;

	if ((table_offset & 0xfff) || (dummy & 0xfff) ||
	    table_offset + RDN_GART_TABLE_BYTES > accel->aperture_size ||
	    (accel->vram_base < RDN_GART_GPU_START + (uint64_t)RDN_GART_PAGES * RDN_GART_PAGE_SIZE &&
	     vram_end >= RDN_GART_GPU_START))
		return -EINVAL;

	gart->accel = accel;
	gart->table_offset = table_offset;
	gart->dummy = dummy;
	for (page = 0; page < RDN_GART_PAGES; page++)
		write_entry(gart, page, dummy);

	/*
	 * evergreen_mc_program(), the system aperture only: video memory is
	 * addressed directly, everything else through the table.
	 */
	rdn_wreg(card, MC_VM_SYSTEM_APERTURE_LOW_ADDR, (uint32_t)(accel->vram_base >> 12));
	rdn_wreg(card, MC_VM_SYSTEM_APERTURE_HIGH_ADDR, (uint32_t)(vram_end >> 12));
	rdn_wreg(card, MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR,
		 (uint32_t)(rdn_vram_addr(accel, table_offset) >> 12));

	/* Setup L2 cache */
	rdn_wreg(card, VM_L2_CNTL, ENABLE_L2_CACHE | ENABLE_L2_FRAGMENT_PROCESSING |
		 ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE |
		 EFFECTIVE_L2_QUEUE_SIZE(7));
	rdn_wreg(card, VM_L2_CNTL2, 0);
	rdn_wreg(card, VM_L2_CNTL3, BANK_SELECT(0) | CACHE_UPDATE_MODE(2));
	/* Setup TLB control */
	tmp = ENABLE_L1_TLB | ENABLE_L1_FRAGMENT_PROCESSING |
	      SYSTEM_ACCESS_MODE_NOT_IN_SYS |
	      SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU |
	      EFFECTIVE_L1_TLB_SIZE(5) | EFFECTIVE_L1_QUEUE_SIZE(5);
	rdn_wreg(card, MC_VM_MD_L1_TLB0_CNTL, tmp);
	rdn_wreg(card, MC_VM_MD_L1_TLB1_CNTL, tmp);
	rdn_wreg(card, MC_VM_MD_L1_TLB2_CNTL, tmp);
	rdn_wreg(card, MC_VM_MB_L1_TLB0_CNTL, tmp);
	rdn_wreg(card, MC_VM_MB_L1_TLB1_CNTL, tmp);
	rdn_wreg(card, MC_VM_MB_L1_TLB2_CNTL, tmp);
	rdn_wreg(card, MC_VM_MB_L1_TLB3_CNTL, tmp);
	rdn_wreg(card, VM_CONTEXT0_PAGE_TABLE_START_ADDR,
		 (uint32_t)(RDN_GART_GPU_START >> 12));
	rdn_wreg(card, VM_CONTEXT0_PAGE_TABLE_END_ADDR,
		 (uint32_t)(RDN_GART_GPU_START >> 12) + RDN_GART_PAGES - 1);
	rdn_wreg(card, VM_CONTEXT0_PAGE_TABLE_BASE_ADDR,
		 (uint32_t)(rdn_vram_addr(accel, table_offset) >> 12));
	rdn_wreg(card, VM_CONTEXT0_CNTL, ENABLE_CONTEXT | PAGE_TABLE_DEPTH(0) |
		 RANGE_PROTECTION_FAULT_ENABLE_DEFAULT);
	rdn_wreg(card, VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR,
		 (uint32_t)(dummy >> 12));
	rdn_wreg(card, VM_CONTEXT1_CNTL, 0);

	rdn_gart_flush(gart);
	rdn_log(card->os, RDN_LOG_INFO,
		"GART of %u MB enabled at GPU address 0x%X, table at aperture offset 0x%X",
		(unsigned)(RDN_GART_PAGES >> 8), (unsigned)RDN_GART_GPU_START,
		(unsigned)table_offset);
	gart->ready = true;
	return 0;
}

/* evergreen_pcie_gart_disable(), without unpinning anything */
void rdn_gart_disable(struct rdn_gart *gart)
{
	struct rdn_card *card;
	uint32_t tmp;

	if (!gart->ready)
		return;
	card = gart->accel->card;
	/* Disable all tables */
	rdn_wreg(card, VM_CONTEXT0_CNTL, 0);
	rdn_wreg(card, VM_CONTEXT1_CNTL, 0);
	/* Setup L2 cache */
	rdn_wreg(card, VM_L2_CNTL, ENABLE_L2_FRAGMENT_PROCESSING |
		 EFFECTIVE_L2_QUEUE_SIZE(7));
	rdn_wreg(card, VM_L2_CNTL2, 0);
	rdn_wreg(card, VM_L2_CNTL3, BANK_SELECT(0) | CACHE_UPDATE_MODE(2));
	/* Setup TLB control */
	tmp = EFFECTIVE_L1_TLB_SIZE(5) | EFFECTIVE_L1_QUEUE_SIZE(5);
	rdn_wreg(card, MC_VM_MD_L1_TLB0_CNTL, tmp);
	rdn_wreg(card, MC_VM_MD_L1_TLB1_CNTL, tmp);
	rdn_wreg(card, MC_VM_MD_L1_TLB2_CNTL, tmp);
	rdn_wreg(card, MC_VM_MB_L1_TLB0_CNTL, tmp);
	rdn_wreg(card, MC_VM_MB_L1_TLB1_CNTL, tmp);
	rdn_wreg(card, MC_VM_MB_L1_TLB2_CNTL, tmp);
	rdn_wreg(card, MC_VM_MB_L1_TLB3_CNTL, tmp);
	gart->ready = false;
}
