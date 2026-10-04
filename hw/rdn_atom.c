/*
 * Glue between the OS layer and the AtomBIOS interpreter.
 *
 * The register access callbacks follow radeon_device.c (cail_*) and
 * radeon.h/r100.c (r100_io_rreg, r100_io_wreg) of the Linux radeon driver,
 * which is the behaviour the reference trace was captured from.
 *
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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

#include "rdn_atom.h"

static uint32_t cail_reg_read(struct card_info *info, uint32_t reg)
{
	return info->os->mmio_read32(info->os->cookie, reg * 4);
}

static void cail_reg_write(struct card_info *info, uint32_t reg, uint32_t val)
{
	info->os->mmio_write32(info->os->cookie, reg * 4, val);
}

/*
 * AtomBIOS indirect I/O goes through the I/O BAR. Registers beyond the BAR
 * are reached through its index/data pair.
 */
static uint32_t cail_ioreg_read(struct card_info *info, uint32_t reg)
{
	struct rdn_os *os = info->os;

	reg *= 4;
	if (reg < RDN_IO_BAR_SIZE)
		return os->io_read32(os->cookie, reg);
	os->io_write32(os->cookie, RDN_MM_INDEX, reg);
	return os->io_read32(os->cookie, RDN_MM_DATA);
}

static void cail_ioreg_write(struct card_info *info, uint32_t reg, uint32_t val)
{
	struct rdn_os *os = info->os;

	reg *= 4;
	if (reg < RDN_IO_BAR_SIZE) {
		os->io_write32(os->cookie, reg, val);
		return;
	}
	os->io_write32(os->cookie, RDN_MM_INDEX, reg);
	os->io_write32(os->cookie, RDN_MM_DATA, val);
}

/* These chips have no separately addressed PLL or MC register space. */
static uint32_t cail_invalid_read(struct card_info *info, uint32_t reg)
{
	rdn_log(info->os, RDN_LOG_ERROR, "invalid PLL/MC read of 0x%x",
		(unsigned)reg);
	return 0;
}

static void cail_invalid_write(struct card_info *info, uint32_t reg,
			       uint32_t val)
{
	rdn_log(info->os, RDN_LOG_ERROR, "invalid PLL/MC write of 0x%x = 0x%x",
		(unsigned)reg, (unsigned)val);
}

int rdn_atom_init(struct rdn_atom *atom, struct rdn_os *os, void *bios)
{
	struct card_info *card = &atom->card;

	memset(atom, 0, sizeof(*atom));
	card->os = os;
	card->reg_read = cail_reg_read;
	card->reg_write = cail_reg_write;
	if (os->io_read32 && os->io_write32) {
		card->ioreg_read = cail_ioreg_read;
		card->ioreg_write = cail_ioreg_write;
	} else {
		/* No I/O space: Linux falls back to MMIO for ATOM IIO too. */
		card->ioreg_read = cail_reg_read;
		card->ioreg_write = cail_reg_write;
	}
	card->mc_read = cail_invalid_read;
	card->mc_write = cail_invalid_write;
	card->pll_read = cail_invalid_read;
	card->pll_write = cail_invalid_write;

	atom->ctx = atom_parse(card, bios);
	if (!atom->ctx)
		return -EINVAL;
	if (atom_allocate_fb_scratch(atom->ctx)) {
		atom_destroy(atom->ctx);
		atom->ctx = NULL;
		return -ENOMEM;
	}
	return 0;
}

void rdn_atom_fini(struct rdn_atom *atom)
{
	if (atom->ctx)
		atom_destroy(atom->ctx);
	atom->ctx = NULL;
}
