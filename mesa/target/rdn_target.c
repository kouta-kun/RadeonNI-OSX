/*
 * The screen behind the off-screen frontend: r600 on the osx-gpu winsys.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "pipe/p_screen.h"
#include "r600/r600_public.h"
#include "rdn_winsys.h"

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
   return ws ? ws->screen : NULL;
}
