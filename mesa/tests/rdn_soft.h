/*
 * The software stand-in for the card (rdn_soft.c), as rdn_target.c uses it
 * in the test program.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_SOFT_H
#define RDN_SOFT_H

#include <stdbool.h>
#include <stdint.h>

struct pipe_screen;

struct pipe_screen *rdn_soft_screen_create(void);
bool rdn_soft_vram_alloc(uint32_t bytes, uint32_t *offset);
void *rdn_soft_vram_map(uint32_t offset);

#endif /* RDN_SOFT_H */
