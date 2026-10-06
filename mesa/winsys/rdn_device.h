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
   /* Bytes alloc_hidden() manages; 0 if there is no such memory. */
   uint64_t hidden_size;
   /* GPU address of offset 0 of what gart_alloc() hands out. */
   uint64_t gart_gpu_base;
   /* From the 3D engine setup (evergreen_gpu_init). */
   uint32_t tile_config;
   uint32_t backend_map;
   uint32_t max_backends;
   uint32_t max_tile_pipes;
   uint32_t max_pipes;
   uint32_t num_ses;
};

/* Screen coordinates: x, y from the top left, width, height. */
#define RDN_REGION_RECTS 256

struct rdn_region {
   /* Rectangles in the shape; only the first RDN_REGION_RECTS are here. */
   uint32_t count;
   int32_t bounds[4];
   int16_t rects[RDN_REGION_RECTS][4];
};

struct rdn_surface {
   uint32_t id;
   uint32_t offset, row_bytes, width, height;
};

struct rdn_device {
   struct rdn_device_info info;

   /* CPU mapping of video memory, from offset 0. */
   void *aperture;
   uint64_t aperture_size;

   /*
    * The surface the display shows, if the device knows it: 32-bit pixels
    * in the CPU's byte order at this aperture offset. Width 0 if unknown.
    */
   struct {
      uint64_t offset;
      uint32_t width, height, pitch_pixels;
   } screen;

   /* Video memory. Offsets are byte offsets into the aperture. */
   int (*alloc)(struct rdn_device *dev, uint64_t size, uint64_t align,
                uint64_t *offset);
   void (*free)(struct rdn_device *dev, uint64_t offset);
   /*
    * Video memory the CPU cannot reach (beyond the aperture), for what only
    * the GPU reads and writes; freed with free(). NULL, or failing, when
    * the device has none.
    */
   int (*alloc_hidden)(struct rdn_device *dev, uint64_t size, uint64_t align,
                       uint64_t *offset);
   /*
    * Memory of the program itself that the GPU reaches through the GART:
    * ordinary cached memory for the CPU. The offset counts from
    * info.gart_gpu_base for the GPU; gart_cpu() gives the CPU's address
    * of the same byte. NULL when the device has no GART.
    */
   int (*gart_alloc)(struct rdn_device *dev, uint64_t size, uint64_t align,
                     uint64_t *offset);
   void (*gart_free)(struct rdn_device *dev, uint64_t offset);
   void *(*gart_cpu)(struct rdn_device *dev, uint64_t offset);

   /*
    * Run `words` command words stored at `offset`, in the CPU's byte order
    * (the device sets the command processor up for that). Everything
    * written to video memory before the call is visible to the GPU.
    * Returns 0 and a fence number.
    */
   int (*submit)(struct rdn_device *dev, uint64_t offset, uint32_t words,
                 uint32_t *fence);
   bool (*fence_done)(struct rdn_device *dev, uint32_t fence);
   /* 0 when reached. After it, the CPU sees what the GPU wrote. */
   int (*fence_wait)(struct rdn_device *dev, uint32_t fence,
                     uint64_t timeout_ns);
   /* Make the CPU see what the GPU wrote, without waiting for anything. */
   void (*sync_for_cpu)(struct rdn_device *dev);

   /*
    * Optional: the shape on the screen of the window system's surface
    * `id`. False if unknown.
    */
   bool (*surface_region)(struct rdn_device *dev, uint32_t id,
                          struct rdn_region *region);

   /*
    * Optional: tell the window system where surface `id` keeps its
    * picture: a linear buffer at this aperture offset. Width 0 takes it
    * away.
    */
   bool (*surface_buffer)(struct rdn_device *dev, uint32_t id, uint64_t offset,
                          uint32_t row_bytes, uint32_t width, uint32_t height);

   /*
    * Optional: the surfaces that have such a buffer, at most `max`;
    * returns how many.
    */
   uint32_t (*surface_list)(struct rdn_device *dev, struct rdn_surface *list,
                            uint32_t max);

   /* Optional: the surface that is locked for reading right now, or 0. */
   uint32_t (*surface_locked)(struct rdn_device *dev);

   void (*destroy)(struct rdn_device *dev);
};

/* Provided by the platform (target/). NULL when there is no usable card. */
struct rdn_device *rdn_device_open(void);

#ifdef __cplusplus
}
#endif

#endif /* RDN_DEVICE_H */
