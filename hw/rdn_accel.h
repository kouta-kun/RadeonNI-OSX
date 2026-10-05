/*
 * Acceleration core: 3D engine setup, command processor, fences.
 *
 * Memory model: everything the GPU reads (ring, indirect buffers, vertex
 * data, shaders, textures) lives in video memory and is reached by the CPU
 * through the aperture (BAR0). There is no GART, no bus mastering and no
 * interrupt; completion is polled.
 *
 * Words the command processor reads are stored little-endian, by the
 * accessors below, on any host. A client that fills an indirect buffer in
 * CPU byte order on a big-endian host asks for the swap at submit time.
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
 * `ring_offset` in it. Ends with a ring test. Returns 0 or a negative errno
 * value.
 */
int rdn_accel_init(struct rdn_accel *accel, struct rdn_card *card,
		   volatile void *aperture, uint32_t aperture_size,
		   const struct rdn_accel_fw *fw,
		   uint32_t ring_offset, uint32_t ring_bytes);
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
 * Run an indirect buffer of `words` words at GPU address `addr`, followed
 * by a fence. `swap` makes the command processor byte-swap each word.
 */
int rdn_ib_submit(struct rdn_accel *accel, uint64_t addr, uint32_t words,
		  bool swap, uint32_t *seq);

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
	/*
	 * Write the indirect buffer big-endian and ask the command
	 * processor to swap it: what a big-endian client does.
	 */
	bool swapped_ib;
};

uint32_t rdn_selftest_work_bytes(void);
int rdn_accel_selftest(struct rdn_accel *accel,
		       const struct rdn_selftest_target *target,
		       uint32_t work_offset);

#endif /* RDN_ACCEL_H */
