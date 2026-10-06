/*
 * Stand-in for winsys/radeon/drm/radeon_drm_winsys.h, so that Mesa's
 * radeon_drm_surface.c builds unchanged on top of this winsys: it only
 * needs a winsys structure with these names in it.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_RADEON_DRM_WINSYS_SHIM_H
#define RDN_RADEON_DRM_WINSYS_SHIM_H

#include "winsys/radeon_winsys.h"
#include "util/list.h"
#include "util/simple_mtx.h"
#include "util/u_inlines.h"
#include "rdn_device.h"

struct radeon_surface_manager;

enum radeon_generation {
   DRV_R300,
   DRV_R600,
   DRV_SI
};

struct radeon_drm_winsys {
   struct radeon_winsys base;
   struct pipe_reference reference;

   /* What radeon_drm_surface.c uses. */
   struct radeon_surface_manager *surf_man;
   struct radeon_info info;
   enum radeon_generation gen;

   /* This winsys. */
   struct rdn_device *dev;
   simple_mtx_t lock;
   /* Command buffers in video memory whose fence has not been reached. */
   struct list_head pending_ibs;
   /*
    * Video memory this process has let go of and keeps for its next
    * request of the same size (rdn_winsys.c, "The cache"): in the order it
    * was let go of, and the same entries by size.
    */
   struct list_head cache_all;
   struct list_head cache_buckets[128];
   uint64_t cached_bytes[2];   /* in the aperture, beyond it */
   /* The newest fence known to have been reached. */
   uint32_t last_done;
   bool have_last_done;
   uint64_t allocated_bytes;
   /*
    * RDN_STATS: what is allocated now, by where it is (aperture, beyond)
    * and by size class (class c: under 4 KB << c), and the most there was.
    */
   struct {
      uint64_t bytes[2], count[2], padding[2];
      uint64_t class_bytes[2][24], class_count[2][24];
      uint64_t peak_bytes[2], creates, cache_hits;
   } mem_stats;
   uint64_t num_flushes;
};

void radeon_surface_init_functions(struct radeon_drm_winsys *ws);

#endif /* RDN_RADEON_DRM_WINSYS_SHIM_H */
