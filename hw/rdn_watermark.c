/*
 * Line buffer and display watermarks for CRTC 0.
 *
 * The display controller reads the picture from video memory into a line
 * buffer ahead of the beam. The watermarks tell the memory controller how
 * late a request of the display's may be served, and the priority marks
 * from which fill level of the buffer the display goes before the 3D
 * engine. Without them the display loses rows while the engine is busy.
 * Both depend on the mode and on the engine and memory clocks, so they are
 * set again whenever one of those changes.
 *
 * Follows the Linux radeon driver: evergreen_bandwidth_update(),
 * evergreen_line_buffer_adjust(), evergreen_program_watermarks() and the
 * bandwidth and latency helpers before them in evergreen.c, with the
 * 20.12 fixed point arithmetic of include/drm/drm_fixed.h. One display, no
 * scaling, not interlaced.
 *
 * Copyright 2009 Red Hat Inc.
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
 */

#include "rdn_card.h"
#include "rdn_pm.h"
#include "linux/evergreend.h"

/* DCE5: half of a line buffer pair, in pixels times two. */
#define LB_SIZE_HALF		(4096 * 2)
#define BYTES_PER_PIXEL		4	/* Linux assumes this whatever the depth */
#define NUM_HEADS		1

/* drm_fixed.h: 20.12 fixed point */
static uint32_t fx(uint32_t a)
{
	return a << 12;
}

static uint32_t fx_mul(uint32_t a, uint32_t b)
{
	return (uint32_t)(((uint64_t)a * b + 2048) >> 12);
}

/*
 * The kernel has no 64-bit division on 32-bit PowerPC without a library
 * routine, so this divides bit by bit; the operands are small.
 */
static uint64_t div64(uint64_t n, uint32_t d)
{
	uint64_t q = 0, r = 0;
	int i;

	for (i = 63; i >= 0; i--) {
		r = (r << 1) | ((n >> i) & 1);
		if (r >= d) {
			r -= d;
			q |= (uint64_t)1 << i;
		}
	}
	return q;
}

static uint32_t fx_div(uint32_t a, uint32_t b)
{
	return (uint32_t)((div64((uint64_t)a << 13, b) + 1) / 2);
}

static uint32_t fx_trunc(uint32_t a)
{
	return a >> 12;
}

static uint32_t min_u32(uint32_t a, uint32_t b)
{
	return a < b ? a : b;
}

struct wm_params {
	uint32_t dram_channels;
	uint32_t yclk;		/* bandwidth per dram data pin in kHz */
	uint32_t sclk;		/* engine clock in kHz */
	uint32_t disp_clk;	/* display clock in kHz */
	uint32_t src_width;
	uint32_t active_time;	/* ns */
	uint32_t blank_time;	/* ns */
	uint32_t lb_size;
};

/* What the memory can carry, times `tenths` / 10. */
static uint32_t dram_bandwidth(const struct wm_params *wm, uint32_t tenths)
{
	uint32_t yclk = fx_div(fx(wm->yclk), fx(1000));
	uint32_t part = fx_div(fx(tenths), fx(10));

	return fx_trunc(fx_mul(fx_mul(fx(wm->dram_channels * 4), yclk), part));
}

/* evergreen_data_return_bandwidth() and evergreen_dmif_request_bandwidth() */
static uint32_t clock_bandwidth(uint32_t khz)
{
	uint32_t clk = fx_div(fx(khz), fx(1000));
	uint32_t efficiency = fx_div(fx(8), fx(10));

	return fx_trunc(fx_mul(fx_mul(fx(32), clk), efficiency));
}

/* The display can use this for a while but not on average. */
static uint32_t available_bandwidth(const struct wm_params *wm)
{
	return min_u32(dram_bandwidth(wm, 7),
		       min_u32(clock_bandwidth(wm->sclk),
			       clock_bandwidth(wm->disp_clk)));
}

static uint32_t average_bandwidth(const struct wm_params *wm)
{
	uint32_t line_time = fx_div(fx(wm->active_time + wm->blank_time), fx(1000));
	uint32_t bandwidth = fx_mul(fx(wm->src_width), fx(BYTES_PER_PIXEL));

	bandwidth = fx_mul(bandwidth, fx(1));	/* vertical scale ratio */
	return fx_trunc(fx_div(bandwidth, line_time));
}

static uint32_t latency_watermark(const struct wm_params *wm)
{
	uint32_t mc_latency = 2000;	/* ns */
	uint32_t available = available_bandwidth(wm);
	uint32_t worst_chunk_return_time, cursor_line_pair_return_time;
	uint32_t dc_latency, latency, lb_fill_bw, line_fill_time;

	if (!available || !wm->disp_clk)
		return 0;
	worst_chunk_return_time = (512 * 8 * 1000) / available;
	cursor_line_pair_return_time = (128 * 4 * 1000) / available;
	dc_latency = 40000000 / wm->disp_clk;
	latency = mc_latency + (NUM_HEADS + 1) * worst_chunk_return_time +
		  NUM_HEADS * cursor_line_pair_return_time + dc_latency;

	lb_fill_bw = min_u32(fx_trunc(fx_div(fx(available), fx(NUM_HEADS))),
			     wm->disp_clk * BYTES_PER_PIXEL / 1000);
	if (!lb_fill_bw)
		return latency;
	/* two source lines for one line shown: no scaling */
	line_fill_time = fx_trunc(fx_div(fx(2 * wm->src_width * BYTES_PER_PIXEL),
					 fx_div(fx(lb_fill_bw), fx(1000))));
	if (line_fill_time < wm->active_time)
		return latency;
	return latency + (line_fill_time - wm->active_time);
}

/* The three checks after which Linux gives the display priority always. */
static bool needs_priority(const struct wm_params *wm)
{
	uint32_t average = average_bandwidth(wm);
	uint32_t line_time = wm->active_time + wm->blank_time;
	uint32_t tolerant_lines = wm->lb_size / wm->src_width <= 2 ? 1 : 2;

	return average > dram_bandwidth(wm, 3) / NUM_HEADS ||
	       average > available_bandwidth(wm) / NUM_HEADS ||
	       latency_watermark(wm) > tolerant_lines * line_time + wm->blank_time;
}

static uint32_t priority_count(const struct wm_params *wm, uint32_t watermark)
{
	uint32_t c = fx_mul(fx(watermark), fx_div(fx(wm->disp_clk), fx(1000)));

	c = fx_mul(c, fx(1));			/* horizontal scale ratio */
	c = fx_div(c, fx(1000));
	c = fx_div(c, fx(16));
	c = fx_trunc(c) & PRIORITY_MARK_MASK;
	if (needs_priority(wm))
		c |= PRIORITY_ALWAYS_ON;
	return c;
}

void rdn_bandwidth_update(struct rdn_card *card)
{
	uint32_t latency[2] = { 0, 0 }, priority[2] = { PRIORITY_OFF, PRIORITY_OFF };
	uint32_t line_time = 0, arb_control3, tmp;
	bool on = card->wm_clock && card->wm_hdisplay && card->wm_htotal;
	unsigned i;

	/* evergreen_line_buffer_adjust(): half of the pair's buffer, as Linux */
	rdn_wreg(card, DC_LB_MEMORY_SPLIT, 0);
	rdn_wreg(card, PIPE0_DMIF_BUFFER_CONTROL, DMIF_BUFFERS_ALLOCATED(on ? 1 : 0));
	for (i = 0; i < 100000; i++) {
		if (rdn_rreg(card, PIPE0_DMIF_BUFFER_CONTROL) &
		    DMIF_BUFFERS_ALLOCATED_COMPLETED)
			break;
		card->os->delay_us(card->os->cookie, 1);
	}

	if (on) {
		struct rdn_pm_state boot;
		struct wm_params wm;
		uint32_t channels;

		/* Clocks nobody has set are the ones the card boots with. */
		if ((!card->sclk || !card->mclk) && !rdn_pm_boot_state(card, &boot)) {
			card->sclk = boot.sclk;
			card->mclk = boot.mclk;
		}

		switch ((rdn_rreg(card, MC_SHARED_CHMAP) & NOOFCHAN_MASK) >> NOOFCHAN_SHIFT) {
		case 1:  channels = 2; break;
		case 2:  channels = 4; break;
		case 3:  channels = 8; break;
		default: channels = 1; break;
		}

		wm.dram_channels = channels;
		wm.disp_clk = card->wm_clock;
		wm.src_width = card->wm_hdisplay;
		wm.active_time = (uint32_t)div64((uint64_t)card->wm_hdisplay * 1000000,
						 card->wm_clock);
		line_time = min_u32((uint32_t)div64((uint64_t)card->wm_htotal * 1000000,
						    card->wm_clock), 65535);
		wm.blank_time = line_time - wm.active_time;
		wm.lb_size = LB_SIZE_HALF;

		/*
		 * Set A is for the high clocks and set B for the low ones of
		 * a driver that switches between two states by itself. This
		 * one does not, so both get the current clocks, unless the
		 * caller has named others for B.
		 */
		for (i = 0; i < 2; i++) {
			bool low = i == 1 && card->wm_low_sclk && card->wm_low_mclk;

			wm.sclk = (low ? card->wm_low_sclk : card->sclk) * 10;
			wm.yclk = (low ? card->wm_low_mclk : card->mclk) * 10;
			if (!wm.sclk || !wm.yclk)
				continue;
			latency[i] = min_u32(latency_watermark(&wm), 65535);
			priority[i] = priority_count(&wm, latency[i]);
		}
		rdn_log(card->os, RDN_LOG_INFO,
			"watermarks for engine %u0 kHz, memory %u0 kHz: latency %u and %u ns, priority 0x%X and 0x%X",
			(unsigned)card->sclk, (unsigned)card->mclk,
			(unsigned)latency[0], (unsigned)latency[1],
			(unsigned)priority[0], (unsigned)priority[1]);
	}

	/* select wm A, then B, then what was selected */
	arb_control3 = rdn_rreg(card, PIPE0_ARBITRATION_CONTROL3);
	tmp = arb_control3;
	tmp &= ~LATENCY_WATERMARK_MASK(3);
	tmp |= LATENCY_WATERMARK_MASK(1);
	rdn_wreg(card, PIPE0_ARBITRATION_CONTROL3, tmp);
	rdn_wreg(card, PIPE0_LATENCY_CONTROL,
		 LATENCY_LOW_WATERMARK(latency[0]) | LATENCY_HIGH_WATERMARK(line_time));
	tmp = rdn_rreg(card, PIPE0_ARBITRATION_CONTROL3);
	tmp &= ~LATENCY_WATERMARK_MASK(3);
	tmp |= LATENCY_WATERMARK_MASK(2);
	rdn_wreg(card, PIPE0_ARBITRATION_CONTROL3, tmp);
	rdn_wreg(card, PIPE0_LATENCY_CONTROL,
		 LATENCY_LOW_WATERMARK(latency[1]) | LATENCY_HIGH_WATERMARK(line_time));
	rdn_wreg(card, PIPE0_ARBITRATION_CONTROL3, arb_control3);

	rdn_wreg(card, PRIORITY_A_CNT, priority[0]);
	rdn_wreg(card, PRIORITY_B_CNT, priority[1]);
}
