/*
 * Hardware-free test of the modeset code.
 *
 * Runs rdn_modeset() for the EDID's preferred mode against a mock card
 * driven by the reference trace of a Linux modeset to the same mode (a
 * phase file from scripts/trace-split.py). The Linux phase contains more
 * than a modeset (connector probing, power management), so the rule is
 * looser than in atom_replay: every access we make must occur in the trace,
 * in the same order, with the same value for writes; trace entries we do
 * not make are skipped. Reads return what the real card returned.
 *
 * A few registers are exempt because our value legitimately differs: the
 * scanout address (Linux relocates the framebuffer address range, we do
 * not) and the read of that range's location.
 *
 * Usage: modeset_replay <vbios.rom> <edid.bin> <phase-file>
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hw/rdn_mode.h"
#include "../hw/rdn_reg.h"

struct access {
	uint8_t write, io;
	uint32_t offset, value;
};

struct mock {
	struct access *trace;
	size_t trace_len, pos, matched, exempt;
	int failed;
	uint64_t clock_ms;
	uint32_t digest;
};

static const uint32_t exempt_regs[] = {
	MC_VM_FB_LOCATION,
	EVERGREEN_GRPH_PRIMARY_SURFACE_ADDRESS,
	EVERGREEN_GRPH_SECONDARY_SURFACE_ADDRESS,
	EVERGREEN_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH,
	EVERGREEN_GRPH_SECONDARY_SURFACE_ADDRESS_HIGH,
};

static void digest_add(struct mock *m, uint32_t w)
{
	int b;

	for (b = 0; b < 4; b++) {
		m->digest ^= (w >> (8 * b)) & 0xff;
		m->digest *= 16777619u;
	}
}

static uint32_t mock_access(struct mock *m, int write, int io, uint32_t offset,
			    uint32_t value)
{
	size_t i;

	digest_add(m, ((uint32_t)write << 8) | (uint32_t)io);
	digest_add(m, offset);
	if (write)
		digest_add(m, value);

	if (!io)
		for (i = 0; i < sizeof(exempt_regs) / sizeof(exempt_regs[0]); i++)
			if (exempt_regs[i] == offset) {
				m->exempt++;
				return 0;
			}
	if (m->failed)
		return 0;

	for (i = m->pos; i < m->trace_len; i++) {
		const struct access *t = &m->trace[i];

		if (t->write == write && t->io == io && t->offset == offset &&
		    (!write || t->value == value)) {
			m->pos = i + 1;
			m->matched++;
			digest_add(m, t->value);
			return t->value;
		}
	}
	fprintf(stderr, "not in the trace after entry %zu (%zu matched): "
		"%c %s %05x %08x\n", m->pos, m->matched, write ? 'W' : 'R',
		io ? "io" : "mmio", (unsigned)offset, (unsigned)value);
	m->failed = 1;
	return 0;
}

static uint32_t os_mmio_read32(void *c, uint32_t off)
{
	return mock_access(c, 0, 0, off, 0);
}

static void os_mmio_write32(void *c, uint32_t off, uint32_t v)
{
	mock_access(c, 1, 0, off, v);
}

static uint32_t os_io_read32(void *c, uint32_t off)
{
	return mock_access(c, 0, 1, off, 0);
}

static void os_io_write32(void *c, uint32_t off, uint32_t v)
{
	mock_access(c, 1, 1, off, v);
}

static void os_delay_us(void *c, uint32_t usec)
{
	struct mock *m = c;

	m->clock_ms += usec / 1000 + 1;
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
		if (strcmp(kind, "mmio") && strcmp(kind, "io"))
			continue;
		if (n == cap) {
			cap = cap ? cap * 2 : 4096;
			t = realloc(t, cap * sizeof(*t));
		}
		t[n].write = rw == 'W';
		t[n].io = !strcmp(kind, "io");
		t[n].offset = off;
		t[n].value = val;
		n++;
	}
	fclose(f);
	*count = n;
	return t;
}

int main(int argc, char **argv)
{
	static uint8_t bios[0x20000], edid[512];
	struct rdn_card card;
	struct rdn_mode mode;
	struct rdn_fb fb;
	struct rdn_os os;
	struct mock m;
	size_t edid_len;
	FILE *f;
	int r;

	if (argc != 4) {
		fprintf(stderr, "usage: %s <vbios.rom> <edid.bin> <phase-file>\n",
			argv[0]);
		return 2;
	}
	f = fopen(argv[1], "rb");
	if (!f || fread(bios, 1, sizeof(bios), f) < 0x200)
		return 2;
	fclose(f);
	f = fopen(argv[2], "rb");
	if (!f || (edid_len = fread(edid, 1, sizeof(edid), f)) < 128)
		return 2;
	fclose(f);

	memset(&m, 0, sizeof(m));
	m.digest = 2166136261u;
	m.trace = load_trace(argv[3], &m.trace_len);
	if (!m.trace)
		return 2;

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

	if (rdn_card_init(&card, &os, bios) ||
	    !rdn_edid_preferred_mode(edid, &mode))
		return 2;

	memset(&fb, 0, sizeof(fb));
	fb.width = mode.hdisplay;
	fb.height = mode.vdisplay;
	/* Linux aligns the pitch of its buffers to 64 pixels; do the same. */
	fb.pitch_pixels = (mode.hdisplay + 63u) & ~63u;

	printf("mode %ux%u %u kHz, h %u-%u/%u, v %u-%u/%u, flags %u, hdmi %d\n",
	       mode.hdisplay, mode.vdisplay, (unsigned)mode.clock,
	       mode.hsync_start, mode.hsync_end, mode.htotal,
	       mode.vsync_start, mode.vsync_end, mode.vtotal,
	       (unsigned)mode.flags, rdn_edid_is_hdmi(edid, (int)edid_len));

	r = rdn_modeset(&card, &mode, &fb, rdn_edid_is_hdmi(edid, (int)edid_len));
	rdn_card_fini(&card);

	if (r || m.failed) {
		printf("FAIL: modeset returned %d, %zu accesses matched\n", r,
		       m.matched);
		return 1;
	}
	printf("PASS: modeset, %zu accesses found in order in the trace "
	       "(%zu exempt), digest %08x\n", m.matched, m.exempt,
	       (unsigned)m.digest);
	return 0;
}
