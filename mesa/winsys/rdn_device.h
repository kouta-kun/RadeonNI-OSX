/*
 * The device as Mesa's winsys sees it.
 *
 * This is the boundary between Mesa and whatever drives the card: on Linux
 * the hardware library itself, in the same process; on Mac OS X the kext,
 * through a user client. Everything the GPU reads lives in video memory,
 * which the device maps for the CPU (the aperture) and addresses for the
 * GPU at info.vram_gpu_base + offset.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_DEVICE_H
#define RDN_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rdn_device_info {
   uint32_t pci_device_id;
   /* GPU address of aperture offset 0. */
   uint64_t vram_gpu_base;
   /* Bytes the allocator below manages. */
   uint64_t vram_size;
   /* From the 3D engine setup (evergreen_gpu_init). */
   uint32_t tile_config;
   uint32_t backend_map;
   uint32_t max_backends;
   uint32_t max_tile_pipes;
   uint32_t max_pipes;
   uint32_t num_ses;
};

struct rdn_device {
   struct rdn_device_info info;

   /* CPU mapping of video memory, from offset 0. */
   void *aperture;
   uint64_t aperture_size;

   /* Video memory. Offsets are byte offsets into the aperture. */
   int (*alloc)(struct rdn_device *dev, uint64_t size, uint64_t align,
                uint64_t *offset);
   void (*free)(struct rdn_device *dev, uint64_t offset);

   /*
    * Run `words` command words stored at `offset`. `swap` asks the command
    * processor to byte-swap each word (a big-endian host that stored them
    * in its own byte order). Everything written to video memory before the
    * call is visible to the GPU. Returns 0 and a fence number.
    */
   int (*submit)(struct rdn_device *dev, uint64_t offset, uint32_t words,
                 bool swap, uint32_t *fence);
   bool (*fence_done)(struct rdn_device *dev, uint32_t fence);
   /* 0 when reached. After it, the CPU sees what the GPU wrote. */
   int (*fence_wait)(struct rdn_device *dev, uint32_t fence,
                     uint64_t timeout_ns);
   /* Make the CPU see what the GPU wrote, without waiting for anything. */
   void (*sync_for_cpu)(struct rdn_device *dev);

   void (*destroy)(struct rdn_device *dev);
};

/* Provided by the platform (target/). NULL when there is no usable card. */
struct rdn_device *rdn_device_open(void);

#ifdef __cplusplus
}
#endif

#endif /* RDN_DEVICE_H */
