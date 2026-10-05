/*
 * Display modes, EDID interpretation and mode setting.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_MODE_H
#define RDN_MODE_H

#include "rdn_card.h"

#define RDN_MODE_NHSYNC		(1 << 0)	/* horizontal sync active low */
#define RDN_MODE_NVSYNC		(1 << 1)	/* vertical sync active low */

struct rdn_mode {
	uint32_t clock;		/* pixel clock in kHz */
	uint16_t hdisplay, hsync_start, hsync_end, htotal;
	uint16_t vdisplay, vsync_start, vsync_end, vtotal;
	uint32_t flags;
};

/*
 * The scanout surface. 32 bpp is XRGB8888, 16 bpp is XRGB1555, 8 bpp is
 * indexed through the colour table (rdn_lut_set).
 */
struct rdn_fb {
	uint32_t aperture_offset;	/* byte offset of pixel (0,0) in BAR0 */
	uint32_t width, height;
	uint32_t pitch_pixels;
	uint32_t bpp;			/* 8, 16 or 32; 0 means 32 */
	/*
	 * True when the host writes pixels as big-endian words; the card
	 * then swaps the bytes of each pixel on scanout.
	 */
	bool big_endian_pixels;
};

/* One entry of the 256-entry colour table, 10 bits per channel. */
struct rdn_lut_entry {
	uint16_t red, green, blue;
};

/*
 * Detailed timing number `index` (0 to 3) of the EDID's base block. False
 * if that descriptor is not a timing. Index 0 is the preferred mode.
 */
bool rdn_edid_detailed_mode(const uint8_t *edid, int index,
			    struct rdn_mode *mode);

/* The preferred (first detailed) timing of an EDID. False if there is none. */
bool rdn_edid_preferred_mode(const uint8_t *edid, struct rdn_mode *mode);

/* True when the EDID carries an HDMI vendor block in a CEA extension. */
bool rdn_edid_is_hdmi(const uint8_t *edid, int len);

/*
 * One-time display setup after the card is posted and before the first
 * rdn_modeset(): transmitter initialisation and the display engine clock.
 * Returns 0 or a negative errno value.
 */
int rdn_display_init(struct rdn_card *card);

/*
 * Set `mode` on CRTC 0 and drive it out of the DVI-I connector's digital
 * output, scanning out `fb`. The card must be posted and rdn_display_init()
 * must have run. `hdmi` selects HDMI
 * rather than DVI signalling. Returns 0 or a negative errno value.
 *
 * The display topology is fixed for now (see rdn_modeset.c).
 */
int rdn_modeset(struct rdn_card *card, const struct rdn_mode *mode,
		const struct rdn_fb *fb, bool hdmi);

/*
 * Write `count` entries of the colour table of CRTC 0 starting at `start`.
 * The table maps 8-bit indices (8 bpp) or channel values (16 and 32 bpp) to
 * colours; rdn_modeset() loads a linear ramp.
 */
void rdn_lut_set(struct rdn_card *card, uint32_t start, uint32_t count,
		 const struct rdn_lut_entry *entries);

#endif /* RDN_MODE_H */
