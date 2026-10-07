/*
 * Hardware-free test of the library's drawing: rdn_blit(), rdn_blit_fill()
 * and rdn_blit_move() (hw/rdn_blit.c).
 *
 * The card is a mock with video memory in an ordinary buffer and a command
 * processor that does what the real one would with the few packets these
 * functions send. It walks the ring, follows every indirect buffer and, at
 * a draw, copies the texels of the bound texture to the pixels of the bound
 * render target, rectangle by rectangle from the vertex buffer. It is a
 * busy GPU: it starts on none of that before the library's call has
 * returned, so that work space the library used twice in one call would be
 * read in its last state. So what is checked is what the library wrote, as
 * the GPU would read it, not what it meant to write:
 *  - the picture afterwards is the one a plain model of the operation
 *    gives (for a move: every source read before anything is written),
 *    and nothing outside it changed;
 *  - no draw takes its texture from memory it is drawing on, which is what
 *    a copy inside one surface must not do on the real GPU;
 *  - every draw is a rectangle list, inside its target, followed by a flush
 *    of the target's cache before its buffer ends;
 *  - an indirect buffer's words are in the ring's byte order.
 * Everything runs twice, with little-endian and with big-endian command
 * words (the kext on a Mac uses the second).
 *
 * All of video memory goes into the digest after each case, so the x86 and
 * the PowerPC build must have written the same bytes everywhere.
 *
 * Usage: blit_ops
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hw/rdn_accel.h"
#include "../hw/rdn_accel_reg.h"

#define VRAM_BASE	0xf00000000ull
#define APERTURE_SIZE	(2u << 20)

/* The screen: narrower than its pitch, and not a multiple of 8 rows. */
#define SCREEN_OFFSET	0x000000
#define SCREEN_WIDTH	200
#define SCREEN_HEIGHT	100
#define SCREEN_PITCH	256
/* Another surface, to copy from. */
#define OTHER_OFFSET	0x040000
#define OTHER_SIZE	64
/* The move's scratch surface. */
#define SCRATCH_OFFSET	0x080000
#define RING_OFFSET	0x100000
#define RING_BYTES	0x10000
#define WORK_OFFSET	0x120000

#define CB_COLOR0_BASE_REG	0x28c60
#define VTX_RESOURCE		0x580
#define RECTLIST		0x11

struct surface {
	uint64_t base;
	uint32_t width, height, pitch;
};

struct mock {
	struct rdn_accel *accel;
	uint8_t *vram;
	/* What the command processor has read, and how far it may. */
	uint32_t rptr, wptr;
	uint32_t fence, digest;
	int failed;
	/* Since the last look: draws, rectangles, indirect buffers. */
	uint32_t draws, rects, ibs;

	/* The state an indirect buffer has set. */
	struct surface cb, tex;
	uint64_t vb_base;
	uint32_t vb_bytes, vb_stride, primitive;
	uint32_t scissor[3][2];
	bool have_cb, have_tex, have_vb, cb_dirty;
};

static void fail(struct mock *m, const char *what, uint32_t a, uint32_t b)
{
	if (!m->failed)
		fprintf(stderr, "mock GPU: %s (0x%x, 0x%x)\n", what, (unsigned)a,
			(unsigned)b);
	m->failed = 1;
}

static void digest_add(struct mock *m, uint8_t byte)
{
	m->digest ^= byte;
	m->digest *= 16777619u;
}

/* A byte of video memory by its GPU address; NULL if there is none there. */
static uint8_t *vram_at(struct mock *m, uint64_t addr, uint32_t bytes)
{
	if (addr < VRAM_BASE || addr - VRAM_BASE > APERTURE_SIZE ||
	    bytes > APERTURE_SIZE - (addr - VRAM_BASE)) {
		fail(m, "address outside video memory", (uint32_t)(addr >> 32),
		     (uint32_t)addr);
		return NULL;
	}
	return m->vram + (addr - VRAM_BASE);
}

/* A command word: big-endian when the command processor swaps. */
static uint32_t command_word(struct mock *m, const uint8_t *p)
{
	if (m->accel->swapped)
		return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
		       ((uint32_t)p[2] << 8) | p[3];
	return rdn_get_le32(p, 0);
}

/* A vertex coordinate: a whole number stored as a little-endian float. */
static uint32_t whole_float(struct mock *m, const uint8_t *p)
{
	uint32_t bits = rdn_get_le32(p, 0);
	uint32_t exponent = bits >> 23, mantissa = (bits & 0x7fffff) | 0x800000;

	if (!bits)
		return 0;
	if (exponent < 127 || exponent > 127 + 23 ||
	    (mantissa & ((1u << (127 + 23 - exponent)) - 1))) {
		fail(m, "vertex coordinate is not a whole number", bits, 0);
		return 0;
	}
	return mantissa >> (127 + 23 - exponent);
}

static bool overlap(const struct surface *a, const struct surface *b)
{
	uint64_t a_end = a->base + (uint64_t)a->pitch * a->height * 4;
	uint64_t b_end = b->base + (uint64_t)b->pitch * b->height * 4;

	return a->base < b_end && b->base < a_end;
}

/*
 * A rectangle list: three corners each (top left, bottom left, bottom
 * right) of x, y, s, t. Nearest texel, clamped to the texture, as the
 * library's sampler state asks.
 */
static void mock_draw(struct mock *m, uint32_t vertices)
{
	uint32_t i, k, x, y;
	uint8_t *vb, *cb, *tex;

	m->draws++;
	if (!m->have_cb || !m->have_tex || !m->have_vb) {
		fail(m, "draw without target, texture or vertices", 0, 0);
		return;
	}
	if (m->primitive != RECTLIST || vertices % 3 || m->vb_stride != 16 ||
	    vertices * 16 != m->vb_bytes) {
		fail(m, "not a list of rectangles", m->primitive, vertices);
		return;
	}
	if (overlap(&m->cb, &m->tex)) {
		fail(m, "the texture is in the memory drawn on",
		     (uint32_t)m->cb.base, (uint32_t)m->tex.base);
		return;
	}
	for (i = 0; i < 3; i++)
		if (m->scissor[i][0] != 0 ||
		    m->scissor[i][1] != (m->cb.width | (m->cb.height << 16))) {
			fail(m, "scissor is not the whole target", i, m->scissor[i][1]);
			return;
		}
	vb = vram_at(m, m->vb_base, m->vb_bytes);
	cb = vram_at(m, m->cb.base, m->cb.pitch * m->cb.height * 4);
	tex = vram_at(m, m->tex.base, m->tex.pitch * m->tex.height * 4);
	if (!vb || !cb || !tex)
		return;

	for (i = 0; i < vertices / 3; i++) {
		uint32_t v[12];

		for (k = 0; k < 12; k++)
			v[k] = whole_float(m, vb + i * 48 + k * 4);
		/* x0 y0 s0 t0, x0 y1 s0 t1, x1 y1 s1 t1 */
		if (v[4] != v[0] || v[9] != v[5] || v[6] != v[2] || v[11] != v[7] ||
		    v[8] <= v[0] || v[5] <= v[1] || v[10] < v[2] || v[7] < v[3] ||
		    v[8] > m->cb.width || v[5] > m->cb.height ||
		    v[10] > m->tex.width || v[7] > m->tex.height) {
			fail(m, "rectangle is malformed or leaves a surface", i, v[8]);
			return;
		}
		m->rects++;
		for (y = v[1]; y < v[5]; y++)
			for (x = v[0]; x < v[8]; x++) {
				/* The texel under the pixel's centre. */
				uint32_t s = v[2] + (2 * (x - v[0]) + 1) * (v[10] - v[2]) /
						    (2 * (v[8] - v[0]));
				uint32_t t = v[3] + (2 * (y - v[1]) + 1) * (v[7] - v[3]) /
						    (2 * (v[5] - v[1]));

				if (s >= m->tex.width)
					s = m->tex.width - 1;
				if (t >= m->tex.height)
					t = m->tex.height - 1;
				memcpy(cb + (y * m->cb.pitch + x) * 4,
				       tex + (t * m->tex.pitch + s) * 4, 4);
			}
	}
	m->cb_dirty = true;
}

static void mock_context_reg(struct mock *m, uint32_t reg, uint32_t value)
{
	switch (reg) {
	case CB_COLOR0_BASE_REG:
		if (m->cb_dirty)
			fail(m, "new target before the old one's cache was flushed", 0, 0);
		m->cb.base = (uint64_t)value << 8;
		m->have_cb = true;
		break;
	case CB_COLOR0_BASE_REG + 4:
		m->cb.pitch = (value + 1) * 8;
		break;
	case CB_COLOR0_BASE_REG + 24:
		m->cb.width = (value & 0xffff) + 1;
		m->cb.height = (value >> 16) + 1;
		break;
	case PA_SC_SCREEN_SCISSOR_TL:
	case PA_SC_SCREEN_SCISSOR_TL + 4:
		m->scissor[0][(reg - PA_SC_SCREEN_SCISSOR_TL) / 4] = value;
		break;
	case PA_SC_GENERIC_SCISSOR_TL:
	case PA_SC_GENERIC_SCISSOR_TL + 4:
		m->scissor[1][(reg - PA_SC_GENERIC_SCISSOR_TL) / 4] = value & 0x7fffffff;
		break;
	case PA_SC_WINDOW_SCISSOR_TL:
	case PA_SC_WINDOW_SCISSOR_TL + 4:
		m->scissor[2][(reg - PA_SC_WINDOW_SCISSOR_TL) / 4] = value & 0x7fffffff;
		break;
	}
}

static void mock_ib(struct mock *m, uint64_t addr, uint32_t words)
{
	const uint8_t *ib = vram_at(m, addr, words * 4);
	uint32_t i = 0, k;

	m->ibs++;
	m->have_cb = m->have_tex = m->have_vb = m->cb_dirty = false;
	m->primitive = 0;
	memset(m->scissor, 0xff, sizeof(m->scissor));
	if (!ib)
		return;
	while (i < words && !m->failed) {
		uint32_t header = command_word(m, ib + i * 4);
		uint32_t count = ((header >> 16) & 0x3fff) + 1;
		uint32_t w[16];

		if (header >> 30 == 2) {	/* type 2: nothing */
			i++;
			continue;
		}
		if (header >> 30 != 3 || i + 1 + count > words) {
			fail(m, "bad packet in an indirect buffer", i, header);
			return;
		}
		for (k = 0; k < 16; k++)
			w[k] = k < count ? command_word(m, ib + (i + 1 + k) * 4) : 0;
		switch ((header >> 8) & 0xff) {
		case PACKET3_SET_CONTEXT_REG:
			for (k = 1; k < count; k++)
				mock_context_reg(m, PACKET3_SET_CONTEXT_REG_START +
						 (w[0] + k - 1) * 4,
						 command_word(m, ib + (i + 1 + k) * 4));
			break;
		case PACKET3_SET_CONFIG_REG:
			if (PACKET3_SET_CONFIG_REG_START + w[0] * 4 == VGT_PRIMITIVE_TYPE)
				m->primitive = w[1];
			break;
		case PACKET3_SET_RESOURCE:
			if (w[0] == 0) {
				m->tex.pitch = (((w[1] >> 6) & 0xfff) + 1) * 8;
				m->tex.width = (w[1] >> 18) + 1;
				m->tex.height = (w[2] & 0x3fff) + 1;
				m->tex.base = (uint64_t)w[3] << 8;
				m->have_tex = true;
			} else if (w[0] == VTX_RESOURCE) {
				m->vb_base = w[1] | ((uint64_t)(w[3] & 0xff) << 32);
				m->vb_bytes = w[2] + 1;
				m->vb_stride = (w[3] >> 8) & 0xfff;
				m->have_vb = true;
			}
			break;
		case PACKET3_SURFACE_SYNC:
			if ((w[0] & PACKET3_CB_ACTION_ENA) && m->have_cb &&
			    ((uint64_t)w[2] << 8) == m->cb.base &&
			    (uint64_t)w[1] * 256 >= (uint64_t)m->cb.pitch * m->cb.height * 4)
				m->cb_dirty = false;
			break;
		case PACKET3_DRAW_INDEX_AUTO:
			mock_draw(m, w[0]);
			break;
		}
		i += 1 + count;
	}
	if (m->cb_dirty)
		fail(m, "buffer ends without flushing the target's cache", 0, 0);
}

/* The command processor: everything from the read to the write pointer. */
static void mock_run(struct mock *m)
{
	struct rdn_accel *accel = m->accel;
	uint32_t mask = accel->ring_words - 1, wptr = m->wptr;

	while (m->rptr != wptr && !m->failed) {
		uint32_t w[4], k, header, count;

		header = command_word(m, m->vram + accel->ring_offset + m->rptr * 4);
		if (header >> 30 == 2) {
			m->rptr = (m->rptr + 1) & mask;
			continue;
		}
		count = ((header >> 16) & 0x3fff) + 1;
		if (header >> 30 != 3 || count > ((wptr - m->rptr - 1) & mask)) {
			fail(m, "bad packet in the ring", m->rptr, header);
			return;
		}
		for (k = 0; k < 4; k++)
			w[k] = k < count ? command_word(m, m->vram + accel->ring_offset +
				((m->rptr + 1 + k) & mask) * 4) : 0;
		switch ((header >> 8) & 0xff) {
		case PACKET3_INDIRECT_BUFFER:
			if ((w[0] & 3) != (accel->swapped ? 2u : 0u))
				fail(m, "buffer's byte order is not the ring's", w[0], 0);
			mock_ib(m, (w[0] & ~3u) | ((uint64_t)(w[1] & 0xff) << 32), w[2]);
			break;
		case PACKET3_SET_CONFIG_REG:
			if (PACKET3_SET_CONFIG_REG_START + w[0] * 4 == RDN_SCRATCH_REG(1))
				m->fence = w[1];
			break;
		}
		m->rptr = (m->rptr + 1 + count) & mask;
	}
}

static uint32_t os_mmio_read32(void *c, uint32_t off)
{
	struct mock *m = c;

	switch (off) {
	case CP_RB_RPTR:
		return m->rptr;
	case CP_RB_WPTR:
		return m->wptr;
	case RDN_SCRATCH_REG(1):
		return m->fence;
	}
	return 0;
}

static void os_mmio_write32(void *c, uint32_t off, uint32_t v)
{
	/* Noted; mock_run() does the work, later. */
	if (off == CP_RB_WPTR)
		((struct mock *)c)->wptr = v;
}

static void os_delay_us(void *c, uint32_t usec)
{
	(void)c;
	(void)usec;
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

/*
 * The test's own picture of the screen, and what it expects.
 */

static uint32_t expect[SCREEN_HEIGHT][SCREEN_WIDTH];
static int cases, failures;

static uint32_t screen_start_pixel(uint32_t x, uint32_t y)
{
	return 0xa5000000 | (y << 12) | x;
}

static uint32_t other_pixel(uint32_t x, uint32_t y)
{
	return 0x5a000000 | (y << 12) | x;
}

/* Fresh video memory: both surfaces patterned, everything else a filler. */
static void reset_vram(struct mock *m)
{
	struct rdn_accel *accel = m->accel;
	uint32_t x, y;

	memset(m->vram, 0xee, APERTURE_SIZE);
	for (y = 0; y < SCREEN_HEIGHT; y++)
		for (x = 0; x < SCREEN_WIDTH; x++) {
			expect[y][x] = screen_start_pixel(x, y);
			rdn_vram_write32(accel, SCREEN_OFFSET + (y * SCREEN_PITCH + x) * 4,
					 expect[y][x]);
		}
	for (y = 0; y < OTHER_SIZE; y++)
		for (x = 0; x < OTHER_SIZE; x++)
			rdn_vram_write32(accel, OTHER_OFFSET + (y * OTHER_SIZE + x) * 4,
					 other_pixel(x, y));
	accel->wptr = 0;
	accel->fence_emitted = 0;
	m->rptr = m->wptr = 0;
	m->fence = 0;
}

/*
 * After a case: let the GPU do what it was given, then see whether the
 * screen is what is expected, and its surroundings whole.
 */
static void check(struct mock *m, const char *name, int r, int want,
		  uint32_t seq, uint32_t ibs, uint32_t rects)
{
	struct rdn_accel *accel = m->accel;
	uint32_t x, y, wrong = 0, i;

	mock_run(m);
	for (y = 0; y < SCREEN_HEIGHT; y++)
		for (x = 0; x < SCREEN_PITCH; x++) {
			uint32_t got = rdn_vram_read32(accel,
				SCREEN_OFFSET + (y * SCREEN_PITCH + x) * 4);
			uint32_t want = x < SCREEN_WIDTH ? expect[y][x] : 0xeeeeeeee;

			if (got != want && !wrong++)
				fprintf(stderr, "%s: pixel (%u,%u) is %08x, expected %08x\n",
					name, (unsigned)x, (unsigned)y, (unsigned)got,
					(unsigned)want);
		}
	/* The other surface is only ever read. */
	for (y = 0; y < OTHER_SIZE; y++)
		for (x = 0; x < OTHER_SIZE; x++)
			if (rdn_vram_read32(accel, OTHER_OFFSET + (y * OTHER_SIZE + x) * 4) !=
			    other_pixel(x, y))
				wrong++;
	/* The rows after the screen's last. */
	for (i = SCREEN_OFFSET + SCREEN_HEIGHT * SCREEN_PITCH * 4; i < OTHER_OFFSET; i++)
		if (m->vram[i] != 0xee)
			wrong++;

	for (i = 0; i < APERTURE_SIZE; i++)
		digest_add(m, m->vram[i]);
	cases++;
	if (r != want || wrong || m->failed || m->ibs != ibs || m->rects != rects ||
	    m->draws != ibs || (ibs && (seq != accel->fence_emitted || m->fence != seq))) {
		failures++;
		printf("FAIL %s: returned %d, %u wrong pixels, %u buffers, %u rectangles\n",
		       name, r, (unsigned)wrong, (unsigned)m->ibs, (unsigned)m->rects);
	} else {
		printf("%s: %u buffers, %u rectangles, digest %08x\n", name,
		       (unsigned)m->ibs, (unsigned)m->rects, (unsigned)m->digest);
	}
	m->failed = 0;
	m->ibs = m->rects = m->draws = 0;
}

static void test_blit(struct mock *m, const struct rdn_draw_surface *screen)
{
	static const struct rdn_blit_rect rects[] = {
		{ 0, 0, 10, 20, 64, 64 },
		{ 16, 8, 150, 3, 33, 17 },
		{ 63, 63, 199, 99, 1, 1 },
	};
	struct rdn_draw_surface other;
	uint32_t seq = 0, i, x, y;
	int r;

	reset_vram(m);
	other.gpu_addr = rdn_vram_addr(m->accel, OTHER_OFFSET);
	other.width = other.height = other.pitch_pixels = OTHER_SIZE;
	for (i = 0; i < 3; i++)
		for (y = 0; y < rects[i].height; y++)
			for (x = 0; x < rects[i].width; x++)
				expect[rects[i].dst_y + y][rects[i].dst_x + x] =
					other_pixel(rects[i].src_x + x, rects[i].src_y + y);
	r = rdn_blit(m->accel, screen, &other, rects, 3, WORK_OFFSET, &seq);
	check(m, "copy from another surface", r, 0, seq, 1, 3);
}

/*
 * The colour 0x11223344 must be the bytes 44 33 22 11 in a surface of
 * little-endian pixels and 11 22 33 44 in one of big-endian pixels, on
 * either host.
 */
static void test_fill(struct mock *m, const struct rdn_draw_surface *screen,
		      bool big_endian_pixels)
{
	static const struct rdn_fill_rect rects[] = {
		{ 5, 7, 50, 20 },
		{ 0, 90, 200, 10 },
		{ 199, 0, 1, 1 },
		{ 30, 20, 3, 75 },
	};
	static const uint8_t bytes[2][4] = {
		{ 0x44, 0x33, 0x22, 0x11 }, { 0x11, 0x22, 0x33, 0x44 }
	};
	uint32_t seq = 0, i, x, y;
	int r;

	reset_vram(m);
	for (i = 0; i < 4; i++)
		for (y = 0; y < rects[i].height; y++)
			for (x = 0; x < rects[i].width; x++)
				expect[rects[i].y + y][rects[i].x + x] =
					rdn_get_le32(bytes[big_endian_pixels], 0);
	r = rdn_blit_fill(m->accel, screen, rects, 4, 0x11223344, big_endian_pixels,
			  WORK_OFFSET, &seq);
	mock_run(m);
	if (memcmp(m->vram + SCREEN_OFFSET + (7 * SCREEN_PITCH + 5) * 4,
		   bytes[big_endian_pixels], 4))
		fail(m, "the filled pixel's bytes are in the wrong order", 0, 0);
	check(m, big_endian_pixels ? "fill, big-endian pixels" :
				     "fill, little-endian pixels", r, 0, seq, 1, 4);
}

/*
 * A move, against the plain model: every source is read from the picture
 * as it was, then the rectangles are written in their order.
 */
static void test_move(struct mock *m, const struct rdn_draw_surface *screen,
		      const char *name, const struct rdn_blit_rect *rects,
		      uint32_t count, uint32_t scratch_width,
		      uint32_t scratch_height, int want)
{
	static uint32_t before[SCREEN_HEIGHT][SCREEN_WIDTH];
	struct rdn_draw_surface scratch;
	uint32_t seq = 0, i, x, y;
	int r;

	reset_vram(m);
	scratch.gpu_addr = rdn_vram_addr(m->accel, SCRATCH_OFFSET);
	scratch.width = scratch_width;
	scratch.height = scratch_height;
	scratch.pitch_pixels = (scratch_width + 63) & ~63u;
	memcpy(before, expect, sizeof(before));
	for (i = 0; i < count && !want; i++)
		for (y = 0; y < rects[i].height; y++)
			for (x = 0; x < rects[i].width; x++)
				expect[rects[i].dst_y + y][rects[i].dst_x + x] =
					before[rects[i].src_y + y][rects[i].src_x + x];
	r = rdn_blit_move(m->accel, screen, &scratch, rects, count, WORK_OFFSET, &seq);
	check(m, name, r, want, seq, want ? 0 : 2, want ? 0 : 2 * count);
}

static void test_moves(struct mock *m, const struct rdn_draw_surface *screen)
{
	/* A block of 60 by 40 at (40,30), a few pixels in each direction. */
	static const struct {
		const char *name;
		struct rdn_blit_rect rect;
	} one[] = {
		{ "move left", { 40, 30, 30, 30, 60, 40 } },
		{ "move right", { 40, 30, 50, 30, 60, 40 } },
		{ "move up", { 40, 30, 40, 20, 60, 40 } },
		{ "move down", { 40, 30, 40, 45, 60, 40 } },
		{ "move up and left", { 40, 30, 33, 21, 60, 40 } },
		{ "move up and right", { 40, 30, 47, 21, 60, 40 } },
		{ "move down and left", { 40, 30, 33, 39, 60, 40 } },
		{ "move down and right", { 40, 30, 47, 39, 60, 40 } },
		{ "move onto itself", { 40, 30, 40, 30, 60, 40 } },
		{ "move without overlap", { 0, 0, 120, 55, 60, 40 } },
		/* Scrolling: all of the screen but one column, one row. */
		{ "scroll left by one", { 1, 0, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT } },
		{ "scroll right by one", { 0, 0, 1, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT } },
		{ "scroll up by one", { 0, 1, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT - 1 } },
		{ "scroll down by one", { 0, 0, 0, 1, SCREEN_WIDTH, SCREEN_HEIGHT - 1 } },
	};
	/*
	 * Two blocks that change places, a third whose source the first
	 * overwrites and whose destination lies over the second's.
	 */
	static const struct rdn_blit_rect several[] = {
		{ 10, 10, 60, 10, 40, 40 },
		{ 60, 10, 10, 10, 40, 40 },
		{ 70, 20, 20, 30, 50, 50 },
	};
	static const struct rdn_blit_rect outside[] = {
		{ 10, 10, 60, 10, 40, 40 },
		{ 150, 10, 161, 10, 40, 40 },
	};
	static const struct rdn_blit_rect empty = { 10, 10, 60, 10, 0, 40 };
	uint32_t i;

	for (i = 0; i < sizeof(one) / sizeof(one[0]); i++)
		test_move(m, screen, one[i].name, &one[i].rect, 1,
			  SCREEN_WIDTH, SCREEN_HEIGHT, 0);
	test_move(m, screen, "move several at once", several, 3,
		  SCREEN_WIDTH, SCREEN_HEIGHT, 0);
	/* The box around the three sources is 110 by 60. */
	test_move(m, screen, "move with the smallest scratch surface", several, 3,
		  110, 60, 0);
	test_move(m, screen, "move refused: scratch surface too narrow", several, 3,
		  109, 60, -EINVAL);
	test_move(m, screen, "move refused: scratch surface too low", several, 3,
		  110, 59, -EINVAL);
	test_move(m, screen, "move refused: destination leaves the surface",
		  outside, 2, SCREEN_WIDTH, SCREEN_HEIGHT, -EINVAL);
	test_move(m, screen, "move refused: empty rectangle", &empty, 1,
		  SCREEN_WIDTH, SCREEN_HEIGHT, -EINVAL);
	test_move(m, screen, "move refused: no rectangles", several, 0,
		  SCREEN_WIDTH, SCREEN_HEIGHT, -EINVAL);
}

/* Refusals must leave everything alone: no command, no byte written. */
static void test_refusals(struct mock *m, const struct rdn_draw_surface *screen)
{
	static const struct rdn_fill_rect outside = { 190, 95, 11, 5 };
	static const struct rdn_fill_rect inside = { 190, 95, 10, 5 };
	struct rdn_draw_surface scratch = *screen;
	static const struct rdn_blit_rect rect = { 0, 0, 5, 5, 10, 10 };
	uint32_t seq = 0;
	int r;

	reset_vram(m);
	r = rdn_blit_fill(m->accel, screen, &outside, 1, 0, false, WORK_OFFSET, &seq);
	check(m, "fill refused: rectangle leaves the surface", r, -EINVAL, seq, 0, 0);

	/* The work area must fit below the end of the aperture. */
	reset_vram(m);
	r = rdn_blit_fill(m->accel, screen, &inside, 1, 0, false,
			  APERTURE_SIZE - rdn_blit_work_bytes() + 0x1000, &seq);
	check(m, "fill refused: work area leaves the aperture", r, -EINVAL, seq, 0, 0);

	reset_vram(m);
	scratch.gpu_addr = rdn_vram_addr(m->accel, SCRATCH_OFFSET);
	r = rdn_blit_move(m->accel, screen, &scratch, &rect, 1,
			  APERTURE_SIZE - rdn_blit_move_work_bytes() + 0x1000, &seq);
	check(m, "move refused: work area leaves the aperture", r, -EINVAL, seq, 0, 0);
}

int main(void)
{
	struct rdn_draw_surface screen;
	struct rdn_accel accel;
	struct rdn_card card;
	struct rdn_os os;
	struct mock m;
	int swapped;

	memset(&m, 0, sizeof(m));
	m.accel = &accel;
	m.digest = 2166136261u;
	m.vram = malloc(APERTURE_SIZE);
	if (!m.vram)
		return 2;

	memset(&os, 0, sizeof(os));
	os.cookie = &m;
	os.mmio_read32 = os_mmio_read32;
	os.mmio_write32 = os_mmio_write32;
	os.delay_us = os_delay_us;
	os.log = os_log;
	memset(&card, 0, sizeof(card));
	card.os = &os;

	for (swapped = 0; swapped < 2; swapped++) {
		/* What rdn_accel_init() leaves, without a card to start. */
		memset(&accel, 0, sizeof(accel));
		accel.card = &card;
		accel.vram_base = VRAM_BASE;
		accel.vram_size = APERTURE_SIZE;
		/* malloc()'s memory is aligned for words. */
		accel.aperture = (volatile uint32_t *)(void *)m.vram;
		accel.aperture_size = APERTURE_SIZE;
		accel.ring_offset = RING_OFFSET;
		accel.ring_words = RING_BYTES / 4;
		accel.ready = true;
		accel.swapped = swapped;
		printf("command words %s-endian\n", swapped ? "big" : "little");

		screen.gpu_addr = rdn_vram_addr(&accel, SCREEN_OFFSET);
		screen.width = SCREEN_WIDTH;
		screen.height = SCREEN_HEIGHT;
		screen.pitch_pixels = SCREEN_PITCH;

		test_blit(&m, &screen);
		test_fill(&m, &screen, false);
		test_fill(&m, &screen, true);
		test_moves(&m, &screen);
		test_refusals(&m, &screen);
	}

	if (failures) {
		printf("FAIL: %d of %d cases\n", failures, cases);
		return 1;
	}
	printf("PASS: %d cases, digest %08x\n", cases, (unsigned)m.digest);
	return 0;
}
