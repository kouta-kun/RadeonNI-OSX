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
   /* Synchronized CP DMA burst limit for GART-to-GART copies; zero if unrestricted. */
   uint32_t cp_dma_gtt_copy_max_bytes;
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

   /* Imported resources retain locally owned drawable storage. */
   void *(*retain_external)(struct rdn_device *dev, uint64_t offset);
   void (*release_external)(struct rdn_device *dev, void *reference);

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
    * Optional: memory of the program's own, page aligned and a whole
    * number of pages, behind the GART as it is, so that the GPU reads what
    * the program writes there. The offset is like gart_alloc()'s. The
    * pages stay wired until gart_unbind_user(), which the caller calls
    * when the GPU is done with them; the program may have freed the
    * memory by then.
    */
   int (*gart_bind_user)(struct rdn_device *dev, void *address, uint64_t size,
                         uint64_t *offset);
   void (*gart_unbind_user)(struct rdn_device *dev, uint64_t offset, uint64_t size);

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
    * Optional: the video decoder (UVD); all three are NULL when the device
    * has none. uvd_init() starts it unless it is up; 0 when it is up
    * afterwards. uvd_submit() runs `words` UVD command words stored at `offset`,
    * in the CPU's byte order, like submit(), and returns a fence; the
    * buffers the commands name must lie in video memory, in the 256 MB
    * the decoder's firmware is in. Its fences are numbered apart from
    * submit()'s. uvd_fence_wait() is like fence_wait(); a timeout of 0
    * only looks.
    */
   int (*uvd_init)(struct rdn_device *dev);
   /* The firmware's epoch (it changes when the firmware is started again), and starting it again now; NULL without the kext's support. */
   int (*uvd_epoch)(struct rdn_device *dev, uint32_t *epoch);
   int (*uvd_reset)(struct rdn_device *dev, uint32_t *epoch);
   int (*uvd_submit)(struct rdn_device *dev, uint64_t offset, uint32_t words,
                     uint32_t *fence);
   int (*uvd_fence_wait)(struct rdn_device *dev, uint32_t fence,
                         uint64_t timeout_ns);

   /*
    * Optional: the decoder in a 256 MB segment of the video memory beyond the aperture (docs/UVD.md), where the CPU
    * cannot reach it. uvd_start() starts the decoder unless it is up, asking for the segment with `want` (1: all of
    * it in the segment; 2: "split": the firmware, ring, messages and feedback stay in the aperture and only the buffers
    * the decoder's other clients use go to the segment; 0: the aperture; a decoder that is up stays where it is), and
    * says where it runs: *segment_first and *segment_size, the bytes of the segment as offsets in video memory, or
    * both 0 for the aperture, and *split. In a segment (not split), everything the decoder touches must be in it: uvd_alloc() is alloc_hidden() inside it (freed with free()); vram_put() and vram_get()
    * copy bytes between the program's memory and video memory inside the segment, slowly; uvd_submit_words() runs
    * decoder command words given by value (instead of uvd_submit()'s offset in the aperture) and returns a fence.
    * All NULL when the device cannot.
    */
   int (*uvd_start)(struct rdn_device *dev, int want, uint64_t *segment_first,
                    uint64_t *segment_size, bool *split);
   int (*uvd_alloc)(struct rdn_device *dev, uint64_t size, uint64_t align,
                    uint64_t *offset);
   int (*vram_put)(struct rdn_device *dev, uint64_t offset, const void *src,
                   uint64_t bytes);
   int (*vram_get)(struct rdn_device *dev, uint64_t offset, void *dst,
                   uint64_t bytes);
   int (*uvd_submit_words)(struct rdn_device *dev, const uint32_t *words,
                           uint32_t count, uint32_t *fence);

   /*
    * Optional: read the screen's size, place and pitch again (a mode set
    * changes them; `screen` is kept as the device last read it).
    */
   void (*screen_refresh)(struct rdn_device *dev);

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
    * returns how many, or -1 if the query failed.
    */
   int (*surface_list)(struct rdn_device *dev, struct rdn_surface *list,
                       uint32_t max);

   /* Query success is separate from existence, including unbuffered reservations. */
   bool (*surface_query)(struct rdn_device *dev, uint32_t id,
                         struct rdn_surface *surface, bool *found);

   /* Optional: the surface that is locked for reading right now, or 0. */
   uint32_t (*surface_locked)(struct rdn_device *dev);

   bool (*surface_backing)(struct rdn_device *dev, uint32_t id,
                           void **address, uint32_t *stride, uint32_t *width, uint32_t *height);

   bool (*surface_gpu_info)(struct rdn_device *dev, uint32_t id, struct rdn_surface *surface);
   int (*surface_gpu_copy)(struct rdn_device *dev, uint32_t id, uint32_t offset,
                           uint32_t stride, uint32_t width, uint32_t height);
   void (*destroy)(struct rdn_device *dev);
};

/* Provided by the platform (target/). NULL when there is no usable card. */
struct rdn_device *rdn_device_open(void);

#ifdef __cplusplus
}
#endif

#endif /* RDN_DEVICE_H */
