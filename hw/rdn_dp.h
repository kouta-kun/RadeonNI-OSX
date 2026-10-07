/*
 * DisplayPort: the AUX channel, the sink's capabilities and EDID, the
 * choice of lane count and link rate, and link training.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_DP_H
#define RDN_DP_H

#include "rdn_mode.h"

/* AUX requests (DisplayPort 1.1a, 2.4.1.2) */
#define RDN_AUX_I2C_WRITE	0x0
#define RDN_AUX_I2C_READ	0x1
#define RDN_AUX_I2C_MOT		0x4	/* middle of transaction */
#define RDN_AUX_NATIVE_WRITE	0x8
#define RDN_AUX_NATIVE_READ	0x9

/* The most bytes one AUX transaction carries. */
#define RDN_AUX_MAX		16

/*
 * One AUX transaction on the output's channel. `size` of 0 is an
 * address-only transaction. Returns the number of bytes moved or a negative
 * errno value; `*reply` is the sink's reply nibble (0 is an acknowledge).
 */
int rdn_dp_aux(struct rdn_card *card, const struct rdn_output *out,
	       uint8_t request, uint32_t address, uint8_t *buf, int size,
	       uint8_t *reply);

/*
 * Read or write `len` (1 to 16) bytes of the sink's registers, with the
 * retries the standard asks for. Returns `len` or a negative errno value.
 */
int rdn_dp_dpcd_read(struct rdn_card *card, const struct rdn_output *out,
		     uint32_t address, uint8_t *buf, int len);
int rdn_dp_dpcd_write(struct rdn_card *card, const struct rdn_output *out,
		      uint32_t address, const uint8_t *buf, int len);

/* Enable the output's hot-plug line; rdn_dp_sense() reads it. */
void rdn_dp_hpd_init(struct rdn_card *card, const struct rdn_output *out);
bool rdn_dp_sense(struct rdn_card *card, const struct rdn_output *out);

/*
 * Look for a DisplayPort sink on the output: read its capabilities into
 * card->dp. Returns 0 when one answered, or a negative errno value.
 */
int rdn_dp_detect(struct rdn_card *card, const struct rdn_output *out);

/*
 * The sink's EDID, as I2C over AUX. Same contract as rdn_edid_read().
 */
int rdn_dp_edid_read(struct rdn_card *card, const struct rdn_output *out,
		     uint8_t *buf);

/*
 * Choose the lowest link rate, then the fewest lanes, that carry `mode` at
 * 24 bits a pixel, within what the sink in card->dp can do. Sets
 * card->dp.rate and card->dp.lanes. Returns 0 or -EINVAL.
 */
int rdn_dp_link_config(struct rdn_card *card, const struct rdn_mode *mode);

/* What link training asks of the source: rdn_modeset.c provides it. */
struct rdn_dp_source {
	void *ctx;
	/* Send training pattern 0 (start), 1, 2 or 3, or 4: training done. */
	int (*pattern)(void *ctx, int pattern);
	/* Drive every lane with this DPCD TRAINING_LANEx_SET value. */
	int (*drive)(void *ctx, uint8_t lane_set);
};

#define RDN_DP_PATTERN_START	0
#define RDN_DP_PATTERN_DONE	4

/*
 * Train the link with card->dp.rate and card->dp.lanes. The transmitter
 * must be enabled. Returns 0 when clock recovery and channel equalisation
 * both succeeded, or a negative errno value; the source is told that
 * training is over either way.
 */
int rdn_dp_link_train(struct rdn_card *card, const struct rdn_output *out,
		      const struct rdn_dp_source *src);

#endif /* RDN_DP_H */
