/*
 * Acceleration core: 3D engine setup, command processor, fences.
 *
 * Memory model: everything the GPU reads (ring, indirect buffers, vertex
 * data, shaders, textures) lives in video memory and is reached by the CPU
 * through the aperture (BAR0). There is no GART, no bus mastering and no
 * interrupt; completion is polled.
 *
 * Byte order: data (vertices, shaders, textures written by this library)
 * is stored little-endian by the accessors below, on any host. Command
 * words, in the ring and in indirect buffers alike, are little-endian
 * unless the accelerator is started `swapped`; then they are big-endian
 * and the command processor swaps them. A big-endian host starts it
 * swapped, as Linux does, so that commands are in its own byte order.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_ACCEL_H
#define RDN_ACCEL_H

#include "rdn_card.h"

/* What evergreen_gpu_init() derives; user space needs some of it. */
struct rdn_gpu_config {
	uint32_t num_ses;
	uint32_t max_pipes;
	uint32_t max_tile_pipes;
	uint32_t max_simds;
	uint32_t max_backends;
	uint32_t max_gprs;
	uint32_t max_threads;
	uint32_t max_gs_threads;
	uint32_t max_stack_entries;
	uint32_t sx_num_of_sets;
	uint32_t sx_max_export_size;
	uint32_t sx_max_export_pos_size;
	uint32_t sx_max_export_smx_size;
	uint32_t max_hw_contexts;
	uint32_t sq_num_cf_insts;
	uint32_t sc_prim_fifo_size;
	uint32_t sc_hiz_tile_fifo_size;
	uint32_t sc_earlyz_tile_fifo_size;
	uint32_t tile_config;
	uint32_t backend_map;
	uint32_t active_simds;
};

/* Microcode images as shipped (big-endian words). */
struct rdn_accel_fw {
	const uint8_t *pfp;
	size_t pfp_size;
	const uint8_t *me;
	size_t me_size;
};

struct rdn_accel {
	struct rdn_card *card;
	struct rdn_gpu_config cfg;

	/* Video memory as the GPU addresses it, and as the CPU maps it. */
	uint64_t vram_base;
	uint64_t vram_size;
	volatile uint32_t *aperture;
	uint32_t aperture_size;

	/* The ring: byte offset in the aperture, size in words, next word. */
	uint32_t ring_offset;
	uint32_t ring_words;
	uint32_t wptr;
	bool ready;
	/*
	 * The command processor byte-swaps command words, as Linux sets it
	 * up on big-endian hosts (BUF_SWAP_32BIT for the ring, the swap
	 * field of each indirect buffer's address).
	 */
	bool swapped;

	uint32_t fence_emitted;
};

static inline void rdn_vram_write32(struct rdn_accel *accel, uint32_t offset,
				    uint32_t value)
{
	accel->aperture[offset / 4] = rdn_swap_le32(value);
}

static inline uint32_t rdn_vram_read32(struct rdn_accel *accel, uint32_t offset)
{
	return rdn_swap_le32(accel->aperture[offset / 4]);
}

/* The GPU's address of a byte offset into the aperture. */
static inline uint64_t rdn_vram_addr(struct rdn_accel *accel, uint32_t offset)
{
	return accel->vram_base + offset;
}

/*
 * Start the 3D engine and the command processor on a posted card.
 * `aperture` maps BAR0; the ring takes `ring_bytes` (a power of two) at
 * `ring_offset` in it. `swapped` chooses big-endian command words; pass
 * RDN_BIG_ENDIAN unless testing the other order. Ends with a ring test.
 * Returns 0 or a negative errno value.
 */
int rdn_accel_init(struct rdn_accel *accel, struct rdn_card *card,
		   volatile void *aperture, uint32_t aperture_size,
		   const struct rdn_accel_fw *fw,
		   uint32_t ring_offset, uint32_t ring_bytes, bool swapped);
void rdn_accel_fini(struct rdn_accel *accel);

/* rdn_gpu.c */
void rdn_gpu_init(struct rdn_accel *accel);

/*
 * Flush the card's cache of host accesses to video memory: needed after
 * the host writes something the GPU will read, and before the host reads
 * something the GPU wrote. rdn_ring_commit() and rdn_fence_wait() do it.
 */
void rdn_hdp_flush(struct rdn_accel *accel);

/*
 * Ring access: reserve, write, commit. rdn_ring_begin() waits for room and
 * fails with -EBUSY if the command processor stops consuming.
 */
int rdn_ring_begin(struct rdn_accel *accel, uint32_t words);
void rdn_ring_emit(struct rdn_accel *accel, uint32_t value);
void rdn_ring_commit(struct rdn_accel *accel);
int rdn_ring_test(struct rdn_accel *accel);

/*
 * A fence is a sequence number the command processor writes to a scratch
 * register when it reaches that point, after flushing the caches.
 */
int rdn_fence_emit(struct rdn_accel *accel, uint32_t *seq);
bool rdn_fence_done(struct rdn_accel *accel, uint32_t seq);
int rdn_fence_wait(struct rdn_accel *accel, uint32_t seq, uint32_t timeout_ms);

/*
 * Run a three-word command buffer that is anywhere the GPU can address:
 * `cpu` is where the CPU writes it (16 words), `gpu_addr` where the GPU
 * finds it. It sets a scratch register, which is then read back. For
 * memory other than video memory (rdn_gart.h).
 */
int rdn_ib_selftest(struct rdn_accel *accel, uint64_t gpu_addr, uint32_t *cpu);

/*
 * Run an indirect buffer of `words` words at GPU address `addr`, followed
 * by a fence. Its words are in the ring's byte order: big-endian when the
 * accelerator was started `swapped`, little-endian otherwise.
 */
int rdn_ib_submit(struct rdn_accel *accel, uint64_t addr, uint32_t words,
		  uint32_t *seq);

/*
 * rdn_blit.c: copy rectangles from one surface to another with the 3D
 * engine. Both are linear, 32 bits a pixel, top row first, in video memory
 * or wherever else the GPU can address; pixels are copied as they are
 * stored. `pitch_pixels` is a multiple of 8.
 *
 * `work_offset` (4 KB aligned) names rdn_blit_work_bytes() of scratch
 * space in the aperture, which the GPU reads until the fence `*seq` is
 * reached: wait for that before the next call with the same space. Uses no
 * floating point. Returns 0 or a negative errno value; -EINVAL if a
 * rectangle leaves either surface.
 */
struct rdn_draw_surface {
	uint64_t gpu_addr;
	uint32_t width, height, pitch_pixels;
};

struct rdn_blit_rect {
	uint32_t src_x, src_y, dst_x, dst_y, width, height;
};

#define RDN_BLIT_MAX_RECTS	256

uint32_t rdn_blit_work_bytes(void);
int rdn_blit(struct rdn_accel *accel, const struct rdn_draw_surface *dst,
	     const struct rdn_draw_surface *src,
	     const struct rdn_blit_rect *rects, uint32_t count,
	     uint32_t work_offset, uint32_t *seq);

/*
 * rdn_selftest.c: draw, with the 3D engine, a square (64,64)-(320,320) and
 * a triangle with corners (768,64), (640,320), (896,320) onto a 32-bit
 * surface. Both carry the same texture: red top left, green top right, blue
 * bottom left, white bottom right. `work_offset` (4 KB aligned) names
 * rdn_selftest_work_bytes() of scratch space in the aperture.
 */
struct rdn_selftest_target {
	uint64_t gpu_addr;
	uint32_t width, height, pitch_pixels;
	bool big_endian_pixels;
};

uint32_t rdn_selftest_work_bytes(void);
int rdn_accel_selftest(struct rdn_accel *accel,
		       const struct rdn_selftest_target *target,
		       uint32_t work_offset);

#endif /* RDN_ACCEL_H */
