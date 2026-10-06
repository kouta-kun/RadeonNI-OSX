/*
 * The memory controller's microcode: ni_mc_load_microcode() of the Linux
 * radeon driver (ni.c), for Turks.
 *
 * Copyright 2010 Advanced Micro Devices, Inc.
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

#include "rdn_mc.h"
#include "atom/atom_port.h"

#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif

#define MC_SEQ_SUP_CNTL				0x28c8
#define		RUN_MASK			(1 << 0)
#define MC_SEQ_SUP_PGM				0x28cc
#define MC_IO_PAD_CNTL_D0			0x29d0
#define		MEM_FALL_OUT_CMD		(1 << 8)
#define MC_SEQ_MISC0				0x2a00
#define		MC_SEQ_MISC0_GDDR5_SHIFT	28
#define		MC_SEQ_MISC0_GDDR5_MASK		0xf0000000
#define		MC_SEQ_MISC0_GDDR5_VALUE	5
#define MC_SEQ_IO_DEBUG_INDEX			0x2a44
#define MC_SEQ_IO_DEBUG_DATA			0x2a48

#define BTC_IO_MC_REGS_SIZE	29
#define TRAINING_TIMEOUT_US	100000

static const uint32_t turks_io_mc_regs[BTC_IO_MC_REGS_SIZE][2] = {
	{0x00000077, 0xff010100},
	{0x00000078, 0x00000000},
	{0x00000079, 0x00001434},
	{0x0000007a, 0xcc08ec08},
	{0x0000007b, 0x00040000},
	{0x0000007c, 0x000080c0},
	{0x0000007d, 0x09000000},
	{0x0000007e, 0x00210404},
	{0x00000081, 0x08a8e800},
	{0x00000082, 0x00030444},
	{0x00000083, 0x00000000},
	{0x00000085, 0x00000001},
	{0x00000086, 0x00000002},
	{0x00000087, 0x48490000},
	{0x00000088, 0x20244647},
	{0x00000089, 0x00000005},
	{0x0000008b, 0x66030000},
	{0x0000008c, 0x00006603},
	{0x0000008d, 0x00000100},
	{0x0000008f, 0x00001c0a},
	{0x00000090, 0xff000001},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00936a00}
};

int rdn_mc_load_microcode(struct rdn_card *card, const void *fw, size_t size)
{
	const uint8_t *p = fw;
	uint32_t mem_type, running;
	int i;

	if (!fw || size != RDN_MC_UCODE_WORDS * 4)
		return -EINVAL;

	mem_type = (rdn_rreg(card, MC_SEQ_MISC0) & MC_SEQ_MISC0_GDDR5_MASK) >>
		   MC_SEQ_MISC0_GDDR5_SHIFT;
	running = rdn_rreg(card, MC_SEQ_SUP_CNTL) & RUN_MASK;
	if (mem_type != MC_SEQ_MISC0_GDDR5_VALUE || running)
		return 1;

	/* reset the engine and set to writable */
	rdn_wreg(card, MC_SEQ_SUP_CNTL, 0x00000008);
	rdn_wreg(card, MC_SEQ_SUP_CNTL, 0x00000010);

	/* load mc io regs */
	for (i = 0; i < BTC_IO_MC_REGS_SIZE; i++) {
		rdn_wreg(card, MC_SEQ_IO_DEBUG_INDEX, turks_io_mc_regs[i][0]);
		rdn_wreg(card, MC_SEQ_IO_DEBUG_DATA, turks_io_mc_regs[i][1]);
	}
	/* load the MC ucode: big-endian words in the file */
	for (i = 0; i < RDN_MC_UCODE_WORDS; i++, p += 4)
		rdn_wreg(card, MC_SEQ_SUP_PGM,
			 ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
			 ((uint32_t)p[2] << 8) | p[3]);

	/* put the engine back into the active state */
	rdn_wreg(card, MC_SEQ_SUP_CNTL, 0x00000008);
	rdn_wreg(card, MC_SEQ_SUP_CNTL, 0x00000004);
	rdn_wreg(card, MC_SEQ_SUP_CNTL, 0x00000001);

	/* wait for training to complete */
	for (i = 0; i < TRAINING_TIMEOUT_US; i++) {
		if (rdn_rreg(card, MC_IO_PAD_CNTL_D0) & MEM_FALL_OUT_CMD)
			return 0;
		card->os->delay_us(card->os->cookie, 1);
	}
	return -ETIMEDOUT;
}
