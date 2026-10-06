/*
 * Hardware-free test of the memory controller microcode load.
 *
 * Runs rdn_mc_load_microcode() with TURKS_mc.bin against a mock card that
 * answers from the reference trace of the Linux driver's start (phase a1
 * from scripts/trace-split.py). The run starts at the read of MC_SEQ_MISC0
 * before the trace's first write to MC_SEQ_SUP_CNTL, and from there every
 * access of ours must be the very next entry of the trace: same direction,
 * same register, same value for a write. The test also wants the count
 * Linux's routine makes: 2 reads, 2 + 58 + 6024 + 3 writes, 1 read.
 *
 * Built for x86 and for big-endian PowerPC; both must print the same.
 *
 * Usage: mc_replay <mc.bin> <init-phase>
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hw/rdn_mc.h"

struct access {
	uint8_t write;
	uint32_t offset, value;
};

struct mock {
	struct access *trace;
	size_t trace_len, pos, matched;
	int failed;
	uint32_t digest;
};

static uint32_t mock_access(struct mock *m, int write, uint32_t offset,
			    uint32_t value)
{
	const struct access *t = &m->trace[m->pos];
	const uint32_t words[3] = { (uint32_t)write, offset, write ? value : 0 };
	int i, b;

	if (m->failed)
		return 0;
	if (m->pos >= m->trace_len || t->write != write || t->offset != offset ||
	    (write && t->value != value)) {
		fprintf(stderr, "trace entry %zu is %c %05x %08x, ours is %c %05x %08x\n",
			m->pos, t->write ? 'W' : 'R', (unsigned)t->offset,
			(unsigned)t->value, write ? 'W' : 'R', (unsigned)offset,
			(unsigned)value);
		m->failed = 1;
		return 0;
	}
	for (i = 0; i < 3; i++)
		for (b = 0; b < 4; b++) {
			m->digest ^= (words[i] >> (8 * b)) & 0xff;
			m->digest *= 16777619u;
		}
	m->pos++;
	m->matched++;
	return t->value;
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

static void *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	void *buf;
	long n;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(n > 0 ? (size_t)n : 1);
	if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	*len = (size_t)n;
	return buf;
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

int main(int argc, char **argv)
{
	static struct mock m;
	struct rdn_card card;
	struct rdn_os os;
	size_t fw_len = 0, i;
	void *fw;
	int r;

	if (argc != 3) {
		fprintf(stderr, "usage: mc_replay <mc.bin> <init-phase>\n");
		return 2;
	}
	fw = read_file(argv[1], &fw_len);
	m.trace = load_trace(argv[2], &m.trace_len);
	if (!fw || !m.trace) {
		fprintf(stderr, "cannot read the inputs\n");
		return 2;
	}
	/* The read of MC_SEQ_MISC0 before the first write to MC_SEQ_SUP_CNTL. */
	for (i = 2; i < m.trace_len; i++)
		if (m.trace[i].write && m.trace[i].offset == 0x28c8)
			break;
	if (i == m.trace_len) {
		printf("FAIL: the trace has no microcode load\n");
		return 1;
	}
	m.pos = i - 2;
	m.digest = 2166136261u;

	memset(&os, 0, sizeof(os));
	os.cookie = &m;
	os.mmio_read32 = os_mmio_read32;
	os.mmio_write32 = os_mmio_write32;
	os.delay_us = os_delay_us;
	memset(&card, 0, sizeof(card));
	card.os = &os;

	r = rdn_mc_load_microcode(&card, fw, fw_len);
	if (r || m.failed || m.matched != 2 + 2 + 58 + 6024 + 3 + 1) {
		printf("FAIL: result %d, %zu accesses matched\n", r, m.matched);
		return 1;
	}
	printf("PASS: microcode load is trace entries %zu..%zu (%zu accesses), digest %08x\n",
	       i - 2, m.pos - 1, m.matched, (unsigned)m.digest);
	return 0;
}
