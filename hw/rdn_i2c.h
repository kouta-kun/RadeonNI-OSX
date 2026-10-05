/*
 * DDC: I2C over the card's GPIO lines, and EDID retrieval.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_I2C_H
#define RDN_I2C_H

#include "rdn_card.h"

/* One I2C line pair, as described by the VBIOS GPIO_I2C_Info table. */
struct rdn_i2c_bus {
	bool valid;
	uint8_t i2c_id;		/* id used by AtomBIOS */
	bool hw_capable;	/* can be used with the hw i2c engine */
	uint32_t mask_clk_reg, mask_data_reg;
	uint32_t a_clk_reg, a_data_reg;
	uint32_t en_clk_reg, en_data_reg;
	uint32_t y_clk_reg, y_data_reg;
	uint32_t mask_clk_mask, mask_data_mask;
	uint32_t a_clk_mask, a_data_mask;
	uint32_t en_clk_mask, en_data_mask;
	uint32_t y_clk_mask, y_data_mask;
};

#define RDN_EDID_BLOCK_SIZE	128
#define RDN_EDID_MAX_BLOCKS	4
#define RDN_EDID_MAX_SIZE	(RDN_EDID_BLOCK_SIZE * RDN_EDID_MAX_BLOCKS)

/* Number of entries in the VBIOS I2C table (0 if there is none). */
int rdn_i2c_bus_count(struct rdn_card *card);

/* Entry by position; returns false past the end. bus->valid may be false. */
bool rdn_i2c_bus_by_index(struct rdn_card *card, int index,
			  struct rdn_i2c_bus *bus);

/* Entry by AtomBIOS i2c id; bus->valid is false when not found. */
void rdn_i2c_bus_by_id(struct rdn_card *card, uint8_t id,
		       struct rdn_i2c_bus *bus);

/*
 * Read `len` bytes from 7-bit address `addr`, after writing the single
 * offset byte `offset`. Returns 0, or a negative errno value.
 */
int rdn_i2c_read(struct rdn_card *card, const struct rdn_i2c_bus *bus,
		 uint8_t addr, uint8_t offset, uint8_t *buf, int len);

/*
 * Read and validate the EDID of the display on `bus`. `buf` must hold
 * RDN_EDID_MAX_SIZE bytes. Returns the number of bytes read (a multiple of
 * 128) or a negative errno value.
 */
int rdn_edid_read(struct rdn_card *card, const struct rdn_i2c_bus *bus,
		  uint8_t *buf);

/* True when the 128-byte block has a valid checksum (and header, for block 0). */
bool rdn_edid_block_valid(const uint8_t *block, bool base_block);

#endif /* RDN_I2C_H */
