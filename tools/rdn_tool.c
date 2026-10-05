/*
 * Linux userspace front end for the hardware library, for milestone 2:
 * drive the real card through sysfs with no kernel driver bound.
 *
 *   rdn_tool [options] status      report POST state (read-only)
 *   rdn_tool [options] post        bring the card up (ASIC_Init if needed)
 *   rdn_tool [options] vramtest    write/read patterns through the aperture
 *   rdn_tool [options] peek REG...  read registers (hex offsets)
 *   rdn_tool [options] edid [file] probe every DDC line for an EDID; save
 *                                  the first one found to file
 *
 * Options:
 *   -s <addr>    PCI address (default 0000:10:00.0)
 *   -b <file>    take the VBIOS from a file instead of the expansion ROM
 *   -t <file>    log every register access, in the format of
 *                scripts/trace-split.py, for comparison with a reference
 *   -n           no I/O BAR: route AtomBIOS indirect I/O through MMIO
 *
 * Must run as root. Refuses to run while a kernel driver owns the card
 * (use scripts/card-bind.sh none).
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../hw/rdn_card.h"
#include "../hw/rdn_i2c.h"
#include "../hw/rdn_reg.h"

#define MMIO_SIZE	0x20000
#define ROM_MAX		0x20000

#define PCI_COMMAND		0x04
#define PCI_COMMAND_IO		0x1
#define PCI_COMMAND_MEMORY	0x2

struct linux_card {
	char sysfs[128];
	int cfg_fd;
	int io_fd;
	volatile uint8_t *mmio;
	FILE *trace;
	uint16_t saved_command;
};

/* The host may be either endianness; the card is always little-endian. */
static uint32_t le32_load(const volatile uint8_t *p)
{
	uint32_t v;

	memcpy(&v, (const void *)p, 4);
	return rdn_swap_le32(v);
}

static void le32_store(volatile uint8_t *p, uint32_t v)
{
	*(volatile uint32_t *)p = rdn_swap_le32(v);
}

static uint32_t os_mmio_read32(void *c, uint32_t off)
{
	struct linux_card *lc = c;
	uint32_t v = rdn_swap_le32(*(volatile uint32_t *)(lc->mmio + off));

	if (lc->trace)
		fprintf(lc->trace, "R mmio %05x %08x 4\n", off, v);
	return v;
}

static void os_mmio_write32(void *c, uint32_t off, uint32_t v)
{
	struct linux_card *lc = c;

	if (lc->trace)
		fprintf(lc->trace, "W mmio %05x %08x 4\n", off, v);
	le32_store(lc->mmio + off, v);
}

static uint32_t os_io_read32(void *c, uint32_t off)
{
	struct linux_card *lc = c;
	uint8_t b[4];
	uint32_t v;

	if (pread(lc->io_fd, b, 4, off) != 4) {
		perror("I/O BAR read");
		exit(1);
	}
	v = le32_load(b);
	if (lc->trace)
		fprintf(lc->trace, "R io %05x %08x 4\n", off, v);
	return v;
}

static void os_io_write32(void *c, uint32_t off, uint32_t v)
{
	struct linux_card *lc = c;
	uint32_t le = rdn_swap_le32(v);

	if (lc->trace)
		fprintf(lc->trace, "W io %05x %08x 4\n", off, v);
	if (pwrite(lc->io_fd, &le, 4, off) != 4) {
		perror("I/O BAR write");
		exit(1);
	}
}

static uint32_t os_cfg_read32(void *c, uint32_t off)
{
	struct linux_card *lc = c;
	uint8_t b[4];

	if (pread(lc->cfg_fd, b, 4, off) != 4)
		return 0xffffffff;
	return le32_load(b);
}

static void os_cfg_write32(void *c, uint32_t off, uint32_t v)
{
	struct linux_card *lc = c;
	uint32_t le = rdn_swap_le32(v);

	if (pwrite(lc->cfg_fd, &le, 4, off) != 4)
		perror("config write");
}

static void os_delay_us(void *c, uint32_t usec)
{
	struct timespec ts;

	(void)c;
	ts.tv_sec = usec / 1000000;
	ts.tv_nsec = (long)(usec % 1000000) * 1000;
	nanosleep(&ts, NULL);
}

static uint64_t os_time_ms(void *c)
{
	struct timespec ts;

	(void)c;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
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
	static const char *const tag[] = { "error", "info", "debug" };

	(void)c;
	if (level == RDN_LOG_DEBUG)
		return;
	fprintf(stderr, "rdn %s: ", tag[level]);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
}

static int sysfs_open(struct linux_card *lc, const char *name, int flags)
{
	char path[192];
	int fd;

	snprintf(path, sizeof(path), "%s/%s", lc->sysfs, name);
	fd = open(path, flags);
	if (fd < 0)
		fprintf(stderr, "%s: %s\n", path, strerror(errno));
	return fd;
}

static int check_identity(struct linux_card *lc)
{
	char path[192], buf[64];
	uint32_t id;
	ssize_t n;

	snprintf(path, sizeof(path), "%s/driver", lc->sysfs);
	n = readlink(path, buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = 0;
		fprintf(stderr, "a kernel driver owns the card (%s); "
			"run scripts/card-bind.sh none first\n",
			strrchr(buf, '/') ? strrchr(buf, '/') + 1 : buf);
		return -1;
	}
	id = os_cfg_read32(lc, 0);
	if (id != 0x675d1002) {
		fprintf(stderr, "unexpected device %04x:%04x, refusing\n",
			id & 0xffff, id >> 16);
		return -1;
	}
	return 0;
}

static void *read_rom(struct linux_card *lc, const char *file)
{
	uint8_t *buf = calloc(1, ROM_MAX);
	ssize_t n;
	int fd;

	if (file) {
		fd = open(file, O_RDONLY);
		if (fd < 0) {
			perror(file);
			return NULL;
		}
	} else {
		/* Writing 1 only turns on ROM address decoding; nothing is flashed. */
		fd = sysfs_open(lc, "rom", O_RDWR);
		if (fd < 0)
			return NULL;
		if (pwrite(fd, "1", 1, 0) != 1) {
			perror("enable ROM decoding");
			return NULL;
		}
	}
	/* sysfs hands out at most a page per read. */
	n = 0;
	while (n < ROM_MAX) {
		ssize_t r = pread(fd, buf + n, ROM_MAX - n, n);

		if (r <= 0)
			break;
		n += r;
	}
	if (!file && pwrite(fd, "0", 1, 0) != 1)
		perror("disable ROM decoding");
	close(fd);
	if (n < 0x200) {
		fprintf(stderr, "VBIOS read returned %zd bytes\n", n);
		return NULL;
	}
	/* The image states its own length; a short read must not pass. */
	if (buf[0] != 0x55 || buf[1] != 0xaa || n < (ssize_t)buf[2] * 512) {
		fprintf(stderr, "VBIOS image is invalid or truncated (%zd bytes)\n", n);
		return NULL;
	}
	fprintf(stderr, "VBIOS: %zd bytes from %s\n", n,
		file ? file : "the PCI expansion ROM");
	return buf;
}

static void print_status(struct rdn_card *card)
{
	static const uint32_t crtc[6] = {
		0x6e70, 0x7a70, 0x10670, 0x11270, 0x11e70, 0x12a70
	};
	int i;

	printf("CRTC_CONTROL   ");
	for (i = 0; i < 6; i++)
		printf(" %08x", rdn_rreg(card, crtc[i]));
	/* On these chips the register holds megabytes. */
	printf("\nCONFIG_MEMSIZE  %08x (%u MB)\n", rdn_rreg(card, CONFIG_MEMSIZE),
	       rdn_rreg(card, CONFIG_MEMSIZE));
	printf("MC_SEQ_SUP_CNTL %08x\n", rdn_rreg(card, MC_SEQ_SUP_CNTL));
	printf("MC_SEQ_MISC0    %08x\n", rdn_rreg(card, MC_SEQ_MISC0));
	printf("state: %s\n", rdn_card_posted(card) ? "posted" : "NOT posted");
}

/*
 * Write a distinct word at many places across the aperture, then read them
 * all back. Memory that is not there, or not trained, fails this.
 */
static int vram_test(struct linux_card *lc)
{
	struct stat st;
	volatile uint32_t *fb;
	size_t size, words, step, i, bad = 0, tested = 0;
	int fd = sysfs_open(lc, "resource0_wc", O_RDWR);

	if (fd < 0)
		fd = sysfs_open(lc, "resource0", O_RDWR);
	if (fd < 0 || fstat(fd, &st))
		return -1;
	size = st.st_size;
	fb = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED) {
		perror("mmap aperture");
		return -1;
	}
	words = size / 4;
	step = 4099;	/* prime, so every address bit takes part */

	for (i = 0; i < words; i += step)
		fb[i] = rdn_swap_le32((uint32_t)i * 2654435761u ^ 0xa5a5a5a5);
	for (i = 0; i < words; i += step) {
		uint32_t want = (uint32_t)i * 2654435761u ^ 0xa5a5a5a5;
		uint32_t got = rdn_swap_le32(fb[i]);

		tested++;
		if (got != want && bad++ < 8)
			printf("  offset %08zx: wrote %08x read %08x\n",
			       i * 4, want, got);
	}
	/* A second pass with the complement catches stuck bits. */
	for (i = 0; i < words; i += step)
		fb[i] = rdn_swap_le32(~((uint32_t)i * 2654435761u ^ 0xa5a5a5a5));
	for (i = 0; i < words; i += step) {
		uint32_t want = ~((uint32_t)i * 2654435761u ^ 0xa5a5a5a5);

		if (rdn_swap_le32(fb[i]) != want)
			bad++;
	}
	printf("aperture %zu MB, %zu words tested twice, %zu mismatches\n",
	       size >> 20, tested, bad);
	munmap((void *)fb, size);
	close(fd);
	return bad ? 1 : 0;
}

/* Probe every I2C line the VBIOS lists; a display answers with its EDID. */
static int edid_probe(struct rdn_card *card, const char *save)
{
	uint8_t edid[RDN_EDID_MAX_SIZE];
	struct rdn_i2c_bus bus;
	int i, r, found = 0;
	FILE *f;

	for (i = 0; rdn_i2c_bus_by_index(card, i, &bus); i++) {
		if (!bus.valid)
			continue;
		r = rdn_edid_read(card, &bus, edid);
		if (r < 0) {
			printf("line %d (id %02x, regs %04x): no EDID (%d)\n", i,
			       bus.i2c_id, (unsigned)bus.mask_clk_reg, r);
			continue;
		}
		printf("line %d (id %02x, regs %04x): EDID, %d bytes, "
		       "preferred mode %ux%u\n", i, bus.i2c_id,
		       (unsigned)bus.mask_clk_reg, r,
		       edid[56] | ((edid[58] >> 4) << 8),
		       edid[59] | ((edid[61] >> 4) << 8));
		if (save && !found) {
			f = fopen(save, "wb");
			if (!f || fwrite(edid, 1, r, f) != (size_t)r)
				perror(save);
			if (f)
				fclose(f);
		}
		found++;
	}
	return found ? 0 : 1;
}

int main(int argc, char **argv)
{
	const char *addr = "0000:10:00.0", *bios_file = NULL, *trace = NULL;
	const char *cmd;
	struct linux_card lc;
	struct rdn_card card;
	struct rdn_os os;
	uint32_t command;
	void *bios;
	int opt, fd, no_io = 0, ret = 0;

	while ((opt = getopt(argc, argv, "s:b:t:n")) != -1) {
		switch (opt) {
		case 's': addr = optarg; break;
		case 'b': bios_file = optarg; break;
		case 't': trace = optarg; break;
		case 'n': no_io = 1; break;
		default: return 2;
		}
	}
	if (optind >= argc) {
		fprintf(stderr, "usage: %s [-s addr] [-b vbios] [-t trace] [-n] "
			"status|post|vramtest|edid [file]\n", argv[0]);
		return 2;
	}
	cmd = argv[optind];

	memset(&lc, 0, sizeof(lc));
	snprintf(lc.sysfs, sizeof(lc.sysfs), "/sys/bus/pci/devices/%s", addr);
	lc.cfg_fd = sysfs_open(&lc, "config", O_RDWR);
	if (lc.cfg_fd < 0 || check_identity(&lc))
		return 1;

	/* Let the kernel wake the device and route its resources. */
	fd = sysfs_open(&lc, "enable", O_WRONLY);
	if (fd >= 0) {
		if (write(fd, "1", 1) != 1)
			perror("enable");
		close(fd);
	}
	command = os_cfg_read32(&lc, PCI_COMMAND);
	lc.saved_command = command & 0xffff;
	if ((command & (PCI_COMMAND_IO | PCI_COMMAND_MEMORY)) !=
	    (PCI_COMMAND_IO | PCI_COMMAND_MEMORY)) {
		uint16_t c = rdn_swap_le16(lc.saved_command | PCI_COMMAND_IO |
					   PCI_COMMAND_MEMORY);
		if (pwrite(lc.cfg_fd, &c, 2, PCI_COMMAND) != 2)
			perror("PCI command");
	}

	fd = sysfs_open(&lc, "resource2", O_RDWR);
	if (fd < 0)
		return 1;
	lc.mmio = mmap(NULL, MMIO_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (lc.mmio == MAP_FAILED) {
		perror("mmap registers");
		return 1;
	}
	lc.io_fd = no_io ? -1 : sysfs_open(&lc, "resource4", O_RDWR);

	memset(&os, 0, sizeof(os));
	os.cookie = &lc;
	os.mmio_read32 = os_mmio_read32;
	os.mmio_write32 = os_mmio_write32;
	if (lc.io_fd >= 0) {
		os.io_read32 = os_io_read32;
		os.io_write32 = os_io_write32;
	}
	os.cfg_read32 = os_cfg_read32;
	os.cfg_write32 = os_cfg_write32;
	os.delay_us = os_delay_us;
	os.time_ms = os_time_ms;
	os.alloc = os_alloc;
	os.free = os_free;
	os.log = os_log;

	bios = read_rom(&lc, bios_file);
	if (!bios || rdn_card_init(&card, &os, bios)) {
		fprintf(stderr, "no usable VBIOS\n");
		return 1;
	}

	if (!strcmp(cmd, "status")) {
		print_status(&card);
	} else if (!strcmp(cmd, "post")) {
		if (trace) {
			lc.trace = fopen(trace, "w");
			if (!lc.trace)
				perror(trace);
		}
		ret = rdn_card_post(&card);
		if (lc.trace) {
			fclose(lc.trace);
			lc.trace = NULL;
		}
		printf("post returned %d\n", ret);
		print_status(&card);
	} else if (!strcmp(cmd, "peek")) {
		int i;

		for (i = optind + 1; i < argc; i++) {
			uint32_t reg = (uint32_t)strtoul(argv[i], NULL, 16);

			printf("%05x = %08x\n", reg, rdn_rreg(&card, reg));
		}
	} else if (!strcmp(cmd, "edid")) {
		ret = edid_probe(&card, optind + 1 < argc ? argv[optind + 1] : NULL);
	} else if (!strcmp(cmd, "vramtest")) {
		print_status(&card);
		ret = vram_test(&lc);
	} else {
		fprintf(stderr, "unknown command %s\n", cmd);
		ret = 2;
	}

	rdn_card_fini(&card);
	free(bios);
	return ret ? 1 : 0;
}
