/*
 * Display modes, EDID interpretation and mode setting.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_MODE_H
#define RDN_MODE_H

#include "rdn_card.h"

/* The highest pixel clock of a mode we offer: one TMDS link, and what DisplayPort at 4 lanes of 1.62 Gbit/s carries. */
#define RDN_MODE_MAX_CLOCK	165000

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

/*
 * Every mode an EDID offers (its detailed, established, standard and CEA
 * timings that are progressive, and 640x480, 800x600 and 1024x768 at 60 Hz
 * if the monitor's range limits allow them), the preferred one first, at
 * most `max`. `len` is the EDID's length. Returns the count.
 */
int rdn_edid_modes(const uint8_t *edid, int len, struct rdn_mode *out, int max);

/* True when the EDID carries an HDMI vendor block in a CEA extension. */
bool rdn_edid_is_hdmi(const uint8_t *edid, int len);

/* The card's outputs, by position; NULL past the last one. */
#define RDN_OUTPUT_DVI	0
#define RDN_OUTPUT_DP	1
const struct rdn_output *rdn_output(int index);

/* The output rdn_modeset() drives from now on. */
void rdn_output_select(struct rdn_card *card, const struct rdn_output *out);

/*
 * Find the display: try each output in turn for an EDID and select the
 * first that has one. `edid` must hold RDN_EDID_MAX_SIZE bytes (rdn_i2c.h).
 * Returns the EDID's length or a negative errno value. On the DisplayPort
 * connector this also reads the sink's capabilities, which rdn_modeset()
 * needs. The card must be posted.
 */
int rdn_output_detect(struct rdn_card *card, uint8_t *edid);

/*
 * One-time display setup after the card is posted and before the first
 * rdn_modeset(): transmitter initialisation and the display engine clock.
 * Returns 0 or a negative errno value.
 */
int rdn_display_init(struct rdn_card *card);

/*
 * Set `mode` on CRTC 0 and drive it out of the selected output (the DVI-I
 * connector's digital output if none was selected), scanning out `fb`. The
 * card must be posted and rdn_display_init() must have run. `hdmi` selects
 * HDMI rather than DVI signalling on the DVI-I connector; DisplayPort
 * ignores it. Returns 0 or a negative errno value; on DisplayPort that
 * includes a link that did not train.
 */
int rdn_modeset(struct rdn_card *card, const struct rdn_mode *mode,
		const struct rdn_fb *fb, bool hdmi);

/* Enable the hot-plug lines of both connectors (after rdn_display_init()). */
void rdn_output_hpd_enable(struct rdn_card *card);

/* Is a display on the selected output (its hot-plug line)? */
bool rdn_output_connected(struct rdn_card *card);

/*
 * Display power management off for the selected output: the picture and
 * the signal stop (on DisplayPort the sink goes to its D3 power state). The
 * way back is rdn_modeset() with the same mode, which trains the link
 * again. `hdmi` as for rdn_modeset().
 */
int rdn_output_disable(struct rdn_card *card, const struct rdn_mode *mode,
		       bool hdmi);

/*
 * Write `count` entries of the colour table of CRTC 0 starting at `start`.
 * The table maps 8-bit indices (8 bpp) or channel values (16 and 32 bpp) to
 * colours; rdn_modeset() loads a linear ramp.
 */
void rdn_lut_set(struct rdn_card *card, uint32_t start, uint32_t count,
		 const struct rdn_lut_entry *entries);

#endif /* RDN_MODE_H */
