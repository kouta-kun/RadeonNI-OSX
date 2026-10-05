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

typedef struct pipe_screen *(*rdn_screen_create_t)(struct radeon_winsys *,
                                                   const struct pipe_screen_config *);

/* Takes ownership of `dev`. */
struct radeon_winsys *rdn_winsys_create(struct rdn_device *dev,
                                        const struct pipe_screen_config *config,
                                        rdn_screen_create_t screen_create);

#endif /* RDN_WINSYS_H */
