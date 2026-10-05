/*
 * Hardware-free test of the DDC code: I2C line lookup in the VBIOS, the
 * bit-banged I2C master and EDID reading.
 *
 * A simulated EDID EEPROM (address 0x50, 256 bytes, one extension block) is
 * attached in turn to every I2C line pair the VBIOS describes. The mock card
 * models the open-drain lines behind the enable (drive low) and Y (read
 * back) registers. A line with no device must fail cleanly.
 *
 * Built for x86 and for big-endian PowerPC; both must print the same.
 *
 * Usage: i2c_edid <vbios.rom>
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hw/rdn_i2c.h"

#define MMIO_WORDS	(0x20000 / 4)

enum phase { IDLE, RX, ACK_OUT, TX, ACK_IN };

struct mock {
	uint32_t regs[MMIO_WORDS];
	uint64_t clock_ms;

	/* The line pair under test and the device on it. */
	struct rdn_i2c_bus bus;
	int present;
	uint8_t mem[256];

	int scl, sda;		/* resulting line levels */
	int sda_slave;		/* 1 = slave has released the data line */
	enum phase phase;
	int nbits, first_byte, rw, acked;
	uint8_t shift, ptr, tx;
	unsigned long starts, stops;
};

static void slave_falling(struct mock *m)
{
	switch (m->phase) {
	case RX:
		if (m->nbits != 8)
			break;
		if (m->first_byte) {
			if ((m->shift >> 1) != 0x50) {
				m->phase = IDLE;
				break;
			}
			m->rw = m->shift & 1;
		} else {
			m->ptr = m->shift;
		}
		m->sda_slave = 0;
		m->phase = ACK_OUT;
		break;
	case ACK_OUT:
		if (m->rw) {
			m->tx = m->mem[m->ptr];
			m->nbits = 0;
			m->sda_slave = (m->tx >> 7) & 1;
			m->phase = TX;
		} else {
			m->sda_slave = 1;
			m->nbits = 0;
			m->shift = 0;
			m->first_byte = 0;
			m->phase = RX;
		}
		break;
	case TX:
		m->nbits++;
		if (m->nbits == 8) {
			m->sda_slave = 1;
			m->phase = ACK_IN;
		} else {
			m->sda_slave = (m->tx >> (7 - m->nbits)) & 1;
		}
		break;
	case ACK_IN:
		if (m->acked) {
			m->ptr++;
			m->tx = m->mem[m->ptr];
			m->nbits = 0;
			m->sda_slave = (m->tx >> 7) & 1;
			m->phase = TX;
		} else {
			m->sda_slave = 1;
			m->phase = IDLE;
		}
		break;
	default:
		break;
	}
}

static void slave_rising(struct mock *m)
{
	if (m->phase == RX && m->nbits < 8) {
		m->shift = (uint8_t)((m->shift << 1) | m->sda);
		m->nbits++;
	} else if (m->phase == ACK_IN) {
		m->acked = !m->sda;
	}
}

/* Recompute the lines after the master changed its drivers. */
static void lines_update(struct mock *m)
{
	uint32_t en_clk = m->regs[m->bus.en_clk_reg / 4];
	uint32_t en_data = m->regs[m->bus.en_data_reg / 4];
	int scl_m = !(en_clk & m->bus.en_clk_mask);
	int sda_m = !(en_data & m->bus.en_data_mask);
	int old_scl = m->scl, old_sda = m->sda;
	int sda;

	if (!m->present) {
		m->scl = scl_m;
		m->sda = sda_m;
		return;
	}

	sda = sda_m && m->sda_slave;

	/* START and STOP: data changes while the clock stays high. */
	if (old_scl && scl_m && old_sda && !sda_m) {
		m->starts++;
		m->phase = RX;
		m->nbits = 0;
		m->shift = 0;
		m->first_byte = 1;
		m->sda_slave = 1;
	} else if (old_scl && scl_m && !old_sda && sda_m && m->sda_slave) {
		m->stops++;
		m->phase = IDLE;
	}

	m->scl = scl_m;
	m->sda = sda;
	if (old_scl && !scl_m)
		slave_falling(m);
	else if (!old_scl && scl_m)
		slave_rising(m);
	m->sda = sda_m && m->sda_slave;
}

static uint32_t os_mmio_read32(void *c, uint32_t off)
{
	struct mock *m = c;
	uint32_t v = m->regs[off / 4];

	if (m->bus.valid && off == m->bus.y_clk_reg) {
		v &= ~m->bus.y_clk_mask;
		if (m->scl)
			v |= m->bus.y_clk_mask;
	}
	if (m->bus.valid && off == m->bus.y_data_reg) {
		v &= ~m->bus.y_data_mask;
		if (m->sda)
			v |= m->bus.y_data_mask;
	}
	return v;
}

static void os_mmio_write32(void *c, uint32_t off, uint32_t v)
{
	struct mock *m = c;

	m->regs[off / 4] = v;
	if (m->bus.valid &&
	    (off == m->bus.en_clk_reg || off == m->bus.en_data_reg))
		lines_update(m);
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
	if (level != RDN_LOG_ERROR)
		return;
	fprintf(stderr, "rdn: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
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

int main(int argc, char **argv)
{
	static struct mock m;
	static uint8_t bios[0x20000];
	uint8_t edid[RDN_EDID_MAX_SIZE];
	struct rdn_i2c_bus bus;
	struct rdn_card card;
	struct rdn_os os;
	FILE *f;
	int i, r, failed = 0, tested = 0;

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

	if (rdn_card_init(&card, &os, bios)) {
		fprintf(stderr, "VBIOS not usable\n");
		return 2;
	}

	printf("i2c table: %d entries\n", rdn_i2c_bus_count(&card));
	for (i = 0; rdn_i2c_bus_by_index(&card, i, &bus); i++) {
		if (!bus.valid) {
			printf("line %d: id %02x not valid\n", i, bus.i2c_id);
			continue;
		}
		printf("line %d: id %02x hw %d mask %04x/%08x a %04x/%08x "
		       "en %04x/%08x,%08x y %04x/%08x,%08x\n",
		       i, bus.i2c_id, bus.hw_capable,
		       (unsigned)bus.mask_clk_reg, (unsigned)bus.mask_clk_mask,
		       (unsigned)bus.a_clk_reg, (unsigned)bus.a_clk_mask,
		       (unsigned)bus.en_clk_reg, (unsigned)bus.en_clk_mask,
		       (unsigned)bus.en_data_mask,
		       (unsigned)bus.y_clk_reg, (unsigned)bus.y_clk_mask,
		       (unsigned)bus.y_data_mask);

		/* A display is attached. */
		memset(&m, 0, sizeof(m));
		m.bus = bus;
		m.present = 1;
		m.scl = m.sda = m.sda_slave = 1;
		make_edid(m.mem);
		memset(edid, 0, sizeof(edid));
		r = rdn_edid_read(&card, &bus, edid);
		if (r != 256 || memcmp(edid, m.mem, 256)) {
			printf("  FAIL with device: returned %d\n", r);
			failed++;
		} else {
			printf("  with device: 256 bytes, identical, "
			       "%lu starts, %lu stops\n", m.starts, m.stops);
		}
		if (m.regs[bus.mask_clk_reg / 4] & bus.mask_clk_mask) {
			printf("  FAIL: pins left masked for software use\n");
			failed++;
		}

		/* Nothing is attached: no acknowledge. */
		memset(&m, 0, sizeof(m));
		m.bus = bus;
		m.scl = m.sda = 1;
		r = rdn_edid_read(&card, &bus, edid);
		if (r >= 0) {
			printf("  FAIL without device: returned %d\n", r);
			failed++;
		} else {
			printf("  without device: error %d\n", r);
		}
		tested++;
	}
	rdn_card_fini(&card);

	if (failed || !tested) {
		printf("FAIL: %d problems on %d lines\n", failed, tested);
		return 1;
	}
	printf("PASS: EDID read over %d simulated I2C lines\n", tested);
	return 0;
}
