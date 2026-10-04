/*
 * Hardware-free test of the AtomBIOS interpreter.
 *
 * Runs ASIC_Init from a VBIOS dump against a mock card whose behaviour comes
 * from a reference trace of the Linux radeon driver (a phase file written by
 * scripts/trace-split.py). Every register access the interpreter makes must
 * be the next access in the trace: same direction, same BAR, same offset and,
 * for writes, the same value. Reads return the value the real card returned.
 *
 * The same source is built for x86 and for big-endian PowerPC; the PowerPC
 * binary runs under qemu-ppc. Both must match the trace and print the same
 * digest.
 *
 * Usage: atom_replay <vbios.rom> <phase-file>
 *
 * Copyright (c) 2026 the osx-gpu contributors
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hw/rdn_atom.h"

enum { KIND_MMIO, KIND_IO };

struct access {
	uint8_t write;
	uint8_t kind;
	uint32_t offset;
	uint32_t value;
};

struct mock {
	const struct access *trace;
	size_t trace_len;
	size_t pos;		/* next trace entry to match */
	size_t matched;
	int failed;
	int quiet;
	struct access first;	/* first access seen, for synchronisation */
	int have_first;
	uint64_t clock_ms;
	uint32_t digest;
};

static void digest_add(struct mock *m, const struct access *a)
{
	const uint32_t words[3] = {
		((uint32_t)a->write << 8) | a->kind, a->offset, a->value
	};
	int i, b;

	/* FNV-1a over the bytes of each word, least significant first. */
	for (i = 0; i < 3; i++)
		for (b = 0; b < 4; b++) {
			m->digest ^= (words[i] >> (8 * b)) & 0xff;
			m->digest *= 16777619u;
		}
}

static uint32_t mock_access(struct mock *m, int write, int kind,
			    uint32_t offset, uint32_t value)
{
	const struct access *t;
	struct access a;

	a.write = (uint8_t)write;
	a.kind = (uint8_t)kind;
	a.offset = offset;
	a.value = value;

	if (!m->have_first) {
		m->first = a;
		m->have_first = 1;
	}
	if (m->failed || !m->trace)
		return 0;

	if (m->pos >= m->trace_len) {
		if (!m->quiet)
			fprintf(stderr, "trace exhausted after %zu accesses\n",
				m->matched);
		m->failed = 1;
		return 0;
	}
	t = &m->trace[m->pos];
	if (t->write != a.write || t->kind != a.kind || t->offset != a.offset ||
	    (write && t->value != a.value)) {
		if (!m->quiet)
			fprintf(stderr,
				"mismatch at trace entry %zu after %zu matches:\n"
				"  expected %c %s %05x %08x\n"
				"  got      %c %s %05x %08x\n",
				m->pos, m->matched,
				t->write ? 'W' : 'R', t->kind ? "io" : "mmio",
				t->offset, t->value,
				write ? 'W' : 'R', kind ? "io" : "mmio",
				offset, value);
		m->failed = 1;
		return 0;
	}
	m->pos++;
	m->matched++;
	a.value = t->value;
	digest_add(m, &a);
	return t->value;
}

static uint32_t os_mmio_read32(void *c, uint32_t off)
{
	return mock_access(c, 0, KIND_MMIO, off, 0);
}

static void os_mmio_write32(void *c, uint32_t off, uint32_t v)
{
	mock_access(c, 1, KIND_MMIO, off, v);
}

static uint32_t os_io_read32(void *c, uint32_t off)
{
	return mock_access(c, 0, KIND_IO, off, 0);
}

static void os_io_write32(void *c, uint32_t off, uint32_t v)
{
	mock_access(c, 1, KIND_IO, off, v);
}

static void os_delay_us(void *c, uint32_t usec)
{
	struct mock *m = c;

	m->clock_ms += usec / 1000 + 1;
}

static uint64_t os_time_ms(void *c)
{
	struct mock *m = c;

	/* Advance on every query so a stuck loop times out. */
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
	struct mock *m = c;

	if (m->quiet || level == RDN_LOG_DEBUG)
		return;
	fprintf(stderr, "atom: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
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
	buf = malloc(n + 1);
	if (fread(buf, 1, n, f) != (size_t)n) {
		fclose(f);
		free(buf);
		return NULL;
	}
	fclose(f);
	((char *)buf)[n] = 0;
	*len = n;
	return buf;
}

/* Parse "R|W mmio|io <offset> <value> <size>" lines; skip config space. */
static struct access *load_trace(const char *path, size_t *count)
{
	FILE *f = fopen(path, "r");
	struct access *t = NULL;
	size_t n = 0, cap = 0;
	char line[128], rw, kind[16];
	unsigned off, val, size;

	if (!f)
		return NULL;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%c %15s %x %x %u", &rw, kind, &off, &val, &size) != 5)
			continue;
		if (strcmp(kind, "mmio") && strcmp(kind, "io"))
			continue;
		if (n == cap) {
			cap = cap ? cap * 2 : 4096;
			t = realloc(t, cap * sizeof(*t));
		}
		t[n].write = rw == 'W';
		t[n].kind = strcmp(kind, "io") ? KIND_MMIO : KIND_IO;
		t[n].offset = off;
		t[n].value = val;
		n++;
	}
	fclose(f);
	*count = n;
	return t;
}

static int run_asic_init(struct mock *m, void *bios)
{
	struct rdn_os os;
	struct rdn_atom atom;
	int ret;

	memset(&os, 0, sizeof(os));
	os.cookie = m;
	os.mmio_read32 = os_mmio_read32;
	os.mmio_write32 = os_mmio_write32;
	os.io_read32 = os_io_read32;
	os.io_write32 = os_io_write32;
	os.delay_us = os_delay_us;
	os.time_ms = os_time_ms;
	os.alloc = os_alloc;
	os.free = os_free;
	os.log = os_log;

	if (rdn_atom_init(&atom, &os, bios))
		return -1;
	ret = atom_asic_init(atom.ctx);
	rdn_atom_fini(&atom);
	return ret;
}

int main(int argc, char **argv)
{
	struct access *trace;
	size_t bios_len, trace_len, start, best_start = 0, best = 0;
	struct mock probe, m;
	void *bios;
	int ret = -1, found = 0;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <vbios.rom> <phase-file>\n", argv[0]);
		return 2;
	}
	bios = read_file(argv[1], &bios_len);
	trace = load_trace(argv[2], &trace_len);
	if (!bios || !trace) {
		fprintf(stderr, "cannot read inputs\n");
		return 2;
	}

	/* Find out what ASIC_Init does first, to locate it in the trace. */
	memset(&probe, 0, sizeof(probe));
	probe.quiet = 1;
	run_asic_init(&probe, bios);
	if (!probe.have_first) {
		fprintf(stderr, "ASIC_Init made no register access\n");
		return 1;
	}

	for (start = 0; start < trace_len && !found; start++) {
		const struct access *t = &trace[start];

		if (t->write != probe.first.write || t->kind != probe.first.kind ||
		    t->offset != probe.first.offset ||
		    (t->write && t->value != probe.first.value))
			continue;
		memset(&m, 0, sizeof(m));
		m.trace = trace;
		m.trace_len = trace_len;
		m.pos = start;
		m.quiet = 1;
		m.digest = 2166136261u;
		ret = run_asic_init(&m, bios);
		if (m.matched > best) {
			best = m.matched;
			best_start = start;
		}
		if (!m.failed && ret == 0)
			found = 1;
	}

	if (!found) {
		/* Rerun the best candidate verbosely to show where it diverges. */
		memset(&m, 0, sizeof(m));
		m.trace = trace;
		m.trace_len = trace_len;
		m.pos = best_start;
		m.digest = 2166136261u;
		ret = run_asic_init(&m, bios);
		printf("FAIL: best candidate starts at trace entry %zu, "
		       "%zu accesses matched, asic_init returned %d\n",
		       best_start, m.matched, ret);
		return 1;
	}

	printf("PASS: asic_init matches trace entries %zu..%zu "
	       "(%zu accesses), digest %08x\n",
	       best_start, m.pos - 1, m.matched, (unsigned)m.digest);
	return 0;
}
