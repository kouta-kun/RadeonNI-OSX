/*
 * Hardware-free test of the GART code.
 *
 * Runs rdn_gart_enable() against a mock card that answers from the
 * reference trace of the Linux driver's start (phase a1). From the trace's
 * first write to VM_L2_CNTL on, every register access of ours must be
 * found in the trace in the same order, with the same value for a write;
 * trace entries in between are skipped (Linux flushes the TLB once more
 * and sets its other tables up). Exempt, because ours legitimately
 * differ: the system aperture (Linux has moved video memory to address 0,
 * we have not), the table's address and the dummy page's. Then the table
 * itself is checked: every entry the dummy page, and what set_page and
 * clear_page leave.
 *
 * Built for x86 and for big-endian PowerPC; both must print the same.
 *
 * Usage: gart_replay <init-phase>
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hw/rdn_gart.h"

#define APERTURE_SIZE	(4u << 20)
#define TABLE_OFFSET	(2u << 20)
#define DUMMY		0x12345000ull

struct access {
	uint8_t write;
	uint32_t offset, value;
};

struct mock {
	struct access *trace;
	size_t trace_len, pos, matched, exempt;
	int failed;
	uint32_t digest;
};

static int is_exempt(uint32_t offset)
{
	return offset == 0x2034 || offset == 0x2038 || offset == 0x203c ||
	       offset == 0x153c || offset == 0x1518;
}

static uint32_t mock_access(struct mock *m, int write, uint32_t offset,
			    uint32_t value)
{
	const uint32_t words[3] = { (uint32_t)write, offset, write ? value : 0 };
	size_t i;
	int w, b;

	for (w = 0; w < 3; w++)
		for (b = 0; b < 4; b++) {
			m->digest ^= (words[w] >> (8 * b)) & 0xff;
			m->digest *= 16777619u;
		}
	if (is_exempt(offset)) {
		m->exempt++;
		return 0;
	}
	if (m->failed)
		return 0;
	for (i = m->pos; i < m->trace_len; i++) {
		const struct access *t = &m->trace[i];

		if (t->write == write && t->offset == offset &&
		    (!write || t->value == value)) {
			m->pos = i + 1;
			m->matched++;
			return t->value;
		}
	}
	fprintf(stderr, "not in the trace after entry %zu: %c %05x %08x\n",
		m->pos, write ? 'W' : 'R', (unsigned)offset, (unsigned)value);
	m->failed = 1;
	return 0;
}

static uint32_t os_mmio_read32(void *c, uint32_t off)
{
	return mock_access(c, 0, off, 0);
}

static void os_mmio_write32(void *c, uint32_t off, uint32_t v)
{
	mock_access(c, 1, off, v);
}

static void os_delay_us(void *c, uint32_t usec)
{
	(void)c;
	(void)usec;
}

static struct access *load_trace(const char *path, size_t *count)
{
	FILE *f = fopen(path, "r");
	struct access *t = NULL;
	size_t n = 0, cap = 0;
	char line[128], dir, kind[16];
	unsigned offset, value;

	if (!f)
		return NULL;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%c %15s %x %x", &dir, kind, &offset, &value) != 4 ||
		    strcmp(kind, "mmio"))
			continue;
		if (n == cap) {
			cap = cap ? cap * 2 : 4096;
			t = realloc(t, cap * sizeof(*t));
		}
		t[n].write = dir == 'W';
		t[n].offset = offset;
		t[n].value = value;
		n++;
	}
	fclose(f);
	*count = n;
	return t;
}

static void os_log(void *c, enum rdn_log_level level, const char *fmt, va_list ap)
{
	(void)c; (void)level; (void)fmt; (void)ap;
}

static uint64_t entry(const uint32_t *aperture, uint32_t page)
{
	const uint8_t *p = (const uint8_t *)aperture + TABLE_OFFSET + page * 8;
	uint64_t v = 0;
	int i;

	/* little-endian in video memory, whatever the host */
	for (i = 7; i >= 0; i--)
		v = (v << 8) | p[i];
	return v;
}

int main(int argc, char **argv)
{
	static struct mock m;
	struct rdn_card card;
	struct rdn_accel accel;
	struct rdn_gart gart;
	struct rdn_os os;
	uint32_t *aperture;
	uint32_t page;
	size_t i;
	int r, bad = 0;

	if (argc != 2) {
		fprintf(stderr, "usage: gart_replay <init-phase>\n");
		return 2;
	}
	m.trace = load_trace(argv[1], &m.trace_len);
	aperture = calloc(1, APERTURE_SIZE);
	if (!m.trace || !aperture) {
		fprintf(stderr, "cannot read the inputs\n");
		return 2;
	}
	for (i = 0; i < m.trace_len; i++)
		if (m.trace[i].write && m.trace[i].offset == 0x1400)
			break;
	if (i == m.trace_len) {
		printf("FAIL: the trace does not enable a GART\n");
		return 1;
	}
	m.pos = i;
	m.digest = 2166136261u;

	memset(&os, 0, sizeof(os));
	os.cookie = &m;
	os.mmio_read32 = os_mmio_read32;
	os.mmio_write32 = os_mmio_write32;
	os.delay_us = os_delay_us;
	os.log = os_log;
	memset(&card, 0, sizeof(card));
	card.os = &os;
	memset(&accel, 0, sizeof(accel));
	accel.card = &card;
	accel.vram_base = 0xf00000000ull;
	accel.vram_size = 1024ull << 20;
	accel.aperture = aperture;
	accel.aperture_size = APERTURE_SIZE;

	r = rdn_gart_enable(&gart, &accel, TABLE_OFFSET, DUMMY);
	for (page = 0; page < RDN_GART_PAGES; page++)
		bad += entry(aperture, page) != (DUMMY | 0x67);
	rdn_gart_set_page(&gart, 5, 0x1abcde000ull);
	bad += entry(aperture, 5) != (0x1abcde000ull | 0x67);
	rdn_gart_clear_page(&gart, 5);
	bad += entry(aperture, 5) != (DUMMY | 0x67);
	bad += rdn_gart_addr(3) != 0x40003000ull;

	if (r || m.failed || bad || m.matched != 17) {
		printf("FAIL: result %d, %zu accesses matched, %d wrong table entries\n",
		       r, m.matched, bad);
		return 1;
	}
	printf("PASS: GART enable, %zu accesses found in order in the trace (%zu exempt), "
	       "%u table entries right, digest %08x\n", m.matched, m.exempt,
	       (unsigned)RDN_GART_PAGES, (unsigned)m.digest);
	return 0;
}
