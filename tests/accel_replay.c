/*
 * Hardware-free test of the acceleration bring-up.
 *
 * Runs rdn_accel_init() (3D engine setup, CP microcode load, ring start,
 * ring test) against a mock card driven by the reference trace of the Linux
 * driver's start (phase a1 from scripts/trace-split.py). The rule is the
 * one modeset_replay uses: every register access we make must occur in the
 * trace, in the same order, with the same value for writes; trace entries
 * we do not make are skipped. Reads return what the real card returned.
 *
 * Exempt, because this library deliberately differs from Linux there:
 *  - where things are in the GPU's address space (the ring is in video
 *    memory, which stays where ASIC_Init put it);
 *  - write-back, which Linux turns on and we leave off, and with it the
 *    ring's read pointer and the scratch registers, which we read back
 *    through the register interface. The mock answers those itself.
 *  - the flush of the card's host-access cache before each ring commit,
 *    which a ring in video memory needs and Linux's ring does not.
 *
 * The ring's memory is an ordinary buffer here; its contents are part of
 * the digest, so the x86 and PowerPC runs must produce the same words.
 *
 * Usage: accel_replay <vbios.rom> <pfp.bin> <me.bin> <init-phase>
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hw/rdn_accel.h"
#include "../hw/rdn_accel_reg.h"

#define APERTURE_SIZE	(4u << 20)
#define RING_OFFSET	(1u << 20)
#define RING_BYTES	(1u << 20)

struct access {
	uint8_t write;
	uint32_t offset, value;
};

struct mock {
	struct access *trace;
	size_t trace_len, pos, matched, exempt;
	int failed, unchecked;
	uint64_t clock_us;
	uint32_t digest;
	uint32_t wptr;
};

static void digest_add(struct mock *m, uint32_t w)
{
	int b;

	for (b = 0; b < 4; b++) {
		m->digest ^= (w >> (8 * b)) & 0xff;
		m->digest *= 16777619u;
	}
}

/* Registers the mock answers itself; returns 1 when `offset` is one. */
static int mock_exempt(struct mock *m, int write, uint32_t offset,
		       uint32_t value, uint32_t *result)
{
	*result = 0;
	switch (offset) {
	case MC_VM_FB_LOCATION:
		/* 1 GB at 0xF00000000, as ASIC_Init leaves this card. */
		*result = 0x0f3f0f00;
		return !write;
	case CONFIG_MEMSIZE:
		*result = 1024;
		return !write;
	case CP_RB_RPTR:
		/* The command processor has consumed everything. */
		*result = m->wptr;
		return 1;
	case CP_RB_WPTR:
		if (write)
			m->wptr = value;
		*result = m->wptr;
		/* Writes are checked against the trace; reads are not. */
		return !write;
	case RDN_SCRATCH_REG(0):
		*result = 0xDEADBEEF;
		return 1;
	case RDN_HDP_MEM_COHERENCY_FLUSH_CNTL:
		/* Our ring is written through the aperture; Linux's is not. */
		return 1;
	case CP_RB_RPTR_ADDR:
	case CP_RB_RPTR_ADDR_HI:
	case SCRATCH_ADDR:
	case SCRATCH_UMSK:
	case CP_RB_BASE:
		return 1;
	case CP_RB_CNTL:
		/* Ours has RB_NO_UPDATE where Linux enables write-back. */
		return write && (value & RB_NO_UPDATE) && !(value & RB_BLKSZ(15) & ~RB_BLKSZ(9));
	}
	return 0;
}

static uint32_t mock_access(struct mock *m, int write, uint32_t offset,
			    uint32_t value)
{
	uint32_t result;
	size_t i;

	digest_add(m, (uint32_t)write);
	digest_add(m, offset);
	if (write)
		digest_add(m, value);

	if (mock_exempt(m, write, offset, value, &result)) {
		m->exempt++;
		return result;
	}
	if (m->failed || m->unchecked)
		return 0;

	for (i = m->pos; i < m->trace_len; i++) {
		const struct access *t = &m->trace[i];

		if (t->write == write && t->offset == offset &&
		    (!write || t->value == value)) {
			m->pos = i + 1;
			m->matched++;
			digest_add(m, t->value);
			return t->value;
		}
	}
	fprintf(stderr, "not in the trace after entry %zu (%zu matched): "
		"%c mmio %05x %08x\n", m->pos, m->matched, write ? 'W' : 'R',
		(unsigned)offset, (unsigned)value);
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
	((struct mock *)c)->clock_us += usec;
}

static uint64_t os_time_ms(void *c)
{
	struct mock *m = c;

	m->clock_us += 1000;
	return m->clock_us / 1000;
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
	if (level == RDN_LOG_DEBUG)
		return;
	fprintf(stderr, "rdn: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
}

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
		if (strcmp(kind, "mmio"))
			continue;
		if (n == cap) {
			cap = cap ? cap * 2 : 4096;
			t = realloc(t, cap * sizeof(*t));
		}
		t[n].write = rw == 'W';
		t[n].offset = off;
		t[n].value = val;
		n++;
	}
	fclose(f);
	*count = n;
	return t;
}

static uint8_t *load_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	uint8_t *buf = malloc(1 << 20);

	if (!f || !buf)
		return NULL;
	*size = fread(buf, 1, 1 << 20, f);
	fclose(f);
	return buf;
}

int main(int argc, char **argv)
{
	struct rdn_accel_fw fw;
	struct rdn_accel accel;
	struct rdn_card card;
	struct rdn_os os;
	struct mock m;
	uint32_t *aperture, seq, i;
	uint8_t *bios;
	size_t bios_size;
	int r;

	if (argc != 5) {
		fprintf(stderr, "usage: %s <vbios.rom> <pfp.bin> <me.bin> <init-phase>\n",
			argv[0]);
		return 2;
	}
	bios = load_file(argv[1], &bios_size);
	fw.pfp = load_file(argv[2], &fw.pfp_size);
	fw.me = load_file(argv[3], &fw.me_size);
	aperture = calloc(1, APERTURE_SIZE);
	if (!bios || bios_size < 0x200 || !fw.pfp || !fw.me || !aperture)
		return 2;

	memset(&m, 0, sizeof(m));
	m.digest = 2166136261u;
	m.trace = load_trace(argv[4], &m.trace_len);
	if (!m.trace)
		return 2;

	memset(&os, 0, sizeof(os));
	os.cookie = &m;
	os.mmio_read32 = os_mmio_read32;
	os.mmio_write32 = os_mmio_write32;
	os.delay_us = os_delay_us;
	os.time_ms = os_time_ms;
	os.alloc = os_alloc;
	os.free = os_free;
	os.log = os_log;

	if (rdn_card_init(&card, &os, bios))
		return 2;

	r = rdn_accel_init(&accel, &card, aperture, APERTURE_SIZE, &fw,
			   RING_OFFSET, RING_BYTES, false);
	if (r || m.failed) {
		printf("FAIL: accel init returned %d\n", r);
		return 1;
	}
	printf("tile_config 0x%x backend_map 0x%x active_simds %u\n",
	       (unsigned)accel.cfg.tile_config, (unsigned)accel.cfg.backend_map,
	       (unsigned)accel.cfg.active_simds);
	printf("init: %zu accesses found in order, %zu exempt, ring at word %u\n",
	       m.matched, m.exempt, (unsigned)accel.wptr);

	/*
	 * An indirect buffer submission and its fence. Linux's own next
	 * steps differ, so only the ring words are checked from here on.
	 */
	m.unchecked = 1;
	r = rdn_ib_submit(&accel, rdn_vram_addr(&accel, 3u << 20), 64, &seq);
	if (r || m.failed) {
		printf("FAIL: ib submit returned %d\n", r);
		return 1;
	}

	/* The ring as bytes, so that both byte orders must agree. */
	for (i = 0; i < accel.wptr * 4; i++)
		digest_add(&m, ((uint8_t *)aperture)[RING_OFFSET + i]);
	printf("PASS: ring %u words, digest %08x\n", (unsigned)accel.wptr,
	       (unsigned)m.digest);
	return 0;
}
