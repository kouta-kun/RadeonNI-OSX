/*
 * What the osx-gpu target offers a test program besides OpenGL.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_TARGET_H
#define RDN_TARGET_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The surface the display shows, once a GL context exists: 32-bit pixels
 * in the CPU's byte order, `pitch` pixels per row. False if the device
 * does not know it.
 */
bool rdn_target_screen(volatile uint32_t **pixels, uint32_t *width,
                       uint32_t *height, uint32_t *pitch);

/*
 * The shape on the screen of the window system's surface `id`: the box
 * around it (x, y from the top left, width, height) and up to `max`
 * rectangles of four numbers each. `*count` is how many the shape has,
 * which may be more than `max`. False if the device cannot tell.
 */
bool rdn_target_surface_region(uint32_t id, int32_t bounds[4],
                               int16_t (*rects)[4], uint32_t max,
                               uint32_t *count);

#endif /* RDN_TARGET_H */
