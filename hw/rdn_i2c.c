/*
 * DDC: I2C over the card's GPIO lines, and EDID retrieval.
 *
 * The VBIOS table lookup and the pin handling (prepare, release, set and
 * get of clock and data) follow radeon_atombios.c and radeon_i2c.c of the
 * Linux radeon driver. The I2C protocol itself is written from the I2C
 * specification; it is not derived from Linux's i2c-algo-bit.
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

#include "rdn_i2c.h"

#ifndef EIO
#define EIO 5
#endif
#ifndef ENXIO
#define ENXIO 6
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif

/*
 * Layout of ATOM_GPIO_I2C_ASSIGMENT, read with explicit little-endian
 * accessors instead of through the packed structure.
 */
#define GPIO_I2C_HEADER_SIZE	4	/* ATOM_COMMON_TABLE_HEADER */
#define GPIO_I2C_ENTRY_SIZE	27
#define GPIO_CLK_MASK_REG	0
#define GPIO_CLK_EN_REG		2
#define GPIO_CLK_Y_REG		4
#define GPIO_CLK_A_REG		6
#define GPIO_DATA_MASK_REG	8
#define GPIO_DATA_EN_REG	10
#define GPIO_DATA_Y_REG		12
#define GPIO_DATA_A_REG		14
#define GPIO_I2C_ID		16
#define GPIO_CLK_MASK_SHIFT	17
#define GPIO_CLK_EN_SHIFT	18
#define GPIO_CLK_Y_SHIFT	19
#define GPIO_CLK_A_SHIFT	20
#define GPIO_DATA_MASK_SHIFT	21
#define GPIO_DATA_EN_SHIFT	22
#define GPIO_DATA_Y_SHIFT	23
#define GPIO_DATA_A_SHIFT	24

#define I2C_ID_HW_CAPABLE	0x80	/* ATOM_I2C_ID_CONFIG.bfHW_Capable */

/* Half a clock period, in microseconds: 50 kHz, well inside DDC's limit. */
#define I2C_HALF_PERIOD_US	10
/* How long a slave may hold the clock low. */
#define I2C_STRETCH_TIMEOUT_MS	50

#define DDC_ADDR		0x50

static bool gpio_i2c_table(struct rdn_card *card, uint16_t *offset, int *count)
{
	struct atom_context *ctx = card->atom.ctx;
	int index = GetIndexIntoMasterTable(DATA, GPIO_I2C_Info);
	uint16_t size;

	if (!atom_parse_data_header(ctx, index, &size, NULL, NULL, offset))
		return false;
	if (size < GPIO_I2C_HEADER_SIZE)
		return false;
	*count = (size - GPIO_I2C_HEADER_SIZE) / GPIO_I2C_ENTRY_SIZE;
	return true;
}

static void bus_from_entry(struct rdn_card *card, uint32_t e,
			   struct rdn_i2c_bus *bus)
{
	const void *bios = card->atom.ctx->bios;
	uint8_t id = rdn_get_u8(bios, e + GPIO_I2C_ID);

	memset(bus, 0, sizeof(*bus));

	bus->mask_clk_reg = rdn_get_le16(bios, e + GPIO_CLK_MASK_REG) * 4;
	bus->mask_data_reg = rdn_get_le16(bios, e + GPIO_DATA_MASK_REG) * 4;
	bus->en_clk_reg = rdn_get_le16(bios, e + GPIO_CLK_EN_REG) * 4;
	bus->en_data_reg = rdn_get_le16(bios, e + GPIO_DATA_EN_REG) * 4;
	bus->y_clk_reg = rdn_get_le16(bios, e + GPIO_CLK_Y_REG) * 4;
	bus->y_data_reg = rdn_get_le16(bios, e + GPIO_DATA_Y_REG) * 4;
	bus->a_clk_reg = rdn_get_le16(bios, e + GPIO_CLK_A_REG) * 4;
	bus->a_data_reg = rdn_get_le16(bios, e + GPIO_DATA_A_REG) * 4;
	bus->mask_clk_mask = 1u << rdn_get_u8(bios, e + GPIO_CLK_MASK_SHIFT);
	bus->mask_data_mask = 1u << rdn_get_u8(bios, e + GPIO_DATA_MASK_SHIFT);
	bus->en_clk_mask = 1u << rdn_get_u8(bios, e + GPIO_CLK_EN_SHIFT);
	bus->en_data_mask = 1u << rdn_get_u8(bios, e + GPIO_DATA_EN_SHIFT);
	bus->y_clk_mask = 1u << rdn_get_u8(bios, e + GPIO_CLK_Y_SHIFT);
	bus->y_data_mask = 1u << rdn_get_u8(bios, e + GPIO_DATA_Y_SHIFT);
	bus->a_clk_mask = 1u << rdn_get_u8(bios, e + GPIO_CLK_A_SHIFT);
	bus->a_data_mask = 1u << rdn_get_u8(bios, e + GPIO_DATA_A_SHIFT);

	bus->hw_capable = (id & I2C_ID_HW_CAPABLE) != 0;
	bus->i2c_id = id;
	bus->valid = bus->mask_clk_reg != 0;
}

int rdn_i2c_bus_count(struct rdn_card *card)
{
	uint16_t offset;
	int count;

	if (!gpio_i2c_table(card, &offset, &count))
		return 0;
	return count;
}

bool rdn_i2c_bus_by_index(struct rdn_card *card, int index,
			  struct rdn_i2c_bus *bus)
{
	uint16_t offset;
	int count;

	memset(bus, 0, sizeof(*bus));
	if (!gpio_i2c_table(card, &offset, &count) || index < 0 || index >= count)
		return false;
	bus_from_entry(card, offset + GPIO_I2C_HEADER_SIZE +
		       index * GPIO_I2C_ENTRY_SIZE, bus);
	return true;
}

void rdn_i2c_bus_by_id(struct rdn_card *card, uint8_t id,
		       struct rdn_i2c_bus *bus)
{
	int i;

	for (i = 0; rdn_i2c_bus_by_index(card, i, bus); i++)
		if (bus->i2c_id == id)
			return;
	memset(bus, 0, sizeof(*bus));
}

/* Pin handling, as in radeon_i2c.c. */

static void i2c_pre_xfer(struct rdn_card *card, const struct rdn_i2c_bus *rec)
{
	uint32_t temp;

	/* switch the pads to ddc mode */
	if (rec->hw_capable) {
		temp = rdn_rreg(card, rec->mask_clk_reg);
		temp &= ~(1u << 16);
		rdn_wreg(card, rec->mask_clk_reg, temp);
	}

	/* clear the output pin values */
	temp = rdn_rreg(card, rec->a_clk_reg) & ~rec->a_clk_mask;
	rdn_wreg(card, rec->a_clk_reg, temp);

	temp = rdn_rreg(card, rec->a_data_reg) & ~rec->a_data_mask;
	rdn_wreg(card, rec->a_data_reg, temp);

	/* set the pins to input */
	temp = rdn_rreg(card, rec->en_clk_reg) & ~rec->en_clk_mask;
	rdn_wreg(card, rec->en_clk_reg, temp);

	temp = rdn_rreg(card, rec->en_data_reg) & ~rec->en_data_mask;
	rdn_wreg(card, rec->en_data_reg, temp);

	/* mask the gpio pins for software use */
	temp = rdn_rreg(card, rec->mask_clk_reg) | rec->mask_clk_mask;
	rdn_wreg(card, rec->mask_clk_reg, temp);
	temp = rdn_rreg(card, rec->mask_clk_reg);

	temp = rdn_rreg(card, rec->mask_data_reg) | rec->mask_data_mask;
	rdn_wreg(card, rec->mask_data_reg, temp);
	temp = rdn_rreg(card, rec->mask_data_reg);
	(void)temp;
}

static void i2c_post_xfer(struct rdn_card *card, const struct rdn_i2c_bus *rec)
{
	uint32_t temp;

	/* unmask the gpio pins for software use */
	temp = rdn_rreg(card, rec->mask_clk_reg) & ~rec->mask_clk_mask;
	rdn_wreg(card, rec->mask_clk_reg, temp);
	temp = rdn_rreg(card, rec->mask_clk_reg);

	temp = rdn_rreg(card, rec->mask_data_reg) & ~rec->mask_data_mask;
	rdn_wreg(card, rec->mask_data_reg, temp);
	temp = rdn_rreg(card, rec->mask_data_reg);
	(void)temp;
}

static int get_clock(struct rdn_card *card, const struct rdn_i2c_bus *rec)
{
	return (rdn_rreg(card, rec->y_clk_reg) & rec->y_clk_mask) != 0;
}

static int get_data(struct rdn_card *card, const struct rdn_i2c_bus *rec)
{
	return (rdn_rreg(card, rec->y_data_reg) & rec->y_data_mask) != 0;
}

/* The lines are open-drain: "high" releases the pin, "low" drives it. */
static void set_clock(struct rdn_card *card, const struct rdn_i2c_bus *rec,
		      int clock)
{
	uint32_t val = rdn_rreg(card, rec->en_clk_reg) & ~rec->en_clk_mask;

	val |= clock ? 0 : rec->en_clk_mask;
	rdn_wreg(card, rec->en_clk_reg, val);
}

static void set_data(struct rdn_card *card, const struct rdn_i2c_bus *rec,
		     int data)
{
	uint32_t val = rdn_rreg(card, rec->en_data_reg) & ~rec->en_data_mask;

	val |= data ? 0 : rec->en_data_mask;
	rdn_wreg(card, rec->en_data_reg, val);
}

/* I2C master, bit-banged. */

static void half_period(struct rdn_card *card)
{
	card->os->delay_us(card->os->cookie, I2C_HALF_PERIOD_US);
}

/* Release the clock and wait for it to rise; a slave may stretch it. */
static int clock_high(struct rdn_card *card, const struct rdn_i2c_bus *bus)
{
	struct rdn_os *os = card->os;
	uint64_t start;

	set_clock(card, bus, 1);
	if (!get_clock(card, bus)) {
		start = os->time_ms(os->cookie);
		while (!get_clock(card, bus)) {
			if (os->time_ms(os->cookie) - start > I2C_STRETCH_TIMEOUT_MS)
				return -ETIMEDOUT;
			os->delay_us(os->cookie, I2C_HALF_PERIOD_US);
		}
	}
	half_period(card);
	return 0;
}

static void clock_low(struct rdn_card *card, const struct rdn_i2c_bus *bus)
{
	set_clock(card, bus, 0);
	half_period(card);
}

/* START: data falls while the clock is high. Also serves as repeated START. */
static int i2c_start(struct rdn_card *card, const struct rdn_i2c_bus *bus)
{
	int r;

	set_data(card, bus, 1);
	half_period(card);
	r = clock_high(card, bus);
	if (r)
		return r;
	set_data(card, bus, 0);
	half_period(card);
	clock_low(card, bus);
	return 0;
}

/* STOP: data rises while the clock is high. */
static void i2c_stop(struct rdn_card *card, const struct rdn_i2c_bus *bus)
{
	set_data(card, bus, 0);
	half_period(card);
	clock_high(card, bus);
	set_data(card, bus, 1);
	half_period(card);
}

static int i2c_write_bit(struct rdn_card *card, const struct rdn_i2c_bus *bus,
			 int bit)
{
	int r;

	set_data(card, bus, bit);
	half_period(card);
	r = clock_high(card, bus);
	clock_low(card, bus);
	return r;
}

static int i2c_read_bit(struct rdn_card *card, const struct rdn_i2c_bus *bus)
{
	int r, bit;

	set_data(card, bus, 1);
	half_period(card);
	r = clock_high(card, bus);
	if (r)
		return r;
	bit = get_data(card, bus);
	clock_low(card, bus);
	return bit;
}

/* Returns 0 when the slave acknowledged, -ENXIO when it did not. */
static int i2c_write_byte(struct rdn_card *card, const struct rdn_i2c_bus *bus,
			  uint8_t byte)
{
	int i, r;

	for (i = 7; i >= 0; i--) {
		r = i2c_write_bit(card, bus, (byte >> i) & 1);
		if (r)
			return r;
	}
	r = i2c_read_bit(card, bus);
	if (r < 0)
		return r;
	return r ? -ENXIO : 0;
}

static int i2c_read_byte(struct rdn_card *card, const struct rdn_i2c_bus *bus,
			 bool ack)
{
	int i, bit, byte = 0;

	for (i = 0; i < 8; i++) {
		bit = i2c_read_bit(card, bus);
		if (bit < 0)
			return bit;
		byte = (byte << 1) | bit;
	}
	bit = i2c_write_bit(card, bus, ack ? 0 : 1);
	if (bit < 0)
		return bit;
	return byte;
}

int rdn_i2c_read(struct rdn_card *card, const struct rdn_i2c_bus *bus,
		 uint8_t addr, uint8_t offset, uint8_t *buf, int len)
{
	int i, r;

	if (!bus->valid)
		return -EINVAL;

	i2c_pre_xfer(card, bus);

	r = i2c_start(card, bus);
	if (r)
		goto out;
	r = i2c_write_byte(card, bus, (uint8_t)(addr << 1));
	if (r)
		goto stop;
	r = i2c_write_byte(card, bus, offset);
	if (r)
		goto stop;
	r = i2c_start(card, bus);
	if (r)
		goto stop;
	r = i2c_write_byte(card, bus, (uint8_t)((addr << 1) | 1));
	if (r)
		goto stop;
	for (i = 0; i < len; i++) {
		r = i2c_read_byte(card, bus, i != len - 1);
		if (r < 0)
			goto stop;
		buf[i] = (uint8_t)r;
	}
	r = 0;
stop:
	i2c_stop(card, bus);
out:
	i2c_post_xfer(card, bus);
	return r;
}

bool rdn_edid_block_valid(const uint8_t *block, bool base_block)
{
	static const uint8_t header[8] = {
		0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
	};
	uint8_t sum = 0;
	int i;

	if (base_block && memcmp(block, header, sizeof(header)))
		return false;
	for (i = 0; i < RDN_EDID_BLOCK_SIZE; i++)
		sum = (uint8_t)(sum + block[i]);
	return sum == 0;
}

int rdn_edid_read(struct rdn_card *card, const struct rdn_i2c_bus *bus,
		  uint8_t *buf)
{
	int r, blocks, i;

	r = rdn_i2c_read(card, bus, DDC_ADDR, 0, buf, RDN_EDID_BLOCK_SIZE);
	if (r)
		return r;
	if (!rdn_edid_block_valid(buf, true))
		return -EIO;

	/* Byte 126 is the number of extension blocks that follow. */
	blocks = 1 + buf[126];
	if (blocks > RDN_EDID_MAX_BLOCKS)
		blocks = RDN_EDID_MAX_BLOCKS;
	for (i = 1; i < blocks; i++) {
		uint8_t *block = buf + i * RDN_EDID_BLOCK_SIZE;

		r = rdn_i2c_read(card, bus, DDC_ADDR,
				 (uint8_t)(i * RDN_EDID_BLOCK_SIZE), block,
				 RDN_EDID_BLOCK_SIZE);
		if (r || !rdn_edid_block_valid(block, false)) {
			rdn_log(card->os, RDN_LOG_ERROR,
				"EDID extension block %d unreadable or invalid", i);
			break;
		}
	}
	return i * RDN_EDID_BLOCK_SIZE;
}
