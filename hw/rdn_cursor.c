/*
 * The display controller's hardware cursor (DCE4/5), for CRTC 0: a 64x64
 * image of 32-bit ARGB, colours already multiplied by alpha, that the
 * controller lays over the picture by itself.
 *
 * Follows the Linux radeon driver's radeon_cursor.c.
 *
 * Copyright 2007-8 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright (c) 2026 kouta-kun and Claude
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "rdn_card.h"
#include "rdn_cursor.h"
#include "rdn_reg.h"

#define EVERGREEN_CUR_CONTROL			0x6998
#define   EVERGREEN_CURSOR_EN			(1u << 0)
#define   EVERGREEN_CURSOR_MODE(x)		(((x) & 0x3u) << 8)
#define   EVERGREEN_CURSOR_24_8_PRE_MULT	2
#define   EVERGREEN_CURSOR_URGENT_CONTROL(x)	(((x) & 0x7u) << 24)
#define   EVERGREEN_CURSOR_URGENT_1_2		4
#define EVERGREEN_CUR_SURFACE_ADDRESS		0x699c
#define EVERGREEN_CUR_SIZE			0x69a0
#define EVERGREEN_CUR_SURFACE_ADDRESS_HIGH	0x69a4
#define EVERGREEN_CUR_POSITION			0x69a8
#define EVERGREEN_CUR_HOT_SPOT			0x69ac
#define EVERGREEN_CUR_UPDATE			0x69c8
#define   EVERGREEN_CURSOR_UPDATE_LOCK		(1u << 16)

#define CURSOR_FORMAT \
	(EVERGREEN_CURSOR_MODE(EVERGREEN_CURSOR_24_8_PRE_MULT) | \
	 EVERGREEN_CURSOR_URGENT_CONTROL(EVERGREEN_CURSOR_URGENT_1_2))

static void cursor_lock(struct rdn_card *card, bool lock)
{
	uint32_t v = rdn_rreg(card, EVERGREEN_CUR_UPDATE);

	if (lock)
		v |= EVERGREEN_CURSOR_UPDATE_LOCK;
	else
		v &= ~EVERGREEN_CURSOR_UPDATE_LOCK;
	rdn_wreg(card, EVERGREEN_CUR_UPDATE, v);
}

void rdn_cursor_set(struct rdn_card *card, uint32_t aperture_offset, int x,
		    int y, bool visible)
{
	uint64_t addr = ((uint64_t)(rdn_rreg(card, MC_VM_FB_LOCATION) & 0xffff) << 24) +
			aperture_offset;
	int xorigin = 0, yorigin = 0;

	/* A cursor partly off the left or top edge starts inside its image. */
	if (x < 0) {
		xorigin = -x < RDN_CURSOR_SIZE - 1 ? -x : RDN_CURSOR_SIZE - 1;
		x = 0;
	}
	if (y < 0) {
		yorigin = -y < RDN_CURSOR_SIZE - 1 ? -y : RDN_CURSOR_SIZE - 1;
		y = 0;
	}

	cursor_lock(card, true);
	if (visible) {
		rdn_wreg(card, EVERGREEN_CUR_POSITION,
			 ((uint32_t)x << 16) | (uint32_t)y);
		rdn_wreg(card, EVERGREEN_CUR_HOT_SPOT,
			 ((uint32_t)xorigin << 16) | (uint32_t)yorigin);
		rdn_wreg(card, EVERGREEN_CUR_SIZE,
			 ((RDN_CURSOR_SIZE - 1) << 16) | (RDN_CURSOR_SIZE - 1));
		rdn_wreg(card, EVERGREEN_CUR_SURFACE_ADDRESS_HIGH,
			 (uint32_t)(addr >> 32));
		rdn_wreg(card, EVERGREEN_CUR_SURFACE_ADDRESS, (uint32_t)addr);
		rdn_wreg(card, EVERGREEN_CUR_CONTROL,
			 EVERGREEN_CURSOR_EN | CURSOR_FORMAT);
	} else {
		rdn_wreg(card, EVERGREEN_CUR_CONTROL, CURSOR_FORMAT);
	}
	cursor_lock(card, false);
}
