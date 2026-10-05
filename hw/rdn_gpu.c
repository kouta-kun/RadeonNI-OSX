/*
 * 3D engine setup for Turks.
 *
 * evergreen_gpu_init() from the Linux radeon driver (evergreen.c), reduced
 * to the Turks case, and r6xx_remap_render_backend() from r600.c. The
 * register writes and their order are Linux's.
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
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */

#include "rdn_accel.h"
#include "rdn_accel_reg.h"

static uint32_t rdn_popcount32(uint32_t v)
{
	uint32_t n = 0;

	for (; v; v &= v - 1)
		n++;
	return n;
}

static uint32_t rdn_remap_render_backend(uint32_t tiling_pipe_num,
					 uint32_t max_rb_num,
					 uint32_t total_max_rb_num,
					 uint32_t disabled_rb_mask)
{
	uint32_t rendering_pipe_num, rb_num_width, req_rb_num;
	uint32_t pipe_rb_ratio, pipe_rb_remain, tmp;
	uint32_t data = 0, mask = 1 << (max_rb_num - 1);
	unsigned i, j;

	/* mask out the RBs that don't exist on that asic */
	tmp = disabled_rb_mask | ((0xff << max_rb_num) & 0xff);
	/* make sure at least one RB is available */
	if ((tmp & 0xff) != 0xff)
		disabled_rb_mask = tmp;

	rendering_pipe_num = 1 << tiling_pipe_num;
	req_rb_num = total_max_rb_num - rdn_popcount32(disabled_rb_mask);

	pipe_rb_ratio = rendering_pipe_num / req_rb_num;
	pipe_rb_remain = rendering_pipe_num - pipe_rb_ratio * req_rb_num;

	/* eg+ */
	rb_num_width = 4;

	for (i = 0; i < max_rb_num; i++) {
		if (!(mask & disabled_rb_mask)) {
			for (j = 0; j < pipe_rb_ratio; j++) {
				data <<= rb_num_width;
				data |= max_rb_num - i - 1;
			}
			if (pipe_rb_remain) {
				data <<= rb_num_width;
				data |= max_rb_num - i - 1;
				pipe_rb_remain--;
			}
		}
		mask >>= 1;
	}

	return data;
}

void rdn_gpu_init(struct rdn_accel *accel)
{
	struct rdn_card *card = accel->card;
	struct rdn_gpu_config *cfg = &accel->cfg;
	uint32_t gb_addr_config;
	uint32_t mc_arb_ramcfg;
	uint32_t sx_debug_1;
	uint32_t smx_dc_ctl0;
	uint32_t sq_config;
	uint32_t sq_lds_resource_mgmt;
	uint32_t sq_gpr_resource_mgmt_1;
	uint32_t sq_gpr_resource_mgmt_2;
	uint32_t sq_gpr_resource_mgmt_3;
	uint32_t sq_thread_resource_mgmt;
	uint32_t sq_thread_resource_mgmt_2;
	uint32_t sq_stack_resource_mgmt_1;
	uint32_t sq_stack_resource_mgmt_2;
	uint32_t sq_stack_resource_mgmt_3;
	uint32_t vgt_cache_invalidation;
	uint32_t hdp_host_path_cntl, tmp;
	uint32_t disabled_rb_mask;
	int i, j, ps_thread_count;
	uint32_t n;

	/* CHIP_TURKS */
	cfg->num_ses = 1;
	cfg->max_pipes = 4;
	cfg->max_tile_pipes = 4;
	cfg->max_simds = 6;
	cfg->max_backends = 2 * cfg->num_ses;
	cfg->max_gprs = 256;
	cfg->max_threads = 248;
	cfg->max_gs_threads = 32;
	cfg->max_stack_entries = 256;
	cfg->sx_num_of_sets = 4;
	cfg->sx_max_export_size = 256;
	cfg->sx_max_export_pos_size = 64;
	cfg->sx_max_export_smx_size = 192;
	cfg->max_hw_contexts = 8;
	cfg->sq_num_cf_insts = 2;

	cfg->sc_prim_fifo_size = 0x100;
	cfg->sc_hiz_tile_fifo_size = 0x30;
	cfg->sc_earlyz_tile_fifo_size = 0x130;
	gb_addr_config = TURKS_GB_ADDR_CONFIG_GOLDEN;
	/* Initialize HDP */
	for (i = 0, j = 0; i < 32; i++, j += 0x18) {
		rdn_wreg(card, (0x2c14 + j), 0x00000000);
		rdn_wreg(card, (0x2c18 + j), 0x00000000);
		rdn_wreg(card, (0x2c1c + j), 0x00000000);
		rdn_wreg(card, (0x2c20 + j), 0x00000000);
		rdn_wreg(card, (0x2c24 + j), 0x00000000);
	}

	rdn_wreg(card, GRBM_CNTL, GRBM_READ_TIMEOUT(0xff));
	rdn_wreg(card, SRBM_INT_CNTL, 0x1);
	rdn_wreg(card, SRBM_INT_ACK, 0x1);

	/* Linux checks the PCIe maximum read request size here; not ported. */

	rdn_rreg(card, MC_SHARED_CHMAP);
	mc_arb_ramcfg = rdn_rreg(card, MC_ARB_RAMCFG);

	/* setup tiling info dword.  gb_addr_config is not adequate since it does
	 * not have bank info, so create a custom tiling dword.
	 * bits 3:0   num_pipes
	 * bits 7:4   num_banks
	 * bits 11:8  group_size
	 * bits 15:12 row_size
	 */
	cfg->tile_config = 0;
	switch (cfg->max_tile_pipes) {
	case 1:
	default:
		cfg->tile_config |= (0 << 0);
		break;
	case 2:
		cfg->tile_config |= (1 << 0);
		break;
	case 4:
		cfg->tile_config |= (2 << 0);
		break;
	case 8:
		cfg->tile_config |= (3 << 0);
		break;
	}
	/* num banks is 8 on all fusion asics. 0 = 4, 1 = 8, 2 = 16 */
	switch ((mc_arb_ramcfg & NOOFBANK_MASK) >> NOOFBANK_SHIFT) {
	case 0: /* four banks */
		cfg->tile_config |= 0 << 4;
		break;
	case 1: /* eight banks */
		cfg->tile_config |= 1 << 4;
		break;
	case 2: /* sixteen banks */
	default:
		cfg->tile_config |= 2 << 4;
		break;
	}
	cfg->tile_config |= 0 << 8;
	cfg->tile_config |=
		((gb_addr_config & 0x30000000) >> 28) << 12;

	tmp = 0;
	for (i = (cfg->num_ses - 1); i >= 0; i--) {
		uint32_t rb_disable_bitmap;

		rdn_wreg(card, GRBM_GFX_INDEX, INSTANCE_BROADCAST_WRITES | SE_INDEX(i));
		rdn_wreg(card, RLC_GFX_INDEX, INSTANCE_BROADCAST_WRITES | SE_INDEX(i));
		rb_disable_bitmap = (rdn_rreg(card, CC_RB_BACKEND_DISABLE) & 0x00ff0000) >> 16;
		tmp <<= 4;
		tmp |= rb_disable_bitmap;
	}
	/* enabled rb are just the one not disabled :) */
	disabled_rb_mask = tmp;
	tmp = 0;
	for (n = 0; n < cfg->max_backends; n++)
		tmp |= (1 << n);
	/* if all the backends are disabled, fix it up here */
	if ((disabled_rb_mask & tmp) == tmp) {
		for (n = 0; n < cfg->max_backends; n++)
			disabled_rb_mask &= ~(1 << n);
	}

	for (i = 0; i < (int)cfg->num_ses; i++) {
		uint32_t simd_disable_bitmap;

		rdn_wreg(card, GRBM_GFX_INDEX, INSTANCE_BROADCAST_WRITES | SE_INDEX(i));
		rdn_wreg(card, RLC_GFX_INDEX, INSTANCE_BROADCAST_WRITES | SE_INDEX(i));
		simd_disable_bitmap = (rdn_rreg(card, CC_GC_SHADER_PIPE_CONFIG) & 0xffff0000) >> 16;
		simd_disable_bitmap |= 0xffffffff << cfg->max_simds;
		tmp <<= 16;
		tmp |= simd_disable_bitmap;
	}
	cfg->active_simds = rdn_popcount32(~tmp);

	rdn_wreg(card, GRBM_GFX_INDEX, INSTANCE_BROADCAST_WRITES | SE_BROADCAST_WRITES);
	rdn_wreg(card, RLC_GFX_INDEX, INSTANCE_BROADCAST_WRITES | SE_BROADCAST_WRITES);

	rdn_wreg(card, GB_ADDR_CONFIG, gb_addr_config);
	rdn_wreg(card, DMIF_ADDR_CONFIG, gb_addr_config);
	rdn_wreg(card, HDP_ADDR_CONFIG, gb_addr_config);
	rdn_wreg(card, DMA_TILING_CONFIG, gb_addr_config);
	rdn_wreg(card, UVD_UDEC_ADDR_CONFIG, gb_addr_config);
	rdn_wreg(card, UVD_UDEC_DB_ADDR_CONFIG, gb_addr_config);
	rdn_wreg(card, UVD_UDEC_DBW_ADDR_CONFIG, gb_addr_config);
	tmp = gb_addr_config & NUM_PIPES_MASK;
	tmp = rdn_remap_render_backend(tmp, cfg->max_backends,
				       EVERGREEN_MAX_BACKENDS, disabled_rb_mask);
	cfg->backend_map = tmp;
	rdn_wreg(card, GB_BACKEND_MAP, tmp);

	rdn_wreg(card, CGTS_SYS_TCC_DISABLE, 0);
	rdn_wreg(card, CGTS_TCC_DISABLE, 0);
	rdn_wreg(card, CGTS_USER_SYS_TCC_DISABLE, 0);
	rdn_wreg(card, CGTS_USER_TCC_DISABLE, 0);

	/* set HW defaults for 3D engine */
	rdn_wreg(card, CP_QUEUE_THRESHOLDS, (ROQ_IB1_START(0x16) |
				     ROQ_IB2_START(0x2b)));

	rdn_wreg(card, CP_MEQ_THRESHOLDS, STQ_SPLIT(0x30));

	rdn_wreg(card, TA_CNTL_AUX, (DISABLE_CUBE_ANISO |
			     SYNC_GRADIENT |
			     SYNC_WALKER |
			     SYNC_ALIGNER));

	sx_debug_1 = rdn_rreg(card, SX_DEBUG_1);
	sx_debug_1 |= ENABLE_NEW_SMX_ADDRESS;
	rdn_wreg(card, SX_DEBUG_1, sx_debug_1);


	smx_dc_ctl0 = rdn_rreg(card, SMX_DC_CTL0);
	smx_dc_ctl0 &= ~NUMBER_OF_SETS(0x1ff);
	smx_dc_ctl0 |= NUMBER_OF_SETS(cfg->sx_num_of_sets);
	rdn_wreg(card, SMX_DC_CTL0, smx_dc_ctl0);


	rdn_wreg(card, SX_EXPORT_BUFFER_SIZES, (COLOR_BUFFER_SIZE((cfg->sx_max_export_size / 4) - 1) |
					POSITION_BUFFER_SIZE((cfg->sx_max_export_pos_size / 4) - 1) |
					SMX_BUFFER_SIZE((cfg->sx_max_export_smx_size / 4) - 1)));

	rdn_wreg(card, PA_SC_FIFO_SIZE, (SC_PRIM_FIFO_SIZE(cfg->sc_prim_fifo_size) |
				 SC_HIZ_TILE_FIFO_SIZE(cfg->sc_hiz_tile_fifo_size) |
				 SC_EARLYZ_TILE_FIFO_SIZE(cfg->sc_earlyz_tile_fifo_size)));

	rdn_wreg(card, VGT_NUM_INSTANCES, 1);
	rdn_wreg(card, SPI_CONFIG_CNTL, 0);
	rdn_wreg(card, SPI_CONFIG_CNTL_1, VTX_DONE_DELAY(4));
	rdn_wreg(card, CP_PERFMON_CNTL, 0);

	rdn_wreg(card, SQ_MS_FIFO_SIZES, (CACHE_FIFO_SIZE(16 * cfg->sq_num_cf_insts) |
				  FETCH_FIFO_HIWATER(0x4) |
				  DONE_FIFO_HIWATER(0xe0) |
				  ALU_UPDATE_FIFO_HIWATER(0x8)));

	sq_config = rdn_rreg(card, SQ_CONFIG);
	sq_config &= ~(PS_PRIO(3) |
		       VS_PRIO(3) |
		       GS_PRIO(3) |
		       ES_PRIO(3));
	sq_config |= (VC_ENABLE |
		      EXPORT_SRC_C |
		      PS_PRIO(0) |
		      VS_PRIO(1) |
		      GS_PRIO(2) |
		      ES_PRIO(3));


	sq_lds_resource_mgmt = rdn_rreg(card, SQ_LDS_RESOURCE_MGMT);

	sq_gpr_resource_mgmt_1 = NUM_PS_GPRS((cfg->max_gprs - (4 * 2)) * 12 / 32);
	sq_gpr_resource_mgmt_1 |= NUM_VS_GPRS((cfg->max_gprs - (4 * 2)) * 6 / 32);
	sq_gpr_resource_mgmt_1 |= NUM_CLAUSE_TEMP_GPRS(4);
	sq_gpr_resource_mgmt_2 = NUM_GS_GPRS((cfg->max_gprs - (4 * 2)) * 4 / 32);
	sq_gpr_resource_mgmt_2 |= NUM_ES_GPRS((cfg->max_gprs - (4 * 2)) * 4 / 32);
	sq_gpr_resource_mgmt_3 = NUM_HS_GPRS((cfg->max_gprs - (4 * 2)) * 3 / 32);
	sq_gpr_resource_mgmt_3 |= NUM_LS_GPRS((cfg->max_gprs - (4 * 2)) * 3 / 32);

	ps_thread_count = 128;

	sq_thread_resource_mgmt = NUM_PS_THREADS(ps_thread_count);
	sq_thread_resource_mgmt |= NUM_VS_THREADS((((cfg->max_threads - ps_thread_count) / 6) / 8) * 8);
	sq_thread_resource_mgmt |= NUM_GS_THREADS((((cfg->max_threads - ps_thread_count) / 6) / 8) * 8);
	sq_thread_resource_mgmt |= NUM_ES_THREADS((((cfg->max_threads - ps_thread_count) / 6) / 8) * 8);
	sq_thread_resource_mgmt_2 = NUM_HS_THREADS((((cfg->max_threads - ps_thread_count) / 6) / 8) * 8);
	sq_thread_resource_mgmt_2 |= NUM_LS_THREADS((((cfg->max_threads - ps_thread_count) / 6) / 8) * 8);

	sq_stack_resource_mgmt_1 = NUM_PS_STACK_ENTRIES((cfg->max_stack_entries * 1) / 6);
	sq_stack_resource_mgmt_1 |= NUM_VS_STACK_ENTRIES((cfg->max_stack_entries * 1) / 6);
	sq_stack_resource_mgmt_2 = NUM_GS_STACK_ENTRIES((cfg->max_stack_entries * 1) / 6);
	sq_stack_resource_mgmt_2 |= NUM_ES_STACK_ENTRIES((cfg->max_stack_entries * 1) / 6);
	sq_stack_resource_mgmt_3 = NUM_HS_STACK_ENTRIES((cfg->max_stack_entries * 1) / 6);
	sq_stack_resource_mgmt_3 |= NUM_LS_STACK_ENTRIES((cfg->max_stack_entries * 1) / 6);

	rdn_wreg(card, SQ_CONFIG, sq_config);
	rdn_wreg(card, SQ_GPR_RESOURCE_MGMT_1, sq_gpr_resource_mgmt_1);
	rdn_wreg(card, SQ_GPR_RESOURCE_MGMT_2, sq_gpr_resource_mgmt_2);
	rdn_wreg(card, SQ_GPR_RESOURCE_MGMT_3, sq_gpr_resource_mgmt_3);
	rdn_wreg(card, SQ_THREAD_RESOURCE_MGMT, sq_thread_resource_mgmt);
	rdn_wreg(card, SQ_THREAD_RESOURCE_MGMT_2, sq_thread_resource_mgmt_2);
	rdn_wreg(card, SQ_STACK_RESOURCE_MGMT_1, sq_stack_resource_mgmt_1);
	rdn_wreg(card, SQ_STACK_RESOURCE_MGMT_2, sq_stack_resource_mgmt_2);
	rdn_wreg(card, SQ_STACK_RESOURCE_MGMT_3, sq_stack_resource_mgmt_3);
	rdn_wreg(card, SQ_DYN_GPR_CNTL_PS_FLUSH_REQ, 0);
	rdn_wreg(card, SQ_LDS_RESOURCE_MGMT, sq_lds_resource_mgmt);

	rdn_wreg(card, PA_SC_FORCE_EOV_MAX_CNTS, (FORCE_EOV_MAX_CLK_CNT(4095) |
					  FORCE_EOV_MAX_REZ_CNT(255)));

	vgt_cache_invalidation = CACHE_INVALIDATION(VC_AND_TC);
	vgt_cache_invalidation |= AUTO_INVLD_EN(ES_AND_GS_AUTO);
	rdn_wreg(card, VGT_CACHE_INVALIDATION, vgt_cache_invalidation);

	rdn_wreg(card, VGT_GS_VERTEX_REUSE, 16);
	rdn_wreg(card, PA_SU_LINE_STIPPLE_VALUE, 0);
	rdn_wreg(card, PA_SC_LINE_STIPPLE_STATE, 0);

	rdn_wreg(card, VGT_VERTEX_REUSE_BLOCK_CNTL, 14);
	rdn_wreg(card, VGT_OUT_DEALLOC_CNTL, 16);

	rdn_wreg(card, CB_PERF_CTR0_SEL_0, 0);
	rdn_wreg(card, CB_PERF_CTR0_SEL_1, 0);
	rdn_wreg(card, CB_PERF_CTR1_SEL_0, 0);
	rdn_wreg(card, CB_PERF_CTR1_SEL_1, 0);
	rdn_wreg(card, CB_PERF_CTR2_SEL_0, 0);
	rdn_wreg(card, CB_PERF_CTR2_SEL_1, 0);
	rdn_wreg(card, CB_PERF_CTR3_SEL_0, 0);
	rdn_wreg(card, CB_PERF_CTR3_SEL_1, 0);

	/* clear render buffer base addresses */
	rdn_wreg(card, CB_COLOR0_BASE, 0);
	rdn_wreg(card, CB_COLOR1_BASE, 0);
	rdn_wreg(card, CB_COLOR2_BASE, 0);
	rdn_wreg(card, CB_COLOR3_BASE, 0);
	rdn_wreg(card, CB_COLOR4_BASE, 0);
	rdn_wreg(card, CB_COLOR5_BASE, 0);
	rdn_wreg(card, CB_COLOR6_BASE, 0);
	rdn_wreg(card, CB_COLOR7_BASE, 0);
	rdn_wreg(card, CB_COLOR8_BASE, 0);
	rdn_wreg(card, CB_COLOR9_BASE, 0);
	rdn_wreg(card, CB_COLOR10_BASE, 0);
	rdn_wreg(card, CB_COLOR11_BASE, 0);

	/* set the shader const cache sizes to 0 */
	for (i = SQ_ALU_CONST_BUFFER_SIZE_PS_0; i < 0x28200; i += 4)
		rdn_wreg(card, i, 0);
	for (i = SQ_ALU_CONST_BUFFER_SIZE_HS_0; i < 0x29000; i += 4)
		rdn_wreg(card, i, 0);

	tmp = rdn_rreg(card, HDP_MISC_CNTL);
	tmp |= HDP_FLUSH_INVALIDATE_CACHE;
	rdn_wreg(card, HDP_MISC_CNTL, tmp);

	hdp_host_path_cntl = rdn_rreg(card, HDP_HOST_PATH_CNTL);
	rdn_wreg(card, HDP_HOST_PATH_CNTL, hdp_host_path_cntl);

	rdn_wreg(card, PA_CL_ENHANCE, CLIP_VTX_REORDER_ENA | NUM_CLIP_SEQ(3));

	card->os->delay_us(card->os->cookie, 50);
}
