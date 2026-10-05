/*
 * Register definitions used by the hardware library. Names and values are
 * those of the Linux radeon driver (evergreend.h, evergreen_reg.h,
 * r600_reg.h), so that code and traces can be compared directly.
 *
 * Copyright 2010 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
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
 *
 * Authors: Alex Deucher
 */

#ifndef RDN_REG_H
#define RDN_REG_H

/* BIOS scratch registers (r600_reg.h) */
#define R600_BIOS_2_SCRATCH				0x172c
#define R600_BIOS_3_SCRATCH				0x1730
#define R600_BIOS_6_SCRATCH				0x173c

/* Memory controller (evergreend.h, nid.h) */
#define MC_SEQ_SUP_CNTL					0x28c8
#define		RUN_MASK					(1 << 0)
#define MC_SEQ_MISC0					0x2a00
#define		MC_SEQ_MISC0_GDDR5_SHIFT			28
#define		MC_SEQ_MISC0_GDDR5_MASK				0xf0000000
#define		MC_SEQ_MISC0_GDDR5_VALUE			5
#define CONFIG_MEMSIZE					0x5428

/* Status registers used by the reset check (evergreend.h) */
#define VM_L2_STATUS					0x140C
#define		L2_BUSY						(1 << 0)
#define SRBM_STATUS					0x0E50
#define		RLC_RQ_PENDING					(1 << 3)
#define		GRBM_RQ_PENDING					(1 << 5)
#define		VMC_BUSY					(1 << 8)
#define		MCB_BUSY					(1 << 9)
#define		MCB_NON_DISPLAY_BUSY				(1 << 10)
#define		MCC_BUSY					(1 << 11)
#define		MCD_BUSY					(1 << 12)
#define		SEM_BUSY					(1 << 14)
#define		RLC_BUSY					(1 << 15)
#define		IH_BUSY						(1 << 17)
#define SRBM_STATUS2					0x0EC4
#define		DMA_BUSY					(1 << 5)
#define GRBM_STATUS					0x8010
#define		CF_RQ_PENDING					(1 << 7)
#define		PF_RQ_PENDING					(1 << 8)
#define		GRBM_EE_BUSY					(1 << 10)
#define		SX_CLEAN					(1 << 11)
#define		DB_CLEAN					(1 << 12)
#define		CB_CLEAN					(1 << 13)
#define		TA_BUSY						(1 << 14)
#define		VGT_BUSY_NO_DMA					(1 << 16)
#define		VGT_BUSY					(1 << 17)
#define		SX_BUSY						(1 << 20)
#define		SH_BUSY						(1 << 21)
#define		SPI_BUSY					(1 << 22)
#define		SC_BUSY						(1 << 24)
#define		PA_BUSY						(1 << 25)
#define		DB_BUSY						(1 << 26)
#define		CP_COHERENCY_BUSY				(1 << 28)
#define		CP_BUSY						(1 << 29)
#define		CB_BUSY						(1 << 30)
#define		GUI_ACTIVE					(1 << 31)
#define DMA_STATUS_REG					0xd034
#define		DMA_IDLE					(1 << 0)

/* Display controllers (evergreen_reg.h) */
#define EVERGREEN_CRTC0_REGISTER_OFFSET			(0x6df0 - 0x6df0)
#define EVERGREEN_CRTC1_REGISTER_OFFSET			(0x79f0 - 0x6df0)
#define EVERGREEN_CRTC2_REGISTER_OFFSET			(0x105f0 - 0x6df0)
#define EVERGREEN_CRTC3_REGISTER_OFFSET			(0x111f0 - 0x6df0)
#define EVERGREEN_CRTC4_REGISTER_OFFSET			(0x11df0 - 0x6df0)
#define EVERGREEN_CRTC5_REGISTER_OFFSET			(0x129f0 - 0x6df0)

#define EVERGREEN_CRTC_CONTROL				0x6e70
#define		EVERGREEN_CRTC_MASTER_EN			(1 << 0)
#define EVERGREEN_CRTC_STATUS_HV_COUNT			0x6ea0

/* Memory address space (evergreend.h) */
#define MC_VM_FB_LOCATION				0x2024

/* VGA (avivod.h) */
#define AVIVO_D1VGA_CONTROL				0x0330

/* Primary surface and viewport of CRTC 0 (evergreen_reg.h) */
#define EVERGREEN_GRPH_ENABLE				0x6800
#define EVERGREEN_GRPH_CONTROL				0x6804
#define		EVERGREEN_GRPH_DEPTH(x)				(((x) & 0x3) << 0)
#define		EVERGREEN_GRPH_DEPTH_32BPP			2
#define		EVERGREEN_GRPH_FORMAT(x)			(((x) & 0x7) << 8)
#define		EVERGREEN_GRPH_FORMAT_ARGB8888			0
#define EVERGREEN_GRPH_LUT_10BIT_BYPASS_CONTROL		0x6808
#define		EVERGREEN_LUT_10BIT_BYPASS_EN			(1 << 8)
#define EVERGREEN_GRPH_SWAP_CONTROL			0x680c
#define		EVERGREEN_GRPH_ENDIAN_SWAP(x)			(((x) & 0x3) << 0)
#define		EVERGREEN_GRPH_ENDIAN_NONE			0
#define		EVERGREEN_GRPH_ENDIAN_8IN32			2
#define EVERGREEN_GRPH_PRIMARY_SURFACE_ADDRESS		0x6810
#define EVERGREEN_GRPH_SECONDARY_SURFACE_ADDRESS	0x6814
#define		EVERGREEN_GRPH_SURFACE_ADDRESS_MASK		0xffffff00
#define EVERGREEN_GRPH_PITCH				0x6818
#define EVERGREEN_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH	0x681c
#define EVERGREEN_GRPH_SECONDARY_SURFACE_ADDRESS_HIGH	0x6820
#define EVERGREEN_GRPH_SURFACE_OFFSET_X			0x6824
#define EVERGREEN_GRPH_SURFACE_OFFSET_Y			0x6828
#define EVERGREEN_GRPH_X_START				0x682c
#define EVERGREEN_GRPH_Y_START				0x6830
#define EVERGREEN_GRPH_X_END				0x6834
#define EVERGREEN_GRPH_Y_END				0x6838
#define EVERGREEN_GRPH_FLIP_CONTROL			0x6848
#define EVERGREEN_DESKTOP_HEIGHT			0x6b04
#define EVERGREEN_VIEWPORT_START			0x6d70
#define EVERGREEN_VIEWPORT_SIZE				0x6d74
#define EVERGREEN_MASTER_UPDATE_MODE			0x6ef8

/* Colour pipeline of CRTC 0 (nid.h, evergreen_reg.h) */
#define NI_INPUT_GAMMA_CONTROL				0x6840
#define		NI_GRPH_INPUT_GAMMA_MODE(x)			(((x) & 0x3) << 0)
#define		NI_INPUT_GAMMA_USE_LUT				0
#define		NI_OVL_INPUT_GAMMA_MODE(x)			(((x) & 0x3) << 4)
#define NI_PRESCALE_GRPH_CONTROL			0x68b4
#define		NI_GRPH_PRESCALE_BYPASS				(1 << 4)
#define NI_PRESCALE_OVL_CONTROL				0x68c4
#define		NI_OVL_PRESCALE_BYPASS				(1 << 4)
#define NI_INPUT_CSC_CONTROL				0x68d4
#define		NI_INPUT_CSC_GRPH_MODE(x)			(((x) & 0x3) << 0)
#define		NI_INPUT_CSC_BYPASS				0
#define		NI_INPUT_CSC_OVL_MODE(x)			(((x) & 0x3) << 4)
#define NI_OUTPUT_CSC_CONTROL				0x68f0
#define		NI_OUTPUT_CSC_GRPH_MODE(x)			(((x) & 0x7) << 0)
#define		NI_OUTPUT_CSC_BYPASS				0
#define		NI_OUTPUT_CSC_OVL_MODE(x)			(((x) & 0x7) << 4)
#define NI_DEGAMMA_CONTROL				0x6960
#define		NI_GRPH_DEGAMMA_MODE(x)				(((x) & 0x3) << 0)
#define		NI_DEGAMMA_BYPASS				0
#define		NI_OVL_DEGAMMA_MODE(x)				(((x) & 0x3) << 4)
#define		NI_ICON_DEGAMMA_MODE(x)				(((x) & 0x3) << 8)
#define		NI_CURSOR_DEGAMMA_MODE(x)			(((x) & 0x3) << 12)
#define NI_GAMUT_REMAP_CONTROL				0x6964
#define		NI_GRPH_GAMUT_REMAP_MODE(x)			(((x) & 0x3) << 0)
#define		NI_GAMUT_REMAP_BYPASS				0
#define		NI_OVL_GAMUT_REMAP_MODE(x)			(((x) & 0x3) << 4)
#define NI_REGAMMA_CONTROL				0x6a80
#define		NI_GRPH_REGAMMA_MODE(x)				(((x) & 0x7) << 0)
#define		NI_REGAMMA_BYPASS				0
#define		NI_OVL_REGAMMA_MODE(x)				(((x) & 0x7) << 4)
#define EVERGREEN_DC_LUT_RW_MODE			0x69e0
#define EVERGREEN_DC_LUT_RW_INDEX			0x69e4
#define EVERGREEN_DC_LUT_30_COLOR			0x69f0
#define EVERGREEN_DC_LUT_WRITE_EN_MASK			0x69f8
#define EVERGREEN_DC_LUT_CONTROL			0x6a00
#define EVERGREEN_DC_LUT_BLACK_OFFSET_BLUE		0x6a04
#define EVERGREEN_DC_LUT_BLACK_OFFSET_GREEN		0x6a08
#define EVERGREEN_DC_LUT_BLACK_OFFSET_RED		0x6a0c
#define EVERGREEN_DC_LUT_WHITE_OFFSET_BLUE		0x6a10
#define EVERGREEN_DC_LUT_WHITE_OFFSET_GREEN		0x6a14
#define EVERGREEN_DC_LUT_WHITE_OFFSET_RED		0x6a18

/* Reset mask bits (radeon.h) */
#define RADEON_RESET_GFX				(1 << 0)
#define RADEON_RESET_COMPUTE				(1 << 1)
#define RADEON_RESET_DMA				(1 << 2)
#define RADEON_RESET_CP					(1 << 3)
#define RADEON_RESET_GRBM				(1 << 4)
#define RADEON_RESET_DMA1				(1 << 5)
#define RADEON_RESET_RLC				(1 << 6)
#define RADEON_RESET_SEM				(1 << 7)
#define RADEON_RESET_IH					(1 << 8)
#define RADEON_RESET_VMC				(1 << 9)
#define RADEON_RESET_MC					(1 << 10)
#define RADEON_RESET_DISPLAY				(1 << 11)

#endif /* RDN_REG_H */
