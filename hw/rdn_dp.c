/*
 * DisplayPort for DCE5 (Northern Islands): the AUX channel through the
 * card's registers, the sink's capabilities and EDID, the choice of lane
 * count and link rate, and link training.
 *
 * Ported from the Linux radeon driver: radeon_dp_auxch.c (the AUX
 * transaction) and atombios_dp.c (link configuration and training). The
 * retry rules around AUX are the ones Linux has in its DisplayPort helper.
 * Nothing here could be compared with a register trace: the reference
 * trace has no DisplayPort sink in it.
 *
 * Differences from Linux, on purpose:
 *  - No spread spectrum on the link clock, and the sink is told so.
 *  - 5.4 Gbit/s is never chosen.
 *  - The sink is not put to sleep (D3) before a mode set.
 *
 * Copyright 2007-8 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2015 Red Hat Inc.
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
 *          Jerome Glisse
 */

#include "rdn_dp.h"
#include "rdn_i2c.h"

#ifndef EIO
#define EIO 5
#endif
#ifndef ENXIO
#define ENXIO 6
#endif
#ifndef E2BIG
#define E2BIG 7
#endif
#ifndef EBUSY
#define EBUSY 16
#endif
#ifndef EINVAL
#define EINVAL 22
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif

/* The AUX channel registers (Linux nid.h); six instances. */
#define AUX_CONTROL			0x6200
#define		AUX_EN			(1 << 0)
#define		AUX_LS_READ_EN		(1 << 8)
#define		AUX_HPD_SEL(x)		(((x) & 0x7) << 20)
#define AUX_SW_CONTROL			0x6204
#define		AUX_SW_GO		(1 << 0)
#define		AUX_SW_WR_BYTES(x)	(((x) & 0x1f) << 16)
#define AUX_SW_INTERRUPT_CONTROL	0x620c
#define		AUX_SW_DONE_ACK		(1 << 1)
#define AUX_SW_STATUS			0x6210
#define		AUX_SW_DONE		(1 << 0)
#define		AUX_SW_RX_TIMEOUT	(1 << 7)
#define		AUX_SW_RX_OVERFLOW	(1 << 8)
#define		AUX_SW_RX_HPD_DISCON	(1 << 9)
#define		AUX_SW_RX_PARTIAL_BYTE	(1 << 10)
#define		AUX_SW_NON_AUX_MODE	(1 << 11)
#define		AUX_SW_RX_SYNC_INVALID_L (1 << 17)
#define		AUX_SW_RX_SYNC_INVALID_H (1 << 18)
#define		AUX_SW_RX_INVALID_START	(1 << 19)
#define		AUX_SW_RX_RECV_NO_DET	(1 << 20)
#define		AUX_SW_RX_RECV_INVALID_H (1 << 22)
#define		AUX_SW_RX_RECV_INVALID_V (1 << 23)
#define AUX_SW_DATA			0x6218
#define		AUX_SW_DATA_RW		(1 << 0)
#define		AUX_SW_DATA_MASK(x)	(((x) & 0xff) << 8)
#define		AUX_SW_AUTOINCREMENT_DISABLE (1u << 31)

#define AUX_RX_ERROR_FLAGS (AUX_SW_RX_OVERFLOW |	     \
			    AUX_SW_RX_HPD_DISCON |	     \
			    AUX_SW_RX_PARTIAL_BYTE |	     \
			    AUX_SW_NON_AUX_MODE |	     \
			    AUX_SW_RX_SYNC_INVALID_L |	     \
			    AUX_SW_RX_SYNC_INVALID_H |	     \
			    AUX_SW_RX_INVALID_START |	     \
			    AUX_SW_RX_RECV_NO_DET |	     \
			    AUX_SW_RX_RECV_INVALID_H |	     \
			    AUX_SW_RX_RECV_INVALID_V)

#define AUX_SW_REPLY_GET_BYTE_COUNT(x)	(((x) >> 24) & 0x1f)

#define BARE_ADDRESS_SIZE	3

static const uint32_t aux_offset[] = {
	0x6200 - 0x6200,
	0x6250 - 0x6200,
	0x62a0 - 0x6200,
	0x6300 - 0x6200,
	0x6350 - 0x6200,
	0x63a0 - 0x6200,
};

/* Hot-plug lines (evergreend.h); six, twelve bytes apart. */
#define DC_HPD_INT_STATUS(n)		(0x601c + (n) * 0xc)
#define		DC_HPDx_SENSE		(1 << 1)
#define DC_HPD_CONTROL(n)		(0x6024 + (n) * 0xc)
#define		DC_HPDx_CONNECTION_TIMER(x)	((x) << 0)
#define		DC_HPDx_RX_INT_TIMER(x)		((x) << 16)
#define		DC_HPDx_EN			(1 << 28)

/* Replies: bits 0-1 answer a native request, bits 2-3 an I2C one. */
#define AUX_NATIVE_REPLY_MASK	0x3
#define AUX_NATIVE_REPLY_ACK	0x0
#define AUX_NATIVE_REPLY_NACK	0x1
#define AUX_I2C_REPLY_MASK	0xc
#define AUX_I2C_REPLY_ACK	0x0
#define AUX_I2C_REPLY_NACK	0x4

/* The sink's registers (DPCD) */
#define DP_DPCD_REV			0x000
#define DP_MAX_LINK_RATE		0x001
#define DP_MAX_LANE_COUNT		0x002
#define		DP_MAX_LANE_COUNT_MASK		0x1f
#define		DP_TPS3_SUPPORTED		(1 << 6)
#define		DP_ENHANCED_FRAME_CAP		(1 << 7)
#define DP_TRAINING_AUX_RD_INTERVAL	0x00e
#define DP_LINK_BW_SET			0x100
#define DP_LANE_COUNT_SET		0x101
#define		DP_LANE_COUNT_ENHANCED_FRAME_EN	(1 << 7)
#define DP_TRAINING_PATTERN_SET		0x102
#define DP_TRAINING_LANE0_SET		0x103
#define		DP_TRAIN_VOLTAGE_SWING_MASK	0x3
#define		DP_TRAIN_MAX_SWING_REACHED	(1 << 2)
#define		DP_TRAIN_PRE_EMPHASIS_SHIFT	3
#define		DP_TRAIN_PRE_EMPHASIS_MASK	(3 << 3)
#define		DP_TRAIN_MAX_PRE_EMPHASIS_REACHED (1 << 5)
#define DP_DOWNSPREAD_CTRL		0x107
#define DP_LANE0_1_STATUS		0x202
#define		DP_LANE_CR_DONE			(1 << 0)
#define		DP_CHANNEL_EQ_BITS		0x7
#define		DP_INTERLANE_ALIGN_DONE		(1 << 0)
#define DP_SET_POWER			0x600
#define		DP_SET_POWER_D0			0x1
#define DP_LINK_STATUS_SIZE		6

#define DP_VOLTAGE_MAX		3
#define DP_PRE_EMPHASIS_MAX	(3 << DP_TRAIN_PRE_EMPHASIS_SHIFT)

#define DDC_ADDR		0x50
#define DDC_SEGMENT_ADDR	0x30

/* radeon_dp_aux_transfer_native() */
int rdn_dp_aux(struct rdn_card *card, const struct rdn_output *out,
	       uint8_t request, uint32_t address, uint8_t *buf, int size,
	       uint8_t *reply)
{
	unsigned instance = out->i2c_id & 0xf;
	struct rdn_i2c_bus bus;
	uint32_t base, tmp, ack = 0;
	int ret = 0, i, bytes, msize, retry_count = 0;
	bool is_write;

	if (size < 0 || size > RDN_AUX_MAX)
		return -E2BIG;
	if (instance >= sizeof(aux_offset) / sizeof(aux_offset[0]))
		return -EINVAL;
	rdn_i2c_bus_by_id(card, out->i2c_id, &bus);
	if (!bus.valid)
		return -EINVAL;
	base = aux_offset[instance];

	switch (request & ~RDN_AUX_I2C_MOT) {
	case RDN_AUX_NATIVE_WRITE:
	case RDN_AUX_I2C_WRITE:
		is_write = true;
		break;
	case RDN_AUX_NATIVE_READ:
	case RDN_AUX_I2C_READ:
		is_write = false;
		break;
	default:
		return -EINVAL;
	}

	/* work out two sizes required */
	msize = 0;
	bytes = BARE_ADDRESS_SIZE;
	if (size) {
		msize = size - 1;
		bytes++;
		if (is_write)
			bytes += size;
	}

	/* switch the pad to aux mode */
	tmp = rdn_rreg(card, bus.mask_clk_reg);
	tmp |= (1 << 16);
	rdn_wreg(card, bus.mask_clk_reg, tmp);

	/* setup AUX control register with correct HPD pin */
	tmp = rdn_rreg(card, AUX_CONTROL + base);
	tmp &= AUX_HPD_SEL(0x7);
	tmp |= AUX_HPD_SEL(out->hpd);
	tmp |= AUX_EN | AUX_LS_READ_EN;
	rdn_wreg(card, AUX_CONTROL + base, tmp);

	/* atombios appears to write this twice lets copy it */
	rdn_wreg(card, AUX_SW_CONTROL + base, AUX_SW_WR_BYTES(bytes));
	rdn_wreg(card, AUX_SW_CONTROL + base, AUX_SW_WR_BYTES(bytes));

	/* write the data header into the registers */
	/* request, address, msg size */
	rdn_wreg(card, AUX_SW_DATA + base,
		 AUX_SW_DATA_MASK((request << 4) | ((address >> 16) & 0xf)) |
		 AUX_SW_AUTOINCREMENT_DISABLE);
	rdn_wreg(card, AUX_SW_DATA + base, AUX_SW_DATA_MASK(address >> 8));
	rdn_wreg(card, AUX_SW_DATA + base, AUX_SW_DATA_MASK(address));
	rdn_wreg(card, AUX_SW_DATA + base, AUX_SW_DATA_MASK(msize));

	/* if we are writing - write the msg buffer */
	if (is_write)
		for (i = 0; i < size; i++)
			rdn_wreg(card, AUX_SW_DATA + base,
				 AUX_SW_DATA_MASK(buf[i]));

	/* clear the ACK */
	rdn_wreg(card, AUX_SW_INTERRUPT_CONTROL + base, AUX_SW_DONE_ACK);

	/* write the size and GO bits */
	rdn_wreg(card, AUX_SW_CONTROL + base,
		 AUX_SW_WR_BYTES(bytes) | AUX_SW_GO);

	/* poll the status registers */
	do {
		tmp = rdn_rreg(card, AUX_SW_STATUS + base);
		if (tmp & AUX_SW_DONE)
			break;
		card->os->delay_us(card->os->cookie, 100);
	} while (retry_count++ < 1000);
	card->dp.aux_status = tmp;

	if (retry_count >= 1000) {
		rdn_log(card->os, RDN_LOG_ERROR,
			"AUX never signalled completion, status %08x",
			(unsigned)tmp);
		return -EIO;
	}
	if (tmp & AUX_SW_RX_TIMEOUT)
		return -ETIMEDOUT;
	if (tmp & AUX_RX_ERROR_FLAGS)
		return -EIO;

	bytes = AUX_SW_REPLY_GET_BYTE_COUNT(tmp);
	if (bytes) {
		rdn_wreg(card, AUX_SW_DATA + base,
			 AUX_SW_DATA_RW | AUX_SW_AUTOINCREMENT_DISABLE);

		tmp = rdn_rreg(card, AUX_SW_DATA + base);
		ack = (tmp >> 8) & 0xff;

		for (i = 0; i < bytes - 1; i++) {
			tmp = rdn_rreg(card, AUX_SW_DATA + base);
			if (!is_write && i < size)
				buf[i] = (tmp >> 8) & 0xff;
		}
		if (!is_write)
			ret = bytes - 1 < size ? bytes - 1 : size;
	}

	rdn_wreg(card, AUX_SW_INTERRUPT_CONTROL + base, AUX_SW_DONE_ACK);

	if (is_write)
		ret = size;
	*reply = (uint8_t)(ack >> 4);
	return ret;
}

/*
 * A native transaction until the sink acknowledges all of it: a sink may
 * defer, and a transaction may be lost, so the standard has the source try
 * again (Linux: drm_dp_dpcd_access(), 32 tries).
 */
static int dpcd_access(struct rdn_card *card, const struct rdn_output *out,
		       uint8_t request, uint32_t address, uint8_t *buf, int len)
{
	int ret = 0, retry;
	uint8_t reply;

	for (retry = 0; retry < 32; retry++) {
		if (ret != 0 && ret != -ETIMEDOUT)
			card->os->delay_us(card->os->cookie, 500);
		ret = rdn_dp_aux(card, out, request, address, buf, len, &reply);
		if (ret < 0)
			continue;
		switch (reply & AUX_NATIVE_REPLY_MASK) {
		case AUX_NATIVE_REPLY_ACK:
			if (ret == len)
				return len;
			ret = -EIO;
			break;
		case AUX_NATIVE_REPLY_NACK:
			ret = -ENXIO;
			break;
		default:
			ret = -EBUSY;
			break;
		}
	}
	return ret;
}

int rdn_dp_dpcd_read(struct rdn_card *card, const struct rdn_output *out,
		     uint32_t address, uint8_t *buf, int len)
{
	return dpcd_access(card, out, RDN_AUX_NATIVE_READ, address, buf, len);
}

int rdn_dp_dpcd_write(struct rdn_card *card, const struct rdn_output *out,
		      uint32_t address, const uint8_t *buf, int len)
{
	uint8_t copy[RDN_AUX_MAX];

	if (len < 0 || len > RDN_AUX_MAX)
		return -E2BIG;
	memcpy(copy, buf, len);
	return dpcd_access(card, out, RDN_AUX_NATIVE_WRITE, address, copy, len);
}

static int dpcd_writeb(struct rdn_card *card, const struct rdn_output *out,
		       uint32_t address, uint8_t value)
{
	return rdn_dp_dpcd_write(card, out, address, &value, 1);
}

/* evergreen_hpd_init() for one line, without the interrupt */
void rdn_dp_hpd_init(struct rdn_card *card, const struct rdn_output *out)
{
	rdn_wreg(card, DC_HPD_CONTROL(out->hpd),
		 DC_HPDx_CONNECTION_TIMER(0x9c4) | DC_HPDx_RX_INT_TIMER(0xfa) |
		 DC_HPDx_EN);
}

/* evergreen_hpd_sense() */
bool rdn_dp_sense(struct rdn_card *card, const struct rdn_output *out)
{
	return !!(rdn_rreg(card, DC_HPD_INT_STATUS(out->hpd)) & DC_HPDx_SENSE);
}

/*
 * radeon_dp_getdpcd(). The hot-plug line has only just been enabled and a
 * monitor may be waking up, so this keeps asking for a while: a second
 * while the line reads unplugged, three while it reads plugged.
 */
int rdn_dp_detect(struct rdn_card *card, const struct rdn_output *out)
{
	uint8_t dpcd[sizeof(card->dp.dpcd)];
	bool sense = false;
	int r = -ENXIO, i;

	memset(&card->dp, 0, sizeof(card->dp));
	rdn_dp_hpd_init(card, out);
	for (i = 0; i < 30; i++) {
		r = rdn_dp_dpcd_read(card, out, DP_DPCD_REV, dpcd, sizeof(dpcd));
		sense = rdn_dp_sense(card, out);
		if (r == (int)sizeof(dpcd) || (!sense && i >= 9))
			break;
		card->os->delay_us(card->os->cookie, 100000);
	}
	if (r != (int)sizeof(dpcd)) {
		rdn_log(card->os, RDN_LOG_INFO,
			"%s: no DisplayPort sink (%d, AUX status %08x, hot-plug %s)",
			out->name, r, (unsigned)card->dp.aux_status,
			sense ? "high" : "low");
		return r < 0 ? r : -EIO;
	}
	memcpy(card->dp.dpcd, dpcd, sizeof(dpcd));
	card->dp.sink = true;
	rdn_log(card->os, RDN_LOG_INFO,
		"%s: DPCD %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x, hot-plug %s, try %d",
		out->name, dpcd[0], dpcd[1], dpcd[2], dpcd[3], dpcd[4], dpcd[5],
		dpcd[6], dpcd[7], dpcd[8], dpcd[9], dpcd[10], dpcd[11],
		dpcd[12], dpcd[13], dpcd[14], sense ? "high" : "low", i + 1);
	return 0;
}

/*
 * One I2C-over-AUX transaction, repeated while the sink defers. Returns the
 * number of bytes moved or a negative errno value.
 */
static int aux_i2c(struct rdn_card *card, const struct rdn_output *out,
		   uint8_t request, uint8_t address, uint8_t *buf, int size)
{
	int ret = -ETIMEDOUT, retry;
	uint8_t reply;

	for (retry = 0; retry < 32; retry++) {
		ret = rdn_dp_aux(card, out, request, address, buf, size, &reply);
		if (ret < 0) {
			card->os->delay_us(card->os->cookie, 500);
			continue;
		}
		if ((reply & AUX_NATIVE_REPLY_MASK) == AUX_NATIVE_REPLY_NACK)
			return -ENXIO;
		if ((reply & AUX_NATIVE_REPLY_MASK) != AUX_NATIVE_REPLY_ACK) {
			ret = -EBUSY;
			card->os->delay_us(card->os->cookie, 500);
			continue;
		}
		if ((reply & AUX_I2C_REPLY_MASK) == AUX_I2C_REPLY_NACK)
			return -ENXIO;
		if ((reply & AUX_I2C_REPLY_MASK) != AUX_I2C_REPLY_ACK) {
			ret = -EBUSY;
			card->os->delay_us(card->os->cookie, 500);
			continue;
		}
		return ret;
	}
	return ret;
}

/*
 * One 128-byte EDID block: the offset written to the EEPROM, then read in
 * transactions of 16 bytes, all with "middle of transaction" set, and an
 * address-only read without it as the I2C stop.
 */
static int edid_block(struct rdn_card *card, const struct rdn_output *out,
		      int block, uint8_t *buf)
{
	uint8_t offset = (uint8_t)((block & 1) * RDN_EDID_BLOCK_SIZE);
	uint8_t segment = (uint8_t)(block >> 1);
	int r, got = 0, empty = 0;

	if (segment) {
		aux_i2c(card, out, RDN_AUX_I2C_WRITE | RDN_AUX_I2C_MOT,
			DDC_SEGMENT_ADDR, NULL, 0);
		aux_i2c(card, out, RDN_AUX_I2C_WRITE | RDN_AUX_I2C_MOT,
			DDC_SEGMENT_ADDR, &segment, 1);
	}
	r = aux_i2c(card, out, RDN_AUX_I2C_WRITE | RDN_AUX_I2C_MOT, DDC_ADDR,
		    NULL, 0);
	if (r < 0)
		goto stop;
	r = aux_i2c(card, out, RDN_AUX_I2C_WRITE | RDN_AUX_I2C_MOT, DDC_ADDR,
		    &offset, 1);
	if (r < 0)
		goto stop;
	r = aux_i2c(card, out, RDN_AUX_I2C_READ | RDN_AUX_I2C_MOT, DDC_ADDR,
		    NULL, 0);
	if (r < 0)
		goto stop;
	while (got < RDN_EDID_BLOCK_SIZE) {
		int n = RDN_EDID_BLOCK_SIZE - got;

		if (n > RDN_AUX_MAX)
			n = RDN_AUX_MAX;
		r = aux_i2c(card, out, RDN_AUX_I2C_READ | RDN_AUX_I2C_MOT,
			    DDC_ADDR, buf + got, n);
		if (r < 0)
			goto stop;
		if (r == 0 && ++empty > 16) {
			r = -EIO;
			goto stop;
		}
		got += r;
	}
	r = 0;
stop:
	aux_i2c(card, out, RDN_AUX_I2C_READ, DDC_ADDR, NULL, 0);
	return r;
}

int rdn_dp_edid_read(struct rdn_card *card, const struct rdn_output *out,
		     uint8_t *buf)
{
	int r, blocks, i, tries;

	/* A checksum can fail on a marginal AUX line: read again. */
	for (tries = 0; tries < 4; tries++) {
		r = edid_block(card, out, 0, buf);
		if (!r && rdn_edid_block_valid(buf, true))
			break;
		if (!r)
			r = -EIO;
	}
	if (r)
		return r;

	blocks = 1 + buf[126];
	if (blocks > RDN_EDID_MAX_BLOCKS)
		blocks = RDN_EDID_MAX_BLOCKS;
	for (i = 1; i < blocks; i++) {
		uint8_t *block = buf + i * RDN_EDID_BLOCK_SIZE;

		r = edid_block(card, out, i, block);
		if (r || !rdn_edid_block_valid(block, false)) {
			rdn_log(card->os, RDN_LOG_ERROR,
				"EDID extension block %d unreadable or invalid", i);
			break;
		}
	}
	return i * RDN_EDID_BLOCK_SIZE;
}

/* radeon_dp_get_dp_link_config(), 8 bits a colour */
int rdn_dp_link_config(struct rdn_card *card, const struct rdn_mode *mode)
{
	static const uint32_t link_rates[] = { 162000, 270000 };
	uint32_t max_rate = (uint32_t)card->dp.dpcd[DP_MAX_LINK_RATE] * 27000;
	unsigned max_lanes = card->dp.dpcd[DP_MAX_LANE_COUNT] &
		DP_MAX_LANE_COUNT_MASK;
	unsigned lanes, i;

	card->dp.rate = 0;
	card->dp.lanes = 0;
	if (!card->dp.sink)
		return -EINVAL;
	for (i = 0; i < sizeof(link_rates) / sizeof(link_rates[0]) &&
	     link_rates[i] <= max_rate; i++)
		for (lanes = 1; lanes <= max_lanes && lanes <= 4; lanes <<= 1)
			if (lanes * link_rates[i] * 8 / 24 >= mode->clock) {
				card->dp.rate = link_rates[i];
				card->dp.lanes = (uint8_t)lanes;
				return 0;
			}
	rdn_log(card->os, RDN_LOG_ERROR,
		"no DisplayPort link for %u kHz: the sink has %u lanes at %u kHz",
		(unsigned)mode->clock, max_lanes, (unsigned)max_rate);
	return -EINVAL;
}

struct train {
	struct rdn_card *card;
	const struct rdn_output *out;
	const struct rdn_dp_source *src;
	uint8_t link_status[DP_LINK_STATUS_SIZE];
};

static uint8_t lane_status(const uint8_t *link_status, int lane)
{
	return (link_status[lane >> 1] >> ((lane & 1) * 4)) & 0xf;
}

static bool clock_recovery_ok(const uint8_t *link_status, int lanes)
{
	int lane;

	for (lane = 0; lane < lanes; lane++)
		if (!(lane_status(link_status, lane) & DP_LANE_CR_DONE))
			return false;
	return true;
}

static bool channel_eq_ok(const uint8_t *link_status, int lanes)
{
	int lane;

	if (!(link_status[2] & DP_INTERLANE_ALIGN_DONE))
		return false;
	for (lane = 0; lane < lanes; lane++)
		if ((lane_status(link_status, lane) & DP_CHANNEL_EQ_BITS) !=
		    DP_CHANNEL_EQ_BITS)
			return false;
	return true;
}

/* dp_get_adjust_train(): the highest request of any lane, for all lanes */
static void get_adjust_train(const uint8_t *link_status, int lanes,
			     uint8_t train_set[4])
{
	uint8_t v = 0, p = 0;
	int lane;

	for (lane = 0; lane < lanes; lane++) {
		uint8_t b = link_status[4 + (lane >> 1)] >> ((lane & 1) * 4);
		uint8_t this_v = b & 0x3;
		uint8_t this_p = (uint8_t)(((b >> 2) & 0x3) <<
					   DP_TRAIN_PRE_EMPHASIS_SHIFT);

		if (this_v > v)
			v = this_v;
		if (this_p > p)
			p = this_p;
	}
	if (v >= DP_VOLTAGE_MAX)
		v |= DP_TRAIN_MAX_SWING_REACHED;
	if (p >= DP_PRE_EMPHASIS_MAX)
		p |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;
	for (lane = 0; lane < 4; lane++)
		train_set[lane] = v | p;
}

/* radeon_dp_update_vs_emph() */
static void update_vs_emph(struct train *t)
{
	struct rdn_dp *dp = &t->card->dp;

	/* set the initial vs/emph on the source; sets all lanes at once */
	t->src->drive(t->src->ctx, dp->train_set[0]);
	/* set the vs/emph on the sink */
	rdn_dp_dpcd_write(t->card, t->out, DP_TRAINING_LANE0_SET,
			  dp->train_set, dp->lanes);
}

/* radeon_dp_set_tp() */
static void set_tp(struct train *t, int tp)
{
	/* set training pattern on the source */
	t->src->pattern(t->src->ctx, tp);
	/* enable training pattern on the sink */
	dpcd_writeb(t->card, t->out, DP_TRAINING_PATTERN_SET, (uint8_t)tp);
}

/*
 * How long the sink wants between a change and the read of its status:
 * its TRAINING_AUX_RD_INTERVAL in units of 4 ms, or the default.
 */
static void train_delay(struct train *t, uint32_t default_us)
{
	uint32_t interval = t->card->dp.dpcd[DP_TRAINING_AUX_RD_INTERVAL] & 0x7f;

	if (interval > 4)
		interval = 4;
	t->card->os->delay_us(t->card->os->cookie,
			      interval ? interval * 4000 : default_us);
}

static int read_link_status(struct train *t)
{
	int r = rdn_dp_dpcd_read(t->card, t->out, DP_LANE0_1_STATUS,
				 t->link_status, DP_LINK_STATUS_SIZE);

	if (r < 0)
		rdn_log(t->card->os, RDN_LOG_ERROR,
			"DisplayPort link status unreadable (%d)", r);
	return r < 0 ? r : 0;
}

/* radeon_dp_link_train_init() */
static void link_train_init(struct train *t)
{
	struct rdn_dp *dp = &t->card->dp;
	uint8_t tmp;

	/* power up the sink */
	if (dp->dpcd[DP_DPCD_REV] >= 0x11) {
		dpcd_writeb(t->card, t->out, DP_SET_POWER, DP_SET_POWER_D0);
		t->card->os->delay_us(t->card->os->cookie, 1000);
	}

	/* the source does not spread its clock */
	dpcd_writeb(t->card, t->out, DP_DOWNSPREAD_CTRL, 0);

	/* set the lane count on the sink */
	tmp = dp->lanes;
	if (dp->dpcd[DP_MAX_LANE_COUNT] & DP_ENHANCED_FRAME_CAP)
		tmp |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
	dpcd_writeb(t->card, t->out, DP_LANE_COUNT_SET, tmp);

	/* set the link rate on the sink */
	dpcd_writeb(t->card, t->out, DP_LINK_BW_SET,
		    (uint8_t)(dp->rate / 27000));

	/* start training on the source */
	t->src->pattern(t->src->ctx, RDN_DP_PATTERN_START);

	/* disable the training pattern on the sink */
	dpcd_writeb(t->card, t->out, DP_TRAINING_PATTERN_SET, 0);
}

/* radeon_dp_link_train_cr() */
static int link_train_cr(struct train *t)
{
	struct rdn_dp *dp = &t->card->dp;
	uint8_t voltage = 0xff;
	int tries = 0, i;

	set_tp(t, 1);
	memset(dp->train_set, 0, 4);
	update_vs_emph(t);

	t->card->os->delay_us(t->card->os->cookie, 400);

	/* clock recovery loop */
	for (;;) {
		train_delay(t, 100);

		if (read_link_status(t))
			return -EIO;

		if (clock_recovery_ok(t->link_status, dp->lanes))
			return 0;

		for (i = 0; i < dp->lanes; i++)
			if ((dp->train_set[i] & DP_TRAIN_MAX_SWING_REACHED) == 0)
				break;
		if (i == dp->lanes) {
			rdn_log(t->card->os, RDN_LOG_ERROR,
				"DisplayPort clock recovery reached max voltage");
			return -EIO;
		}

		if ((dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK) == voltage) {
			if (++tries == 5) {
				rdn_log(t->card->os, RDN_LOG_ERROR,
					"DisplayPort clock recovery tried 5 times");
				return -EIO;
			}
		} else {
			tries = 0;
		}

		voltage = dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK;

		/* Compute new train_set as requested by sink */
		get_adjust_train(t->link_status, dp->lanes, dp->train_set);
		update_vs_emph(t);
	}
}

/* radeon_dp_link_train_ce() */
static int link_train_ce(struct train *t)
{
	struct rdn_dp *dp = &t->card->dp;
	int tries = 0;

	set_tp(t, (dp->dpcd[DP_MAX_LANE_COUNT] & DP_TPS3_SUPPORTED) ? 3 : 2);

	/* channel equalization loop */
	for (;;) {
		train_delay(t, 400);

		if (read_link_status(t))
			return -EIO;

		if (channel_eq_ok(t->link_status, dp->lanes))
			return 0;

		/* Try 5 times */
		if (tries > 5) {
			rdn_log(t->card->os, RDN_LOG_ERROR,
				"DisplayPort channel equalisation failed: 5 tries");
			return -EIO;
		}

		/* Compute new train_set as requested by sink */
		get_adjust_train(t->link_status, dp->lanes, dp->train_set);
		update_vs_emph(t);
		tries++;
	}
}

/* radeon_dp_link_train() */
int rdn_dp_link_train(struct rdn_card *card, const struct rdn_output *out,
		      const struct rdn_dp_source *src)
{
	struct rdn_dp *dp = &card->dp;
	const char *stage = "clock recovery";
	struct train t;
	int r;

	if (!dp->sink || !dp->lanes)
		return -EINVAL;
	memset(&t, 0, sizeof(t));
	t.card = card;
	t.out = out;
	t.src = src;

	link_train_init(&t);
	r = link_train_cr(&t);
	if (!r) {
		stage = "channel equalisation";
		r = link_train_ce(&t);
	}

	/* radeon_dp_link_train_finish() */
	card->os->delay_us(card->os->cookie, 400);
	/* disable the training pattern on the sink */
	dpcd_writeb(card, out, DP_TRAINING_PATTERN_SET, 0);
	/* disable the training pattern on the source */
	src->pattern(src->ctx, RDN_DP_PATTERN_DONE);

	rdn_log(card->os, r ? RDN_LOG_ERROR : RDN_LOG_INFO,
		"DisplayPort link %u lanes at %u kHz: %s%s, voltage %u, pre-emphasis %u, status %02x %02x %02x",
		(unsigned)dp->lanes, (unsigned)dp->rate,
		r ? "failed in " : "trained", r ? stage : "",
		(unsigned)(dp->train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK),
		(unsigned)((dp->train_set[0] & DP_TRAIN_PRE_EMPHASIS_MASK) >>
			   DP_TRAIN_PRE_EMPHASIS_SHIFT),
		t.link_status[0], t.link_status[1], t.link_status[2]);
	return r;
}
