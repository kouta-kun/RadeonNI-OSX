/*
 * The screen behind the off-screen frontend: r600 on the osx-gpu winsys.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pipe/p_screen.h"
#include "r600/r600_public.h"
#include "rdn_winsys.h"
#include "rdn_target.h"
#include "util/macros.h"

static struct rdn_device *the_device;

struct external_store {
   struct external_store *next;
   uint64_t offset, size;
   unsigned references;
};
static struct external_store *external_stores;
static pthread_mutex_t external_lock = PTHREAD_MUTEX_INITIALIZER;

static bool track_store(uint64_t offset, uint64_t size)
{
   struct external_store *store = calloc(1, sizeof(*store));
   if (!store) {
      the_device->free(the_device, offset);
      return false;
   }
   store->offset = offset;
   store->size = size;
   store->references = 1;
   pthread_mutex_lock(&external_lock);
   store->next = external_stores;
   external_stores = store;
   pthread_mutex_unlock(&external_lock);
   return true;
}

static void *retain_store(struct rdn_device *dev, uint64_t offset)
{
   struct external_store *store;
   pthread_mutex_lock(&external_lock);
   for (store = external_stores; store; store = store->next) {
      if (offset >= store->offset && offset - store->offset < store->size) {
         ++store->references;
         break;
      }
   }
   pthread_mutex_unlock(&external_lock);
   return store;
}

static void release_store(struct rdn_device *dev, void *reference)
{
   struct external_store *store = reference, **link;
   pthread_mutex_lock(&external_lock);
   if (--store->references == 0) {
      for (link = &external_stores; *link != store; link = &(*link)->next) {}
      *link = store->next;
      dev->free(dev, store->offset);
      free(store);
   }
   pthread_mutex_unlock(&external_lock);
}

#ifdef RDN_TARGET_SOFT
#include "tests/rdn_soft.h"
/* The screen is the software one, with ordinary memory for video memory. */
static bool soft;
#endif

PUBLIC bool
rdn_target_screen(volatile uint32_t **pixels, uint32_t *width,
                  uint32_t *height, uint32_t *pitch)
{
   if (the_device && the_device->screen_refresh)
      the_device->screen_refresh(the_device);
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
   if (!track_store(o, bytes))
      return false;
   *offset = (uint32_t)o;
   return true;
}

bool
rdn_target_vram_alloc_hidden(uint32_t bytes, uint32_t *offset)
{
   uint64_t o;

   if (!the_device || !the_device->alloc_hidden ||
       the_device->alloc_hidden(the_device, bytes, 4096, &o))
      return false;
   if (!track_store(o, bytes))
      return false;
   *offset = (uint32_t)o;
   return true;
}

bool
rdn_target_uvd_epoch(uint32_t *epoch)
{
   return the_device && the_device->uvd_epoch && !the_device->uvd_epoch(the_device, epoch);
}

bool
rdn_target_uvd_reset(uint32_t *epoch)
{
   return the_device && the_device->uvd_reset && !the_device->uvd_reset(the_device, epoch);
}

void
rdn_target_vram_free(uint32_t offset)
{
   struct external_store *store;
   if (!the_device)
      return;
   /* Take a temporary reference while locating the owner's reference. */
   store = retain_store(the_device, offset);
   if (store) {
      release_store(the_device, store);
      release_store(the_device, store);
   }
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

bool
rdn_target_surface_query(uint32_t id, uint32_t info[5], bool *found)
{
   struct rdn_surface s = {0};
   *found = false;
   if (!the_device || !the_device->surface_query ||
       !the_device->surface_query(the_device, id, &s, found))
      return false;
   info[0] = s.id;
   info[1] = s.offset;
   info[2] = s.row_bytes;
   info[3] = s.width;
   info[4] = s.height;
   return true;
}

bool
rdn_target_surface_snapshot(uint32_t (*list)[5], uint32_t max, uint32_t *count)
{
   struct rdn_surface s[32];
   int n, i;

   *count = 0;
   if (!the_device || !the_device->surface_list)
      return false;
   n = the_device->surface_list(the_device, s, MIN2(max, 32));
   if (n < 0 || n > MIN2(max, 32))
      return false;
   for (i = 0; i < n; i++) {
      list[i][0] = s[i].id;
      list[i][1] = s[i].offset;
      list[i][2] = s[i].row_bytes;
      list[i][3] = s[i].width;
      list[i][4] = s[i].height;
   }
   *count = n;
   return true;
}

uint32_t
rdn_target_surface_list(uint32_t (*list)[5], uint32_t max)
{
   uint32_t count;
   rdn_target_surface_snapshot(list, max, &count);
   return count;
}


uint32_t
rdn_target_surface_locked(void)
{
   return the_device && the_device->surface_locked ?
          the_device->surface_locked(the_device) : 0;
}

struct pipe_screen *osmesa_create_screen(void);

/*
 * glBufferSubData under glthread: the program's thread copies the data
 * into an upload buffer and the GPU copies from there, instead of the data
 * going through glthread's queue (two copies, and the program waits when
 * the queue is full). radeonsi's default; r600 leaves it off. For the
 * programs the GL bundle mirrors vertex memory for (the same list, or
 * RDN_VAR=1), whose every flush is a glBufferSubData: 14 % of Call of
 * Duty 2's thread. RDN_SUBDATA_COPY=1 or 0 decides for any program.
 */
static bool
rdn_subdata_copy(void)
{
   const char *env = getenv("RDN_SUBDATA_COPY"), *name = NULL;
   char line[256];
   bool on = false;
   FILE *f;

   if (env)
      return atoi(env) != 0;
   if ((env = getenv("RDN_VAR")) != NULL)
      return atoi(env) != 0;
#ifdef __APPLE__
   name = getprogname();
#endif
   if (!name || !(f = fopen("/Library/Application Support/RadeonNI/vertexrange", "r")))
      return false;
   while (fgets(line, sizeof(line), f)) {
      line[strcspn(line, "\r\n")] = 0;
      if (!strcmp(line[0] == '+' ? line + 1 : line, name))
         on = true;
   }
   fclose(f);
   return on;
}

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
   dev->retain_external = retain_store;
   dev->release_external = release_store;
   /*
    * Buffers over the program's own memory (GL_AMD_pinned_memory). r600
    * leaves it off on big-endian; vertex arrays, the only thing they are
    * for here, are fetched with the swap any vertex buffer gets.
    */
   if (dev->gart_bind_user)
      ((struct pipe_caps *)&ws->screen->caps)->resource_from_user_memory = true;
   if (rdn_subdata_copy())
      ((struct pipe_caps *)&ws->screen->caps)->allow_glthread_buffer_subdata_opt = true;
   return ws->screen;
}

bool
rdn_target_surface_backing(uint32_t id, void **address, uint32_t *stride,
                           uint32_t *width, uint32_t *height)
{
   return the_device && the_device->surface_backing &&
          the_device->surface_backing(the_device, id, address, stride, width, height);
}

bool rdn_target_surface_gpu_info(uint32_t id, uint32_t *stride, uint32_t *width, uint32_t *height)
{
   struct rdn_surface surface;
   if (!the_device || !the_device->surface_gpu_info ||
       !the_device->surface_gpu_info(the_device, id, &surface)) return false;
   *stride = surface.row_bytes; *width = surface.width; *height = surface.height;
   return true;
}

int rdn_target_surface_gpu_copy(uint32_t id, uint32_t offset, uint32_t stride,
                                uint32_t width, uint32_t height)
{
   return the_device && the_device->surface_gpu_copy ?
      the_device->surface_gpu_copy(the_device, id, offset, stride, width, height) : 1;
}
