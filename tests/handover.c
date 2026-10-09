/*
 * Hardware-free test of the firmware hand-over marker (rdn_handover_mark /
 * rdn_handover_take) against a register file.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "../hw/rdn_card.h"
#include "../hw/rdn_reg.h"
#include "../hw/linux/evergreend.h"

static uint32_t regs[0x20000 / 4];

static uint32_t rd(void *c, uint32_t off) { return regs[off / 4]; }
static void wr(void *c, uint32_t off, uint32_t v) { regs[off / 4] = v; }

static int fails;

static void expect(const char *what, int got, int want)
{
	printf("%-44s %d %s\n", what, got, got == want ? "ok" : "FAIL");
	if (got != want)
		fails++;
}

int main(void)
{
	struct rdn_os os;
	struct rdn_card card;

	memset(&os, 0, sizeof(os));
	os.mmio_read32 = rd;
	os.mmio_write32 = wr;
	memset(&card, 0, sizeof(card));
	card.os = &os;

	expect("no marker", rdn_handover_take(&card), 0);

	rdn_handover_mark(&card);
	expect("marker, card not running", rdn_handover_take(&card), -1);
	expect("marker is cleared by take", rdn_handover_take(&card), 0);

	rdn_handover_mark(&card);
	regs[CONFIG_MEMSIZE / 4] = 0x400;
	regs[EVERGREEN_CRTC_CONTROL / 4] = EVERGREEN_CRTC_MASTER_EN;
	expect("marker, memory and CRTC but no surface", rdn_handover_take(&card), -1);

	rdn_handover_mark(&card);
	regs[EVERGREEN_GRPH_ENABLE / 4] = 1;
	expect("marker, running card", rdn_handover_take(&card), 1);
	expect("second take", rdn_handover_take(&card), 0);

	regs[0x851c / 4] = 0x12345678;
	expect("wrong marker value", rdn_handover_take(&card), 0);

	printf("%s\n", fails ? "FAIL" : "PASS");
	return fails != 0;
}
