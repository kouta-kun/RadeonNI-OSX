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

/* The scanout surface: 32 bits per pixel, XRGB. */
struct rdn_fb {
	uint32_t aperture_offset;	/* byte offset of pixel (0,0) in BAR0 */
	uint32_t width, height;
	uint32_t pitch_pixels;
	/*
	 * True when the host writes pixels as big-endian 32-bit words; the
	 * card then swaps the bytes of each pixel on scanout.
	 */
	bool big_endian_pixels;
};

/* The preferred (first detailed) timing of an EDID. False if there is none. */
bool rdn_edid_preferred_mode(const uint8_t *edid, struct rdn_mode *mode);

/* True when the EDID carries an HDMI vendor block in a CEA extension. */
bool rdn_edid_is_hdmi(const uint8_t *edid, int len);

/*
 * Set `mode` on CRTC 0 and drive it out of the DVI-I connector's digital
 * output, scanning out `fb`. The card must be posted. `hdmi` selects HDMI
 * rather than DVI signalling. Returns 0 or a negative errno value.
 *
 * The display topology is fixed for now (see rdn_modeset.c).
 */
int rdn_modeset(struct rdn_card *card, const struct rdn_mode *mode,
		const struct rdn_fb *fb, bool hdmi);

#endif /* RDN_MODE_H */
