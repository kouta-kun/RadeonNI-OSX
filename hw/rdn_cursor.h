/*
 * The display controller's hardware cursor.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_CURSOR_H
#define RDN_CURSOR_H

#include <stdbool.h>
#include <stdint.h>

struct rdn_card;

/* The image is always this many pixels each way. */
#define RDN_CURSOR_SIZE		64

/*
 * Show the cursor image stored at `aperture_offset` (a multiple of 4096;
 * RDN_CURSOR_SIZE squared little-endian ARGB words, colours multiplied by
 * alpha) with its top left corner at x, y on the screen, or hide it. x
 * and y may be negative. Only register writes: safe where sleeping is not
 * allowed.
 */
void rdn_cursor_set(struct rdn_card *card, uint32_t aperture_offset, int x,
		    int y, bool visible);

#endif /* RDN_CURSOR_H */
