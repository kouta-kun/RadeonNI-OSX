/*
 * Mode setting for DCE5 (Northern Islands) through AtomBIOS command tables.
 *
 * The sequence, the parameter blocks and the PLL divider computation follow
 * the Linux radeon driver: atombios_crtc.c, atombios_encoders.c,
 * radeon_display.c (radeon_compute_pll_avivo), radeon_atombios.c
 * (radeon_atom_get_clock_info) and radeon_clocks.c.
 *
 * Always CRTC 0. The two outputs of the HD 7570 this driver is written for
 * are listed below as its VBIOS object table describes them and as the
 * Linux driver uses them; they should be read from that table instead.
 * The DVI-I connector's digital output is checked against a register trace
 * of Linux. The DisplayPort connector is not: the trace has no DisplayPort
 * sink in it, so that path follows the Linux sources only.
 *
 * Parameter blocks are built byte by byte in little-endian layout, so there
 * are no bitfields or host-endian structure fields to get wrong.
 *
 * Copyright 2007-8 Advanced Micro Devices, Inc.
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
 * Authors: Dave Airlie
 *          Alex Deucher
 */

#include "rdn_mode.h"
#include "rdn_dp.h"
#include "rdn_i2c.h"
#include "rdn_reg.h"

#ifndef ENODEV
#define ENODEV 19
#endif

#define CRTC_ID			0

/*
 * The outputs, in the order rdn_output_detect() tries them. Digital
 * encoders follow radeon_atom_pick_dig_encoder() for DCE5: two for each
 * UNIPHY, the first of the pair for link A. Both outputs are on link A.
 */
static const struct rdn_output outputs[] = {
	{
		"DVI-I", false, CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_I,
		ENCODER_OBJECT_ID_INTERNAL_UNIPHY, 0, 0,
		0,	/* RADEON_HPD_1 */
		0x93,
	},
	{
		"DisplayPort", true, CONNECTOR_OBJECT_ID_DISPLAYPORT,
		ENCODER_OBJECT_ID_INTERNAL_UNIPHY2, 2, 4,
		3,	/* RADEON_HPD_4 */
		0x92,
	},
};

const struct rdn_output *rdn_output(int index)
{
	if (index < 0 || index >= (int)(sizeof(outputs) / sizeof(outputs[0])))
		return NULL;
	return &outputs[index];
}

static const struct rdn_output *card_output(struct rdn_card *card)
{
	return card->output ? card->output : &outputs[RDN_OUTPUT_DVI];
}

/* The output carries DisplayPort signalling, not TMDS. */
static bool output_is_dp(struct rdn_card *card)
{
	return card_output(card)->displayport && card->dp.sink;
}

/*
 * radeon_atom_pick_pll() for DCE5: the DCPLL clocks every DisplayPort link
 * when the board has no external clock for that, as this one has not.
 */
static int output_pll(struct rdn_card *card)
{
	return output_is_dp(card) ? ATOM_DCPLL : ATOM_PPLL1;
}

#define PS_WORDS		16

/* PLL flags (radeon_mode.h) */
#define RADEON_PLL_USE_REF_DIV		(1 << 2)
#define RADEON_PLL_PREFER_LOW_REF_DIV	(1 << 6)
#define RADEON_PLL_USE_FRAC_FB_DIV	(1 << 10)
#define RADEON_PLL_USE_POST_DIV		(1 << 12)

struct rdn_pll {
	uint32_t reference_freq;	/* 10 kHz units */
	uint32_t reference_div, post_div;
	uint32_t pll_out_min, pll_out_max;
	uint32_t min_ref_div, max_ref_div;
	uint32_t min_post_div, max_post_div;
	uint32_t min_feedback_div, max_feedback_div;
	uint32_t flags;
};

/* A zeroed parameter block; bytes are stored at their table offsets. */
struct ps {
	uint8_t b[PS_WORDS * 4];
};

static void ps_init(struct ps *ps)
{
	memset(ps, 0, sizeof(*ps));
}

static void ps_u8(struct ps *ps, int off, uint32_t v)
{
	ps->b[off] = (uint8_t)v;
}

static void ps_le16(struct ps *ps, int off, uint32_t v)
{
	ps->b[off] = (uint8_t)v;
	ps->b[off + 1] = (uint8_t)(v >> 8);
}

static void ps_le32(struct ps *ps, int off, uint32_t v)
{
	ps_le16(ps, off, v & 0xffff);
	ps_le16(ps, off + 2, v >> 16);
}

/*
 * The interpreter takes the block as 32-bit words holding little-endian
 * data and writes results back the same way.
 */
static int ps_exec(struct rdn_card *card, int index, struct ps *ps)
{
	uint32_t words[PS_WORDS];
	int r;

	memcpy(words, ps->b, sizeof(words));
	r = atom_execute_table(card->atom.ctx, index, words, PS_WORDS);
	memcpy(ps->b, words, sizeof(words));
	if (r)
		rdn_log(card->os, RDN_LOG_ERROR,
			"AtomBIOS command table %d failed (%d)", index, r);
	return r;
}

static bool cmd_table_rev(struct rdn_card *card, int index, uint8_t *frev,
			  uint8_t *crev)
{
	return atom_parse_cmd_header(card->atom.ctx, index, frev, crev);
}

/* Refuse a table whose revision is not the one this code was written for. */
static int cmd_table_check(struct rdn_card *card, int index, const char *name,
			   int want_frev, int want_crev)
{
	uint8_t frev = 0, crev = 0;

	if (!cmd_table_rev(card, index, &frev, &crev) ||
	    frev != want_frev || crev != want_crev) {
		rdn_log(card->os, RDN_LOG_ERROR,
			"%s is revision %d.%d, need %d.%d", name, frev, crev,
			want_frev, want_crev);
		return -EINVAL;
	}
	return 0;
}

/* ENABLE_CRTC_PS_ALLOCATION: ucCRTC, ucEnable. Also used for lock and blank. */
static int crtc_cmd(struct rdn_card *card, int index, int state)
{
	struct ps ps;

	ps_init(&ps);
	ps_u8(&ps, 0, CRTC_ID);
	ps_u8(&ps, 1, state);
	return ps_exec(card, index, &ps);
}

static int lock_crtc(struct rdn_card *card, int lock)
{
	return crtc_cmd(card, GetIndexIntoMasterTable(COMMAND,
			UpdateCRTC_DoubleBufferRegisters), lock);
}

static int enable_crtc(struct rdn_card *card, int state)
{
	return crtc_cmd(card, GetIndexIntoMasterTable(COMMAND, EnableCRTC), state);
}

static int enable_crtc_memreq(struct rdn_card *card, int state)
{
	return crtc_cmd(card, GetIndexIntoMasterTable(COMMAND, EnableCRTCMemReq),
			state);
}

static int blank_crtc(struct rdn_card *card, int state)
{
	return crtc_cmd(card, GetIndexIntoMasterTable(COMMAND, BlankCRTC), state);
}

/* radeon_atom_get_clock_info() and radeon_get_clock_info(), pixel PLL only */
static int get_pll_info(struct rdn_card *card, struct rdn_pll *pll)
{
	struct atom_context *ctx = card->atom.ctx;
	int index = GetIndexIntoMasterTable(DATA, FirmwareInfo);
	ATOM_FIRMWARE_INFO *info;
	ATOM_FIRMWARE_INFO_V1_2 *info_12;
	uint8_t frev, crev;
	uint16_t data_offset;

	if (!atom_parse_data_header(ctx, index, NULL, &frev, &crev, &data_offset))
		return -EINVAL;
	info = (ATOM_FIRMWARE_INFO *)((uint8_t *)ctx->bios + data_offset);
	info_12 = (ATOM_FIRMWARE_INFO_V1_2 *)info;

	memset(pll, 0, sizeof(*pll));
	pll->reference_freq = le16_to_cpu(info->usReferenceClock);
	if ((frev < 2) && (crev < 2))
		pll->pll_out_min = le16_to_cpu(info->usMinPixelClockPLL_Output);
	else
		pll->pll_out_min = le32_to_cpu(info_12->ulMinPixelClockPLL_Output);
	pll->pll_out_max = le32_to_cpu(info->ulMaxPixelClockPLL_Output);
	if (pll->pll_out_min == 0)
		pll->pll_out_min = 64800;

	pll->min_post_div = 2;
	pll->max_post_div = 0x7f;
	pll->min_ref_div = 2;
	pll->max_ref_div = 0x3ff;
	pll->min_feedback_div = 4;
	pll->max_feedback_div = 0x7ff;
	return 0;
}

static unsigned gcd_u(unsigned a, unsigned b)
{
	while (b) {
		unsigned t = a % b;

		a = b;
		b = t;
	}
	return a;
}

#define DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#define DIV_ROUND_CLOSEST(n, d)	(((n) + (d) / 2) / (d))

static unsigned clamp_u(unsigned v, unsigned lo, unsigned hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static void avivo_reduce_ratio(unsigned *nom, unsigned *den,
			       unsigned nom_min, unsigned den_min)
{
	unsigned tmp;

	/* reduce the numbers to a simpler ratio */
	tmp = gcd_u(*nom, *den);
	*nom /= tmp;
	*den /= tmp;

	/* make sure nominator is large enough */
	if (*nom < nom_min) {
		tmp = DIV_ROUND_UP(nom_min, *nom);
		*nom *= tmp;
		*den *= tmp;
	}

	/* make sure the denominator is large enough */
	if (*den < den_min) {
		tmp = DIV_ROUND_UP(den_min, *den);
		*nom *= tmp;
		*den *= tmp;
	}
}

static void avivo_get_fb_ref_div(unsigned nom, unsigned den, unsigned post_div,
				 unsigned fb_div_max, unsigned ref_div_max,
				 unsigned *fb_div, unsigned *ref_div)
{
	/* limit reference * post divider to a maximum */
	ref_div_max = clamp_u(100 / post_div, 1u, ref_div_max);

	/* get matching reference and feedback divider */
	*ref_div = clamp_u(den / post_div, 1u, ref_div_max);
	*fb_div = DIV_ROUND_CLOSEST(nom * *ref_div * post_div, den);

	/* limit fb divider to its maximum */
	if (*fb_div > fb_div_max) {
		*ref_div = (*ref_div * fb_div_max) / (*fb_div);
		*fb_div = fb_div_max;
	}
}

/* radeon_compute_pll_avivo(); `freq` is in kHz */
static void compute_pll(const struct rdn_pll *pll, uint32_t freq,
			uint32_t *fb_div_p, uint32_t *frac_fb_div_p,
			uint32_t *ref_div_p, uint32_t *post_div_p)
{
	unsigned target_clock = pll->flags & RADEON_PLL_USE_FRAC_FB_DIV ?
		freq : freq / 10;
	unsigned fb_div_min, fb_div_max, fb_div;
	unsigned post_div_min, post_div_max, post_div;
	unsigned ref_div_min, ref_div_max, ref_div;
	unsigned post_div_best, diff_best;
	unsigned nom, den;

	/* determine allowed feedback divider range */
	fb_div_min = pll->min_feedback_div;
	fb_div_max = pll->max_feedback_div;

	if (pll->flags & RADEON_PLL_USE_FRAC_FB_DIV) {
		fb_div_min *= 10;
		fb_div_max *= 10;
	}

	/* determine allowed ref divider range */
	if (pll->flags & RADEON_PLL_USE_REF_DIV)
		ref_div_min = pll->reference_div;
	else
		ref_div_min = pll->min_ref_div;

	if (pll->flags & RADEON_PLL_USE_FRAC_FB_DIV &&
	    pll->flags & RADEON_PLL_USE_REF_DIV)
		ref_div_max = pll->reference_div;
	else
		ref_div_max = pll->max_ref_div;

	/* determine allowed post divider range */
	if (pll->flags & RADEON_PLL_USE_POST_DIV) {
		post_div_min = pll->post_div;
		post_div_max = pll->post_div;
	} else {
		unsigned vco_min, vco_max;

		vco_min = pll->pll_out_min;
		vco_max = pll->pll_out_max;

		if (pll->flags & RADEON_PLL_USE_FRAC_FB_DIV) {
			vco_min *= 10;
			vco_max *= 10;
		}

		post_div_min = vco_min / target_clock;
		if ((target_clock * post_div_min) < vco_min)
			++post_div_min;
		if (post_div_min < pll->min_post_div)
			post_div_min = pll->min_post_div;

		post_div_max = vco_max / target_clock;
		if ((target_clock * post_div_max) > vco_max)
			--post_div_max;
		if (post_div_max > pll->max_post_div)
			post_div_max = pll->max_post_div;
	}

	/* represent the searched ratio as fractional number */
	nom = target_clock;
	den = pll->reference_freq;

	/* reduce the numbers to a simpler ratio */
	avivo_reduce_ratio(&nom, &den, fb_div_min, post_div_min);

	/* now search for a post divider */
	post_div_best = post_div_max;
	diff_best = ~0u;

	for (post_div = post_div_min; post_div <= post_div_max; ++post_div) {
		unsigned diff, got;

		avivo_get_fb_ref_div(nom, den, post_div, fb_div_max,
				     ref_div_max, &fb_div, &ref_div);
		got = (pll->reference_freq * fb_div) / (ref_div * post_div);
		diff = target_clock > got ? target_clock - got : got - target_clock;

		if (diff <= diff_best) {
			post_div_best = post_div;
			diff_best = diff;
		}
	}
	post_div = post_div_best;

	/* get the feedback and reference divider for the optimal value */
	avivo_get_fb_ref_div(nom, den, post_div, fb_div_max, ref_div_max,
			     &fb_div, &ref_div);

	/* reduce the numbers to a simpler ratio once more */
	/* this also makes sure that the reference divider is large enough */
	avivo_reduce_ratio(&fb_div, &ref_div, fb_div_min, ref_div_min);

	/* avoid high jitter with small fractional dividers */
	if (pll->flags & RADEON_PLL_USE_FRAC_FB_DIV && (fb_div % 10)) {
		unsigned jitter_min = (9 - (fb_div % 10)) * 20 + 50;

		if (fb_div_min < jitter_min)
			fb_div_min = jitter_min;
		if (fb_div < fb_div_min) {
			unsigned tmp = DIV_ROUND_UP(fb_div_min, fb_div);

			fb_div *= tmp;
			ref_div *= tmp;
		}
	}

	/* and finally save the result */
	if (pll->flags & RADEON_PLL_USE_FRAC_FB_DIV) {
		*fb_div_p = fb_div / 10;
		*frac_fb_div_p = fb_div % 10;
	} else {
		*fb_div_p = fb_div;
		*frac_fb_div_p = 0;
	}
	*ref_div_p = ref_div;
	*post_div_p = post_div;
}

/*
 * atombios_adjust_pll(): let the BIOS adjust the PLL frequency for this
 * transmitter. ADJUST_DISPLAY_PLL_PS_ALLOCATION_V3; returns kHz, 0 on error.
 */
static uint32_t adjust_pll(struct rdn_card *card, const struct rdn_mode *mode,
			   int encoder_mode, struct rdn_pll *pll)
{
	int index = GetIndexIntoMasterTable(COMMAND, AdjustDisplayPll);
	struct ps ps;

	pll->flags = RADEON_PLL_PREFER_LOW_REF_DIV;

	if (cmd_table_check(card, index, "AdjustDisplayPll", 1, 3))
		return 0;

	ps_init(&ps);
	/* usPixelClock; for DisplayPort the link clock, 16200 or 27000 */
	ps_le16(&ps, 0, (output_is_dp(card) ? card->dp.rate : mode->clock) / 10);
	ps_u8(&ps, 2, card_output(card)->transmitter_id); /* ucTransmitterID */
	ps_u8(&ps, 3, encoder_mode);			/* ucEncodeMode */
	ps_u8(&ps, 4, DISPPLL_CONFIG_COHERENT_MODE);	/* ucDispPllConfig */
	ps_u8(&ps, 5, 0);				/* ucExtTransmitterID */
	if (ps_exec(card, index, &ps))
		return 0;

	/* Output: ulDispPllFreq, ucRefDiv, ucPostDiv */
	if (ps.b[4]) {
		pll->flags |= RADEON_PLL_USE_FRAC_FB_DIV | RADEON_PLL_USE_REF_DIV;
		pll->reference_div = ps.b[4];
	}
	if (ps.b[5]) {
		pll->flags |= RADEON_PLL_USE_FRAC_FB_DIV | RADEON_PLL_USE_POST_DIV;
		pll->post_div = ps.b[5];
	}
	return rdn_get_le32(ps.b, 0) * 10;
}

/* atombios_set_encoder_crtc_source(), SELECT_CRTC_SOURCE_PARAMETERS_V2 */
static int select_crtc_source(struct rdn_card *card, int encoder_mode)
{
	static const uint8_t dig_id[] = {
		ASIC_INT_DIG1_ENCODER_ID, ASIC_INT_DIG2_ENCODER_ID,
		ASIC_INT_DIG3_ENCODER_ID, ASIC_INT_DIG4_ENCODER_ID,
		ASIC_INT_DIG5_ENCODER_ID, ASIC_INT_DIG6_ENCODER_ID,
	};
	int index = GetIndexIntoMasterTable(COMMAND, SelectCRTC_Source);
	struct ps ps;

	if (cmd_table_check(card, index, "SelectCRTC_Source", 1, 2))
		return -EINVAL;
	ps_init(&ps);
	ps_u8(&ps, 0, CRTC_ID);				/* ucCRTC */
	ps_u8(&ps, 1, dig_id[card_output(card)->dig_encoder]); /* ucEncoderID */
	ps_u8(&ps, 2, encoder_mode);			/* ucEncodeMode */
	return ps_exec(card, index, &ps);
}

/*
 * atombios_crtc_program_ss() with ATOM_DISABLE, V3 parameters: all zero but
 * the PLL in ucSpreadSpectrumType, and pixel PLL 1 is zero there too.
 */
static int disable_spread_spectrum(struct rdn_card *card)
{
	int index = GetIndexIntoMasterTable(COMMAND, EnableSpreadSpectrumOnPPLL);
	struct ps ps;

	ps_init(&ps);
	if (output_pll(card) == ATOM_DCPLL)
		ps_u8(&ps, 2, ATOM_PPLL_SS_TYPE_V3_DCPLL);
	return ps_exec(card, index, &ps);
}

/* atombios_crtc_program_pll(), PIXEL_CLOCK_PARAMETERS_V6 */
static int program_pll(struct rdn_card *card, const struct rdn_mode *mode,
		       int encoder_mode, uint32_t ref_div, uint32_t fb_div,
		       uint32_t frac_fb_div, uint32_t post_div)
{
	int index = GetIndexIntoMasterTable(COMMAND, SetPixelClock);
	struct ps ps;

	if (cmd_table_check(card, index, "SetPixelClock", 1, 6))
		return -EINVAL;
	ps_init(&ps);
	/* ulDispEngClkFreq: pixel clock in the low 24 bits, CRTC in the top 8 */
	ps_le32(&ps, 0, ((uint32_t)CRTC_ID << 24) | (mode->clock / 10));
	ps_le16(&ps, 4, fb_div);			/* usFbDiv */
	ps_u8(&ps, 6, post_div);			/* ucPostDiv */
	ps_u8(&ps, 7, ref_div);				/* ucRefDiv */
	ps_u8(&ps, 8, output_pll(card));		/* ucPpll */
	ps_u8(&ps, 9, card_output(card)->transmitter_id); /* ucTransmitterID */
	ps_u8(&ps, 10, encoder_mode);			/* ucEncoderMode */
	ps_u8(&ps, 11, 0);	/* ucMiscInfo: 24 bpp for HDMI is 0 */
	ps_le32(&ps, 12, frac_fb_div * 100000);		/* ulFbDivDecFrac */
	return ps_exec(card, index, &ps);
}

/* atombios_set_crtc_dtd_timing(), SET_CRTC_USING_DTD_TIMING_PARAMETERS */
static int set_dtd_timing(struct rdn_card *card, const struct rdn_mode *mode)
{
	int index = GetIndexIntoMasterTable(COMMAND, SetCRTC_UsingDTDTiming);
	uint16_t misc = 0;
	struct ps ps;

	ps_init(&ps);
	ps_le16(&ps, 0, mode->hdisplay);			/* usH_Size */
	ps_le16(&ps, 2, mode->htotal - mode->hdisplay);		/* usH_Blanking_Time */
	ps_le16(&ps, 4, mode->vdisplay);			/* usV_Size */
	ps_le16(&ps, 6, mode->vtotal - mode->vdisplay);		/* usV_Blanking_Time */
	ps_le16(&ps, 8, mode->hsync_start - mode->hdisplay);	/* usH_SyncOffset */
	ps_le16(&ps, 10, mode->hsync_end - mode->hsync_start);	/* usH_SyncWidth */
	ps_le16(&ps, 12, mode->vsync_start - mode->vdisplay);	/* usV_SyncOffset */
	ps_le16(&ps, 14, mode->vsync_end - mode->vsync_start);	/* usV_SyncWidth */
	if (mode->flags & RDN_MODE_NVSYNC)
		misc |= ATOM_VSYNC_POLARITY;
	if (mode->flags & RDN_MODE_NHSYNC)
		misc |= ATOM_HSYNC_POLARITY;
	ps_le16(&ps, 16, misc);					/* susModeMiscInfo */
	ps_u8(&ps, 18, 0);					/* ucH_Border */
	ps_u8(&ps, 19, 0);					/* ucV_Border */
	ps_u8(&ps, 20, CRTC_ID);				/* ucCRTC */
	return ps_exec(card, index, &ps);
}

/* atombios_overscan_setup(): no overscan. SET_CRTC_OVERSCAN_PS_ALLOCATION */
static int overscan_setup(struct rdn_card *card)
{
	int index = GetIndexIntoMasterTable(COMMAND, SetCRTC_OverScan);
	struct ps ps;

	ps_init(&ps);
	ps_u8(&ps, 8, CRTC_ID);					/* ucCRTC */
	return ps_exec(card, index, &ps);
}

/* atombios_scaler_setup(): scaler off. ENABLE_SCALER_PS_ALLOCATION */
static int scaler_setup(struct rdn_card *card)
{
	int index = GetIndexIntoMasterTable(COMMAND, EnableScaler);
	struct ps ps;

	ps_init(&ps);
	ps_u8(&ps, 0, CRTC_ID);					/* ucScaler */
	ps_u8(&ps, 1, ATOM_SCALER_DISABLE);			/* ucEnable */
	return ps_exec(card, index, &ps);
}

/* dce4_crtc_do_set_base(): 32 bpp, linear, at the given aperture offset */
static void set_base(struct rdn_card *card, const struct rdn_mode *mode,
		     const struct rdn_fb *fb)
{
	uint32_t fb_format, fb_swap = EVERGREEN_GRPH_ENDIAN_NONE;
	uint64_t fb_location;
	uint32_t tmp;

	switch (fb->bpp) {
	case 8:
		fb_format = EVERGREEN_GRPH_DEPTH(EVERGREEN_GRPH_DEPTH_8BPP) |
			EVERGREEN_GRPH_FORMAT(EVERGREEN_GRPH_FORMAT_INDEXED);
		break;
	case 16:
		fb_format = EVERGREEN_GRPH_DEPTH(EVERGREEN_GRPH_DEPTH_16BPP) |
			EVERGREEN_GRPH_FORMAT(EVERGREEN_GRPH_FORMAT_ARGB1555);
		if (fb->big_endian_pixels)
			fb_swap = EVERGREEN_GRPH_ENDIAN_8IN16;
		break;
	default:
		fb_format = EVERGREEN_GRPH_DEPTH(EVERGREEN_GRPH_DEPTH_32BPP) |
			EVERGREEN_GRPH_FORMAT(EVERGREEN_GRPH_FORMAT_ARGB8888);
		if (fb->big_endian_pixels)
			fb_swap = EVERGREEN_GRPH_ENDIAN_8IN32;
		break;
	}
	fb_swap = EVERGREEN_GRPH_ENDIAN_SWAP(fb_swap);

	/*
	 * The aperture shows the start of the card's framebuffer address
	 * range, which the BIOS placed where MC_VM_FB_LOCATION says. Linux
	 * reprograms that range to start at 0; this code leaves it alone.
	 */
	fb_location = ((uint64_t)(rdn_rreg(card, MC_VM_FB_LOCATION) & 0xffff) << 24) +
		fb->aperture_offset;

	rdn_wreg(card, AVIVO_D1VGA_CONTROL, 0);

	rdn_wreg(card, EVERGREEN_GRPH_FLIP_CONTROL, 0);

	rdn_wreg(card, EVERGREEN_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH,
		 (uint32_t)(fb_location >> 32));
	rdn_wreg(card, EVERGREEN_GRPH_SECONDARY_SURFACE_ADDRESS_HIGH,
		 (uint32_t)(fb_location >> 32));
	rdn_wreg(card, EVERGREEN_GRPH_PRIMARY_SURFACE_ADDRESS,
		 (uint32_t)fb_location & EVERGREEN_GRPH_SURFACE_ADDRESS_MASK);
	rdn_wreg(card, EVERGREEN_GRPH_SECONDARY_SURFACE_ADDRESS,
		 (uint32_t)fb_location & EVERGREEN_GRPH_SURFACE_ADDRESS_MASK);
	rdn_wreg(card, EVERGREEN_GRPH_CONTROL, fb_format);
	rdn_wreg(card, EVERGREEN_GRPH_SWAP_CONTROL, fb_swap);

	/* 8 bpc scanout goes through the LUT */
	tmp = rdn_rreg(card, EVERGREEN_GRPH_LUT_10BIT_BYPASS_CONTROL);
	tmp &= ~EVERGREEN_LUT_10BIT_BYPASS_EN;
	rdn_wreg(card, EVERGREEN_GRPH_LUT_10BIT_BYPASS_CONTROL, tmp);

	rdn_wreg(card, EVERGREEN_GRPH_SURFACE_OFFSET_X, 0);
	rdn_wreg(card, EVERGREEN_GRPH_SURFACE_OFFSET_Y, 0);
	rdn_wreg(card, EVERGREEN_GRPH_X_START, 0);
	rdn_wreg(card, EVERGREEN_GRPH_Y_START, 0);
	rdn_wreg(card, EVERGREEN_GRPH_X_END, fb->width);
	rdn_wreg(card, EVERGREEN_GRPH_Y_END, fb->height);

	rdn_wreg(card, EVERGREEN_GRPH_PITCH, fb->pitch_pixels);
	rdn_wreg(card, EVERGREEN_GRPH_ENABLE, 1);

	rdn_wreg(card, EVERGREEN_DESKTOP_HEIGHT, fb->height);
	rdn_wreg(card, EVERGREEN_VIEWPORT_START, 0);
	rdn_wreg(card, EVERGREEN_VIEWPORT_SIZE,
		 ((uint32_t)mode->hdisplay << 16) |
		 ((mode->vdisplay + 1u) & ~1u));

	/* set pageflip to happen anywhere in vblank interval */
	rdn_wreg(card, EVERGREEN_MASTER_UPDATE_MODE, 0);
}

void rdn_lut_set(struct rdn_card *card, uint32_t start, uint32_t count,
		 const struct rdn_lut_entry *entries)
{
	uint32_t i;

	if (start >= 256)
		return;
	if (count > 256 - start)
		count = 256 - start;

	rdn_wreg(card, EVERGREEN_DC_LUT_RW_MODE, 0);
	rdn_wreg(card, EVERGREEN_DC_LUT_WRITE_EN_MASK, 0x00000007);
	/* The index advances by itself after each colour written. */
	rdn_wreg(card, EVERGREEN_DC_LUT_RW_INDEX, start);
	for (i = 0; i < count; i++)
		rdn_wreg(card, EVERGREEN_DC_LUT_30_COLOR,
			 ((uint32_t)(entries[i].red & 0x3ff) << 20) |
			 ((uint32_t)(entries[i].green & 0x3ff) << 10) |
			 (uint32_t)(entries[i].blue & 0x3ff));
}

/* dce5_crtc_load_lut(), with a linear ramp: colours pass through unchanged */
static void load_lut(struct rdn_card *card)
{
	uint32_t i;

	card->os->delay_us(card->os->cookie, 10000);

	rdn_wreg(card, NI_INPUT_CSC_CONTROL,
		 (NI_INPUT_CSC_GRPH_MODE(NI_INPUT_CSC_BYPASS) |
		  NI_INPUT_CSC_OVL_MODE(NI_INPUT_CSC_BYPASS)));
	rdn_wreg(card, NI_PRESCALE_GRPH_CONTROL, NI_GRPH_PRESCALE_BYPASS);
	rdn_wreg(card, NI_PRESCALE_OVL_CONTROL, NI_OVL_PRESCALE_BYPASS);
	rdn_wreg(card, NI_INPUT_GAMMA_CONTROL,
		 (NI_GRPH_INPUT_GAMMA_MODE(NI_INPUT_GAMMA_USE_LUT) |
		  NI_OVL_INPUT_GAMMA_MODE(NI_INPUT_GAMMA_USE_LUT)));

	rdn_wreg(card, EVERGREEN_DC_LUT_CONTROL, 0);

	rdn_wreg(card, EVERGREEN_DC_LUT_BLACK_OFFSET_BLUE, 0);
	rdn_wreg(card, EVERGREEN_DC_LUT_BLACK_OFFSET_GREEN, 0);
	rdn_wreg(card, EVERGREEN_DC_LUT_BLACK_OFFSET_RED, 0);

	rdn_wreg(card, EVERGREEN_DC_LUT_WHITE_OFFSET_BLUE, 0xffff);
	rdn_wreg(card, EVERGREEN_DC_LUT_WHITE_OFFSET_GREEN, 0xffff);
	rdn_wreg(card, EVERGREEN_DC_LUT_WHITE_OFFSET_RED, 0xffff);

	rdn_wreg(card, EVERGREEN_DC_LUT_RW_MODE, 0);
	rdn_wreg(card, EVERGREEN_DC_LUT_WRITE_EN_MASK, 0x00000007);

	rdn_wreg(card, EVERGREEN_DC_LUT_RW_INDEX, 0);
	for (i = 0; i < 256; i++) {
		/* 10 bits per channel: red, green, blue */
		uint32_t v = i << 2;

		rdn_wreg(card, EVERGREEN_DC_LUT_30_COLOR,
			 (v << 20) | (v << 10) | v);
	}

	rdn_wreg(card, NI_DEGAMMA_CONTROL,
		 (NI_GRPH_DEGAMMA_MODE(NI_DEGAMMA_BYPASS) |
		  NI_OVL_DEGAMMA_MODE(NI_DEGAMMA_BYPASS) |
		  NI_ICON_DEGAMMA_MODE(NI_DEGAMMA_BYPASS) |
		  NI_CURSOR_DEGAMMA_MODE(NI_DEGAMMA_BYPASS)));
	rdn_wreg(card, NI_GAMUT_REMAP_CONTROL,
		 (NI_GRPH_GAMUT_REMAP_MODE(NI_GAMUT_REMAP_BYPASS) |
		  NI_OVL_GAMUT_REMAP_MODE(NI_GAMUT_REMAP_BYPASS)));
	rdn_wreg(card, NI_REGAMMA_CONTROL,
		 (NI_GRPH_REGAMMA_MODE(NI_REGAMMA_BYPASS) |
		  NI_OVL_REGAMMA_MODE(NI_REGAMMA_BYPASS)));
	rdn_wreg(card, NI_OUTPUT_CSC_CONTROL,
		 (NI_OUTPUT_CSC_GRPH_MODE(NI_OUTPUT_CSC_BYPASS) |
		  NI_OUTPUT_CSC_OVL_MODE(NI_OUTPUT_CSC_BYPASS)));
	/* XXX match this to the depth of the crtc fmt block, move to modeset? */
	rdn_wreg(card, 0x6940, 0);
}

/*
 * The CEA-861 formats the AVI infoframe can name by number. A mode is one
 * of them only if every timing and both sync polarities agree; the clock
 * may be the 1000/1001 variant. The last field is the picture aspect code
 * (1 is 4:3, 2 is 16:9).
 */
static const struct {
	uint8_t vic, aspect;
	struct rdn_mode mode;
} cea_modes[] = {
	{ 1, 1, { 25175, 640, 656, 752, 800, 480, 490, 492, 525,
		  RDN_MODE_NHSYNC | RDN_MODE_NVSYNC } },
	{ 2, 1, { 27000, 720, 736, 798, 858, 480, 489, 495, 525,
		  RDN_MODE_NHSYNC | RDN_MODE_NVSYNC } },
	{ 4, 2, { 74250, 1280, 1390, 1430, 1650, 720, 725, 730, 750, 0 } },
	{ 16, 2, { 148500, 1920, 2008, 2052, 2200, 1080, 1084, 1089, 1125, 0 } },
	{ 17, 1, { 27000, 720, 732, 796, 864, 576, 581, 586, 625,
		   RDN_MODE_NHSYNC | RDN_MODE_NVSYNC } },
	{ 19, 2, { 74250, 1280, 1720, 1760, 1980, 720, 725, 730, 750, 0 } },
	{ 31, 2, { 148500, 1920, 2448, 2492, 2640, 1080, 1084, 1089, 1125, 0 } },
};

static bool cea_match(const struct rdn_mode *mode, uint8_t *vic,
		      uint8_t *aspect)
{
	unsigned i;

	for (i = 0; i < sizeof(cea_modes) / sizeof(cea_modes[0]); i++) {
		const struct rdn_mode *c = &cea_modes[i].mode;
		uint32_t low = c->clock - c->clock / 1001 - 5;

		if (mode->clock < low || mode->clock > c->clock + 5 ||
		    mode->hdisplay != c->hdisplay ||
		    mode->hsync_start != c->hsync_start ||
		    mode->hsync_end != c->hsync_end ||
		    mode->htotal != c->htotal ||
		    mode->vdisplay != c->vdisplay ||
		    mode->vsync_start != c->vsync_start ||
		    mode->vsync_end != c->vsync_end ||
		    mode->vtotal != c->vtotal || mode->flags != c->flags)
			continue;
		*vic = cea_modes[i].vic;
		*aspect = cea_modes[i].aspect;
		return true;
	}
	return false;
}

/*
 * The video half of radeon_audio_hdmi_mode_set() for DCE4 and later
 * (evergreen_hdmi.c): general control packets, no deep colour, and the AVI
 * infoframe, which tells the display what the picture is. A display may
 * refuse an HDMI signal that has none. The picture is muted meanwhile.
 * Not done: audio (clock regeneration, audio packets and infoframe).
 *
 * The infoframe is what drm_hdmi_avi_infoframe_from_display_mode() builds:
 * RGB, underscanned, active picture as coded, the CEA format number when
 * the mode is one. Its checksum makes header and payload sum to zero.
 */
static void hdmi_mode_set(struct rdn_card *card, const struct rdn_mode *mode)
{
	uint8_t frame[14], vic = 0, aspect = 0, sum;
	uint32_t tmp;
	unsigned i;

	memset(frame, 0, sizeof(frame));
	cea_match(mode, &vic, &aspect);
	frame[1] = (1 << 4) | 2;	/* active format valid, underscan */
	frame[2] = (uint8_t)(aspect << 4) | 8;	/* active as the picture */
	frame[4] = vic;
	sum = 0x82 + 2 + 13;		/* type, version, length */
	for (i = 1; i < sizeof(frame); i++)
		sum = (uint8_t)(sum + frame[i]);
	frame[0] = (uint8_t)(0x100 - sum);

	/* dce4_set_mute() */
	rdn_wreg(card, HDMI_GC, rdn_rreg(card, HDMI_GC) | HDMI_GC_AVMUTE);

	/* dce4_set_vbi_packet() */
	rdn_wreg(card, HDMI_VBI_PACKET_CONTROL,
		 HDMI_NULL_SEND | HDMI_GC_SEND | HDMI_GC_CONT);

	/* dce4_hdmi_set_color_depth(), 8 bits per colour */
	tmp = rdn_rreg(card, HDMI_CONTROL);
	tmp &= ~(HDMI_DEEP_COLOR_ENABLE | HDMI_DEEP_COLOR_DEPTH_MASK);
	rdn_wreg(card, HDMI_CONTROL, tmp);

	/* evergreen_set_avi_packet(); the top byte is the version */
	rdn_wreg(card, AFMT_AVI_INFO0, frame[0] | (frame[1] << 8) |
		 (frame[2] << 16) | ((uint32_t)frame[3] << 24));
	rdn_wreg(card, AFMT_AVI_INFO1, frame[4] | (frame[5] << 8) |
		 (frame[6] << 16) | ((uint32_t)frame[7] << 24));
	rdn_wreg(card, AFMT_AVI_INFO2, frame[8] | (frame[9] << 8) |
		 (frame[10] << 16) | ((uint32_t)frame[11] << 24));
	rdn_wreg(card, AFMT_AVI_INFO3, frame[12] | (frame[13] << 8) |
		 ((uint32_t)2 << 24));
	tmp = rdn_rreg(card, HDMI_INFOFRAME_CONTROL1);
	tmp &= ~HDMI_AVI_INFO_LINE_MASK;
	rdn_wreg(card, HDMI_INFOFRAME_CONTROL1, tmp | HDMI_AVI_INFO_LINE(2));

	rdn_wreg(card, HDMI_GC, rdn_rreg(card, HDMI_GC) & ~HDMI_GC_AVMUTE);

	rdn_log(card->os, RDN_LOG_INFO, "HDMI AVI infoframe, format %u",
		(unsigned)vic);
}

/*
 * evergreen_hdmi_enable() for a display without audio: send the AVI
 * infoframe with every frame. With DVI signalling nothing is sent.
 */
static void hdmi_enable(struct rdn_card *card, bool enable)
{
	rdn_wreg(card, HDMI_INFOFRAME_CONTROL0,
		 enable ? HDMI_AVI_INFO_SEND | HDMI_AVI_INFO_CONT : 0);
}

/*
 * atombios_dig_encoder_setup2(), DIG_ENCODER_CONTROL_PARAMETERS_V4.
 * acConfig: bits 0-1 DisplayPort link rate, bits 4-6 the digital encoder.
 * For the panel-mode action Linux tests the panel mode byte as if it were
 * an encoder mode, finds "DisplayPort" (0) and sends the DisplayPort lane
 * count, which is 0 on a TMDS output. The training actions of DisplayPort
 * take the same block as the setup.
 */
static int dig_encoder_setup(struct rdn_card *card, const struct rdn_mode *mode,
			     int action, int encoder_mode)
{
	int index = GetIndexIntoMasterTable(COMMAND, DIGxEncoderControl);
	const struct rdn_output *out = card_output(card);
	bool dp = output_is_dp(card);
	uint8_t config = (uint8_t)(out->dig_encoder << 4);
	struct ps ps;

	if (cmd_table_check(card, index, "DIGxEncoderControl", 1, 4))
		return -EINVAL;
	if (dp && card->dp.rate == 270000)
		config |= ATOM_ENCODER_CONFIG_V4_DPLINKRATE_2_70GHZ;
	ps_init(&ps);
	ps_le16(&ps, 0, mode->clock / 10);		/* usPixelClock */
	ps_u8(&ps, 2, config);				/* acConfig */
	ps_u8(&ps, 3, action);				/* ucAction */
	if (action == ATOM_ENCODER_CMD_SETUP_PANEL_MODE) {
		ps_u8(&ps, 4, DP_PANEL_MODE_EXTERNAL_DP_MODE); /* ucPanelMode */
		ps_u8(&ps, 5, dp ? card->dp.lanes : 0);	/* ucLaneNum */
	} else {
		ps_u8(&ps, 4, encoder_mode);		/* ucEncoderMode */
		ps_u8(&ps, 5, dp ? card->dp.lanes : 4);	/* ucLaneNum */
	}
	ps_u8(&ps, 6, PANEL_8BIT_PER_COLOR);		/* ucBitPerColor */
	ps_u8(&ps, 7, out->hpd + 1);			/* ucHPD_ID */
	return ps_exec(card, index, &ps);
}

/*
 * atombios_dig_transmitter_setup2(), DIG_TRANSMITTER_CONTROL_PARAMETERS_V4.
 * acConfig: bit 0 dual link, bit 1 coherent mode, bit 2 link B, bit 3
 * encoder select, bits 4-5 reference clock source (the PLL), bits 6-7
 * transmitter select. For DisplayPort the clock is the link's. The action
 * SETUP_VSEMPH takes a lane (0: all at once) and `lane_set`, the voltage
 * swing and pre-emphasis as the sink's TRAINING_LANEx_SET register has
 * them, where the other actions take the clock.
 */
static int dig_transmitter_setup(struct rdn_card *card,
				 const struct rdn_mode *mode, int action,
				 uint8_t lane_set)
{
	int index = GetIndexIntoMasterTable(COMMAND, UNIPHYTransmitterControl);
	const struct rdn_output *out = card_output(card);
	bool dp = output_is_dp(card);
	uint8_t config = 0;
	struct ps ps;

	if (cmd_table_check(card, index, "UNIPHYTransmitterControl", 1, 4))
		return -EINVAL;
	config |= 1 << 1;				/* fCoherentMode */
	config |= (out->dig_encoder & 1) << 3;		/* ucEncoderSel */
	config |= (output_pll(card) & 3) << 4;		/* ucRefClkSource */
	config |= out->transmitter_sel << 6;		/* ucTransmitterSel */
	ps_init(&ps);
	if (action == ATOM_TRANSMITTER_ACTION_SETUP_VSEMPH) {
		ps_u8(&ps, 0, 0);			/* asMode.ucLaneSel */
		ps_u8(&ps, 1, lane_set);		/* asMode.ucLaneSet */
	} else {
		/* usPixelClock */
		ps_le16(&ps, 0, (dp ? card->dp.rate : mode->clock) / 10);
	}
	ps_u8(&ps, 2, config);				/* acConfig */
	ps_u8(&ps, 3, action);				/* ucAction */
	ps_u8(&ps, 4, dp ? card->dp.lanes : 4);		/* ucLaneNum */
	return ps_exec(card, index, &ps);
}

/* What rdn_dp_link_train() asks of the source. */
struct train_ctx {
	struct rdn_card *card;
	const struct rdn_mode *mode;
};

/* radeon_dp_set_tp() and the start and end of training, source side */
static int train_pattern(void *ctx, int pattern)
{
	static const uint8_t action[] = {
		ATOM_ENCODER_CMD_DP_LINK_TRAINING_START,
		ATOM_ENCODER_CMD_DP_LINK_TRAINING_PATTERN1,
		ATOM_ENCODER_CMD_DP_LINK_TRAINING_PATTERN2,
		ATOM_ENCODER_CMD_DP_LINK_TRAINING_PATTERN3,
		ATOM_ENCODER_CMD_DP_LINK_TRAINING_COMPLETE,
	};
	struct train_ctx *t = ctx;

	return dig_encoder_setup(t->card, t->mode, action[pattern],
				 ATOM_ENCODER_MODE_DP);
}

static int train_drive(void *ctx, uint8_t lane_set)
{
	struct train_ctx *t = ctx;

	return dig_transmitter_setup(t->card, t->mode,
				     ATOM_TRANSMITTER_ACTION_SETUP_VSEMPH,
				     lane_set);
}

/* atombios_dig_transmitter_setup(ATOM_TRANSMITTER_ACTION_INIT) */
static int dig_transmitter_init(struct rdn_card *card,
				const struct rdn_output *out)
{
	int index = GetIndexIntoMasterTable(COMMAND, UNIPHYTransmitterControl);
	struct ps ps;

	if (cmd_table_check(card, index, "UNIPHYTransmitterControl", 1, 4))
		return -EINVAL;
	ps_init(&ps);
	ps_le16(&ps, 0, out->connector_id);		/* usInitInfo */
	/* acConfig: coherent mode and the transmitter; no encoder yet */
	ps_u8(&ps, 2, (1 << 1) | (out->transmitter_sel << 6));
	ps_u8(&ps, 3, ATOM_TRANSMITTER_ACTION_INIT);	/* ucAction */
	ps_u8(&ps, 4, 4);				/* ucLaneNum */
	return ps_exec(card, index, &ps);
}

/*
 * radeon_dp_getsinktype(), DP_ENCODER_SERVICE_PARAMETERS: what the BIOS
 * finds on a DisplayPort connector, as a connector object id.
 * CONNECTOR_OBJECT_ID_DISPLAYPORT is a DisplayPort sink; anything else is a
 * passive adapter to DVI or HDMI, or nothing.
 */
static int dp_sink_type(struct rdn_card *card, const struct rdn_output *out)
{
	int index = GetIndexIntoMasterTable(COMMAND, DPEncoderService);
	struct ps ps;

	ps_init(&ps);
	ps_u8(&ps, 2, out->i2c_id);			/* ucConfig */
	ps_u8(&ps, 3, ATOM_DP_ACTION_GET_SINK_TYPE);	/* ucAction */
	if (ps_exec(card, index, &ps))
		return -EINVAL;
	return ps.b[4];					/* ucStatus */
}

void rdn_output_select(struct rdn_card *card, const struct rdn_output *out)
{
	card->output = out;
}

/*
 * The EDID of the display on `out`. On the DisplayPort connector only a
 * DisplayPort sink is looked for: a passive adapter to DVI or HDMI, which
 * would answer as plain I2C on the same pins, is not supported.
 */
static int output_edid(struct rdn_card *card, const struct rdn_output *out,
		       uint8_t *edid)
{
	struct rdn_i2c_bus bus;
	int r;

	if (!out->displayport) {
		rdn_i2c_bus_by_id(card, out->i2c_id, &bus);
		return rdn_edid_read(card, &bus, edid);
	}
	rdn_log(card->os, RDN_LOG_INFO, "%s: the BIOS reports sink type 0x%02x",
		out->name, dp_sink_type(card, out));
	r = rdn_dp_detect(card, out);
	if (r)
		return r;
	r = rdn_dp_edid_read(card, out, edid);
	if (r < 0)
		card->dp.sink = false;
	return r;
}

int rdn_output_detect(struct rdn_card *card, uint8_t *edid)
{
	int r = -ENODEV, i;

	for (i = 0; rdn_output(i); i++) {
		r = output_edid(card, rdn_output(i), edid);
		if (r > 0) {
			rdn_output_select(card, rdn_output(i));
			return r;
		}
		rdn_log(card->os, RDN_LOG_INFO, "%s: no EDID (%d)",
			rdn_output(i)->name, r);
	}
	return r < 0 ? r : -ENODEV;
}

/*
 * What Linux does once at driver start, before any mode is set
 * (radeon_atom_encoder_init() and radeon_atom_disp_eng_pll_init()): tell
 * the BIOS which connector each transmitter serves, and start the display
 * engine clock. Without that clock the CRTC timing is wrong even though
 * every modeset register holds the right value.
 */
int rdn_display_init(struct rdn_card *card)
{
	struct atom_context *ctx = card->atom.ctx;
	ATOM_FIRMWARE_INFO_V2_1 *info;
	uint32_t dispclk;
	uint16_t data_offset;
	struct ps ps;
	int index, r;

	/* Every transmitter, in Linux's order: DisplayPort's, then DVI's. */
	r = dig_transmitter_init(card, &outputs[RDN_OUTPUT_DP]);
	if (r)
		return r;
	r = dig_transmitter_init(card, &outputs[RDN_OUTPUT_DVI]);
	if (r)
		return r;

	/* atombios_crtc_set_disp_eng_pll(), PIXEL_CLOCK_PARAMETERS_V6 */
	if (!atom_parse_data_header(ctx, GetIndexIntoMasterTable(DATA, FirmwareInfo),
				    NULL, NULL, NULL, &data_offset))
		return -EINVAL;
	info = (ATOM_FIRMWARE_INFO_V2_1 *)((uint8_t *)ctx->bios + data_offset);
	dispclk = le32_to_cpu(info->ulDefaultDispEngineClkFreq);
	if (dispclk == 0)
		dispclk = 54000;	/* 540 MHz */

	index = GetIndexIntoMasterTable(COMMAND, SetPixelClock);
	if (cmd_table_check(card, index, "SetPixelClock", 1, 6))
		return -EINVAL;
	ps_init(&ps);
	ps_le32(&ps, 0, dispclk);			/* ulDispEngClkFreq */
	ps_u8(&ps, 8, ATOM_DCPLL);			/* ucPpll */
	rdn_log(card->os, RDN_LOG_INFO, "display engine clock %u0 kHz",
		(unsigned)dispclk);
	return ps_exec(card, index, &ps);
}

int rdn_modeset(struct rdn_card *card, const struct rdn_mode *mode,
		const struct rdn_fb *fb, bool hdmi)
{
	int encoder_mode = hdmi ? ATOM_ENCODER_MODE_HDMI : ATOM_ENCODER_MODE_DVI;
	uint32_t adjusted_clock, fb_div, frac_fb_div, ref_div, post_div;
	const struct rdn_output *out = card_output(card);
	bool dp = output_is_dp(card);
	struct rdn_pll pll;
	int r;

	if (out->displayport && !dp) {
		rdn_log(card->os, RDN_LOG_ERROR,
			"%s: no DisplayPort sink was detected", out->name);
		return -ENODEV;
	}
	if (dp) {
		/* radeon_dp_set_link_config() */
		hdmi = false;
		encoder_mode = ATOM_ENCODER_MODE_DP;
		r = rdn_dp_link_config(card, mode);
		if (r)
			return r;
	}

	r = get_pll_info(card, &pll);
	if (r)
		return r;

	/* Mode fixup and encoder prepare */
	adjusted_clock = adjust_pll(card, mode, encoder_mode, &pll);
	if (!adjusted_clock)
		return -EINVAL;
	r = select_crtc_source(card, encoder_mode);
	if (r)
		return r;

	/* CRTC prepare: lock, then off */
	r = lock_crtc(card, ATOM_ENABLE);
	if (r)
		return r;
	/* Changing mode on a running CRTC: blank it first, as Linux does. */
	if (card->crtc_on)
		blank_crtc(card, ATOM_ENABLE);
	card->crtc_on = false;
	enable_crtc_memreq(card, ATOM_DISABLE);
	enable_crtc(card, ATOM_DISABLE);

	/* CRTC mode set */
	compute_pll(&pll, adjusted_clock, &fb_div, &frac_fb_div, &ref_div,
		    &post_div);
	rdn_log(card->os, RDN_LOG_INFO,
		"mode %ux%u, %u kHz: PLL %u kHz, fb %u.%u ref %u post %u",
		mode->hdisplay, mode->vdisplay, (unsigned)mode->clock,
		(unsigned)adjusted_clock, (unsigned)fb_div, (unsigned)frac_fb_div,
		(unsigned)ref_div, (unsigned)post_div);
	disable_spread_spectrum(card);
	r = program_pll(card, mode, encoder_mode, ref_div, fb_div, frac_fb_div,
			post_div);
	if (r)
		goto unlock;
	r = set_dtd_timing(card, mode);
	if (r)
		goto unlock;
	set_base(card, mode, fb);
	/* radeon_bandwidth_update() */
	card->wm_clock = mode->clock;
	card->wm_hdisplay = mode->hdisplay;
	card->wm_htotal = mode->htotal;
	rdn_bandwidth_update(card);
	overscan_setup(card);
	scaler_setup(card);

	/* Encoder prepare: output off while the CRTC comes up */
	if (dp)
		dig_encoder_setup(card, mode, ATOM_ENCODER_CMD_DP_VIDEO_OFF,
				  encoder_mode);
	dig_transmitter_setup(card, mode, ATOM_TRANSMITTER_ACTION_DISABLE, 0);
	if (hdmi)
		hdmi_mode_set(card, mode);

	/* CRTC commit: on, unblanked, unlocked */
	enable_crtc(card, ATOM_ENABLE);
	enable_crtc_memreq(card, ATOM_ENABLE);
	blank_crtc(card, ATOM_DISABLE);
	load_lut(card);
	card->crtc_on = true;
unlock:
	lock_crtc(card, ATOM_DISABLE);
	if (r)
		return r;

	/* Encoder commit */
	if (!dp)
		hdmi_enable(card, hdmi);
	r = dig_encoder_setup(card, mode, ATOM_ENCODER_CMD_SETUP, encoder_mode);
	if (r)
		return r;
	dig_encoder_setup(card, mode, ATOM_ENCODER_CMD_SETUP_PANEL_MODE,
			  encoder_mode);
	r = dig_transmitter_setup(card, mode, ATOM_TRANSMITTER_ACTION_ENABLE, 0);
	if (r || !dp)
		return r;

	/*
	 * DisplayPort: train the link, then let the picture out. Linux
	 * turns the video on whether training succeeded or not, and so
	 * does this; the failure is still returned.
	 */
	{
		struct train_ctx ctx = { card, mode };
		struct rdn_dp_source src = { &ctx, train_pattern, train_drive };
		int v;

		r = rdn_dp_link_train(card, out, &src);
		v = dig_encoder_setup(card, mode, ATOM_ENCODER_CMD_DP_VIDEO_ON,
				      encoder_mode);
		return r ? r : v;
	}
}
