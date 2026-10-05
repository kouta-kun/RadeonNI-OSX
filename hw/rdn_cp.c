/*
 * Command processor: microcode, ring, fences, indirect buffers.
 *
 * Follows the Linux radeon driver: evergreen_cp_load_microcode(),
 * evergreen_cp_start(), evergreen_cp_resume() and
 * evergreen_ring_ib_execute() in evergreen.c, r700_cp_stop() in rv770.c,
 * r600_ring_test() and r600_fence_ring_emit() in r600.c, and the ring
 * helpers in radeon_ring.c. Differences: the ring is in video memory
 * instead of GART memory, write-back is off (the read pointer and fences
 * are read from registers), and no interrupt is raised.
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

#define u32 uint32_t
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#include "linux/evergreen_blit_shaders.h"
#undef u32

#define RDN_CP_PACKET2		0x80000000
/* Linux pads every commit to 16 words. */
#define RDN_RING_ALIGN_MASK	15

#define RING_TEST_REG		RDN_SCRATCH_REG(0)
#define FENCE_REG		RDN_SCRATCH_REG(1)
#define RPTR_SAVE_REG		RDN_SCRATCH_REG(2)

#define RDN_USEC_TIMEOUT	100000

static void rdn_udelay(struct rdn_accel *accel, uint32_t usec)
{
	accel->card->os->delay_us(accel->card->os->cookie, usec);
}

static uint32_t rdn_get_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t rdn_bswap32(uint32_t v)
{
	return (v << 24) | ((v & 0xff00) << 8) | ((v >> 8) & 0xff00) | (v >> 24);
}

static uint32_t rdn_order_base_2(uint32_t v)
{
	uint32_t n = 0;

	while ((1u << n) < v)
		n++;
	return n;
}

static void rdn_cp_stop(struct rdn_accel *accel)
{
	struct rdn_card *card = accel->card;

	rdn_wreg(card, CP_ME_CNTL, (CP_ME_HALT | CP_PFP_HALT));
	rdn_wreg(card, SCRATCH_UMSK, 0);
	accel->ready = false;
}

static int rdn_cp_load_microcode(struct rdn_accel *accel,
				 const struct rdn_accel_fw *fw)
{
	struct rdn_card *card = accel->card;
	const uint8_t *fw_data;
	int i;

	if (!fw || !fw->me || !fw->pfp ||
	    fw->pfp_size != EVERGREEN_PFP_UCODE_SIZE * 4 ||
	    fw->me_size != EVERGREEN_PM4_UCODE_SIZE * 4)
		return -EINVAL;

	rdn_cp_stop(accel);
	rdn_wreg(card, CP_RB_CNTL,
		 (accel->swapped ? BUF_SWAP_32BIT : 0) |
		 RB_NO_UPDATE | RB_BLKSZ(15) | RB_BUFSZ(3));

	fw_data = fw->pfp;
	rdn_wreg(card, CP_PFP_UCODE_ADDR, 0);
	for (i = 0; i < EVERGREEN_PFP_UCODE_SIZE; i++, fw_data += 4)
		rdn_wreg(card, CP_PFP_UCODE_DATA, rdn_get_be32(fw_data));
	rdn_wreg(card, CP_PFP_UCODE_ADDR, 0);

	fw_data = fw->me;
	rdn_wreg(card, CP_ME_RAM_WADDR, 0);
	for (i = 0; i < EVERGREEN_PM4_UCODE_SIZE; i++, fw_data += 4)
		rdn_wreg(card, CP_ME_RAM_DATA, rdn_get_be32(fw_data));

	rdn_wreg(card, CP_PFP_UCODE_ADDR, 0);
	rdn_wreg(card, CP_ME_RAM_WADDR, 0);
	rdn_wreg(card, CP_ME_RAM_RADDR, 0);
	return 0;
}

/*
 * Ring
 */

static uint32_t rdn_ring_free(struct rdn_accel *accel)
{
	uint32_t rptr = rdn_rreg(accel->card, CP_RB_RPTR);
	uint32_t mask = accel->ring_words - 1;
	/* One word is kept free so that full and empty differ. */
	uint32_t free = (rptr + accel->ring_words - accel->wptr) & mask;

	return free ? free - 1 : accel->ring_words - 1;
}

int rdn_ring_begin(struct rdn_accel *accel, uint32_t words)
{
	uint32_t need = (words + RDN_RING_ALIGN_MASK) & ~RDN_RING_ALIGN_MASK;
	uint32_t i;

	if (need >= accel->ring_words)
		return -ENOMEM;
	for (i = 0; rdn_ring_free(accel) < need; i++) {
		if (i >= RDN_USEC_TIMEOUT)
			return -EBUSY;
		rdn_udelay(accel, 1);
	}
	return 0;
}

void rdn_ring_emit(struct rdn_accel *accel, uint32_t value)
{
	if (accel->swapped)
		value = rdn_bswap32(value);
	rdn_vram_write32(accel, accel->ring_offset + accel->wptr * 4, value);
	accel->wptr = (accel->wptr + 1) & (accel->ring_words - 1);
}

/*
 * The card caches what the host writes to and reads from video memory.
 * This makes the GPU see the host's writes and the host see the GPU's
 * (Linux: r600_mmio_hdp_flush()).
 */
void rdn_hdp_flush(struct rdn_accel *accel)
{
	rdn_wreg(accel->card, RDN_HDP_MEM_COHERENCY_FLUSH_CNTL, 0x1);
}

void rdn_ring_commit(struct rdn_accel *accel)
{
	while (accel->wptr & RDN_RING_ALIGN_MASK)
		rdn_ring_emit(accel, RDN_CP_PACKET2);
	rdn_hdp_flush(accel);
	rdn_wreg(accel->card, CP_RB_WPTR, accel->wptr);
	(void)rdn_rreg(accel->card, CP_RB_WPTR);
}

int rdn_ring_test(struct rdn_accel *accel)
{
	struct rdn_card *card = accel->card;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	rdn_wreg(card, RING_TEST_REG, 0xCAFEDEAD);
	r = rdn_ring_begin(accel, 3);
	if (r) {
		rdn_log(card->os, RDN_LOG_ERROR, "cp failed to lock ring (%d)", r);
		return r;
	}
	rdn_ring_emit(accel, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	rdn_ring_emit(accel, ((RING_TEST_REG - PACKET3_SET_CONFIG_REG_START) >> 2));
	rdn_ring_emit(accel, 0xDEADBEEF);
	rdn_ring_commit(accel);
	for (i = 0; i < RDN_USEC_TIMEOUT; i++) {
		tmp = rdn_rreg(card, RING_TEST_REG);
		if (tmp == 0xDEADBEEF)
			break;
		rdn_udelay(accel, 1);
	}
	if (i < RDN_USEC_TIMEOUT) {
		rdn_log(card->os, RDN_LOG_INFO, "ring test succeeded in %u usecs", i);
	} else {
		rdn_log(card->os, RDN_LOG_ERROR,
			"ring test failed (scratch(0x%04X)=0x%08X)",
			(unsigned)RING_TEST_REG, (unsigned)tmp);
		r = -EINVAL;
	}
	return r;
}

static int rdn_cp_start(struct rdn_accel *accel)
{
	struct rdn_card *card = accel->card;
	int r, i;
	uint32_t cp_me;

	r = rdn_ring_begin(accel, 7);
	if (r) {
		rdn_log(card->os, RDN_LOG_ERROR, "cp failed to lock ring (%d)", r);
		return r;
	}
	rdn_ring_emit(accel, PACKET3(PACKET3_ME_INITIALIZE, 5));
	rdn_ring_emit(accel, 0x1);
	rdn_ring_emit(accel, 0x0);
	rdn_ring_emit(accel, accel->cfg.max_hw_contexts - 1);
	rdn_ring_emit(accel, PACKET3_ME_INITIALIZE_DEVICE_ID(1));
	rdn_ring_emit(accel, 0);
	rdn_ring_emit(accel, 0);
	rdn_ring_commit(accel);

	cp_me = 0xff;
	rdn_wreg(card, CP_ME_CNTL, cp_me);

	r = rdn_ring_begin(accel, evergreen_default_size + 19);
	if (r) {
		rdn_log(card->os, RDN_LOG_ERROR, "cp failed to lock ring (%d)", r);
		return r;
	}

	/* setup clear context state */
	rdn_ring_emit(accel, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	rdn_ring_emit(accel, PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	for (i = 0; i < (int)evergreen_default_size; i++)
		rdn_ring_emit(accel, evergreen_default_state[i]);

	rdn_ring_emit(accel, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	rdn_ring_emit(accel, PACKET3_PREAMBLE_END_CLEAR_STATE);

	/* set clear context state */
	rdn_ring_emit(accel, PACKET3(PACKET3_CLEAR_STATE, 0));
	rdn_ring_emit(accel, 0);

	/* SQ_VTX_BASE_VTX_LOC */
	rdn_ring_emit(accel, 0xc0026f00);
	rdn_ring_emit(accel, 0x00000000);
	rdn_ring_emit(accel, 0x00000000);
	rdn_ring_emit(accel, 0x00000000);

	/* Clear consts */
	rdn_ring_emit(accel, 0xc0036f00);
	rdn_ring_emit(accel, 0x00000bc4);
	rdn_ring_emit(accel, 0xffffffff);
	rdn_ring_emit(accel, 0xffffffff);
	rdn_ring_emit(accel, 0xffffffff);

	rdn_ring_emit(accel, 0xc0026900);
	rdn_ring_emit(accel, 0x00000316);
	rdn_ring_emit(accel, 0x0000000e); /* VGT_VERTEX_REUSE_BLOCK_CNTL */
	rdn_ring_emit(accel, 0x00000010); /*  */

	rdn_ring_commit(accel);

	return 0;
}

static int rdn_cp_resume(struct rdn_accel *accel)
{
	struct rdn_card *card = accel->card;
	uint64_t ring_addr = rdn_vram_addr(accel, accel->ring_offset);
	uint32_t tmp;
	uint32_t rb_bufsz;
	int r;

	/* Reset cp; if cp is reset, then PA, SH, VGT also need to be reset */
	rdn_wreg(card, GRBM_SOFT_RESET, (SOFT_RESET_CP |
					 SOFT_RESET_PA |
					 SOFT_RESET_SH |
					 SOFT_RESET_VGT |
					 SOFT_RESET_SPI |
					 SOFT_RESET_SX));
	rdn_rreg(card, GRBM_SOFT_RESET);
	rdn_udelay(accel, 15000);
	rdn_wreg(card, GRBM_SOFT_RESET, 0);
	rdn_rreg(card, GRBM_SOFT_RESET);

	/* Set ring buffer size */
	rb_bufsz = rdn_order_base_2(accel->ring_words / 2);
	tmp = (rdn_order_base_2(4096 / 8) << 8) | rb_bufsz;
	if (accel->swapped)
		tmp |= BUF_SWAP_32BIT;
	rdn_wreg(card, CP_RB_CNTL, tmp);
	rdn_wreg(card, CP_SEM_WAIT_TIMER, 0x0);
	rdn_wreg(card, CP_SEM_INCOMPLETE_TIMER_CNTL, 0x0);

	/* Set the write pointer delay */
	rdn_wreg(card, CP_RB_WPTR_DELAY, 0);

	/* Initialize the ring buffer's read and write pointers */
	rdn_wreg(card, CP_RB_CNTL, tmp | RB_RPTR_WR_ENA);
	rdn_wreg(card, CP_RB_RPTR_WR, 0);
	accel->wptr = 0;
	rdn_wreg(card, CP_RB_WPTR, accel->wptr);

	/*
	 * No write-back: the addresses point at the ring's own memory so
	 * that they are valid, and updates are turned off.
	 */
	rdn_wreg(card, CP_RB_RPTR_ADDR, (uint32_t)ring_addr & 0xFFFFFFFC);
	rdn_wreg(card, CP_RB_RPTR_ADDR_HI, (uint32_t)(ring_addr >> 32) & 0xFF);
	rdn_wreg(card, SCRATCH_ADDR, (uint32_t)(ring_addr >> 8));
	tmp |= RB_NO_UPDATE;
	rdn_wreg(card, SCRATCH_UMSK, 0);

	rdn_udelay(accel, 1000);
	rdn_wreg(card, CP_RB_CNTL, tmp);

	rdn_wreg(card, CP_RB_BASE, (uint32_t)(ring_addr >> 8));
	rdn_wreg(card, CP_DEBUG, (1 << 27) | (1 << 28));

	r = rdn_cp_start(accel);
	if (r)
		return r;
	accel->ready = true;
	r = rdn_ring_test(accel);
	if (r) {
		accel->ready = false;
		return r;
	}
	return 0;
}

/*
 * Fences
 */

int rdn_fence_emit(struct rdn_accel *accel, uint32_t *seq)
{
	uint32_t cp_coher_cntl = PACKET3_TC_ACTION_ENA | PACKET3_VC_ACTION_ENA |
		PACKET3_SH_ACTION_ENA | PACKET3_FULL_CACHE_ENA;
	int r;

	r = rdn_ring_begin(accel, 13);
	if (r)
		return r;
	accel->fence_emitted++;
	/* flush read cache */
	rdn_ring_emit(accel, PACKET3(PACKET3_SURFACE_SYNC, 3));
	rdn_ring_emit(accel, cp_coher_cntl);
	rdn_ring_emit(accel, 0xFFFFFFFF);
	rdn_ring_emit(accel, 0);
	rdn_ring_emit(accel, 10); /* poll interval */
	rdn_ring_emit(accel, PACKET3(PACKET3_EVENT_WRITE, 0));
	rdn_ring_emit(accel, EVENT_TYPE(CACHE_FLUSH_AND_INV_EVENT) | EVENT_INDEX(0));
	/* wait for 3D idle clean */
	rdn_ring_emit(accel, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	rdn_ring_emit(accel, (WAIT_UNTIL - PACKET3_SET_CONFIG_REG_START) >> 2);
	rdn_ring_emit(accel, WAIT_3D_IDLE_bit | WAIT_3D_IDLECLEAN_bit);
	/* Emit fence sequence */
	rdn_ring_emit(accel, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	rdn_ring_emit(accel, ((FENCE_REG - PACKET3_SET_CONFIG_REG_START) >> 2));
	rdn_ring_emit(accel, accel->fence_emitted);
	rdn_ring_commit(accel);
	if (seq)
		*seq = accel->fence_emitted;
	return 0;
}

bool rdn_fence_done(struct rdn_accel *accel, uint32_t seq)
{
	/* Sequence numbers wrap; "reached" is within half the range. */
	return (int32_t)(rdn_rreg(accel->card, FENCE_REG) - seq) >= 0;
}

int rdn_fence_wait(struct rdn_accel *accel, uint32_t seq, uint32_t timeout_ms)
{
	struct rdn_os *os = accel->card->os;
	uint64_t start = os->time_ms(os->cookie);

	while (!rdn_fence_done(accel, seq)) {
		if (os->time_ms(os->cookie) - start > timeout_ms) {
			rdn_log(os, RDN_LOG_ERROR,
				"fence %u not reached (at %u), GRBM_STATUS 0x%08X",
				(unsigned)seq,
				(unsigned)rdn_rreg(accel->card, FENCE_REG),
				(unsigned)rdn_rreg(accel->card, GRBM_STATUS));
			return -ETIMEDOUT;
		}
		rdn_udelay(accel, 10);
	}
	/* The host may now read what the GPU drew. */
	rdn_hdp_flush(accel);
	return 0;
}

int rdn_ib_submit(struct rdn_accel *accel, uint64_t addr, uint32_t words,
		  uint32_t *seq)
{
	uint32_t next_rptr;
	int r;

	r = rdn_ring_begin(accel, 9);
	if (r)
		return r;

	/* set to DX10/11 mode */
	rdn_ring_emit(accel, PACKET3(PACKET3_MODE_CONTROL, 0));
	rdn_ring_emit(accel, 1);

	next_rptr = accel->wptr + 3 + 4;
	rdn_ring_emit(accel, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	rdn_ring_emit(accel, ((RPTR_SAVE_REG - PACKET3_SET_CONFIG_REG_START) >> 2));
	rdn_ring_emit(accel, next_rptr);

	rdn_ring_emit(accel, PACKET3(PACKET3_INDIRECT_BUFFER, 2));
	/*
	 * The buffer's words are swapped exactly when the ring's are: with
	 * only one of the two the command processor hangs (seen on the card).
	 */
	rdn_ring_emit(accel, (accel->swapped ? (2 << 0) : 0) |
			     ((uint32_t)addr & 0xFFFFFFFC));
	rdn_ring_emit(accel, (uint32_t)(addr >> 32) & 0xFF);
	rdn_ring_emit(accel, words);
	rdn_ring_commit(accel);

	return rdn_fence_emit(accel, seq);
}

/*
 * Bring-up
 */

int rdn_accel_init(struct rdn_accel *accel, struct rdn_card *card,
		   volatile void *aperture, uint32_t aperture_size,
		   const struct rdn_accel_fw *fw,
		   uint32_t ring_offset, uint32_t ring_bytes, bool swapped)
{
	uint32_t fb_location;
	int r;

	memset(accel, 0, sizeof(*accel));
	accel->card = card;
	accel->aperture = (volatile uint32_t *)aperture;
	accel->aperture_size = aperture_size;
	accel->ring_offset = ring_offset;
	accel->ring_words = ring_bytes / 4;
	accel->swapped = swapped;
	if (!aperture || (ring_bytes & (ring_bytes - 1)) || ring_bytes < 4096 ||
	    ring_offset + ring_bytes > aperture_size)
		return -EINVAL;

	/*
	 * Video memory stays where ASIC_Init put it in the GPU's address
	 * space; Linux moves it (evergreen_mc_program), this library does
	 * not. The register holds the first and last 16 MB block.
	 */
	fb_location = rdn_rreg(card, MC_VM_FB_LOCATION);
	accel->vram_base = (uint64_t)(fb_location & 0xffff) << 24;
	accel->vram_size = (uint64_t)rdn_rreg(card, CONFIG_MEMSIZE) << 20;
	rdn_log(card->os, RDN_LOG_INFO,
		"video memory %u MB at GPU address 0x%X%08X, aperture %u MB",
		(unsigned)(accel->vram_size >> 20),
		(unsigned)(accel->vram_base >> 32), (unsigned)accel->vram_base,
		(unsigned)(aperture_size >> 20));

	rdn_gpu_init(accel);

	r = rdn_cp_load_microcode(accel, fw);
	if (r) {
		rdn_log(card->os, RDN_LOG_ERROR, "CP microcode missing or of the wrong size");
		return r;
	}
	return rdn_cp_resume(accel);
}

void rdn_accel_fini(struct rdn_accel *accel)
{
	if (accel->card)
		rdn_cp_stop(accel);
}
