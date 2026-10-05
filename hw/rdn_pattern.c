/*
 * Test pattern.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include "rdn_pattern.h"

/* 5x7 glyphs for the digits and 'x', one byte per row, bit 4 leftmost. */
static const uint8_t font[11][7] = {
	{ 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },	/* 0 */
	{ 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },	/* 1 */
	{ 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },	/* 2 */
	{ 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e },	/* 3 */
	{ 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },	/* 4 */
	{ 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e },	/* 5 */
	{ 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e },	/* 6 */
	{ 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },	/* 7 */
	{ 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },	/* 8 */
	{ 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c },	/* 9 */
	{ 0x00, 0x00, 0x11, 0x0a, 0x04, 0x0a, 0x11 },	/* x */
};

struct canvas {
	volatile uint32_t *pix;
	uint32_t width, height, pitch;
};

static void fill(struct canvas *c, uint32_t x0, uint32_t y0, uint32_t w,
		 uint32_t h, uint32_t rgb)
{
	uint32_t x, y;

	for (y = y0; y < y0 + h && y < c->height; y++)
		for (x = x0; x < x0 + w && x < c->width; x++)
			c->pix[y * c->pitch + x] = rgb;
}

/* Glyph index 0-9 for digits, 10 for 'x'. */
static void glyph(struct canvas *c, uint32_t x0, uint32_t y0, uint32_t scale,
		  int g, uint32_t rgb)
{
	int row, col;

	for (row = 0; row < 7; row++)
		for (col = 0; col < 5; col++)
			if (font[g][row] & (0x10 >> col))
				fill(c, x0 + col * scale, y0 + row * scale,
				     scale, scale, rgb);
}

/* Decimal digits of v into out[], returns the count. */
static int digits(uint32_t v, int *out)
{
	int tmp[10], n = 0, i;

	do {
		tmp[n++] = (int)(v % 10);
		v /= 10;
	} while (v);
	for (i = 0; i < n; i++)
		out[i] = tmp[n - 1 - i];
	return n;
}

void rdn_pattern_draw(volatile uint32_t *pixels, uint32_t width,
		      uint32_t height, uint32_t pitch_pixels)
{
	static const uint32_t bars[8] = {
		0xffffff, 0xffff00, 0x00ffff, 0x00ff00,
		0xff00ff, 0xff0000, 0x0000ff, 0x000000
	};
	const uint32_t m = 16, scale = 8;
	uint32_t w = width, h = height, i, tw, th, x;
	int label[24], n;
	struct canvas c;

	c.pix = pixels;
	c.width = width;
	c.height = height;
	c.pitch = pitch_pixels;

	fill(&c, 0, 0, w, h, 0x000000);
	fill(&c, 0, 0, w, 1, 0xffffff);
	fill(&c, 0, h - 1, w, 1, 0xffffff);
	fill(&c, 0, 0, 1, h, 0xffffff);
	fill(&c, w - 1, 0, 1, h, 0xffffff);

	for (i = 0; i < 8; i++) {
		uint32_t x0 = m + (w - 2 * m) * i / 8;
		uint32_t x1 = m + (w - 2 * m) * (i + 1) / 8;

		fill(&c, x0, m, x1 - x0, h - 2 * m, bars[i]);
	}

	n = digits(w, label);
	label[n++] = 10;
	n += digits(h, label + n);

	tw = (uint32_t)n * 6 * scale;
	th = 7 * scale;
	fill(&c, (w - tw) / 2 - 2 * scale, (h - th) / 2 - 2 * scale,
	     tw + 3 * scale, th + 4 * scale, 0x000000);
	x = (w - tw) / 2;
	for (i = 0; i < (uint32_t)n; i++, x += 6 * scale)
		glyph(&c, x, (h - th) / 2, scale, label[i], 0xffffff);
}
