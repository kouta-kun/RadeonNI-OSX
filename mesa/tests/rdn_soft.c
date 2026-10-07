/*
 * A stand-in for the card, for rdn_gltest where there is none (RDN_SOFT=1):
 * Mesa's software rasteriser over a block of ordinary memory that plays
 * video memory. A store named by RDN_TARGET_VRAM_HANDLE and an offset is
 * imported from that block as it is from the card's, so what the front end
 * does with contexts, share lists and stores can be checked on the host
 * and under qemu-ppc. It says nothing about r600 or the card.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>

#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "frontend/sw_winsys.h"
#include "frontend/winsys_handle.h"
#include "softpipe/sp_public.h"
#include "util/u_memory.h"

#include "rdn_target.h"
#include "rdn_soft.h"

#define SOFT_VRAM_BYTES (16u << 20)

static uint8_t *soft_vram;
static uint32_t soft_vram_used;

/* A store: nothing but where it is. */
struct soft_dt {
   uint8_t *map;
};

static void
soft_destroy(struct sw_winsys *ws)
{
}

static bool
soft_format_supported(struct sw_winsys *ws, unsigned tex_usage,
                      enum pipe_format format)
{
   return true;
}

/* Only stores are display targets here; the screen never makes one. */
static struct sw_displaytarget *
soft_dt_create(struct sw_winsys *ws, unsigned tex_usage,
               enum pipe_format format, unsigned width, unsigned height,
               unsigned alignment, const void *front_private,
               unsigned *stride)
{
   return NULL;
}

static struct sw_displaytarget *
soft_dt_from_handle(struct sw_winsys *ws, const struct pipe_resource *templat,
                    struct winsys_handle *whandle, unsigned *stride)
{
   struct soft_dt *dt;

   if (whandle->type != WINSYS_HANDLE_TYPE_KMS ||
       whandle->handle != RDN_TARGET_VRAM_HANDLE ||
       (uint64_t)whandle->offset + (uint64_t)whandle->stride * templat->height0 >
       soft_vram_used)
      return NULL;
   dt = CALLOC_STRUCT(soft_dt);
   if (!dt)
      return NULL;
   dt->map = soft_vram + whandle->offset;
   *stride = whandle->stride;
   return (struct sw_displaytarget *)dt;
}

static bool
soft_dt_get_handle(struct sw_winsys *ws, struct sw_displaytarget *dt,
                   struct winsys_handle *whandle)
{
   return false;
}

static void *
soft_dt_map(struct sw_winsys *ws, struct sw_displaytarget *dt, unsigned flags)
{
   return ((struct soft_dt *)dt)->map;
}

static void
soft_dt_unmap(struct sw_winsys *ws, struct sw_displaytarget *dt)
{
}

static void
soft_dt_display(struct sw_winsys *ws, struct sw_displaytarget *dt,
                void *context_private, unsigned nboxes, struct pipe_box *box)
{
}

static void
soft_dt_destroy(struct sw_winsys *ws, struct sw_displaytarget *dt)
{
   FREE(dt);
}

static struct sw_winsys soft_winsys = {
   .destroy = soft_destroy,
   .is_displaytarget_format_supported = soft_format_supported,
   .displaytarget_create = soft_dt_create,
   .displaytarget_from_handle = soft_dt_from_handle,
   .displaytarget_get_handle = soft_dt_get_handle,
   .displaytarget_map = soft_dt_map,
   .displaytarget_unmap = soft_dt_unmap,
   .displaytarget_display = soft_dt_display,
   .displaytarget_destroy = soft_dt_destroy,
};

struct pipe_screen *
rdn_soft_screen_create(void)
{
   struct pipe_screen *screen;
   struct pipe_caps *caps;

   soft_vram = calloc(1, SOFT_VRAM_BYTES);
   if (!soft_vram)
      return NULL;
   screen = softpipe_create_screen(&soft_winsys);
   if (!screen)
      return NULL;
   /*
    * What glthread asks of a driver before it starts. softpipe's buffers
    * are plain memory, so both hold; it only never says so.
    */
   caps = (struct pipe_caps *)&screen->caps;
   caps->map_unsynchronized_thread_safe = true;
   caps->allow_mapped_buffers_during_execution = true;
   return screen;
}

/* Handed out from the start, 4096 bytes at a time, and never taken back. */
bool
rdn_soft_vram_alloc(uint32_t bytes, uint32_t *offset)
{
   bytes = (bytes + 4095) & ~4095u;
   if (!soft_vram || bytes > SOFT_VRAM_BYTES - soft_vram_used)
      return false;
   *offset = soft_vram_used;
   soft_vram_used += bytes;
   return true;
}

void *
rdn_soft_vram_map(uint32_t offset)
{
   return soft_vram ? soft_vram + offset : NULL;
}
