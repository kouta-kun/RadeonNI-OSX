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

/*
 * Video memory for a surface's own picture: `bytes` of it, a multiple of
 * 4096 from the start of the aperture (the offset is what
 * OSMesaSurfaceStorage and rdn_target_surface_buffer take). False if there
 * is none.
 */
bool rdn_target_vram_alloc(uint32_t bytes, uint32_t *offset);
void rdn_target_vram_free(uint32_t offset);
/*
 * Where the CPU finds such memory, with all the GPU has finished writing
 * to it by now: 32-bit pixels are in the CPU's byte order. NULL if the
 * device has no such address.
 */
void *rdn_target_vram_map(uint32_t offset);
/* Tell the window system where surface `id` keeps its picture (width 0: nowhere). */
bool rdn_target_surface_buffer(uint32_t id, uint32_t offset, uint32_t row_bytes,
                               uint32_t width, uint32_t height);
#define RDN_TARGET_VRAM_HANDLE 0x7571
/* The surfaces that keep their picture in such a buffer: id, offset, row
 * bytes, width, height each; at most `max`; returns how many. */
uint32_t rdn_target_surface_list(uint32_t (*list)[5], uint32_t max);
/* The surface that is locked for reading at this moment, or 0. */
uint32_t rdn_target_surface_locked(void);

/* The `handle` that names the screen to OSMesaMakeCurrentDirect. */
#define RDN_TARGET_SCREEN_HANDLE 0x7570

#endif /* RDN_TARGET_H */
