/*
 * EDID interpretation: preferred timing and HDMI detection.
 * Written from the VESA E-EDID and CEA-861 layouts.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include "rdn_mode.h"

#define EDID_DTD_OFFSET		54
#define EDID_DTD_SIZE		18
#define EDID_DTD_COUNT		4
#define EDID_BLOCK		128
#define CEA_EXT_TAG		0x02
#define CEA_VENDOR_BLOCK	3

bool rdn_edid_preferred_mode(const uint8_t *edid, struct rdn_mode *mode)
{
	return rdn_edid_detailed_mode(edid, 0, mode);
}

/* A detailed timing from its 18-byte descriptor. False if it is not a timing. */
static bool dtd_mode(const uint8_t *d, struct rdn_mode *mode)
{
	uint32_t clock;
	uint16_t hact, hblank, vact, vblank, hso, hsw, vso, vsw;

	/* A descriptor that is not a timing has a zero pixel clock. */
	clock = (uint32_t)(d[0] | (d[1] << 8)) * 10;
	if (!clock)
		return false;
	/* No interlaced scanning here, and no pixel clock a single link cannot carry. */
	if ((d[17] & 0x80) || clock > RDN_MODE_MAX_CLOCK)
		return false;

	hact = (uint16_t)(d[2] | ((d[4] >> 4) << 8));
	hblank = (uint16_t)(d[3] | ((d[4] & 0xf) << 8));
	vact = (uint16_t)(d[5] | ((d[7] >> 4) << 8));
	vblank = (uint16_t)(d[6] | ((d[7] & 0xf) << 8));
	hso = (uint16_t)(d[8] | (((d[11] >> 6) & 3) << 8));
	hsw = (uint16_t)(d[9] | (((d[11] >> 4) & 3) << 8));
	vso = (uint16_t)((d[10] >> 4) | (((d[11] >> 2) & 3) << 4));
	vsw = (uint16_t)((d[10] & 0xf) | ((d[11] & 3) << 4));

	memset(mode, 0, sizeof(*mode));
	mode->clock = clock;
	mode->hdisplay = hact;
	mode->hsync_start = (uint16_t)(hact + hso);
	mode->hsync_end = (uint16_t)(hact + hso + hsw);
	mode->htotal = (uint16_t)(hact + hblank);
	mode->vdisplay = vact;
	mode->vsync_start = (uint16_t)(vact + vso);
	mode->vsync_end = (uint16_t)(vact + vso + vsw);
	mode->vtotal = (uint16_t)(vact + vblank);

	/* Digital separate sync: bit 1 is horizontal, bit 2 vertical polarity. */
	if ((d[17] & 0x18) == 0x18) {
		if (!(d[17] & 0x02))
			mode->flags |= RDN_MODE_NHSYNC;
		if (!(d[17] & 0x04))
			mode->flags |= RDN_MODE_NVSYNC;
	}
	return true;
}

bool rdn_edid_detailed_mode(const uint8_t *edid, int index,
			    struct rdn_mode *mode)
{
	if (index < 0 || index >= EDID_DTD_COUNT)
		return false;
	return dtd_mode(edid + EDID_DTD_OFFSET + index * EDID_DTD_SIZE, mode);
}

bool rdn_edid_is_hdmi(const uint8_t *edid, int len)
{
	int b, i, end;

	for (b = 1; (b + 1) * EDID_BLOCK <= len; b++) {
		const uint8_t *ext = edid + b * EDID_BLOCK;

		if (ext[0] != CEA_EXT_TAG)
			continue;
		/* Data blocks run from byte 4 up to the detailed timings. */
		end = ext[2];
		if (end > EDID_BLOCK - 1)
			end = EDID_BLOCK - 1;
		for (i = 4; i < end; i += (ext[i] & 0x1f) + 1) {
			int tag = ext[i] >> 5, size = ext[i] & 0x1f;

			/* HDMI Licensing IEEE OUI 00-0C-03, stored LSB first. */
			if (tag == CEA_VENDOR_BLOCK && size >= 3 &&
			    i + 3 < EDID_BLOCK && ext[i + 1] == 0x03 &&
			    ext[i + 2] == 0x0c && ext[i + 3] == 0x00)
				return true;
		}
	}
	return false;
}


/*
 * The list of modes an EDID offers: what the monitor says it takes, and a
 * few every program expects. In this order, without repeats (the same size
 * within a hertz of the same refresh rate counts as the same mode):
 *  1. the detailed timings, the base block's first (the preferred one,
 *     mode 1) and then those of CEA extensions;
 *  2. the established timings (bytes 35 to 37) and the standard timings
 *     (bytes 38 to 53) that VESA DMT has numbers for;
 *  3. CEA video codes that are progressive;
 *  4. 640x480, 800x600 and 1024x768 at 60 Hz if the monitor's range limits
 *     allow it: every monitor with a scaler shows them, few list them, and
 *     Quake 3 and Doom 3 start in one of them.
 */
extern const struct rdn_mode rdn_dmt_modes[];
extern const unsigned rdn_dmt_mode_count;

static unsigned mode_hz(const struct rdn_mode *m)
{
	uint32_t total = (uint32_t)m->htotal * m->vtotal;

	return total ? (unsigned)(((uint64_t)m->clock * 1000 + total / 2) / total) : 0;
}

static bool have_mode(const struct rdn_mode *list, int n, const struct rdn_mode *m)
{
	int i;

	for (i = 0; i < n; i++) {
		unsigned a = mode_hz(&list[i]), b = mode_hz(m);

		if (list[i].hdisplay == m->hdisplay && list[i].vdisplay == m->vdisplay &&
		    (a > b ? a - b : b - a) <= 1)
			return true;
	}
	return false;
}

static void add_mode(struct rdn_mode *out, int *n, int max, const struct rdn_mode *m)
{
	if (*n >= max || have_mode(out, *n, m))
		return;
	out[(*n)++] = *m;
}

/* The DMT timing of a size and rate (to a hertz), or NULL. */
static const struct rdn_mode *dmt_find(unsigned w, unsigned h, unsigned hz)
{
	const struct rdn_mode *best = NULL;
	unsigned i;

	/*
	 * Of the timings of that size and rate (the ordinary one and the
	 * reduced blanking one, which a monitor must say it takes), the one
	 * with the longer blanking: every monitor takes it.
	 */
	for (i = 0; i < rdn_dmt_mode_count; i++) {
		const struct rdn_mode *m = &rdn_dmt_modes[i];
		unsigned r = mode_hz(m);

		if (m->hdisplay == w && m->vdisplay == h && (r > hz ? r - hz : hz - r) <= 1 &&
		    (!best || m->htotal - m->hdisplay > best->htotal - best->hdisplay))
			best = m;
	}
	return best;
}

/* The monitor's range limits (a display range descriptor); false if it has none. */
static bool edid_ranges(const uint8_t *edid, unsigned *vmin, unsigned *vmax,
			unsigned *hmin, unsigned *hmax, unsigned *clock_khz)
{
	int i;

	for (i = 0; i < EDID_DTD_COUNT; i++) {
		const uint8_t *d = edid + EDID_DTD_OFFSET + i * EDID_DTD_SIZE;

		if (d[0] || d[1] || d[3] != 0xfd)
			continue;
		*vmin = d[5];
		*vmax = d[6];
		*hmin = d[7];
		*hmax = d[8];
		*clock_khz = d[9] == 0xff ? 0 : (unsigned)d[9] * 10000;
		return true;
	}
	return false;
}

static bool in_ranges(const uint8_t *edid, const struct rdn_mode *m)
{
	unsigned vmin, vmax, hmin, hmax, clock;
	unsigned hz = mode_hz(m), khz = m->htotal ? m->clock / m->htotal : 0;

	if (!edid_ranges(edid, &vmin, &vmax, &hmin, &hmax, &clock))
		return true;
	return hz >= vmin && hz <= vmax && khz >= hmin && khz <= hmax &&
	       (!clock || m->clock <= clock);
}

/* Progressive CEA-861 video codes the DMT table lacks. */
static const struct { uint8_t vic; struct rdn_mode m; } cea_modes[] = {
	{ 2,  { 27000, 720, 736, 798, 858, 480, 489, 495, 525, RDN_MODE_NHSYNC | RDN_MODE_NVSYNC } },
	{ 3,  { 27000, 720, 736, 798, 858, 480, 489, 495, 525, RDN_MODE_NHSYNC | RDN_MODE_NVSYNC } },
	{ 17, { 27000, 720, 732, 796, 864, 576, 581, 586, 625, RDN_MODE_NHSYNC | RDN_MODE_NVSYNC } },
	{ 18, { 27000, 720, 732, 796, 864, 576, 581, 586, 625, RDN_MODE_NHSYNC | RDN_MODE_NVSYNC } },
	{ 19, { 74250, 1280, 1720, 1760, 1980, 720, 725, 730, 750, 0 } },
	{ 31, { 148500, 1920, 2448, 2492, 2640, 1080, 1084, 1089, 1125, 0 } },
};

/*
 * Quartz gives a program that asks for a size without a rate the last mode
 * of that size in the list (the log showed Quake 3 getting 640x480 at
 * 75 Hz and 1920x1080 at 50 Hz while 60 Hz modes of those sizes were
 * there, and Doom 3's 800x600 at 60 Hz because that mode happened to be the
 * last). So in each group of modes of one size the one nearest 60 Hz goes
 * last. Every mode stays: a program that asks for a rate still gets it.
 * `*pref` is the index of the preferred mode and follows it.
 */
static unsigned hz_away(const struct rdn_mode *m)
{
	unsigned hz = mode_hz(m);

	return hz > 60 ? hz - 60 : 60 - hz;
}

static void order_for_quartz(struct rdn_mode *l, int n, int *pref)
{
	int i, j, best, last;

	for (i = 0; i < n; i++) {
		bool first = true;

		for (j = 0; j < i; j++)
			if (l[j].hdisplay == l[i].hdisplay && l[j].vdisplay == l[i].vdisplay)
				first = false;
		if (!first)
			continue;
		best = last = i;
		for (j = i + 1; j < n; j++) {
			if (l[j].hdisplay != l[i].hdisplay || l[j].vdisplay != l[i].vdisplay)
				continue;
			last = j;
			if (hz_away(&l[j]) < hz_away(&l[best]))
				best = j;
		}
		if (best != last) {
			struct rdn_mode t = l[best];

			l[best] = l[last];
			l[last] = t;
			if (*pref == best)
				*pref = last;
			else if (*pref == last)
				*pref = best;
		}
	}
}

int rdn_edid_modes(const uint8_t *edid, int len, struct rdn_mode *out, int max,
		   int *preferred)
{
	/* Established timings: bit 7 down to bit 0 of bytes 35 and 36; 0 is a size VESA DMT lacks. */
	static const struct { uint16_t w, h, hz; } est1[8] = {
		{ 0, 0, 0 }, { 0, 0, 0 }, { 640, 480, 60 }, { 0, 0, 0 },
		{ 640, 480, 72 }, { 640, 480, 75 }, { 800, 600, 56 }, { 800, 600, 60 },
	};
	static const struct { uint16_t w, h, hz; } est2[8] = {
		{ 800, 600, 72 }, { 800, 600, 75 }, { 0, 0, 0 }, { 0, 0, 0 },
		{ 1024, 768, 60 }, { 1024, 768, 70 }, { 1024, 768, 75 }, { 1280, 1024, 75 },
	};
	struct rdn_mode m;
	const struct rdn_mode *d;
	int n = 0, i, b, k, blocks = len / EDID_BLOCK;

	if (blocks < 1)
		return 0;

	/* 1: the detailed timings. */
	for (i = 0; i < EDID_DTD_COUNT; i++)
		if (rdn_edid_detailed_mode(edid, i, &m))
			add_mode(out, &n, max, &m);
	for (b = 1; b < blocks; b++) {
		const uint8_t *ext = edid + b * EDID_BLOCK;
		int dtd = ext[2];

		if (ext[0] != CEA_EXT_TAG || dtd < 4)
			continue;
		for (; dtd + EDID_DTD_SIZE <= EDID_BLOCK - 1; dtd += EDID_DTD_SIZE)
			if (dtd_mode(ext + dtd, &m))
				add_mode(out, &n, max, &m);
	}

	/* 2: established and standard timings. */
	for (k = 0; k < 8; k++) {
		if ((edid[35] & (0x80 >> k)) && est1[k].w &&
		    (d = dmt_find(est1[k].w, est1[k].h, est1[k].hz)))
			add_mode(out, &n, max, d);
		if ((edid[36] & (0x80 >> k)) && est2[k].w &&
		    (d = dmt_find(est2[k].w, est2[k].h, est2[k].hz)))
			add_mode(out, &n, max, d);
	}
	for (k = 0; k < 8; k++) {
		unsigned w, h, hz;
		const uint8_t *s = edid + 38 + 2 * k;

		if (s[0] == 0x01 && s[1] == 0x01)
			continue;
		w = ((unsigned)s[0] + 31) * 8;
		switch (s[1] >> 6) {
		case 0: h = edid[18] > 1 || edid[19] >= 3 ? w * 10 / 16 : w; break;
		case 1: h = w * 3 / 4; break;
		case 2: h = w * 4 / 5; break;
		default: h = w * 9 / 16; break;
		}
		hz = (s[1] & 0x3f) + 60;
		if ((d = dmt_find(w, h, hz)))
			add_mode(out, &n, max, d);
	}

	/* 3: CEA video codes. */
	for (b = 1; b < blocks; b++) {
		const uint8_t *ext = edid + b * EDID_BLOCK;
		int end = ext[2];

		if (ext[0] != CEA_EXT_TAG)
			continue;
		if (end > EDID_BLOCK - 1)
			end = EDID_BLOCK - 1;
		for (i = 4; i < end; i += (ext[i] & 0x1f) + 1) {
			if ((ext[i] >> 5) != 2)
				continue;
			for (k = 1; k <= (ext[i] & 0x1f) && i + k < end; k++) {
				unsigned vic = ext[i + k] & 0x7f, j;

				for (j = 0; j < sizeof(cea_modes) / sizeof(cea_modes[0]); j++)
					if (cea_modes[j].vic == vic)
						add_mode(out, &n, max, &cea_modes[j].m);
				/* The codes whose timings VESA DMT has as well. */
				if (vic == 1)
					d = dmt_find(640, 480, 60);
				else if (vic == 4)
					d = dmt_find(1280, 720, 60);
				else if (vic == 16)
					d = dmt_find(1920, 1080, 60);
				else
					d = NULL;
				if (d)
					add_mode(out, &n, max, d);
			}
		}
	}

	/* 4: the three every program expects. */
	if ((d = dmt_find(640, 480, 60)) && in_ranges(edid, d))
		add_mode(out, &n, max, d);
	if ((d = dmt_find(800, 600, 60)) && in_ranges(edid, d))
		add_mode(out, &n, max, d);
	if ((d = dmt_find(1024, 768, 60)) && in_ranges(edid, d))
		add_mode(out, &n, max, d);
	{
		int pref = 0;

		order_for_quartz(out, n, &pref);
		if (preferred)
			*preferred = pref;
	}
	return n;
}
