/*
 * Inside the library: building an indirect buffer that draws (rdn_blit.c).
 * rdn_accel.h has what callers of the library use.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_DRAW_H
#define RDN_DRAW_H

#include "rdn_accel.h"

#define RDN_DI_PT_TRILIST		0x4
#define RDN_DI_PT_RECTLIST		0x11

/* An indirect buffer being written at `base` in the aperture. */
struct rdn_ib {
	struct rdn_accel *accel;
	uint32_t base, words;
	/* Command words are big-endian: see rdn_accel.swapped. */
	bool swapped;
};

void rdn_ib_emit(struct rdn_ib *ib, uint32_t value);

/* CLEAR_STATE and everything a draw needs that the pieces below do not set. */
void rdn_draw_default_state(struct rdn_ib *ib);

/*
 * The vertex and pixel shader: stored once at an aperture offset (256 bytes
 * apart, RDN_DRAW_SHADER_BYTES in all), then named in the buffer by the
 * GPU address of that place.
 */
#define RDN_DRAW_PS			0x100
#define RDN_DRAW_SHADER_BYTES		0x200
void rdn_draw_put_shaders(struct rdn_accel *accel, uint32_t offset);
void rdn_draw_shaders(struct rdn_ib *ib, uint64_t vs_addr);

/*
 * The surface drawn on and the texture drawn with. `swap_bytes` makes the
 * GPU store each pixel's four bytes in the opposite order.
 */
void rdn_draw_render_target(struct rdn_ib *ib, const struct rdn_draw_surface *t,
			    bool swap_bytes);
void rdn_draw_texture(struct rdn_ib *ib, const struct rdn_draw_surface *t);
void rdn_draw_scissors(struct rdn_ib *ib, uint32_t x1, uint32_t y1,
		       uint32_t x2, uint32_t y2);

/* Vertices of four floats (x, y, s, t), in pixels and texels. */
void rdn_draw_vertices(struct rdn_ib *ib, uint64_t gpu_addr, uint32_t bytes);
void rdn_draw_auto(struct rdn_ib *ib, uint32_t primitive, uint32_t vertices);

void rdn_draw_surface_sync(struct rdn_ib *ib, uint32_t sync_type,
			   uint32_t size, uint64_t mc_addr);

#endif /* RDN_DRAW_H */
