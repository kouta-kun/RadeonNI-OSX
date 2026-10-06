/*
 * Engine and memory clocks: the PowerPlay table and the static switch
 * between two of its states.
 *
 * Follows the Linux radeon driver: radeon_set_power_state() in
 * radeon_pm.c, evergreen_pm_misc(), evergreen_pm_prepare(),
 * evergreen_pm_finish() and evergreen_get_temp() in evergreen.c,
 * radeon_atom_set_engine_clock(), radeon_atom_set_memory_clock(),
 * radeon_atom_set_voltage() and the PowerPlay parsing in
 * radeon_atombios.c. Unlike Linux's profile method this does not hold the
 * clocks down to FirmwareInfo's defaults, which on this card are the slow
 * boot clocks.
 *
 * Copyright 2007-8 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
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

#include <string.h>

#include "rdn_pm.h"
#include "rdn_reg.h"
#include "atom/atombios.h"

#ifndef EBUSY
#define EBUSY 16
#endif

#define CG_MULT_THERMAL_STATUS			0x740
#define		ASIC_T_MASK			0x07FF0000
#define		ASIC_T_SHIFT			16
#define EVERGREEN_CRTC_DISP_READ_REQUEST_DISABLE	(1 << 24)

/* ATOM_PPLIB_POWERPLAYTABLE, by offset: the table is little-endian. */
#define PP_NUM_STATES			5
#define PP_STATE_ENTRY_SIZE		6
#define PP_CLOCK_INFO_SIZE		7
#define PP_NON_CLOCK_SIZE		8
#define PP_STATE_ARRAY_OFFSET		9
#define PP_CLOCK_INFO_ARRAY_OFFSET	11
#define PP_NON_CLOCK_ARRAY_OFFSET	13
#define PP_HEADER_SIZE			15

/* ATOM_PPLIB_EVERGREEN_CLOCK_INFO: clocks are 24 bits, low word first. */
#define PP_CLOCK_SCLK			0
#define PP_CLOCK_MCLK			3
#define PP_CLOCK_VDDC			6
#define PP_CLOCK_VDDCI			8
#define PP_EVERGREEN_CLOCK_SIZE		12

#define PP_CLASSIFICATION_BOOT		0x0008

/* ATOM_FIRMWARE_INFO_V2_2 */
#define FWI_DEFAULT_SCLK		8
#define FWI_DEFAULT_MCLK		12
#define FWI_BOOT_VDDC			46
#define FWI_V2_2_SIZE			48

static unsigned get_le16(const uint8_t *p)
{
	return p[0] | (p[1] << 8);
}

static uint32_t get_le24(const uint8_t *p)
{
	return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16);
}

static uint32_t get_le32(const uint8_t *p)
{
	return get_le24(p) | ((uint32_t)p[3] << 24);
}

static const uint8_t *data_table(struct rdn_card *card, int index,
				 uint16_t *size)
{
	struct atom_context *ctx = card->atom.ctx;
	uint16_t offset;

	if (!atom_parse_data_header(ctx, index, size, NULL, NULL, &offset))
		return NULL;
	return (const uint8_t *)ctx->bios + offset;
}

int rdn_pm_boot_state(struct rdn_card *card, struct rdn_pm_state *state)
{
	uint16_t size = 0;
	const uint8_t *info = data_table(card,
		GetIndexIntoMasterTable(DATA, FirmwareInfo), &size);

	if (!info || size < FWI_V2_2_SIZE)
		return -EINVAL;
	memset(state, 0, sizeof(*state));
	state->sclk = get_le32(info + FWI_DEFAULT_SCLK);
	state->mclk = get_le32(info + FWI_DEFAULT_MCLK);
	state->vddc = (uint16_t)get_le16(info + FWI_BOOT_VDDC);
	return 0;
}

/*
 * Calls `visit` for every clock mode of every state but the boot state,
 * whose entry does not hold what the card boots with.
 */
static int pp_walk(struct rdn_card *card,
		   void (*visit)(void *arg, unsigned state, unsigned mode,
				 unsigned classification,
				 const struct rdn_pm_state *clocks),
		   void *arg)
{
	uint16_t size = 0;
	const uint8_t *pp = data_table(card,
		GetIndexIntoMasterTable(DATA, PowerPlayInfo), &size);
	unsigned states, entry, clock_size, non_clock_size;
	unsigned state_off, clock_off, non_clock_off, i, j;

	if (!pp || size < PP_HEADER_SIZE)
		return -EINVAL;
	states = pp[PP_NUM_STATES];
	entry = pp[PP_STATE_ENTRY_SIZE];
	clock_size = pp[PP_CLOCK_INFO_SIZE];
	non_clock_size = pp[PP_NON_CLOCK_SIZE];
	state_off = get_le16(pp + PP_STATE_ARRAY_OFFSET);
	clock_off = get_le16(pp + PP_CLOCK_INFO_ARRAY_OFFSET);
	non_clock_off = get_le16(pp + PP_NON_CLOCK_ARRAY_OFFSET);
	if (entry < 2 || clock_size < PP_EVERGREEN_CLOCK_SIZE ||
	    non_clock_size < 2 || state_off + states * entry > size)
		return -EINVAL;

	for (i = 0; i < states; i++) {
		const uint8_t *s = pp + state_off + i * entry;
		unsigned non_clock = non_clock_off + s[0] * non_clock_size;
		unsigned classification;

		if (non_clock + 2 > size)
			return -EINVAL;
		classification = get_le16(pp + non_clock);
		for (j = 1; j < entry; j++) {
			unsigned clock = clock_off + s[j] * clock_size;
			struct rdn_pm_state c;

			if (clock + PP_EVERGREEN_CLOCK_SIZE > size)
				return -EINVAL;
			c.sclk = get_le24(pp + clock + PP_CLOCK_SCLK);
			c.mclk = get_le24(pp + clock + PP_CLOCK_MCLK);
			c.vddc = (uint16_t)get_le16(pp + clock + PP_CLOCK_VDDC);
			c.vddci = (uint16_t)get_le16(pp + clock + PP_CLOCK_VDDCI);
			visit(arg, i, j - 1, classification, &c);
		}
	}
	return 0;
}

/* 0xff01 and its like are flags, not voltages (radeon_atombios.c). */
static bool is_voltage(unsigned mv)
{
	return mv && (mv & 0xff00) != 0xff00;
}

static void visit_best(void *arg, unsigned state, unsigned mode,
		       unsigned classification, const struct rdn_pm_state *c)
{
	struct rdn_pm_state *best = arg;

	if (classification & PP_CLASSIFICATION_BOOT)
		return;
	if (c->sclk > best->sclk ||
	    (c->sclk == best->sclk && c->mclk > best->mclk))
		*best = *c;
}

int rdn_pm_performance_state(struct rdn_card *card, struct rdn_pm_state *state)
{
	int r;

	memset(state, 0, sizeof(*state));
	r = pp_walk(card, visit_best, state);
	if (r)
		return r;
	if (!state->sclk || !state->mclk)
		return -EINVAL;
	if (!is_voltage(state->vddc))
		state->vddc = 0;
	if (!is_voltage(state->vddci))
		state->vddci = 0;
	return 0;
}

static void visit_log(void *arg, unsigned state, unsigned mode,
		      unsigned classification, const struct rdn_pm_state *c)
{
	struct rdn_card *card = arg;

	rdn_log(card->os, RDN_LOG_INFO,
		"power state %u (class 0x%04X) mode %u: engine %u0 kHz, memory %u0 kHz, vddc %u, vddci %u",
		state, classification, mode, (unsigned)c->sclk,
		(unsigned)c->mclk, c->vddc, c->vddci);
}

void rdn_pm_log_states(struct rdn_card *card)
{
	struct rdn_pm_state boot;

	if (!rdn_pm_boot_state(card, &boot))
		rdn_log(card->os, RDN_LOG_INFO,
			"boot state: engine %u0 kHz, memory %u0 kHz, vddc %u",
			(unsigned)boot.sclk, (unsigned)boot.mclk, boot.vddc);
	if (pp_walk(card, visit_log, card))
		rdn_log(card->os, RDN_LOG_ERROR, "PowerPlay table not understood");
}

/*
 * One 32-bit parameter, little-endian in the interpreter's word, and the
 * same word back.
 */
static int exec32(struct rdn_card *card, int index, uint32_t *value)
{
	uint32_t ps[4];
	int r;

	memset(ps, 0, sizeof(ps));
	ps[0] = cpu_to_le32(*value);
	r = atom_execute_table(card->atom.ctx, index, ps, 4);
	*value = le32_to_cpu(ps[0]);
	if (r)
		rdn_log(card->os, RDN_LOG_ERROR,
			"AtomBIOS command table %d failed (%d)", index, r);
	return r;
}

int rdn_pm_get_clocks(struct rdn_card *card, uint32_t *sclk, uint32_t *mclk)
{
	int r;

	*sclk = *mclk = 0;
	r = exec32(card, GetIndexIntoMasterTable(COMMAND, GetEngineClock), sclk);
	if (r)
		return r;
	return exec32(card, GetIndexIntoMasterTable(COMMAND, GetMemoryClock), mclk);
}

/* radeon_atom_set_voltage() */
static int set_voltage(struct rdn_card *card, unsigned mv, unsigned type)
{
	int index = GetIndexIntoMasterTable(COMMAND, SetVoltage);
	uint8_t frev = 0, crev = 0;
	uint32_t ps;

	if (!atom_parse_cmd_header(card->atom.ctx, index, &frev, &crev))
		return -EINVAL;
	/*
	 * Revisions 2 and 3 take the same block: type, mode (0: set), level
	 * in mV as a little-endian word. Revision 1 takes an index instead.
	 */
	if (crev != 2 && crev != 3) {
		rdn_log(card->os, RDN_LOG_ERROR,
			"SetVoltage is revision %d.%d, need 1.2 or 1.3", frev, crev);
		return -EINVAL;
	}
	ps = type | ((uint32_t)mv << 16);
	return exec32(card, index, &ps);
}

static int set_voltages(struct rdn_card *card, const struct rdn_pm_state *from,
			const struct rdn_pm_state *to)
{
	int r;

	if (is_voltage(to->vddc) && to->vddc != from->vddc) {
		r = set_voltage(card, to->vddc, SET_VOLTAGE_TYPE_ASIC_VDDC);
		if (r)
			return r;
	}
	if (is_voltage(to->vddci) && to->vddci != from->vddci) {
		r = set_voltage(card, to->vddci, SET_VOLTAGE_TYPE_ASIC_VDDCI);
		if (r)
			return r;
	}
	return 0;
}

/* evergreen_pm_prepare() and evergreen_pm_finish(), for the one CRTC in use. */
static void crtc_read_requests(struct rdn_card *card, bool on)
{
	uint32_t tmp;

	if (!card->crtc_on)
		return;
	tmp = rdn_rreg(card, EVERGREEN_CRTC_CONTROL);
	if (on)
		tmp &= ~EVERGREEN_CRTC_DISP_READ_REQUEST_DISABLE;
	else
		tmp |= EVERGREEN_CRTC_DISP_READ_REQUEST_DISABLE;
	rdn_wreg(card, EVERGREEN_CRTC_CONTROL, tmp);
}

int rdn_pm_set(struct rdn_card *card, const struct rdn_pm_state *from,
	       const struct rdn_pm_state *to, unsigned what)
{
	/* upvolt before raising clocks, downvolt after lowering clocks */
	bool voltage_after = to->sclk < from->sclk;
	uint32_t v, sclk, mclk;
	int r = 0;

	if (rdn_rreg(card, GRBM_STATUS) & GUI_ACTIVE)
		return -EBUSY;

	crtc_read_requests(card, false);

	if ((what & RDN_PM_VOLTAGE) && !voltage_after)
		r = set_voltages(card, from, to);
	if (!r && (what & RDN_PM_SCLK) && to->sclk != from->sclk) {
		v = to->sclk;
		r = exec32(card, GetIndexIntoMasterTable(COMMAND, SetEngineClock), &v);
	}
	if (!r && (what & RDN_PM_MCLK) && to->mclk != from->mclk) {
		v = to->mclk;
		r = exec32(card, GetIndexIntoMasterTable(COMMAND, SetMemoryClock), &v);
	}
	if (!r && (what & RDN_PM_VOLTAGE) && voltage_after)
		r = set_voltages(card, from, to);

	crtc_read_requests(card, true);

	/* The display's watermarks depend on both clocks: read them back. */
	if (!rdn_pm_get_clocks(card, &sclk, &mclk) && sclk && mclk) {
		/* The VBIOS reads 649.96 MHz for 650: round to the MHz. */
		card->sclk = (sclk + 50) / 100 * 100;
		card->mclk = (mclk + 50) / 100 * 100;
		rdn_bandwidth_update(card);
	}
	return r;
}

/* evergreen_get_temp(), the branch for everything but Juniper */
int rdn_pm_temperature(struct rdn_card *card)
{
	uint32_t temp = (rdn_rreg(card, CG_MULT_THERMAL_STATUS) & ASIC_T_MASK) >>
			ASIC_T_SHIFT;
	int actual;

	if (temp & 0x400)
		actual = -256;
	else if (temp & 0x200)
		actual = 255;
	else if (temp & 0x100)
		actual = (int)(temp & 0x1ff) - 0x200;
	else
		actual = temp & 0xff;
	return actual * 1000 / 2;
}
