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

#endif /* RDN_TARGET_H */
