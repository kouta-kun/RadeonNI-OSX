/*
 * Drawing self-test: a textured rectangle and a textured triangle.
 *
 * The first thing drawn by the 3D engine under this library, and a check
 * that can be repeated wherever the library runs. One indirect buffer sets
 * up the whole state, so it depends on nothing a client did before.
 *
 * The state setup, the two shaders and the way a draw is issued come from
 * the Linux radeon driver's blit code as of Linux 3.9 (evergreen_blit_kms.c
 * and evergreen_blit_shaders.c). Differences: surfaces are linear rather
 * than tiled, so the scanout surface can be the render target, and
 * everything the GPU reads is stored little-endian on any host.
 *
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
 * Authors:
 *     Alex Deucher <alexander.deucher@amd.com>
 */

#include "rdn_accel.h"
#include "rdn_accel_reg.h"

#define u32 uint32_t
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#include "linux/evergreen_blit_shaders.h"
#undef u32

#ifndef CB_COLOR0_BASE
#define CB_COLOR0_BASE			0x28c60
#endif
#define RDN_CB_ENDIAN(x)		((x) << 0)
#define RDN_CB_NON_DISP_TILING_ORDER	(1 << 4)
#define RDN_ENDIAN_8IN32		2
#define RDN_COLOR_8_8_8_8		0x1a
#define RDN_DI_PT_TRILIST		0x4
#define RDN_DI_PT_RECTLIST		0x11
#define RDN_DI_INDEX_SIZE_16_BIT	0x0
#define RDN_DI_SRC_SEL_AUTO_INDEX	0x2
#define RDN_SQ_SEL_Y			1
#define RDN_SQ_SEL_Z			2
#define RDN_SQ_SEL_W			3

/* The vertex and pixel shaders of the blit code, little-endian variants. */
static const uint32_t selftest_vs[] = {
	0x00000004,
	0x80800400,
	0x0000a03c,
	0x95000688,
	0x00004000,
	0x15200688,
	0x00000000,
	0x00000000,
	0x3c000000,
	0x67961001,
	0x00080000,
	0x00000000,
	0x1c000000,
	0x67961000,
	0x00000008,
	0x00000000,
};

static const uint32_t selftest_ps[] = {
	0x00000003,
	0xa00c0000,
	0x00000008,
	0x80400000,
	0x00000000,
	0x95200688,
	0x00380400,
	0x00146b10,
	0x00380000,
	0x20146b10,
	0x00380400,
	0x40146b00,
	0x80380000,
	0x60146b00,
	0x00000000,
	0x00000000,
	0x00000010,
	0x000d1000,
	0xb0800000,
	0x00000000,
};

/* Byte offsets of the pieces inside the work area. */
#define WORK_IB			0x0000
#define WORK_VS			0x4000
#define WORK_PS			0x4100
#define WORK_VB			0x4200
#define WORK_TEXTURE		0x8000
#define RDN_SELFTEST_WORK_BYTES	0x10000

#define TEX_SIZE		64

struct ib {
	struct rdn_accel *accel;
	uint32_t base, words;
};

static void ib_emit(struct ib *ib, uint32_t value)
{
	rdn_vram_write32(ib->accel, ib->base + ib->words * 4, value);
	ib->words++;
}

static uint32_t float_bits(float f)
{
	union { float f; uint32_t u; } v;

	v.f = f;
	return v.u;
}

static void cp_set_surface_sync(struct ib *ib, uint32_t sync_type,
				uint32_t size, uint64_t mc_addr)
{
	uint32_t cp_coher_size;

	if (size == 0xffffffff)
		cp_coher_size = 0xffffffff;
	else
		cp_coher_size = ((size + 255) >> 8);

	ib_emit(ib, PACKET3(PACKET3_SURFACE_SYNC, 3));
	ib_emit(ib, sync_type);
	ib_emit(ib, cp_coher_size);
	ib_emit(ib, (uint32_t)(mc_addr >> 8));
	ib_emit(ib, 10); /* poll interval */
}

static void set_default_state(struct ib *ib)
{
	uint32_t sq_config, sq_gpr_resource_mgmt_1, sq_gpr_resource_mgmt_2, sq_gpr_resource_mgmt_3;
	uint32_t sq_thread_resource_mgmt, sq_thread_resource_mgmt_2;
	uint32_t sq_stack_resource_mgmt_1, sq_stack_resource_mgmt_2, sq_stack_resource_mgmt_3;
	uint32_t i;

	/* set clear context state */
	ib_emit(ib, PACKET3(PACKET3_CLEAR_STATE, 0));
	ib_emit(ib, 0);

	/* CHIP_TURKS */
	sq_config = VC_ENABLE;
	sq_config |= (EXPORT_SRC_C |
		      CS_PRIO(0) |
		      LS_PRIO(0) |
		      HS_PRIO(0) |
		      PS_PRIO(0) |
		      VS_PRIO(1) |
		      GS_PRIO(2) |
		      ES_PRIO(3));

	sq_gpr_resource_mgmt_1 = (NUM_PS_GPRS(93) |
				  NUM_VS_GPRS(46) |
				  NUM_CLAUSE_TEMP_GPRS(4));
	sq_gpr_resource_mgmt_2 = (NUM_GS_GPRS(31) |
				  NUM_ES_GPRS(31));
	sq_gpr_resource_mgmt_3 = (NUM_HS_GPRS(23) |
				  NUM_LS_GPRS(23));
	sq_thread_resource_mgmt = (NUM_PS_THREADS(128) |
				   NUM_VS_THREADS(20) |
				   NUM_GS_THREADS(20) |
				   NUM_ES_THREADS(20));
	sq_thread_resource_mgmt_2 = (NUM_HS_THREADS(20) |
				     NUM_LS_THREADS(20));
	sq_stack_resource_mgmt_1 = (NUM_PS_STACK_ENTRIES(42) |
				    NUM_VS_STACK_ENTRIES(42));
	sq_stack_resource_mgmt_2 = (NUM_GS_STACK_ENTRIES(42) |
				    NUM_ES_STACK_ENTRIES(42));
	sq_stack_resource_mgmt_3 = (NUM_HS_STACK_ENTRIES(42) |
				    NUM_LS_STACK_ENTRIES(42));

	/* disable dyn gprs */
	ib_emit(ib, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	ib_emit(ib, (SQ_DYN_GPR_CNTL_PS_FLUSH_REQ - PACKET3_SET_CONFIG_REG_START) >> 2);
	ib_emit(ib, 0);

	/* setup LDS */
	ib_emit(ib, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	ib_emit(ib, (SQ_LDS_RESOURCE_MGMT - PACKET3_SET_CONFIG_REG_START) >> 2);
	ib_emit(ib, 0x10001000);

	/* SQ config */
	ib_emit(ib, PACKET3(PACKET3_SET_CONFIG_REG, 11));
	ib_emit(ib, (SQ_CONFIG - PACKET3_SET_CONFIG_REG_START) >> 2);
	ib_emit(ib, sq_config);
	ib_emit(ib, sq_gpr_resource_mgmt_1);
	ib_emit(ib, sq_gpr_resource_mgmt_2);
	ib_emit(ib, sq_gpr_resource_mgmt_3);
	ib_emit(ib, 0);
	ib_emit(ib, 0);
	ib_emit(ib, sq_thread_resource_mgmt);
	ib_emit(ib, sq_thread_resource_mgmt_2);
	ib_emit(ib, sq_stack_resource_mgmt_1);
	ib_emit(ib, sq_stack_resource_mgmt_2);
	ib_emit(ib, sq_stack_resource_mgmt_3);

	/* CONTEXT_CONTROL */
	ib_emit(ib, 0xc0012800);
	ib_emit(ib, 0x80000000);
	ib_emit(ib, 0x80000000);

	/* SQ_VTX_BASE_VTX_LOC */
	ib_emit(ib, 0xc0026f00);
	ib_emit(ib, 0x00000000);
	ib_emit(ib, 0x00000000);
	ib_emit(ib, 0x00000000);

	/* SET_SAMPLER */
	ib_emit(ib, 0xc0036e00);
	ib_emit(ib, 0x00000000);
	ib_emit(ib, 0x00000012);
	ib_emit(ib, 0x00000000);
	ib_emit(ib, 0x00000000);

	/* set to DX10/11 mode */
	ib_emit(ib, PACKET3(PACKET3_MODE_CONTROL, 0));
	ib_emit(ib, 1);

	/* Linux chains to the default state; here it is inline. */
	for (i = 0; i < evergreen_default_size; i++)
		ib_emit(ib, evergreen_default_state[i]);
}

static void set_render_target(struct ib *ib, const struct rdn_selftest_target *t)
{
	uint32_t cb_color_info;
	uint32_t h = (t->height + 7) & ~7u;
	uint32_t pitch, slice;

	cb_color_info = CB_FORMAT(RDN_COLOR_8_8_8_8) |
		CB_SOURCE_FORMAT(CB_SF_EXPORT_NORM) |
		CB_ARRAY_MODE(ARRAY_LINEAR_ALIGNED);
	if (t->big_endian_pixels)
		cb_color_info |= RDN_CB_ENDIAN(RDN_ENDIAN_8IN32);

	pitch = (t->pitch_pixels / 8) - 1;
	slice = ((t->pitch_pixels * h) / 64) - 1;

	ib_emit(ib, PACKET3(PACKET3_SET_CONTEXT_REG, 15));
	ib_emit(ib, (CB_COLOR0_BASE - PACKET3_SET_CONTEXT_REG_START) >> 2);
	ib_emit(ib, (uint32_t)(t->gpu_addr >> 8));
	ib_emit(ib, pitch);
	ib_emit(ib, slice);
	ib_emit(ib, 0);
	ib_emit(ib, cb_color_info);
	/*
	 * CB_COLOR0_ATTRIB: without the non-display tiling order, only
	 * every other group of four pixel columns of a linear surface is
	 * written (seen on the card).
	 */
	ib_emit(ib, RDN_CB_NON_DISP_TILING_ORDER);
	ib_emit(ib, (t->width - 1) | ((t->height - 1) << 16));
	ib_emit(ib, 0);
	ib_emit(ib, 0);
	ib_emit(ib, 0);
	ib_emit(ib, 0);
	ib_emit(ib, 0);
	ib_emit(ib, 0);
	ib_emit(ib, 0);
	ib_emit(ib, 0);
}

static void set_shaders(struct ib *ib, uint64_t vs_addr, uint64_t ps_addr)
{
	/* VS */
	ib_emit(ib, PACKET3(PACKET3_SET_CONTEXT_REG, 3));
	ib_emit(ib, (SQ_PGM_START_VS - PACKET3_SET_CONTEXT_REG_START) >> 2);
	ib_emit(ib, (uint32_t)(vs_addr >> 8));
	ib_emit(ib, 2);
	ib_emit(ib, 0);

	/* PS */
	ib_emit(ib, PACKET3(PACKET3_SET_CONTEXT_REG, 4));
	ib_emit(ib, (SQ_PGM_START_PS - PACKET3_SET_CONTEXT_REG_START) >> 2);
	ib_emit(ib, (uint32_t)(ps_addr >> 8));
	ib_emit(ib, 1);
	ib_emit(ib, 0);
	ib_emit(ib, 2);

	cp_set_surface_sync(ib, PACKET3_SH_ACTION_ENA, 512, vs_addr);
}

static void set_vtx_resource(struct ib *ib, uint64_t gpu_addr)
{
	uint32_t sq_vtx_constant_word2, sq_vtx_constant_word3;

	/* high addr, stride */
	sq_vtx_constant_word2 = SQ_VTXC_BASE_ADDR_HI((uint32_t)(gpu_addr >> 32) & 0xff) |
		SQ_VTXC_STRIDE(16);

	/* xyzw swizzles */
	sq_vtx_constant_word3 = SQ_VTCX_SEL_X(SQ_SEL_X) |
		SQ_VTCX_SEL_Y(RDN_SQ_SEL_Y) |
		SQ_VTCX_SEL_Z(RDN_SQ_SEL_Z) |
		SQ_VTCX_SEL_W(RDN_SQ_SEL_W);

	ib_emit(ib, PACKET3(PACKET3_SET_RESOURCE, 8));
	ib_emit(ib, 0x580);
	ib_emit(ib, (uint32_t)gpu_addr);
	ib_emit(ib, 48 - 1); /* size */
	ib_emit(ib, sq_vtx_constant_word2);
	ib_emit(ib, sq_vtx_constant_word3);
	ib_emit(ib, 0);
	ib_emit(ib, 0);
	ib_emit(ib, 0);
	ib_emit(ib, S__SQ_CONSTANT_TYPE(SQ_TEX_VTX_VALID_BUFFER));

	cp_set_surface_sync(ib, PACKET3_VC_ACTION_ENA, 48, gpu_addr);
}

static void set_tex_resource(struct ib *ib, uint32_t w, uint32_t h,
			     uint32_t pitch, uint64_t gpu_addr, uint32_t size)
{
	uint32_t sq_tex_resource_word0, sq_tex_resource_word1;
	uint32_t sq_tex_resource_word4, sq_tex_resource_word7;

	sq_tex_resource_word0 = TEX_DIM(SQ_TEX_DIM_2D);
	sq_tex_resource_word0 |= ((((pitch >> 3) - 1) << 6) |
				  ((w - 1) << 18));
	sq_tex_resource_word1 = ((h - 1) << 0) |
				TEX_ARRAY_MODE(ARRAY_LINEAR_ALIGNED);
	/* xyzw swizzles */
	sq_tex_resource_word4 = TEX_DST_SEL_X(SQ_SEL_X) |
				TEX_DST_SEL_Y(RDN_SQ_SEL_Y) |
				TEX_DST_SEL_Z(RDN_SQ_SEL_Z) |
				TEX_DST_SEL_W(RDN_SQ_SEL_W);

	sq_tex_resource_word7 = RDN_COLOR_8_8_8_8 |
		S__SQ_CONSTANT_TYPE(SQ_TEX_VTX_VALID_TEXTURE);

	cp_set_surface_sync(ib, PACKET3_TC_ACTION_ENA, size, gpu_addr);

	ib_emit(ib, PACKET3(PACKET3_SET_RESOURCE, 8));
	ib_emit(ib, 0);
	ib_emit(ib, sq_tex_resource_word0);
	ib_emit(ib, sq_tex_resource_word1);
	ib_emit(ib, (uint32_t)(gpu_addr >> 8));
	ib_emit(ib, (uint32_t)(gpu_addr >> 8));
	ib_emit(ib, sq_tex_resource_word4);
	ib_emit(ib, 0);
	ib_emit(ib, 0);
	ib_emit(ib, sq_tex_resource_word7);
}

static void set_scissors(struct ib *ib, uint32_t x1, uint32_t y1,
			 uint32_t x2, uint32_t y2)
{
	ib_emit(ib, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	ib_emit(ib, (PA_SC_SCREEN_SCISSOR_TL - PACKET3_SET_CONTEXT_REG_START) >> 2);
	ib_emit(ib, (x1 << 0) | (y1 << 16));
	ib_emit(ib, (x2 << 0) | (y2 << 16));

	ib_emit(ib, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	ib_emit(ib, (PA_SC_GENERIC_SCISSOR_TL - PACKET3_SET_CONTEXT_REG_START) >> 2);
	ib_emit(ib, (x1 << 0) | (y1 << 16) | (1u << 31));
	ib_emit(ib, (x2 << 0) | (y2 << 16));

	ib_emit(ib, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	ib_emit(ib, (PA_SC_WINDOW_SCISSOR_TL - PACKET3_SET_CONTEXT_REG_START) >> 2);
	ib_emit(ib, (x1 << 0) | (y1 << 16) | (1u << 31));
	ib_emit(ib, (x2 << 0) | (y2 << 16));
}

static void draw_auto(struct ib *ib, uint32_t primitive)
{
	ib_emit(ib, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	ib_emit(ib, (VGT_PRIMITIVE_TYPE - PACKET3_SET_CONFIG_REG_START) >> 2);
	ib_emit(ib, primitive);

	ib_emit(ib, PACKET3(PACKET3_INDEX_TYPE, 0));
	ib_emit(ib, RDN_DI_INDEX_SIZE_16_BIT);

	ib_emit(ib, PACKET3(PACKET3_NUM_INSTANCES, 0));
	ib_emit(ib, 1);

	ib_emit(ib, PACKET3(PACKET3_DRAW_INDEX_AUTO, 1));
	ib_emit(ib, 3);
	ib_emit(ib, RDN_DI_SRC_SEL_AUTO_INDEX);
}

/* Three vertices of (x, y, s, t), in pixels and texels. */
static void put_vertices(struct rdn_accel *accel, uint32_t offset,
			 const float v[12])
{
	int i;

	for (i = 0; i < 12; i++)
		rdn_vram_write32(accel, offset + i * 4, float_bits(v[i]));
}

/*
 * The texture: four quadrants, red and green on top, blue and white below,
 * each pixel an XRGB word.
 */
static void put_texture(struct rdn_accel *accel, uint32_t offset)
{
	static const uint32_t quadrant[4] = {
		0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffffff
	};
	uint32_t x, y;

	for (y = 0; y < TEX_SIZE; y++)
		for (x = 0; x < TEX_SIZE; x++)
			rdn_vram_write32(accel, offset + (y * TEX_SIZE + x) * 4,
				quadrant[(y / (TEX_SIZE / 2)) * 2 + x / (TEX_SIZE / 2)]);
}

uint32_t rdn_selftest_work_bytes(void)
{
	return RDN_SELFTEST_WORK_BYTES;
}

int rdn_accel_selftest(struct rdn_accel *accel,
		       const struct rdn_selftest_target *target,
		       uint32_t work_offset)
{
	/* Rectangle lists take three corners: top left, bottom left, bottom right. */
	const float rect[12] = {
		64.0f, 64.0f, 0.0f, 0.0f,
		64.0f, 320.0f, 0.0f, (float)TEX_SIZE,
		320.0f, 320.0f, (float)TEX_SIZE, (float)TEX_SIZE,
	};
	const float tri[12] = {
		768.0f, 64.0f, (float)TEX_SIZE / 2, 0.0f,
		640.0f, 320.0f, 0.0f, (float)TEX_SIZE,
		896.0f, 320.0f, (float)TEX_SIZE, (float)TEX_SIZE,
	};
	struct ib ib;
	uint32_t seq, i;
	int r;

	if (work_offset + RDN_SELFTEST_WORK_BYTES > accel->aperture_size ||
	    (work_offset & 0xfff) || target->width < 960 || target->height < 384)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(selftest_vs); i++)
		rdn_vram_write32(accel, work_offset + WORK_VS + i * 4, selftest_vs[i]);
	for (i = 0; i < ARRAY_SIZE(selftest_ps); i++)
		rdn_vram_write32(accel, work_offset + WORK_PS + i * 4, selftest_ps[i]);
	put_texture(accel, work_offset + WORK_TEXTURE);
	put_vertices(accel, work_offset + WORK_VB, rect);
	put_vertices(accel, work_offset + WORK_VB + 48, tri);

	ib.accel = accel;
	ib.base = work_offset + WORK_IB;
	ib.words = 0;

	set_default_state(&ib);
	set_shaders(&ib, rdn_vram_addr(accel, work_offset + WORK_VS),
		    rdn_vram_addr(accel, work_offset + WORK_PS));
	set_render_target(&ib, target);
	set_tex_resource(&ib, TEX_SIZE, TEX_SIZE, TEX_SIZE,
			 rdn_vram_addr(accel, work_offset + WORK_TEXTURE),
			 TEX_SIZE * TEX_SIZE * 4);
	set_scissors(&ib, 0, 0, target->width, target->height);

	set_vtx_resource(&ib, rdn_vram_addr(accel, work_offset + WORK_VB));
	draw_auto(&ib, RDN_DI_PT_RECTLIST);

	set_scissors(&ib, 0, 0, target->width, target->height);
	set_vtx_resource(&ib, rdn_vram_addr(accel, work_offset + WORK_VB + 48));
	draw_auto(&ib, RDN_DI_PT_TRILIST);

	/* Make the render target's caches reach memory. */
	cp_set_surface_sync(&ib, PACKET3_CB_ACTION_ENA | PACKET3_CB0_DEST_BASE_ENA,
			    target->pitch_pixels * target->height * 4,
			    target->gpu_addr);

	while (ib.words & 0xf)
		ib_emit(&ib, 0x80000000);
	if (ib.words * 4 > WORK_VS)
		return -ENOMEM;

	r = rdn_ib_submit(accel, rdn_vram_addr(accel, ib.base), ib.words, false,
			  &seq);
	if (r)
		return r;
	return rdn_fence_wait(accel, seq, 2000);
}
