/*
 * Mesa winsys for the Radeon HD 7570 without the Linux DRM.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_WINSYS_H
#define RDN_WINSYS_H

#include "winsys/radeon_winsys.h"
#include "rdn_device.h"

struct pipe_screen_config;

/*
 * resource_from_handle with a winsys_handle of type WINSYS_HANDLE_TYPE_KMS
 * and this handle gives the surface the display shows (rdn_device's
 * screen), linear, not owned by the resource.
 */
#define RDN_WINSYS_HANDLE_SCREEN 0x7570
/*
 * And with this handle, video memory that the caller got from the device
 * itself (rdn_device's alloc) at the winsys_handle's offset: a linear
 * surface whose place the caller can tell others.
 */
#define RDN_WINSYS_HANDLE_VRAM 0x7571

typedef struct pipe_screen *(*rdn_screen_create_t)(struct radeon_winsys *,
                                                   const struct pipe_screen_config *);

/* Takes ownership of `dev`. */
struct radeon_winsys *rdn_winsys_create(struct rdn_device *dev,
                                        const struct pipe_screen_config *config,
                                        rdn_screen_create_t screen_create);

#endif /* RDN_WINSYS_H */
