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
   /* Freed buffers the GPU may still be using (rdn_buffer_destroy). */
   struct list_head dead_bos;
   uint64_t allocated_bytes;
   uint64_t num_flushes;
};

void radeon_surface_init_functions(struct radeon_drm_winsys *ws);

#endif /* RDN_RADEON_DRM_WINSYS_SHIM_H */
