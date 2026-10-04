/*
 * Card bring-up: POST detection and ASIC_Init.
 *
 * Follows the Linux radeon driver: radeon_card_posted() and
 * radeon_boot_test_post_card() in radeon_device.c,
 * radeon_atom_initialize_bios_scratch_regs() in radeon_atombios.c,
 * evergreen_asic_reset() and its helpers in evergreen.c, and
 * r600_set_bios_scratch_engine_hung() in r600.c.
 *
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 * Copyright 2010 Advanced Micro Devices, Inc.
 * Copyright (c) 2026 the osx-gpu contributors
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
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */

#include "rdn_card.h"
#include "rdn_reg.h"

static const uint32_t crtc_offsets[RDN_NUM_CRTC] = {
	EVERGREEN_CRTC0_REGISTER_OFFSET,
	EVERGREEN_CRTC1_REGISTER_OFFSET,
	EVERGREEN_CRTC2_REGISTER_OFFSET,
	EVERGREEN_CRTC3_REGISTER_OFFSET,
	EVERGREEN_CRTC4_REGISTER_OFFSET,
	EVERGREEN_CRTC5_REGISTER_OFFSET
};

int rdn_card_init(struct rdn_card *card, struct rdn_os *os, void *bios)
{
	memset(card, 0, sizeof(*card));
	card->os = os;
	return rdn_atom_init(&card->atom, os, bios);
}

void rdn_card_fini(struct rdn_card *card)
{
	rdn_atom_fini(&card->atom);
}

bool rdn_card_posted(struct rdn_card *card)
{
	uint32_t reg = 0;
	int i;

	/* first check CRTCs */
	for (i = 0; i < RDN_NUM_CRTC; i++)
		reg |= rdn_rreg(card, EVERGREEN_CRTC_CONTROL + crtc_offsets[i]);
	if (reg & EVERGREEN_CRTC_MASTER_EN)
		return true;

	/* then check MEM_SIZE, in case the crtcs are off */
	if (rdn_rreg(card, CONFIG_MEMSIZE))
		return true;

	return false;
}

static void rdn_initialize_bios_scratch_regs(struct rdn_card *card)
{
	uint32_t bios_2_scratch, bios_6_scratch;

	bios_2_scratch = rdn_rreg(card, R600_BIOS_2_SCRATCH);
	bios_6_scratch = rdn_rreg(card, R600_BIOS_6_SCRATCH);

	/* let the bios control the backlight */
	bios_2_scratch &= ~ATOM_S2_VRI_BRIGHT_ENABLE;

	/* tell the bios not to handle mode switching */
	bios_6_scratch |= ATOM_S6_ACC_BLOCK_DISPLAY_SWITCH;

	/* clear the vbios dpms state */
	bios_2_scratch &= ~ATOM_S2_DEVICE_DPMS_STATE;

	rdn_wreg(card, R600_BIOS_2_SCRATCH, bios_2_scratch);
	rdn_wreg(card, R600_BIOS_6_SCRATCH, bios_6_scratch);
}

static void rdn_set_bios_scratch_engine_hung(struct rdn_card *card, bool hung)
{
	uint32_t tmp = rdn_rreg(card, R600_BIOS_3_SCRATCH);

	if (hung)
		tmp |= ATOM_S3_ASIC_GUI_ENGINE_HUNG;
	else
		tmp &= ~ATOM_S3_ASIC_GUI_ENGINE_HUNG;

	rdn_wreg(card, R600_BIOS_3_SCRATCH, tmp);
}

static bool rdn_is_display_hung(struct rdn_card *card)
{
	uint32_t crtc_hung = 0;
	uint32_t crtc_status[RDN_NUM_CRTC];
	uint32_t i, j, tmp;

	for (i = 0; i < RDN_NUM_CRTC; i++) {
		if (rdn_rreg(card, EVERGREEN_CRTC_CONTROL + crtc_offsets[i]) &
		    EVERGREEN_CRTC_MASTER_EN) {
			crtc_status[i] = rdn_rreg(card,
				EVERGREEN_CRTC_STATUS_HV_COUNT + crtc_offsets[i]);
			crtc_hung |= (1 << i);
		}
	}

	for (j = 0; j < 10; j++) {
		for (i = 0; i < RDN_NUM_CRTC; i++) {
			if (crtc_hung & (1 << i)) {
				tmp = rdn_rreg(card,
					EVERGREEN_CRTC_STATUS_HV_COUNT + crtc_offsets[i]);
				if (tmp != crtc_status[i])
					crtc_hung &= ~(1 << i);
			}
		}
		if (crtc_hung == 0)
			return false;
		card->os->delay_us(card->os->cookie, 100);
	}

	return true;
}

static uint32_t rdn_gpu_check_soft_reset(struct rdn_card *card)
{
	uint32_t reset_mask = 0;
	uint32_t tmp;

	/* GRBM_STATUS */
	tmp = rdn_rreg(card, GRBM_STATUS);
	if (tmp & (PA_BUSY | SC_BUSY |
		   SH_BUSY | SX_BUSY |
		   TA_BUSY | VGT_BUSY |
		   DB_BUSY | CB_BUSY |
		   SPI_BUSY | VGT_BUSY_NO_DMA))
		reset_mask |= RADEON_RESET_GFX;

	if (tmp & (CF_RQ_PENDING | PF_RQ_PENDING |
		   CP_BUSY | CP_COHERENCY_BUSY))
		reset_mask |= RADEON_RESET_CP;

	if (tmp & GRBM_EE_BUSY)
		reset_mask |= RADEON_RESET_GRBM | RADEON_RESET_GFX | RADEON_RESET_CP;

	/* DMA_STATUS_REG */
	tmp = rdn_rreg(card, DMA_STATUS_REG);
	if (!(tmp & DMA_IDLE))
		reset_mask |= RADEON_RESET_DMA;

	/* SRBM_STATUS2 */
	tmp = rdn_rreg(card, SRBM_STATUS2);
	if (tmp & DMA_BUSY)
		reset_mask |= RADEON_RESET_DMA;

	/* SRBM_STATUS */
	tmp = rdn_rreg(card, SRBM_STATUS);
	if (tmp & (RLC_RQ_PENDING | RLC_BUSY))
		reset_mask |= RADEON_RESET_RLC;

	if (tmp & IH_BUSY)
		reset_mask |= RADEON_RESET_IH;

	if (tmp & SEM_BUSY)
		reset_mask |= RADEON_RESET_SEM;

	if (tmp & GRBM_RQ_PENDING)
		reset_mask |= RADEON_RESET_GRBM;

	if (tmp & VMC_BUSY)
		reset_mask |= RADEON_RESET_VMC;

	if (tmp & (MCB_BUSY | MCB_NON_DISPLAY_BUSY |
		   MCC_BUSY | MCD_BUSY))
		reset_mask |= RADEON_RESET_MC;

	if (rdn_is_display_hung(card))
		reset_mask |= RADEON_RESET_DISPLAY;

	/* VM_L2_STATUS */
	tmp = rdn_rreg(card, VM_L2_STATUS);
	if (tmp & L2_BUSY)
		reset_mask |= RADEON_RESET_VMC;

	/* Skip MC reset as it's mostly likely not hung, just busy */
	if (reset_mask & RADEON_RESET_MC) {
		rdn_log(card->os, RDN_LOG_DEBUG, "MC busy: 0x%08X, clearing.",
			(unsigned)reset_mask);
		reset_mask &= ~RADEON_RESET_MC;
	}

	return reset_mask;
}

/*
 * Linux soft-resets whichever blocks report busy here. Those are the
 * acceleration engines, which this library never starts, so a busy block
 * means the card was left in a state we did not create. Report it and go on;
 * the soft reset itself is not ported.
 */
static void rdn_asic_reset_check(struct rdn_card *card)
{
	uint32_t reset_mask;

	reset_mask = rdn_gpu_check_soft_reset(card);
	if (reset_mask)
		rdn_set_bios_scratch_engine_hung(card, true);

	reset_mask = rdn_gpu_check_soft_reset(card);
	reset_mask = rdn_gpu_check_soft_reset(card);
	if (!reset_mask)
		rdn_set_bios_scratch_engine_hung(card, false);
	else
		rdn_log(card->os, RDN_LOG_ERROR,
			"GPU blocks busy before init (mask 0x%08X); soft reset is not implemented",
			(unsigned)reset_mask);
}

int rdn_card_post(struct rdn_card *card)
{
	int r;

	rdn_log(card->os, RDN_LOG_INFO, "card is %s on entry",
		rdn_card_posted(card) ? "posted" : "not posted");

	rdn_initialize_bios_scratch_regs(card);
	rdn_asic_reset_check(card);

	if (rdn_card_posted(card))
		return 0;

	rdn_log(card->os, RDN_LOG_INFO, "GPU not posted. posting now...");
	r = atom_asic_init(card->atom.ctx);
	if (r) {
		rdn_log(card->os, RDN_LOG_ERROR, "ASIC_Init failed (%d)", r);
		return r < 0 ? r : -EINVAL;
	}
	return 0;
}
