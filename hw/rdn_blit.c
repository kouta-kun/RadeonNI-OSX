/*
 * Drawing with the 3D engine from inside this library: the pieces of an
 * indirect buffer that draws textured rectangles and triangles, and what is
 * built from them: rdn_blit(), which copies rectangles from one surface to
 * another, rdn_blit_fill(), which fills rectangles with one colour, and
 * rdn_blit_move(), which copies rectangles inside one surface.
 *
 * One indirect buffer sets up the whole state, so it depends on nothing a
 * client did before, and clients must not depend on what it leaves (Mesa's
 * r600 sets its whole state at the start of every command buffer).
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
#include "rdn_draw.h"

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
#define RDN_DI_INDEX_SIZE_16_BIT	0x0
#define RDN_DI_SRC_SEL_AUTO_INDEX	0x2
#define RDN_SQ_SEL_Y			1
#define RDN_SQ_SEL_Z			2
#define RDN_SQ_SEL_W			3

/* The vertex and pixel shaders of the blit code, little-endian variants. */
static const uint32_t draw_vs[] = {
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

static const uint32_t draw_ps[] = {
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

static uint32_t swap32(uint32_t v)
{
	return (v << 24) | ((v & 0xff00) << 8) | ((v >> 8) & 0xff00) | (v >> 24);
}

void rdn_ib_emit(struct rdn_ib *ib, uint32_t value)
{
	if (ib->swapped)
		value = swap32(value);
	rdn_vram_write32(ib->accel, ib->base + ib->words * 4, value);
	ib->words++;
}

void rdn_draw_put_shaders(struct rdn_accel *accel, uint32_t offset)
{
	uint32_t i;

	for (i = 0; i < ARRAY_SIZE(draw_vs); i++)
		rdn_vram_write32(accel, offset + i * 4, draw_vs[i]);
	for (i = 0; i < ARRAY_SIZE(draw_ps); i++)
		rdn_vram_write32(accel, offset + RDN_DRAW_PS + i * 4, draw_ps[i]);
}

void rdn_draw_surface_sync(struct rdn_ib *ib, uint32_t sync_type,
			   uint32_t size, uint64_t mc_addr)
{
	uint32_t cp_coher_size;

	if (size == 0xffffffff)
		cp_coher_size = 0xffffffff;
	else
		cp_coher_size = ((size + 255) >> 8);

	rdn_ib_emit(ib, PACKET3(PACKET3_SURFACE_SYNC, 3));
	rdn_ib_emit(ib, sync_type);
	rdn_ib_emit(ib, cp_coher_size);
	rdn_ib_emit(ib, (uint32_t)(mc_addr >> 8));
	rdn_ib_emit(ib, 10); /* poll interval */
}

void rdn_draw_default_state(struct rdn_ib *ib)
{
	uint32_t sq_config, sq_gpr_resource_mgmt_1, sq_gpr_resource_mgmt_2, sq_gpr_resource_mgmt_3;
	uint32_t sq_thread_resource_mgmt, sq_thread_resource_mgmt_2;
	uint32_t sq_stack_resource_mgmt_1, sq_stack_resource_mgmt_2, sq_stack_resource_mgmt_3;
	uint32_t i;

	/* set clear context state */
	rdn_ib_emit(ib, PACKET3(PACKET3_CLEAR_STATE, 0));
	rdn_ib_emit(ib, 0);

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
	rdn_ib_emit(ib, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	rdn_ib_emit(ib, (SQ_DYN_GPR_CNTL_PS_FLUSH_REQ - PACKET3_SET_CONFIG_REG_START) >> 2);
	rdn_ib_emit(ib, 0);

	/* setup LDS */
	rdn_ib_emit(ib, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	rdn_ib_emit(ib, (SQ_LDS_RESOURCE_MGMT - PACKET3_SET_CONFIG_REG_START) >> 2);
	rdn_ib_emit(ib, 0x10001000);

	/* SQ config */
	rdn_ib_emit(ib, PACKET3(PACKET3_SET_CONFIG_REG, 11));
	rdn_ib_emit(ib, (SQ_CONFIG - PACKET3_SET_CONFIG_REG_START) >> 2);
	rdn_ib_emit(ib, sq_config);
	rdn_ib_emit(ib, sq_gpr_resource_mgmt_1);
	rdn_ib_emit(ib, sq_gpr_resource_mgmt_2);
	rdn_ib_emit(ib, sq_gpr_resource_mgmt_3);
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, sq_thread_resource_mgmt);
	rdn_ib_emit(ib, sq_thread_resource_mgmt_2);
	rdn_ib_emit(ib, sq_stack_resource_mgmt_1);
	rdn_ib_emit(ib, sq_stack_resource_mgmt_2);
	rdn_ib_emit(ib, sq_stack_resource_mgmt_3);

	/* CONTEXT_CONTROL */
	rdn_ib_emit(ib, 0xc0012800);
	rdn_ib_emit(ib, 0x80000000);
	rdn_ib_emit(ib, 0x80000000);

	/* SQ_VTX_BASE_VTX_LOC */
	rdn_ib_emit(ib, 0xc0026f00);
	rdn_ib_emit(ib, 0x00000000);
	rdn_ib_emit(ib, 0x00000000);
	rdn_ib_emit(ib, 0x00000000);

	/* SET_SAMPLER */
	rdn_ib_emit(ib, 0xc0036e00);
	rdn_ib_emit(ib, 0x00000000);
	rdn_ib_emit(ib, 0x00000012);
	rdn_ib_emit(ib, 0x00000000);
	rdn_ib_emit(ib, 0x00000000);

	/* set to DX10/11 mode */
	rdn_ib_emit(ib, PACKET3(PACKET3_MODE_CONTROL, 0));
	rdn_ib_emit(ib, 1);

	/* Linux chains to the default state; here it is inline. */
	for (i = 0; i < evergreen_default_size; i++)
		rdn_ib_emit(ib, evergreen_default_state[i]);
}

void rdn_draw_render_target(struct rdn_ib *ib, const struct rdn_draw_surface *t,
			    bool swap_bytes)
{
	uint32_t cb_color_info;
	uint32_t h = (t->height + 7) & ~7u;
	uint32_t pitch, slice;

	cb_color_info = CB_FORMAT(RDN_COLOR_8_8_8_8) |
		CB_SOURCE_FORMAT(CB_SF_EXPORT_NORM) |
		CB_ARRAY_MODE(ARRAY_LINEAR_ALIGNED);
	if (swap_bytes)
		cb_color_info |= RDN_CB_ENDIAN(RDN_ENDIAN_8IN32);

	pitch = (t->pitch_pixels / 8) - 1;
	slice = ((t->pitch_pixels * h) / 64) - 1;

	rdn_ib_emit(ib, PACKET3(PACKET3_SET_CONTEXT_REG, 15));
	rdn_ib_emit(ib, (CB_COLOR0_BASE - PACKET3_SET_CONTEXT_REG_START) >> 2);
	rdn_ib_emit(ib, (uint32_t)(t->gpu_addr >> 8));
	rdn_ib_emit(ib, pitch);
	rdn_ib_emit(ib, slice);
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, cb_color_info);
	/*
	 * CB_COLOR0_ATTRIB: without the non-display tiling order, only
	 * every other group of four pixel columns of a linear surface is
	 * written (seen on the card).
	 */
	rdn_ib_emit(ib, RDN_CB_NON_DISP_TILING_ORDER);
	rdn_ib_emit(ib, (t->width - 1) | ((t->height - 1) << 16));
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, 0);
}

void rdn_draw_shaders(struct rdn_ib *ib, uint64_t vs_addr)
{
	uint64_t ps_addr = vs_addr + RDN_DRAW_PS;


	/* VS */
	rdn_ib_emit(ib, PACKET3(PACKET3_SET_CONTEXT_REG, 3));
	rdn_ib_emit(ib, (SQ_PGM_START_VS - PACKET3_SET_CONTEXT_REG_START) >> 2);
	rdn_ib_emit(ib, (uint32_t)(vs_addr >> 8));
	rdn_ib_emit(ib, 2);
	rdn_ib_emit(ib, 0);

	/* PS */
	rdn_ib_emit(ib, PACKET3(PACKET3_SET_CONTEXT_REG, 4));
	rdn_ib_emit(ib, (SQ_PGM_START_PS - PACKET3_SET_CONTEXT_REG_START) >> 2);
	rdn_ib_emit(ib, (uint32_t)(ps_addr >> 8));
	rdn_ib_emit(ib, 1);
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, 2);

	rdn_draw_surface_sync(ib, PACKET3_SH_ACTION_ENA, 512, vs_addr);
}

void rdn_draw_vertices(struct rdn_ib *ib, uint64_t gpu_addr, uint32_t bytes)
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

	rdn_ib_emit(ib, PACKET3(PACKET3_SET_RESOURCE, 8));
	rdn_ib_emit(ib, 0x580);
	rdn_ib_emit(ib, (uint32_t)gpu_addr);
	rdn_ib_emit(ib, bytes - 1); /* size */
	rdn_ib_emit(ib, sq_vtx_constant_word2);
	rdn_ib_emit(ib, sq_vtx_constant_word3);
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, S__SQ_CONSTANT_TYPE(SQ_TEX_VTX_VALID_BUFFER));

	rdn_draw_surface_sync(ib, PACKET3_VC_ACTION_ENA, bytes, gpu_addr);
}

void rdn_draw_texture(struct rdn_ib *ib, const struct rdn_draw_surface *t)
{
	uint32_t w = t->width, h = t->height, pitch = t->pitch_pixels;
	uint32_t size = pitch * h * 4;
	uint64_t gpu_addr = t->gpu_addr;
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

	rdn_draw_surface_sync(ib, PACKET3_TC_ACTION_ENA, size, gpu_addr);

	rdn_ib_emit(ib, PACKET3(PACKET3_SET_RESOURCE, 8));
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, sq_tex_resource_word0);
	rdn_ib_emit(ib, sq_tex_resource_word1);
	rdn_ib_emit(ib, (uint32_t)(gpu_addr >> 8));
	rdn_ib_emit(ib, (uint32_t)(gpu_addr >> 8));
	rdn_ib_emit(ib, sq_tex_resource_word4);
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, 0);
	rdn_ib_emit(ib, sq_tex_resource_word7);
}

void rdn_draw_scissors(struct rdn_ib *ib, uint32_t x1, uint32_t y1,
			 uint32_t x2, uint32_t y2)
{
	rdn_ib_emit(ib, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	rdn_ib_emit(ib, (PA_SC_SCREEN_SCISSOR_TL - PACKET3_SET_CONTEXT_REG_START) >> 2);
	rdn_ib_emit(ib, (x1 << 0) | (y1 << 16));
	rdn_ib_emit(ib, (x2 << 0) | (y2 << 16));

	rdn_ib_emit(ib, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	rdn_ib_emit(ib, (PA_SC_GENERIC_SCISSOR_TL - PACKET3_SET_CONTEXT_REG_START) >> 2);
	rdn_ib_emit(ib, (x1 << 0) | (y1 << 16) | (1u << 31));
	rdn_ib_emit(ib, (x2 << 0) | (y2 << 16));

	rdn_ib_emit(ib, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	rdn_ib_emit(ib, (PA_SC_WINDOW_SCISSOR_TL - PACKET3_SET_CONTEXT_REG_START) >> 2);
	rdn_ib_emit(ib, (x1 << 0) | (y1 << 16) | (1u << 31));
	rdn_ib_emit(ib, (x2 << 0) | (y2 << 16));
}

void rdn_draw_auto(struct rdn_ib *ib, uint32_t primitive, uint32_t vertices)
{
	rdn_ib_emit(ib, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	rdn_ib_emit(ib, (VGT_PRIMITIVE_TYPE - PACKET3_SET_CONFIG_REG_START) >> 2);
	rdn_ib_emit(ib, primitive);

	rdn_ib_emit(ib, PACKET3(PACKET3_INDEX_TYPE, 0));
	rdn_ib_emit(ib, RDN_DI_INDEX_SIZE_16_BIT);

	rdn_ib_emit(ib, PACKET3(PACKET3_NUM_INSTANCES, 0));
	rdn_ib_emit(ib, 1);

	rdn_ib_emit(ib, PACKET3(PACKET3_DRAW_INDEX_AUTO, 1));
	rdn_ib_emit(ib, vertices);
	rdn_ib_emit(ib, RDN_DI_SRC_SEL_AUTO_INDEX);
}

/*
 * The bits of a whole number below 2^24 as a single-precision float. The
 * kernel must not use floating point registers, so no conversion by the
 * compiler here.
 */
static uint32_t float_bits_of(uint32_t v)
{
	uint32_t top = 23;

	if (!v)
		return 0;
	while (!(v >> top))
		top--;
	return ((127 + top) << 23) | ((v << (23 - top)) & 0x7fffff);
}

/*
 * Byte offsets of the pieces inside the work area. The vertices of
 * RDN_BLIT_MAX_RECTS rectangles end at 0xb000.
 */
#define WORK_IB			0x0000
#define WORK_SHADERS		0x4000
#define WORK_VB			0x8000
#define WORK_COLOUR		0xc000
#define RDN_BLIT_WORK_BYTES	0x10000

/*
 * The texture a fill draws with: every texel the colour. Its row length is
 * one this library has drawn with before (the self-test's texture, and
 * every surface the kext has copied, have rows that are multiples of 64).
 */
#define COLOUR_PITCH		64
#define COLOUR_ROWS		8

uint32_t rdn_blit_work_bytes(void)
{
	return RDN_BLIT_WORK_BYTES;
}

uint32_t rdn_blit_move_work_bytes(void)
{
	return 2 * RDN_BLIT_WORK_BYTES;
}

static bool work_ok(struct rdn_accel *accel, uint32_t work_offset, uint32_t bytes)
{
	return !(work_offset & 0xfff) && work_offset <= accel->aperture_size &&
	       bytes <= accel->aperture_size - work_offset;
}

static bool surface_ok(const struct rdn_draw_surface *s)
{
	return s->width && s->height && s->width <= s->pitch_pixels &&
	       !(s->pitch_pixels & 7) && s->pitch_pixels <= 16384 &&
	       s->height <= 16384;
}

/* Is the rectangle, which must not be empty, inside the surface? */
static bool rect_inside(const struct rdn_draw_surface *s, uint32_t x, uint32_t y,
			uint32_t width, uint32_t height)
{
	return width && height &&
	       x <= s->width && width <= s->width - x &&
	       y <= s->height && height <= s->height - y;
}

/*
 * Rectangle number `index` of the vertex buffer at `vb`: `width` by `height`
 * pixels at (dx, dy), showing `swidth` by `sheight` texels from (sx, sy).
 * A rectangle is three corners of (x, y, s, t), in pixels and texels: top
 * left, bottom left, bottom right.
 */
static void put_rect(struct rdn_accel *accel, uint32_t vb, uint32_t index,
		     uint32_t dx, uint32_t dy, uint32_t width, uint32_t height,
		     uint32_t sx, uint32_t sy, uint32_t swidth, uint32_t sheight)
{
	const uint32_t v[12] = {
		dx, dy, sx, sy,
		dx, dy + height, sx, sy + sheight,
		dx + width, dy + height, sx + swidth, sy + sheight,
	};
	uint32_t k;

	for (k = 0; k < 12; k++)
		rdn_vram_write32(accel, vb + index * 48 + k * 4, float_bits_of(v[k]));
}

/*
 * Build and submit the indirect buffer that draws the `count` rectangles
 * put into the work area's vertex buffer: on `dst`, with `src` as the
 * texture.
 */
static int draw_rects(struct rdn_accel *accel, const struct rdn_draw_surface *dst,
		      const struct rdn_draw_surface *src, uint32_t count,
		      uint32_t work_offset, uint32_t *seq)
{
	struct rdn_ib ib;

	rdn_draw_put_shaders(accel, work_offset + WORK_SHADERS);

	ib.accel = accel;
	ib.base = work_offset + WORK_IB;
	ib.words = 0;
	ib.swapped = accel->swapped;

	rdn_draw_default_state(&ib);
	rdn_draw_shaders(&ib, rdn_vram_addr(accel, work_offset + WORK_SHADERS));
	/* Neither side swaps bytes: the pixels arrive as they are stored. */
	rdn_draw_render_target(&ib, dst, false);
	rdn_draw_texture(&ib, src);
	rdn_draw_scissors(&ib, 0, 0, dst->width, dst->height);
	rdn_draw_vertices(&ib, rdn_vram_addr(accel, work_offset + WORK_VB),
			  count * 48);
	rdn_draw_auto(&ib, RDN_DI_PT_RECTLIST, count * 3);

	/* Make the render target's caches reach memory. */
	rdn_draw_surface_sync(&ib, PACKET3_CB_ACTION_ENA | PACKET3_CB0_DEST_BASE_ENA,
			      dst->pitch_pixels * dst->height * 4, dst->gpu_addr);

	while (ib.words & 0xf)
		rdn_ib_emit(&ib, 0x80000000);
	if (ib.words * 4 > WORK_SHADERS)
		return -ENOMEM;

	return rdn_ib_submit(accel, rdn_vram_addr(accel, ib.base), ib.words, seq);
}

int rdn_blit(struct rdn_accel *accel, const struct rdn_draw_surface *dst,
	     const struct rdn_draw_surface *src,
	     const struct rdn_blit_rect *rects, uint32_t count,
	     uint32_t work_offset, uint32_t *seq)
{
	uint32_t i;

	if (!work_ok(accel, work_offset, RDN_BLIT_WORK_BYTES) ||
	    !count || count > RDN_BLIT_MAX_RECTS ||
	    !surface_ok(dst) || !surface_ok(src))
		return -EINVAL;
	for (i = 0; i < count; i++) {
		const struct rdn_blit_rect *r = &rects[i];

		if (!rect_inside(src, r->src_x, r->src_y, r->width, r->height) ||
		    !rect_inside(dst, r->dst_x, r->dst_y, r->width, r->height))
			return -EINVAL;
	}
	for (i = 0; i < count; i++) {
		const struct rdn_blit_rect *r = &rects[i];

		put_rect(accel, work_offset + WORK_VB, i,
			 r->dst_x, r->dst_y, r->width, r->height,
			 r->src_x, r->src_y, r->width, r->height);
	}
	return draw_rects(accel, dst, src, count, work_offset, seq);
}

/*
 * A fill is the same textured draw as a copy, from a texture that is the
 * colour all over, with every corner's texture coordinates at its middle.
 * The other way would be a pixel shader that writes a constant, and the
 * state to hand it one: code the card has never run. This way nothing
 * reaches the GPU that the copy does not send for every picture the kext
 * shows, except different numbers in the vertex buffer, and the price is
 * five hundred words written through the aperture.
 */
int rdn_blit_fill(struct rdn_accel *accel, const struct rdn_draw_surface *dst,
		  const struct rdn_fill_rect *rects, uint32_t count,
		  uint32_t colour, bool big_endian_pixels,
		  uint32_t work_offset, uint32_t *seq)
{
	struct rdn_draw_surface texture;
	uint32_t i;

	if (!work_ok(accel, work_offset, RDN_BLIT_WORK_BYTES) ||
	    !count || count > RDN_BLIT_MAX_RECTS || !surface_ok(dst))
		return -EINVAL;
	for (i = 0; i < count; i++)
		if (!rect_inside(dst, rects[i].x, rects[i].y, rects[i].width,
				 rects[i].height))
			return -EINVAL;

	/* The draw copies bytes: the texels are what a pixel is to be. */
	if (big_endian_pixels)
		colour = swap32(colour);
	for (i = 0; i < COLOUR_PITCH * COLOUR_ROWS; i++)
		rdn_vram_write32(accel, work_offset + WORK_COLOUR + i * 4, colour);
	texture.gpu_addr = rdn_vram_addr(accel, work_offset + WORK_COLOUR);
	texture.width = texture.pitch_pixels = COLOUR_PITCH;
	texture.height = COLOUR_ROWS;

	for (i = 0; i < count; i++)
		put_rect(accel, work_offset + WORK_VB, i,
			 rects[i].x, rects[i].y, rects[i].width, rects[i].height,
			 COLOUR_PITCH / 2, COLOUR_ROWS / 2, 0, 0);
	return draw_rects(accel, dst, &texture, count, work_offset, seq);
}

/*
 * The GPU must not take its texture from the surface it is drawing on, and
 * cutting a move into pieces that do not overlap would not change that:
 * every piece would still read and write the one surface. So there are two
 * draws, each a plain copy between two surfaces: every source to `scratch`,
 * then from there to its destination. In `scratch` a source lies where it
 * does in the box around all the sources, so sources that share pixels
 * share them there too, and the second draw finds every pixel as it was
 * before the first destination was written.
 *
 * Each draw has a work area of its own, because the GPU has not read the
 * first when the second is written.
 */
int rdn_blit_move(struct rdn_accel *accel, const struct rdn_draw_surface *surface,
		  const struct rdn_draw_surface *scratch,
		  const struct rdn_blit_rect *rects, uint32_t count,
		  uint32_t work_offset, uint32_t *seq)
{
	uint32_t i, x0 = 0xffffffff, y0 = 0xffffffff, x1 = 0, y1 = 0;
	int ret;

	if (!work_ok(accel, work_offset, 2 * RDN_BLIT_WORK_BYTES) ||
	    !count || count > RDN_BLIT_MAX_RECTS ||
	    !surface_ok(surface) || !surface_ok(scratch))
		return -EINVAL;
	for (i = 0; i < count; i++) {
		const struct rdn_blit_rect *r = &rects[i];

		if (!rect_inside(surface, r->src_x, r->src_y, r->width, r->height) ||
		    !rect_inside(surface, r->dst_x, r->dst_y, r->width, r->height))
			return -EINVAL;
		if (r->src_x < x0)
			x0 = r->src_x;
		if (r->src_y < y0)
			y0 = r->src_y;
		if (r->src_x + r->width > x1)
			x1 = r->src_x + r->width;
		if (r->src_y + r->height > y1)
			y1 = r->src_y + r->height;
	}
	if (x1 - x0 > scratch->width || y1 - y0 > scratch->height)
		return -EINVAL;

	for (i = 0; i < count; i++) {
		const struct rdn_blit_rect *r = &rects[i];

		put_rect(accel, work_offset + WORK_VB, i,
			 r->src_x - x0, r->src_y - y0, r->width, r->height,
			 r->src_x, r->src_y, r->width, r->height);
	}
	ret = draw_rects(accel, scratch, surface, count, work_offset, seq);
	if (ret)
		return ret;

	work_offset += RDN_BLIT_WORK_BYTES;
	for (i = 0; i < count; i++) {
		const struct rdn_blit_rect *r = &rects[i];

		put_rect(accel, work_offset + WORK_VB, i,
			 r->dst_x, r->dst_y, r->width, r->height,
			 r->src_x - x0, r->src_y - y0, r->width, r->height);
	}
	return draw_rects(accel, surface, scratch, count, work_offset, seq);
}
