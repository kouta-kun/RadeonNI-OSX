/*
 * The screen behind the off-screen frontend: r600 on the osx-gpu winsys.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pipe/p_screen.h"
#include "r600/r600_public.h"
#include "rdn_winsys.h"
#include "rdn_target.h"
#include "util/macros.h"

static struct rdn_device *the_device;

#ifdef RDN_TARGET_SOFT
#include "tests/rdn_soft.h"
/* The screen is the software one, with ordinary memory for video memory. */
static bool soft;
#endif

PUBLIC bool
rdn_target_screen(volatile uint32_t **pixels, uint32_t *width,
                  uint32_t *height, uint32_t *pitch)
{
   if (!the_device || !the_device->screen.width)
      return false;
   *pixels = (volatile uint32_t *)((uint8_t *)the_device->aperture +
                                   the_device->screen.offset);
   *width = the_device->screen.width;
   *height = the_device->screen.height;
   *pitch = the_device->screen.pitch_pixels;
   return true;
}

bool
rdn_target_surface_region(uint32_t id, int32_t bounds[4], int16_t (*rects)[4],
                          uint32_t max, uint32_t *count)
{
   static struct rdn_region region;
   uint32_t n;

   if (!the_device || !the_device->surface_region ||
       !the_device->surface_region(the_device, id, &region))
      return false;
   memcpy(bounds, region.bounds, sizeof(region.bounds));
   *count = region.count;
   n = MIN2(MIN2(region.count, RDN_REGION_RECTS), max);
   memcpy(rects, region.rects, n * sizeof(region.rects[0]));
   return true;
}

bool
rdn_target_vram_alloc(uint32_t bytes, uint32_t *offset)
{
   uint64_t o;

#ifdef RDN_TARGET_SOFT
   if (soft)
      return rdn_soft_vram_alloc(bytes, offset);
#endif
   if (!the_device || the_device->alloc(the_device, bytes, 4096, &o))
      return false;
   *offset = (uint32_t)o;
   return true;
}

void
rdn_target_vram_free(uint32_t offset)
{
   if (the_device)
      the_device->free(the_device, offset);
}

void *
rdn_target_vram_map(uint32_t offset)
{
#ifdef RDN_TARGET_SOFT
   if (soft)
      return rdn_soft_vram_map(offset);
#endif
   if (!the_device || !the_device->aperture || offset >= the_device->aperture_size)
      return NULL;
   the_device->sync_for_cpu(the_device);
   return (uint8_t *)the_device->aperture + offset;
}

bool
rdn_target_surface_buffer(uint32_t id, uint32_t offset, uint32_t row_bytes,
                          uint32_t width, uint32_t height)
{
   return the_device && the_device->surface_buffer &&
          the_device->surface_buffer(the_device, id, offset, row_bytes, width,
                                     height);
}

uint32_t
rdn_target_surface_list(uint32_t (*list)[5], uint32_t max)
{
   struct rdn_surface s[32];
   uint32_t n, i;

   if (!the_device || !the_device->surface_list)
      return 0;
   n = the_device->surface_list(the_device, s, MIN2(max, 32));
   for (i = 0; i < n; i++) {
      list[i][0] = s[i].id;
      list[i][1] = s[i].offset;
      list[i][2] = s[i].row_bytes;
      list[i][3] = s[i].width;
      list[i][4] = s[i].height;
   }
   return n;
}

uint32_t
rdn_target_surface_locked(void)
{
   return the_device && the_device->surface_locked ?
          the_device->surface_locked(the_device) : 0;
}

struct pipe_screen *osmesa_create_screen(void);

struct pipe_screen *
osmesa_create_screen(void)
{
   struct rdn_device *dev;
   struct radeon_winsys *ws;

#ifdef RDN_TARGET_SOFT
   /* The test program without the card: tests/rdn_soft.c. */
   if (getenv("RDN_SOFT")) {
      soft = true;
      return rdn_soft_screen_create();
   }
#endif
   dev = rdn_device_open();
   if (!dev) {
      fprintf(stderr, "rdn: no device\n");
      return NULL;
   }
   ws = rdn_winsys_create(dev, NULL, r600_screen_create);
   if (!ws)
      return NULL;
   the_device = dev;
   return ws->screen;
}
