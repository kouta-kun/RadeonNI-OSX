/*
 * Stand-in for libdrm's xf86drm.h, for building Mesa's copy of the radeon
 * surface layout code (winsys/radeon/drm/radeon_surface.c) unchanged where
 * there is no DRM. That code asks the kernel for two numbers and for the
 * driver version; rdn_surface.c sets them here.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_XF86DRM_SHIM_H
#define RDN_XF86DRM_SHIM_H

#include <errno.h>
#include <stdint.h>
#include "drm-uapi/radeon_drm.h"

extern uint32_t rdn_shim_device_id;
extern uint32_t rdn_shim_tiling_config;

typedef struct {
   int version_major;
   int version_minor;
   int version_patchlevel;
} drmVersion, *drmVersionPtr;

static inline drmVersionPtr drmGetVersion(int fd)
{
   /* The last radeon DRM version; the surface code only checks minimums. */
   static drmVersion version = { 2, 51, 0 };

   (void)fd;
   return &version;
}

static inline void drmFreeVersion(drmVersionPtr version)
{
   (void)version;
}

static inline int drmCommandWriteRead(int fd, unsigned long index, void *data,
                                      unsigned long size)
{
   struct drm_radeon_info *info = (struct drm_radeon_info *)data;
   uint32_t *value = (uint32_t *)(uintptr_t)info->value;

   (void)fd;
   (void)size;
   if (index != DRM_RADEON_INFO)
      return -EINVAL;
   switch (info->request) {
   case RADEON_INFO_DEVICE_ID:
      *value = rdn_shim_device_id;
      return 0;
   case RADEON_INFO_TILING_CONFIG:
      *value = rdn_shim_tiling_config;
      return 0;
   }
   return -EINVAL;
}

#endif /* RDN_XF86DRM_SHIM_H */
