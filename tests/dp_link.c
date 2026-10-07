/*
 * Hardware-free test of the DisplayPort code: AUX transactions through a
 * model of the card's AUX registers, with a simulated sink behind them
 * that has capabilities, an EDID EEPROM on I2C-over-AUX and link training
 * that needs two rounds of each phase. The sink defers every third
 * transaction and the first one of all is lost, as real ones do.
 *
 * The model is this project's reading of Linux's radeon_dp_auxch.c, so the
 * test checks the code against that reading, not against a card.
 *
 * Built for x86 and for big-endian PowerPC; both must print the same.
 *
 * Usage: dp_link <vbios.rom>
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hw/rdn_dp.h"
#include "../hw/rdn_i2c.h"

#define MMIO_WORDS	(0x20000 / 4)

#define AUX_BASE	0x62a0		/* channel 2 */
#define AUX_PAD_REG	0x6450
#define HPD4_STATUS	0x6040
#define HPD4_CONTROL	0x6048

struct mock {
	uint32_t regs[MMIO_WORDS];
	uint64_t clock_ms;
	int present;

	/* The AUX engine: bytes written, the reply, and a read position. */
	uint8_t tx[32], rx[32];
	int tx_len, rx_len, rx_pos;
	int transactions, lost, deferred;

	/* The sink */
	uint8_t dpcd[0x700];
	uint8_t edid[256];
	uint8_t i2c_offset;
	int i2c_writes, i2c_stops;
	int cr_rounds, eq_rounds;

	/* What training asked of the source */
	char source_log[256];
};

/* Link status as the sink would report it for what was written to it. */
static void sink_update_status(struct mock *m)
{
	int lanes = m->dpcd[0x101] & 0x1f, pattern = m->dpcd[0x102] & 0x3;
	uint8_t set = m->dpcd[0x103], status = 0, adjust = 0;
	int lane;

	if (pattern == 1) {
		/* Clock recovery wants voltage swing 1. */
		if ((set & 0x3) >= 1)
			status = 0x1;
		else
			adjust = 0x1;
	} else if (pattern == 2 || pattern == 3) {
		/* Equalisation wants pre-emphasis 1 as well. */
		if (((set >> 3) & 0x3) >= 1)
			status = 0x7;
		else {
			status = 0x1;
			adjust = 0x1 | (0x1 << 2);
		}
	}
	memset(m->dpcd + 0x202, 0, 6);
	for (lane = 0; lane < lanes; lane++) {
		m->dpcd[0x202 + (lane >> 1)] |= status << ((lane & 1) * 4);
		m->dpcd[0x206 + (lane >> 1)] |= adjust << ((lane & 1) * 4);
	}
	if (status == 0x7)
		m->dpcd[0x204] = 0x1;	/* inter-lane alignment */
}

static void sink_transaction(struct mock *m)
{
	uint8_t request = m->tx[0] >> 4;
	uint32_t address = ((uint32_t)(m->tx[0] & 0xf) << 16) |
		((uint32_t)m->tx[1] << 8) | m->tx[2];
	int len = m->tx_len > 3 ? m->tx[3] + 1 : 0, i;

	m->rx_len = 0;
	m->rx_pos = 0;
	m->transactions++;
	if (m->transactions % 3 == 0) {
		/* Defer: native for native requests, I2C for I2C ones. */
		m->rx[m->rx_len++] = (request & 0x8) ? 0x20 : 0x80;
		m->deferred++;
		return;
	}
	m->rx[m->rx_len++] = 0x00;	/* acknowledge */
	switch (request & ~RDN_AUX_I2C_MOT) {
	case RDN_AUX_NATIVE_READ:
		for (i = 0; i < len; i++)
			m->rx[m->rx_len++] = m->dpcd[address + i];
		break;
	case RDN_AUX_NATIVE_WRITE:
		for (i = 0; i < len; i++)
			m->dpcd[address + i] = m->tx[4 + i];
		if (address == 0x102)
			m->cr_rounds = m->eq_rounds = 0;
		sink_update_status(m);
		break;
	case RDN_AUX_I2C_WRITE:
		if (address != 0x50) {
			m->rx[0] = 0x40;	/* I2C not acknowledged */
			break;
		}
		if (len) {
			m->i2c_offset = m->tx[4];
			m->i2c_writes++;
		}
		break;
	case RDN_AUX_I2C_READ:
		if (address != 0x50) {
			m->rx[0] = 0x40;
			break;
		}
		/* A sink may send fewer bytes than asked: at most 12 here. */
		for (i = 0; i < len && i < 12; i++)
			m->rx[m->rx_len++] = m->edid[m->i2c_offset++];
		if (!(request & RDN_AUX_I2C_MOT))
			m->i2c_stops++;
		break;
	}
}

static uint32_t os_mmio_read32(void *c, uint32_t off)
{
	struct mock *m = c;

	if (off == AUX_BASE + 0x18) {
		if (m->rx_pos < m->rx_len)
			return (uint32_t)m->rx[m->rx_pos++] << 8;
		return 0;
	}
	if (off == HPD4_STATUS)
		return (m->present && (m->regs[HPD4_CONTROL / 4] & (1u << 28))) ?
			0x2 : 0;
	return m->regs[off / 4];
}

static void os_mmio_write32(void *c, uint32_t off, uint32_t v)
{
	struct mock *m = c;

	m->regs[off / 4] = v;
	if (off == AUX_BASE + 0x18) {
		/* Bit 0 turns the register to the reply; else a byte to send. */
		if (v & 1)
			m->rx_pos = 0;
		else if (m->tx_len < (int)sizeof(m->tx))
			m->tx[m->tx_len++] = (v >> 8) & 0xff;
	}
	if (off == AUX_BASE + 0x04 && (v & 1)) {
		int bytes = (v >> 16) & 0x1f;
		/* The channel needs its pad in AUX mode and to be enabled. */
		int usable = (m->regs[AUX_PAD_REG / 4] & (1 << 16)) &&
			(m->regs[AUX_BASE / 4] & 1) &&
			((m->regs[AUX_BASE / 4] >> 20) & 7) == 3;

		if (bytes > m->tx_len)
			bytes = m->tx_len;
		m->tx_len = bytes;
		if (!m->present || !usable || m->lost++ == 0) {
			m->rx_len = 0;
			m->regs[(AUX_BASE + 0x10) / 4] = 0x1 | (1 << 7);
		} else {
			sink_transaction(m);
			m->regs[(AUX_BASE + 0x10) / 4] =
				0x1 | ((uint32_t)m->rx_len << 24);
		}
		m->tx_len = 0;
	}
}

static void os_delay_us(void *c, uint32_t usec)
{
	(void)c;
	(void)usec;
}

static uint64_t os_time_ms(void *c)
{
	struct mock *m = c;

	return m->clock_ms++;
}

static void *os_alloc(void *c, size_t size)
{
	(void)c;
	return calloc(1, size);
}

static void os_free(void *c, void *ptr)
{
	(void)c;
	free(ptr);
}

static void os_log(void *c, enum rdn_log_level level, const char *fmt,
		   va_list ap)
{
	(void)c;
	(void)level;
	printf("rdn: ");
	vprintf(fmt, ap);
	printf("\n");
}

static void make_edid(uint8_t *e)
{
	static const uint8_t header[8] = {
		0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
	};
	uint8_t sum;
	int i, b;

	for (i = 0; i < 256; i++)
		e[i] = (uint8_t)(i * 37 + 11);
	memcpy(e, header, 8);
	e[126] = 1;	/* one extension block */
	for (b = 0; b < 2; b++) {
		sum = 0;
		for (i = 0; i < 127; i++)
			sum = (uint8_t)(sum + e[b * 128 + i]);
		e[b * 128 + 127] = (uint8_t)(0x100 - sum);
	}
}

static int src_pattern(void *ctx, int pattern)
{
	struct mock *m = ctx;
	char s[8];

	snprintf(s, sizeof(s), "P%d ", pattern);
	strncat(m->source_log, s, sizeof(m->source_log) - strlen(m->source_log) - 1);
	return 0;
}

static int src_drive(void *ctx, uint8_t lane_set)
{
	struct mock *m = ctx;
	char s[8];

	snprintf(s, sizeof(s), "D%02x ", lane_set);
	strncat(m->source_log, s, sizeof(m->source_log) - strlen(m->source_log) - 1);
	return 0;
}

int main(int argc, char **argv)
{
	static const struct { uint32_t clock; uint8_t max_rate, max_lanes;
			      uint32_t rate; uint8_t lanes; } cfg[] = {
		{ 148500, 0x0a, 4, 162000, 4 },
		{ 85500, 0x0a, 4, 162000, 2 },
		{ 25175, 0x06, 4, 162000, 1 },
		{ 148500, 0x0a, 2, 270000, 2 },
		{ 241500, 0x14, 4, 270000, 4 },
		{ 148500, 0x06, 2, 0, 0 },
	};
	static struct mock m;
	static uint8_t bios[0x20000];
	uint8_t edid[RDN_EDID_MAX_SIZE], buf[4];
	const struct rdn_output *out = rdn_output(RDN_OUTPUT_DP);
	struct rdn_dp_source src = { &m, src_pattern, src_drive };
	struct rdn_mode mode;
	struct rdn_card card;
	struct rdn_os os;
	unsigned i;
	int r, failed = 0;
	FILE *f;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <vbios.rom>\n", argv[0]);
		return 2;
	}
	f = fopen(argv[1], "rb");
	if (!f || fread(bios, 1, sizeof(bios), f) < 0x200) {
		fprintf(stderr, "cannot read %s\n", argv[1]);
		return 2;
	}
	fclose(f);

	memset(&os, 0, sizeof(os));
	os.cookie = &m;
	os.mmio_read32 = os_mmio_read32;
	os.mmio_write32 = os_mmio_write32;
	os.delay_us = os_delay_us;
	os.time_ms = os_time_ms;
	os.alloc = os_alloc;
	os.free = os_free;
	os.log = os_log;

	if (rdn_card_init(&card, &os, bios) || !out) {
		fprintf(stderr, "VBIOS not usable\n");
		return 2;
	}
	printf("output %s: connector %02x transmitter %02x/%u encoder %u hpd %u i2c %02x\n",
	       out->name, out->connector_id, out->transmitter_id,
	       out->transmitter_sel, out->dig_encoder, out->hpd, out->i2c_id);

	/* Nothing plugged in. */
	r = rdn_dp_detect(&card, out);
	printf("no sink: detect %d, sink %d\n", r, card.dp.sink);
	if (r >= 0 || card.dp.sink)
		failed++;

	/* A DisplayPort 1.1 sink: 2.7 Gbit/s, 4 lanes, enhanced framing. */
	m.present = 1;
	m.dpcd[0x000] = 0x11;
	m.dpcd[0x001] = 0x0a;
	m.dpcd[0x002] = 0x84;
	m.dpcd[0x003] = 0x01;
	make_edid(m.edid);
	r = rdn_dp_detect(&card, out);
	printf("sink: detect %d, sink %d, pad %08x, control %08x\n", r,
	       card.dp.sink, (unsigned)m.regs[AUX_PAD_REG / 4],
	       (unsigned)m.regs[AUX_BASE / 4]);
	if (r || !card.dp.sink || memcmp(card.dp.dpcd, m.dpcd, 15))
		failed++;

	memset(edid, 0, sizeof(edid));
	r = rdn_dp_edid_read(&card, out, edid);
	printf("EDID: %d bytes, %s, %d offset writes, %d stops\n", r,
	       memcmp(edid, m.edid, 256) ? "wrong" : "right", m.i2c_writes,
	       m.i2c_stops);
	if (r != 256 || memcmp(edid, m.edid, 256) || m.i2c_stops != 2)
		failed++;

	/* A write and a read back of several bytes. */
	buf[0] = 0xa1; buf[1] = 0xb2; buf[2] = 0xc3; buf[3] = 0xd4;
	r = rdn_dp_dpcd_write(&card, out, 0x300, buf, 4);
	memset(buf, 0, sizeof(buf));
	if (r == 4)
		r = rdn_dp_dpcd_read(&card, out, 0x300, buf, 4);
	printf("DPCD write and read: %d, %02x %02x %02x %02x\n", r, buf[0],
	       buf[1], buf[2], buf[3]);
	if (r != 4 || buf[0] != 0xa1 || buf[3] != 0xd4)
		failed++;

	/* Lane count and rate for a few modes and sinks. */
	memset(&mode, 0, sizeof(mode));
	for (i = 0; i < sizeof(cfg) / sizeof(cfg[0]); i++) {
		card.dp.dpcd[1] = cfg[i].max_rate;
		card.dp.dpcd[2] = cfg[i].max_lanes;
		mode.clock = cfg[i].clock;
		r = rdn_dp_link_config(&card, &mode);
		printf("%u kHz on %u lanes at code %02x: %d, %u lanes at %u kHz\n",
		       (unsigned)mode.clock, cfg[i].max_lanes, cfg[i].max_rate,
		       r, card.dp.lanes, (unsigned)card.dp.rate);
		if (card.dp.rate != cfg[i].rate || card.dp.lanes != cfg[i].lanes ||
		    (r != 0) != (cfg[i].rate == 0))
			failed++;
	}

	/* Training: 1080p, which is 4 lanes at 1.62 Gbit/s. */
	memcpy(card.dp.dpcd, m.dpcd, 15);
	mode.clock = 148500;
	rdn_dp_link_config(&card, &mode);
	r = rdn_dp_link_train(&card, out, &src);
	printf("training: %d, source saw %s\n", r, m.source_log);
	printf("sink: power %02x, spread %02x, rate %02x, lanes %02x, pattern %02x, drive %02x %02x %02x %02x\n",
	       m.dpcd[0x600], m.dpcd[0x107], m.dpcd[0x100], m.dpcd[0x101],
	       m.dpcd[0x102], m.dpcd[0x103], m.dpcd[0x104], m.dpcd[0x105],
	       m.dpcd[0x106]);
	if (r || m.dpcd[0x600] != 1 || m.dpcd[0x100] != 0x06 ||
	    m.dpcd[0x101] != 0x84 || m.dpcd[0x102] != 0 ||
	    m.dpcd[0x103] != 0x09 || m.dpcd[0x106] != 0x09 ||
	    strcmp(m.source_log, "P0 P1 D00 D01 P2 D09 P4 "))
		failed++;

	/* A sink that never recovers the clock: training must give up. */
	m.source_log[0] = 0;
	m.present = 0;
	r = rdn_dp_link_train(&card, out, &src);
	printf("training without a sink: %d, source saw %s\n", r, m.source_log);
	if (r >= 0)
		failed++;

	printf("%d transactions, %d deferred\n", m.transactions, m.deferred);
	rdn_card_fini(&card);
	if (failed) {
		printf("FAIL: %d checks\n", failed);
		return 1;
	}
	printf("PASS: DisplayPort AUX, EDID, link configuration and training\n");
	return 0;
}
