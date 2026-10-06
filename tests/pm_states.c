/*
 * Hardware-free test of the power state code: the PowerPlay table of a
 * VBIOS dump and the command tables that switch state.
 *
 * Prints the boot state, the state rdn_pm_performance_state() chooses and
 * every state in the table, then runs the switch to the performance state
 * and back against a mock card. The mock is a plain register file: a read
 * returns what was last written there, or zero, and the engine PLL's
 * change status follows its requests. That is not how the card answers,
 * so what the tables do here is not what they do on the card; the point
 * is that the interpreter runs them the same way on x86 and on big-endian
 * PowerPC. The memory clock is left out: SetMemoryClock waits for lock
 * bits of the memory PLL that this mock does not have. With -v every
 * access is printed.
 *
 * Usage: pm_states [-v] <vbios.rom>
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hw/rdn_pm.h"

#define MMIO_WORDS	(0x20000 / 4)

struct mock {
	uint32_t regs[MMIO_WORDS];
	uint32_t index;		/* the indirect register pair's address */
	uint64_t clock_ms;
	unsigned accesses;
	uint32_t digest;
	int verbose;
	int errors;
};

static void note(struct mock *m, int write, int io, uint32_t off, uint32_t v)
{
	const uint32_t words[3] = { ((uint32_t)write << 8) | io, off, v };
	int i, b;

	/* FNV-1a over the bytes of each word, least significant first. */
	for (i = 0; i < 3; i++)
		for (b = 0; b < 4; b++) {
			m->digest ^= (words[i] >> (8 * b)) & 0xff;
			m->digest *= 16777619u;
		}
	m->accesses++;
	if (m->verbose)
		printf("  %c %s 0x%05x 0x%08x\n", write ? 'W' : 'R',
		       io ? "io" : "mmio", (unsigned)off, (unsigned)v);
}

static uint32_t os_mmio_read32(void *c, uint32_t off)
{
	struct mock *m = c;
	uint32_t v = off / 4 < MMIO_WORDS ? m->regs[off / 4] : 0;

	/*
	 * CG_SPLL_STATUS: SetEngineClock waits here for the PLL to report
	 * that it has taken a change (SPLL_CHG_STATUS follows
	 * SCLK_MUX_UPDATE, SPLL_CTLREQ_CHG and bit 24 of CG_SPLL_FUNC_CNTL_2).
	 */
	if (off == 0x60c)
		v = (m->regs[0x604 / 4] & ((1 << 26) | (1 << 24) | (1 << 23))) ? (1 << 1) : 0;
	note(m, 0, 0, off, v);
	return v;
}

static void os_mmio_write32(void *c, uint32_t off, uint32_t v)
{
	struct mock *m = c;

	if (off / 4 < MMIO_WORDS)
		m->regs[off / 4] = v;
	note(m, 1, 0, off, v);
}

/* The I/O BAR: an index/data pair onto the same registers. */
static uint32_t os_io_read32(void *c, uint32_t off)
{
	struct mock *m = c;
	uint32_t v = 0;

	if (off == 0)
		v = m->index;
	else if (off == 4 && m->index / 4 < MMIO_WORDS)
		v = m->regs[m->index / 4];
	else if (off / 4 < MMIO_WORDS)
		v = m->regs[off / 4];
	note(m, 0, 1, off, v);
	return v;
}

static void os_io_write32(void *c, uint32_t off, uint32_t v)
{
	struct mock *m = c;

	if (off == 0)
		m->index = v;
	else if (off == 4 && m->index / 4 < MMIO_WORDS)
		m->regs[m->index / 4] = v;
	else if (off / 4 < MMIO_WORDS)
		m->regs[off / 4] = v;
	note(m, 1, 1, off, v);
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

	if (level == RDN_LOG_ERROR)
		m->errors++;
	else if (level != RDN_LOG_INFO)
		return;
	printf("%s", level == RDN_LOG_ERROR ? "error: " : "");
	vprintf(fmt, ap);
	printf("\n");
}

static void *read_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	void *buf;
	long len;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(len > 0 ? (size_t)len : 1);
	if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	return buf;
}

static void print_state(const char *name, const struct rdn_pm_state *s)
{
	printf("%s: engine %u0 kHz, memory %u0 kHz, vddc %u, vddci %u\n", name,
	       (unsigned)s->sclk, (unsigned)s->mclk, s->vddc, s->vddci);
}

static int run_switch(struct mock *m, struct rdn_card *card, const char *name,
		      const struct rdn_pm_state *from,
		      const struct rdn_pm_state *to)
{
	unsigned before = m->accesses;
	int r;

	if (m->verbose)
		printf("%s:\n", name);
	r = rdn_pm_set(card, from, to, RDN_PM_VOLTAGE | RDN_PM_SCLK);
	printf("%s: result %d, %u register accesses\n", name, r,
	       m->accesses - before);
	return r;
}

int main(int argc, char **argv)
{
	static struct mock m;
	struct rdn_pm_state boot, perf;
	struct rdn_card card;
	struct rdn_os os;
	void *bios;
	int failed = 0;

	if (argc > 1 && !strcmp(argv[1], "-v")) {
		m.verbose = 1;
		argv++;
		argc--;
	}
	if (argc != 2) {
		fprintf(stderr, "usage: pm_states [-v] <vbios.rom>\n");
		return 2;
	}
	bios = read_file(argv[1]);
	if (!bios) {
		fprintf(stderr, "cannot read %s\n", argv[1]);
		return 2;
	}
	m.digest = 2166136261u;

	memset(&os, 0, sizeof(os));
	os.cookie = &m;
	os.mmio_read32 = os_mmio_read32;
	os.mmio_write32 = os_mmio_write32;
	os.io_read32 = os_io_read32;
	os.io_write32 = os_io_write32;
	os.delay_us = os_delay_us;
	os.time_ms = os_time_ms;
	os.alloc = os_alloc;
	os.free = os_free;
	os.log = os_log;

	if (rdn_card_init(&card, &os, bios)) {
		printf("FAIL: VBIOS not accepted\n");
		return 1;
	}
	rdn_pm_log_states(&card);
	if (rdn_pm_boot_state(&card, &boot) ||
	    rdn_pm_performance_state(&card, &perf)) {
		printf("FAIL: no boot or performance state\n");
		return 1;
	}
	print_state("boot", &boot);
	print_state("performance", &perf);
	if (perf.sclk <= boot.sclk || perf.mclk < boot.mclk)
		failed = 1;

	failed |= run_switch(&m, &card, "boot to performance", &boot, &perf) != 0;
	failed |= run_switch(&m, &card, "performance to boot", &perf, &boot) != 0;
	rdn_card_fini(&card);
	free(bios);

	if (failed || m.errors) {
		printf("FAIL: %d errors\n", m.errors);
		return 1;
	}
	printf("PASS: %u accesses, digest %08x\n", m.accesses, (unsigned)m.digest);
	return 0;
}
