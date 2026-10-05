/*
 * Test pattern, shared by every front end so that what the user is asked to
 * look at is always the same picture.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_PATTERN_H
#define RDN_PATTERN_H

#include "rdn_os.h"

/*
 * Draw into a 32-bit XRGB surface, writing each pixel as a native 32-bit
 * word (so the surface must be scanned out with rdn_fb.big_endian_pixels
 * set to RDN_BIG_ENDIAN):
 *  - a one-pixel white line on all four edges;
 *  - inside it a black margin 15 pixels wide;
 *  - eight vertical bars, left to right: white, yellow, cyan, green,
 *    magenta, red, blue, black;
 *  - in the middle a black box with "<width>x<height>" in white digits.
 */
void rdn_pattern_draw(volatile uint32_t *pixels, uint32_t width,
		      uint32_t height, uint32_t pitch_pixels);

#endif /* RDN_PATTERN_H */
