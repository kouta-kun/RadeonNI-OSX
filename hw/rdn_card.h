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

struct rdn_card {
	struct rdn_os *os;
	struct rdn_atom atom;
	/* CRTC 0 is scanning out; kept up to date by POST and modeset. */
	bool crtc_on;
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

#endif /* RDN_CARD_H */
