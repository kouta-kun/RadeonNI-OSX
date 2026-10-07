/*
 * The card: state shared by all parts of the hardware library.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_CARD_H
#define RDN_CARD_H

#include "rdn_os.h"
#include "rdn_atom.h"

/* Turks has six display controllers. */
#define RDN_NUM_CRTC	6

/*
 * A connector with the encoder path the card wires to it, in the terms the
 * VBIOS command tables take (rdn_modeset.c has the list).
 */
struct rdn_output {
	const char *name;
	bool displayport;	/* a DisplayPort connector */
	uint8_t connector_id;	/* connector object id */
	uint8_t transmitter_id;	/* encoder object id of the UNIPHY */
	uint8_t transmitter_sel;	/* which UNIPHY: 0 to 2 */
	uint8_t dig_encoder;	/* digital encoder Linux uses with it */
	uint8_t hpd;		/* hot-plug line, 0 is HPD1 */
	uint8_t i2c_id;		/* DDC line, and the AUX channel in its low bits */
};

/* The DisplayPort link of the output in use (rdn_dp.c). */
struct rdn_dp {
	bool sink;		/* a DisplayPort sink answered on AUX */
	uint8_t dpcd[15];	/* its receiver capabilities */
	uint32_t rate;		/* link clock in kHz, 162000 or 270000 */
	uint8_t lanes;
	uint8_t train_set[4];	/* voltage swing and pre-emphasis in use */
	uint32_t aux_status;	/* AUX_SW_STATUS of the last transaction */
};

struct rdn_card {
	struct rdn_os *os;
	struct rdn_atom atom;
	/* The output rdn_modeset() drives; NULL is the DVI-I connector. */
	const struct rdn_output *output;
	struct rdn_dp dp;
	/* CRTC 0 is scanning out; kept up to date by POST and modeset. */
	bool crtc_on;
	/*
	 * Engine and memory clock in units of 10 kHz, as rdn_pm_set() left
	 * them; zero until someone asks: what the card boots with.
	 */
	uint32_t sclk, mclk;
	/*
	 * What the display watermarks are computed from: the mode on CRTC 0
	 * (clock in kHz; zero before the first mode set) and, if not zero,
	 * other clocks for the second watermark set (rdn_watermark.c).
	 */
	uint32_t wm_clock, wm_hdisplay, wm_htotal;
	uint32_t wm_low_sclk, wm_low_mclk;
};

/*
 * The register BAR is 128 KB. Registers beyond it (the 3D engine's context
 * registers) are reached through the index/data pair at its start, as
 * Linux's r100_mm_rreg() and r100_mm_wreg() do.
 */
#define RDN_MMIO_SIZE		0x20000
#define RDN_MMIO_INDEX		0x0000
#define RDN_MMIO_DATA		0x0004

static inline uint32_t rdn_rreg(struct rdn_card *card, uint32_t reg)
{
	if (reg < RDN_MMIO_SIZE)
		return card->os->mmio_read32(card->os->cookie, reg);
	card->os->mmio_write32(card->os->cookie, RDN_MMIO_INDEX, reg);
	return card->os->mmio_read32(card->os->cookie, RDN_MMIO_DATA);
}

static inline void rdn_wreg(struct rdn_card *card, uint32_t reg, uint32_t val)
{
	if (reg < RDN_MMIO_SIZE) {
		card->os->mmio_write32(card->os->cookie, reg, val);
		return;
	}
	card->os->mmio_write32(card->os->cookie, RDN_MMIO_INDEX, reg);
	card->os->mmio_write32(card->os->cookie, RDN_MMIO_DATA, val);
}

/*
 * Bind the library to a card. `bios` is the VBIOS image (from the PCI
 * expansion ROM or a file) and must stay valid until rdn_card_fini().
 */
int rdn_card_init(struct rdn_card *card, struct rdn_os *os, void *bios);
void rdn_card_fini(struct rdn_card *card);

/* True when a CRTC is running or the memory size register is set. */
bool rdn_card_posted(struct rdn_card *card);

/*
 * Bring the card up from any state, in the order the Linux radeon driver
 * uses: BIOS scratch registers, engine reset check, then ASIC_Init if the
 * card is not posted. Returns 0 or a negative errno value.
 */
int rdn_card_post(struct rdn_card *card);

/*
 * Program the line buffer and the display watermarks of CRTC 0 for the
 * mode and clocks recorded in the card. rdn_modeset() and rdn_pm_set()
 * call it.
 */
void rdn_bandwidth_update(struct rdn_card *card);

#endif /* RDN_CARD_H */
