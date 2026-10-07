/*
 * Drawing self-test: a textured rectangle and a textured triangle.
 *
 * The first thing drawn by the 3D engine under this library, and a check
 * that can be repeated wherever the library runs. One indirect buffer sets
 * up the whole state, so it depends on nothing a client did before. The
 * pieces of that buffer are in rdn_blit.c, which has their origin (the
 * Linux radeon driver's blit code).
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

/* Byte offsets of the pieces inside the work area. */
#define WORK_IB			0x0000
#define WORK_VS			0x4000
#define WORK_VB			0x4200
#define WORK_TEXTURE		0x8000
#define RDN_SELFTEST_WORK_BYTES	0x10000

#define TEX_SIZE		64

static uint32_t float_bits(float f)
{
	union { float f; uint32_t u; } v;

	v.f = f;
	return v.u;
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
	struct rdn_draw_surface surface, texture;
	struct rdn_ib ib;
	uint32_t seq;
	int r;

	if (work_offset + RDN_SELFTEST_WORK_BYTES > accel->aperture_size ||
	    (work_offset & 0xfff) || target->width < 960 || target->height < 384)
		return -EINVAL;

	rdn_draw_put_shaders(accel, work_offset + WORK_VS);
	put_texture(accel, work_offset + WORK_TEXTURE);
	put_vertices(accel, work_offset + WORK_VB, rect);
	put_vertices(accel, work_offset + WORK_VB + 48, tri);

	ib.accel = accel;
	ib.base = work_offset + WORK_IB;
	ib.words = 0;
	ib.swapped = accel->swapped;

	surface.gpu_addr = target->gpu_addr;
	surface.width = target->width;
	surface.height = target->height;
	surface.pitch_pixels = target->pitch_pixels;
	texture.gpu_addr = rdn_vram_addr(accel, work_offset + WORK_TEXTURE);
	texture.width = texture.height = texture.pitch_pixels = TEX_SIZE;

	rdn_draw_default_state(&ib);
	rdn_draw_shaders(&ib, rdn_vram_addr(accel, work_offset + WORK_VS));
	rdn_draw_render_target(&ib, &surface, target->big_endian_pixels);
	rdn_draw_texture(&ib, &texture);
	rdn_draw_scissors(&ib, 0, 0, target->width, target->height);

	rdn_draw_vertices(&ib, rdn_vram_addr(accel, work_offset + WORK_VB), 48);
	rdn_draw_auto(&ib, RDN_DI_PT_RECTLIST, 3);

	rdn_draw_scissors(&ib, 0, 0, target->width, target->height);
	rdn_draw_vertices(&ib, rdn_vram_addr(accel, work_offset + WORK_VB + 48), 48);
	rdn_draw_auto(&ib, RDN_DI_PT_TRILIST, 3);

	/* Make the render target's caches reach memory. */
	rdn_draw_surface_sync(&ib, PACKET3_CB_ACTION_ENA | PACKET3_CB0_DEST_BASE_ENA,
			      target->pitch_pixels * target->height * 4,
			      target->gpu_addr);

	while (ib.words & 0xf)
		rdn_ib_emit(&ib, 0x80000000);
	if (ib.words * 4 > WORK_VS)
		return -ENOMEM;

	r = rdn_ib_submit(accel, rdn_vram_addr(accel, ib.base), ib.words, &seq);
	if (r)
		return r;
	return rdn_fence_wait(accel, seq, 2000);
}
