/*
 * The screen behind the off-screen frontend: r600 on the osx-gpu winsys.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "pipe/p_screen.h"
#include "r600/r600_public.h"
#include "rdn_winsys.h"
#include "rdn_target.h"
#include "util/macros.h"

static struct rdn_device *the_device;

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

struct pipe_screen *osmesa_create_screen(void);

struct pipe_screen *
osmesa_create_screen(void)
{
   struct rdn_device *dev = rdn_device_open();
   struct radeon_winsys *ws;

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
