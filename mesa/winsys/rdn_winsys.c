/*
 * Mesa winsys for the Radeon HD 7570 without the Linux DRM.
 *
 * It gives r600 what the radeon DRM winsys gives it, over rdn_device.h:
 *  - buffers are ranges of video memory, mapped through the aperture, each
 *    with a fixed GPU address. The driver is told it has GPU virtual
 *    addressing (r600_has_virtual_memory), so it writes those addresses
 *    into the command stream itself and nothing has to be patched;
 *  - a command stream is copied into video memory and run as one indirect
 *    buffer, synchronously on the calling thread;
 *  - a fence is the device's fence number.
 * Surface layouts come from Mesa's own radeon_drm_surface.c and
 * radeon_surface.c, built unchanged (see shim/).
 *
 * Not here: buffer sharing between processes, a second (DMA) ring, any
 * checking of the command stream.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "radeon_drm_winsys.h"
#include "rdn_winsys.h"
#include "radeon_surface.h"

#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "util/os_time.h"
#include "util/u_cpu_detect.h"
#include "util/u_math.h"
#include "util/u_memory.h"

uint32_t rdn_shim_device_id;
uint32_t rdn_shim_tiling_config;

/* Command words per stream; r600 flushes itself well below this. */
#define RDN_CS_MAX_DW (64 * 1024)

struct rdn_fence {
   struct pipe_reference reference;
   uint32_t number;
   bool submitted;
   bool reached;
};

struct rdn_bo {
   struct pb_buffer_lean base;
   uint64_t offset;
   enum radeon_bo_domain domain;
   enum radeon_bo_flag flags;
   /* The last submissions that use it at all, and that write to it. */
   struct rdn_fence *last_use;
   struct rdn_fence *last_write;
   /* Memory that is not from the allocator (the screen). */
   bool foreign;
};

struct rdn_cs_buffer {
   struct rdn_bo *bo;
   unsigned usage;
};

struct rdn_cs {
   struct radeon_drm_winsys *ws;
   struct rdn_cs_buffer *buffers;
   unsigned num_buffers, max_buffers;
   void (*flush_cs)(void *ctx, unsigned flags, struct pipe_fence_handle **fence);
   void *flush_data;
   struct rdn_fence *next_fence;
};

struct rdn_pending_ib {
   struct list_head list;
   uint64_t offset;
   struct rdn_fence *fence;
};

static inline struct radeon_drm_winsys *rdn_winsys(struct radeon_winsys *rws)
{
   return (struct radeon_drm_winsys *)rws;
}

static inline struct rdn_bo *rdn_bo(struct pb_buffer_lean *buf)
{
   return (struct rdn_bo *)buf;
}

/*
 * Fences
 */

static void rdn_fence_set(struct rdn_fence **dst, struct rdn_fence *src)
{
   struct rdn_fence *old = *dst;

   if (pipe_reference(old ? &old->reference : NULL, src ? &src->reference : NULL))
      FREE(old);
   *dst = src;
}

static struct rdn_fence *rdn_fence_new(void)
{
   struct rdn_fence *fence = CALLOC_STRUCT(rdn_fence);

   if (fence)
      pipe_reference_init(&fence->reference, 1);
   return fence;
}

static bool rdn_fence_wait(struct radeon_drm_winsys *ws, struct rdn_fence *fence,
                           uint64_t timeout_ns)
{
   if (!fence || fence->reached)
      return true;
   if (!fence->submitted)
      return false;
   if (timeout_ns == 0) {
      if (!ws->dev->fence_done(ws->dev, fence->number))
         return false;
   } else if (ws->dev->fence_wait(ws->dev, fence->number, timeout_ns)) {
      return false;
   }
   fence->reached = true;
   return true;
}

static bool rdn_ws_fence_wait(struct radeon_winsys *rws,
                              struct pipe_fence_handle *fence, uint64_t timeout)
{
   return rdn_fence_wait(rdn_winsys(rws), (struct rdn_fence *)fence, timeout);
}

static void rdn_ws_fence_reference(struct radeon_winsys *rws,
                                   struct pipe_fence_handle **dst,
                                   struct pipe_fence_handle *src)
{
   rdn_fence_set((struct rdn_fence **)dst, (struct rdn_fence *)src);
}

/*
 * Buffers
 */

static struct pb_buffer_lean *
rdn_buffer_create(struct radeon_winsys *rws, uint64_t size, unsigned alignment,
                  enum radeon_bo_domain domain, enum radeon_bo_flag flags)
{
   struct radeon_drm_winsys *ws = rdn_winsys(rws);
   struct rdn_bo *bo = CALLOC_STRUCT(rdn_bo);
   int r;

   if (!bo)
      return NULL;
   /* The GPU addresses surfaces in units of 256 bytes. */
   alignment = MAX2(alignment, 256);
   alignment = util_next_power_of_two(alignment);
   size = align64(MAX2(size, 1), 256);

   simple_mtx_lock(&ws->lock);
   r = ws->dev->alloc(ws->dev, size, alignment, &bo->offset);
   if (!r)
      ws->allocated_bytes += size;
   simple_mtx_unlock(&ws->lock);
   if (r) {
      fprintf(stderr, "rdn: out of video memory (%llu bytes asked, %llu in use)\n",
              (unsigned long long)size, (unsigned long long)ws->allocated_bytes);
      FREE(bo);
      return NULL;
   }

   pipe_reference_init(&bo->base.reference, 1);
   bo->base.alignment_log2 = util_logbase2(alignment);
   bo->base.usage = 0;
   bo->base.size = size;
   bo->domain = domain;
   bo->flags = flags;
   return &bo->base;
}

static void rdn_buffer_destroy(struct radeon_winsys *rws, struct pb_buffer_lean *buf)
{
   struct radeon_drm_winsys *ws = rdn_winsys(rws);
   struct rdn_bo *bo = rdn_bo(buf);

   /* The GPU may still be using the memory. */
   rdn_fence_wait(ws, bo->last_use, OS_TIMEOUT_INFINITE);
   rdn_fence_set(&bo->last_use, NULL);
   rdn_fence_set(&bo->last_write, NULL);

   if (!bo->foreign) {
      simple_mtx_lock(&ws->lock);
      ws->dev->free(ws->dev, bo->offset);
      ws->allocated_bytes -= bo->base.size;
      simple_mtx_unlock(&ws->lock);
   }
   FREE(bo);
}

static int rdn_cs_lookup(struct rdn_cs *cs, struct rdn_bo *bo)
{
   unsigned i;

   for (i = 0; i < cs->num_buffers; i++)
      if (cs->buffers[i].bo == bo)
         return i;
   return -1;
}

static bool rdn_cs_is_buffer_referenced(struct radeon_cmdbuf *rcs,
                                        struct pb_buffer_lean *buf, unsigned usage)
{
   struct rdn_cs *cs = (struct rdn_cs *)rcs->priv;
   int i;

   if (!cs)
      return false;
   i = rdn_cs_lookup(cs, rdn_bo(buf));
   if (i < 0)
      return false;
   return (cs->buffers[i].usage & usage) != 0;
}

static bool rdn_buffer_wait(struct radeon_winsys *rws, struct pb_buffer_lean *buf,
                            uint64_t timeout, unsigned usage)
{
   struct rdn_bo *bo = rdn_bo(buf);

   /* A reader only has to wait for writers. */
   return rdn_fence_wait(rdn_winsys(rws),
                         (usage & RADEON_USAGE_WRITE) ? bo->last_use : bo->last_write,
                         timeout);
}

static void *rdn_buffer_map(struct radeon_winsys *rws, struct pb_buffer_lean *buf,
                            struct radeon_cmdbuf *rcs, enum pipe_map_flags usage)
{
   struct radeon_drm_winsys *ws = rdn_winsys(rws);
   struct rdn_bo *bo = rdn_bo(buf);
   struct rdn_cs *cs = rcs ? (struct rdn_cs *)rcs->priv : NULL;

   if (!(usage & PIPE_MAP_UNSYNCHRONIZED)) {
      /* Reading needs pending writes done; writing needs every use done. */
      unsigned conflicts = (usage & PIPE_MAP_WRITE) ? RADEON_USAGE_READWRITE
                                                    : RADEON_USAGE_WRITE;
      unsigned wait_usage = (usage & PIPE_MAP_WRITE) ? RADEON_USAGE_READWRITE
                                                     : RADEON_USAGE_READ;

      if (usage & PIPE_MAP_DONTBLOCK) {
         if (cs && rdn_cs_is_buffer_referenced(rcs, buf, conflicts)) {
            cs->flush_cs(cs->flush_data, RADEON_FLUSH_ASYNC_START_NEXT_GFX_IB_NOW, NULL);
            return NULL;
         }
         if (!rdn_buffer_wait(rws, buf, 0, wait_usage))
            return NULL;
      } else {
         if (cs && rdn_cs_is_buffer_referenced(rcs, buf, conflicts))
            cs->flush_cs(cs->flush_data, RADEON_FLUSH_START_NEXT_GFX_IB_NOW, NULL);
         rdn_buffer_wait(rws, buf, OS_TIMEOUT_INFINITE, wait_usage);
      }
      ws->dev->sync_for_cpu(ws->dev);
   }
   return (uint8_t *)ws->dev->aperture + bo->offset;
}

static void rdn_buffer_unmap(struct radeon_winsys *rws, struct pb_buffer_lean *buf)
{
}

static void rdn_buffer_get_metadata(struct radeon_winsys *rws, struct pb_buffer_lean *buf,
                                    struct radeon_bo_metadata *md, struct radeon_surf *surf)
{
}

static void rdn_buffer_set_metadata(struct radeon_winsys *rws, struct pb_buffer_lean *buf,
                                    struct radeon_bo_metadata *md,
                                    const struct radeon_surf *surf)
{
}

static struct pb_buffer_lean *
rdn_buffer_from_handle(struct radeon_winsys *rws, struct winsys_handle *whandle,
                       unsigned vm_alignment, bool is_prime_linear_buffer)
{
   struct radeon_drm_winsys *ws = rdn_winsys(rws);
   struct rdn_bo *bo;

   bool screen = whandle->handle == RDN_WINSYS_HANDLE_SCREEN;

   /* The screen, or video memory the caller allocated itself. */
   if (whandle->type != WINSYS_HANDLE_TYPE_KMS ||
       (screen && !ws->dev->screen.width) ||
       (!screen && whandle->handle != RDN_WINSYS_HANDLE_VRAM))
      return NULL;
   bo = CALLOC_STRUCT(rdn_bo);
   if (!bo)
      return NULL;
   pipe_reference_init(&bo->base.reference, 1);
   bo->base.alignment_log2 = 8;
   bo->base.usage = 0;
   if (screen) {
      bo->base.size = (uint64_t)ws->dev->screen.pitch_pixels * 4 *
                      ws->dev->screen.height;
      bo->offset = ws->dev->screen.offset;
   } else {
      /* The surface's place in it is the handle's offset (r600 adds it). */
      bo->base.size = ws->dev->aperture_size;
      bo->offset = 0;
   }
   bo->domain = RADEON_DOMAIN_VRAM;
   bo->flags = 0;
   bo->foreign = true;
   return &bo->base;
}

static struct pb_buffer_lean *
rdn_buffer_from_ptr(struct radeon_winsys *rws, void *pointer, uint64_t size,
                    enum radeon_bo_flag flags)
{
   return NULL;
}

static bool rdn_buffer_false(struct pb_buffer_lean *buf)
{
   return false;
}

static bool rdn_buffer_get_handle(struct radeon_winsys *rws, struct pb_buffer_lean *buf,
                                  struct winsys_handle *whandle)
{
   return false;
}

static bool rdn_buffer_commit(struct radeon_winsys *rws, struct pb_buffer_lean *buf,
                              uint64_t offset, uint64_t size, bool commit)
{
   return false;
}

static uint64_t rdn_buffer_get_virtual_address(struct pb_buffer_lean *buf)
{
   /* Filled in at creation of the winsys: see rdn_winsys_create(). */
   extern uint64_t rdn_vram_gpu_base;

   return rdn_vram_gpu_base + rdn_bo(buf)->offset;
}

uint64_t rdn_vram_gpu_base;

static unsigned rdn_buffer_get_reloc_offset(struct pb_buffer_lean *buf)
{
   return 0;
}

static enum radeon_bo_domain rdn_buffer_get_initial_domain(struct pb_buffer_lean *buf)
{
   return rdn_bo(buf)->domain;
}

static enum radeon_bo_flag rdn_buffer_get_flags(struct pb_buffer_lean *buf)
{
   return rdn_bo(buf)->flags;
}

/*
 * Command streams
 */

static struct radeon_winsys_ctx *rdn_ctx_create(struct radeon_winsys *rws, unsigned flags)
{
   /* Nothing per context; r600 only wants something that is not NULL. */
   return (struct radeon_winsys_ctx *)rws;
}

static void rdn_ctx_destroy(struct radeon_winsys_ctx *ctx)
{
}

static enum pipe_reset_status
rdn_ctx_query_reset_status(struct radeon_winsys_ctx *ctx, bool full_reset_only,
                           bool *needs_reset, bool *reset_completed)
{
   if (needs_reset)
      *needs_reset = false;
   if (reset_completed)
      *reset_completed = false;
   return PIPE_NO_RESET;
}

static bool rdn_cs_create(struct radeon_cmdbuf *rcs, struct radeon_winsys_ctx *ctx,
                          enum amd_ip_type ip_type,
                          void (*flush)(void *ctx, unsigned flags,
                                        struct pipe_fence_handle **fence),
                          void *flush_ctx)
{
   struct rdn_cs *cs;

   /* One ring: the 3D command processor. */
   if (ip_type != AMD_IP_GFX)
      return false;
   cs = CALLOC_STRUCT(rdn_cs);
   if (!cs)
      return false;
   memset(rcs, 0, sizeof(*rcs));
   rcs->current.buf = MALLOC(RDN_CS_MAX_DW * 4);
   if (!rcs->current.buf) {
      FREE(cs);
      return false;
   }
   rcs->current.max_dw = RDN_CS_MAX_DW;
   cs->ws = (struct radeon_drm_winsys *)ctx;
   cs->flush_cs = flush;
   cs->flush_data = flush_ctx;
   rcs->priv = cs;
   return true;
}

static void rdn_cs_release_buffers(struct rdn_cs *cs)
{
   unsigned i;

   for (i = 0; i < cs->num_buffers; i++)
      radeon_bo_reference(&cs->ws->base, (struct pb_buffer_lean **)&cs->buffers[i].bo, NULL);
   cs->num_buffers = 0;
}

static void rdn_cs_destroy(struct radeon_cmdbuf *rcs)
{
   struct rdn_cs *cs = (struct rdn_cs *)rcs->priv;

   if (!cs)
      return;
   rdn_cs_release_buffers(cs);
   rdn_fence_set(&cs->next_fence, NULL);
   FREE(cs->buffers);
   FREE(rcs->current.buf);
   FREE(cs);
   rcs->priv = NULL;
}

static unsigned rdn_cs_add_buffer(struct radeon_cmdbuf *rcs, struct pb_buffer_lean *buf,
                                  unsigned usage, enum radeon_bo_domain domain)
{
   struct rdn_cs *cs = (struct rdn_cs *)rcs->priv;
   int i = rdn_cs_lookup(cs, rdn_bo(buf));

   if (i >= 0) {
      cs->buffers[i].usage |= usage;
      return i;
   }
   if (cs->num_buffers == cs->max_buffers) {
      cs->max_buffers = cs->max_buffers ? cs->max_buffers * 2 : 64;
      cs->buffers = realloc(cs->buffers, cs->max_buffers * sizeof(*cs->buffers));
   }
   i = cs->num_buffers++;
   cs->buffers[i].bo = NULL;
   radeon_bo_reference(&cs->ws->base, (struct pb_buffer_lean **)&cs->buffers[i].bo, buf);
   cs->buffers[i].usage = usage;
   return i;
}

static int rdn_cs_lookup_buffer(struct radeon_cmdbuf *rcs, struct pb_buffer_lean *buf)
{
   return rdn_cs_lookup((struct rdn_cs *)rcs->priv, rdn_bo(buf));
}

static bool rdn_cs_validate(struct radeon_cmdbuf *rcs)
{
   return true;
}

static bool rdn_cs_check_space(struct radeon_cmdbuf *rcs, unsigned dw)
{
   return rcs->current.max_dw - rcs->current.cdw >= dw;
}

static unsigned rdn_cs_get_buffer_list(struct radeon_cmdbuf *rcs,
                                       struct radeon_bo_list_item *list)
{
   struct rdn_cs *cs = (struct rdn_cs *)rcs->priv;
   unsigned i;

   if (list)
      for (i = 0; i < cs->num_buffers; i++) {
         list[i].bo_size = cs->buffers[i].bo->base.size;
         list[i].vm_address = rdn_buffer_get_virtual_address(&cs->buffers[i].bo->base);
         list[i].priority_usage = cs->buffers[i].usage;
      }
   return cs->num_buffers;
}

/* Give back the command buffers the GPU has finished with. */
static void rdn_reap_ibs(struct radeon_drm_winsys *ws, bool wait)
{
   list_for_each_entry_safe(struct rdn_pending_ib, ib, &ws->pending_ibs, list) {
      if (!rdn_fence_wait(ws, ib->fence, wait ? OS_TIMEOUT_INFINITE : 0))
         break;
      ws->dev->free(ws->dev, ib->offset);
      rdn_fence_set(&ib->fence, NULL);
      list_del(&ib->list);
      FREE(ib);
   }
}

static int rdn_cs_flush(struct radeon_cmdbuf *rcs, unsigned flags,
                        struct pipe_fence_handle **pfence)
{
   struct rdn_cs *cs = (struct rdn_cs *)rcs->priv;
   struct radeon_drm_winsys *ws = cs->ws;
   struct rdn_pending_ib *ib;
   struct rdn_fence *fence;
   uint64_t offset;
   unsigned i;
   int r;

   /* Pad to 8 words, as the command processor fetches (radeon_drm_cs.c). */
   while (rcs->current.cdw & 7)
      rcs->current.buf[rcs->current.cdw++] = 0x80000000; /* type2 nop packet */

   fence = cs->next_fence;
   cs->next_fence = NULL;
   if (!fence)
      fence = rdn_fence_new();
   ib = CALLOC_STRUCT(rdn_pending_ib);
   if (!fence || !ib) {
      FREE(ib);
      return -ENOMEM;
   }

   simple_mtx_lock(&ws->lock);
   rdn_reap_ibs(ws, false);
   r = ws->dev->alloc(ws->dev, rcs->current.cdw * 4, 4096, &offset);
   if (r) {
      /* Wait for the earlier ones to finish and try once more. */
      rdn_reap_ibs(ws, true);
      r = ws->dev->alloc(ws->dev, rcs->current.cdw * 4, 4096, &offset);
   }
   if (!r) {
      /* The words are in the CPU's byte order, which the device expects. */
      memcpy((uint8_t *)ws->dev->aperture + offset, rcs->current.buf,
             rcs->current.cdw * 4);
      r = ws->dev->submit(ws->dev, offset, rcs->current.cdw, &fence->number);
      if (r)
         ws->dev->free(ws->dev, offset);
   }
   if (r) {
      simple_mtx_unlock(&ws->lock);
      fprintf(stderr, "rdn: command submission failed (%d)\n", r);
      FREE(ib);
      /* Nothing ran: do not leave anyone waiting on this fence. */
      fence->reached = true;
   } else {
      fence->submitted = true;
      ib->offset = offset;
      rdn_fence_set(&ib->fence, fence);
      list_addtail(&ib->list, &ws->pending_ibs);
      ws->num_flushes++;
      simple_mtx_unlock(&ws->lock);

      for (i = 0; i < cs->num_buffers; i++) {
         rdn_fence_set(&cs->buffers[i].bo->last_use, fence);
         if (cs->buffers[i].usage & RADEON_USAGE_WRITE)
            rdn_fence_set(&cs->buffers[i].bo->last_write, fence);
      }
   }

   rdn_cs_release_buffers(cs);
   rcs->current.cdw = 0;
   rcs->used_vram_kb = 0;
   rcs->used_gart_kb = 0;

   if (pfence)
      rdn_fence_set((struct rdn_fence **)pfence, fence);
   rdn_fence_set(&fence, NULL);
   return r;
}

static struct pipe_fence_handle *rdn_cs_get_next_fence(struct radeon_cmdbuf *rcs)
{
   struct rdn_cs *cs = (struct rdn_cs *)rcs->priv;
   struct rdn_fence *fence = NULL;

   if (!cs->next_fence)
      cs->next_fence = rdn_fence_new();
   rdn_fence_set(&fence, cs->next_fence);
   return (struct pipe_fence_handle *)fence;
}

static bool rdn_cs_request_feature(struct radeon_cmdbuf *rcs, enum radeon_feature_id fid,
                                   bool enable)
{
   return false;
}

static void rdn_cs_sync_flush(struct radeon_cmdbuf *rcs)
{
   /* Flushes are synchronous. */
}

/*
 * The winsys
 */

static void rdn_query_info(struct radeon_winsys *rws, struct radeon_info *info)
{
   *info = rdn_winsys(rws)->info;
}

static uint64_t rdn_query_value(struct radeon_winsys *rws, enum radeon_value_id value)
{
   struct radeon_drm_winsys *ws = rdn_winsys(rws);

   switch (value) {
   case RADEON_VRAM_USAGE:
   case RADEON_REQUESTED_VRAM_MEMORY:
   case RADEON_MAPPED_VRAM:
      return ws->allocated_bytes;
   case RADEON_NUM_GFX_IBS:
      return ws->num_flushes;
   case RADEON_TIMESTAMP:
      return os_time_get_nano();
   default:
      return 0;
   }
}

static bool rdn_read_registers(struct radeon_winsys *rws, unsigned reg_offset,
                               unsigned num_registers, uint32_t *out)
{
   return false;
}

static int rdn_get_fd(struct radeon_winsys *rws)
{
   return -1;
}

static bool rdn_winsys_unref(struct radeon_winsys *rws)
{
   return pipe_reference(&rdn_winsys(rws)->reference, NULL);
}

static void rdn_winsys_destroy(struct radeon_winsys *rws)
{
   struct radeon_drm_winsys *ws = rdn_winsys(rws);

   rdn_reap_ibs(ws, true);
   if (ws->surf_man)
      radeon_surface_manager_free(ws->surf_man);
   ws->dev->destroy(ws->dev);
   simple_mtx_destroy(&ws->lock);
   FREE(ws);
}

/* What do_winsys_init() in radeon_drm_winsys.c derives for an Evergreen card. */
static void rdn_init_info(struct radeon_drm_winsys *ws)
{
   const struct rdn_device_info *dev = &ws->dev->info;
   struct radeon_info *info = &ws->info;
   uint32_t vram_kb = (uint32_t)(dev->vram_size / 1024);

   /* The last radeon DRM: every feature r600 keys on a version is there. */
   info->drm_major = 2;
   info->drm_minor = 51;
   info->drm_patchlevel = 0;
   info->is_amdgpu = false;

   info->pci_id = dev->pci_device_id;
   info->family = CHIP_TURKS;
   info->gfx_level = EVERGREEN;
   ws->gen = DRV_R600;
   info->has_dedicated_vram = true;

   info->ip[AMD_IP_GFX].num_queues = 1;
   info->ip[AMD_IP_GFX].ver_major = 5;
   info->ip[AMD_IP_GFX].ib_alignment = 4096;
   info->ip[AMD_IP_SDMA].num_queues = 0;

   info->has_userptr = false;
   info->gart_size_kb = 0;
   info->vram_size_kb = vram_kb;
   info->vram_vis_size_kb = vram_kb;
   info->max_heap_size_kb = vram_kb;
   info->gart_page_size = 4096;
   info->min_alloc_size = 4096;
   info->pte_fragment_size = 64 * 1024;
   info->max_alignment = 1024 * 1024;

   info->max_render_backends = dev->max_backends;
   info->clock_crystal_freq = 27000;
   info->r600_num_banks = 4 << ((dev->tile_config & 0xf0) >> 4);
   info->r600_pipe_interleave_bytes = 256 << ((dev->tile_config & 0xf00) >> 8);
   if (!info->r600_pipe_interleave_bytes)
      info->r600_pipe_interleave_bytes = 512;
   info->num_tile_pipes = dev->max_tile_pipes;
   info->r600_gb_backend_map = dev->backend_map;
   info->r600_gb_backend_map_valid = true;
   info->enabled_rb_mask = BITFIELD_MASK(info->max_render_backends);
   info->num_rb = util_bitcount64(info->enabled_rb_mask);

   /* Every buffer has a fixed GPU address: see the top of this file. */
   info->r600_has_virtual_memory = true;

   info->r600_max_quad_pipes = dev->max_pipes;
   info->num_cu = 1;
   info->max_se = dev->num_ses;
   info->num_se = dev->num_ses;
   info->max_sa_per_se = 1;

   info->gfx_ib_pad_with_type2 = true;
   info->has_cp_dma = true;
   info->tcc_cache_line_size = 64;
   info->has_graphics = true;
   info->has_image_opcodes = true;
}

struct radeon_winsys *rdn_winsys_create(struct rdn_device *dev,
                                        const struct pipe_screen_config *config,
                                        rdn_screen_create_t screen_create)
{
   struct radeon_drm_winsys *ws = CALLOC_STRUCT(radeon_drm_winsys);

   if (!ws) {
      dev->destroy(dev);
      return NULL;
   }
   ws->dev = dev;
   pipe_reference_init(&ws->reference, 1);
   simple_mtx_init(&ws->lock, mtx_plain);
   list_inithead(&ws->pending_ibs);
   rdn_vram_gpu_base = dev->info.vram_gpu_base;
   rdn_init_info(ws);

   rdn_shim_device_id = dev->info.pci_device_id;
   rdn_shim_tiling_config = dev->info.tile_config;
   ws->surf_man = radeon_surface_manager_new(-1);
   if (!ws->surf_man) {
      fprintf(stderr, "rdn: no surface layout for device %04x\n",
              (unsigned)dev->info.pci_device_id);
      goto fail;
   }

   ws->base.unref = rdn_winsys_unref;
   ws->base.destroy = rdn_winsys_destroy;
   ws->base.get_fd = rdn_get_fd;
   ws->base.query_info = rdn_query_info;
   ws->base.query_value = rdn_query_value;
   ws->base.read_registers = rdn_read_registers;

   ws->base.buffer_create = rdn_buffer_create;
   ws->base.buffer_destroy = rdn_buffer_destroy;
   ws->base.buffer_map = rdn_buffer_map;
   ws->base.buffer_unmap = rdn_buffer_unmap;
   ws->base.buffer_wait = rdn_buffer_wait;
   ws->base.buffer_get_metadata = rdn_buffer_get_metadata;
   ws->base.buffer_set_metadata = rdn_buffer_set_metadata;
   ws->base.buffer_from_handle = rdn_buffer_from_handle;
   ws->base.buffer_from_ptr = rdn_buffer_from_ptr;
   ws->base.buffer_is_user_ptr = rdn_buffer_false;
   ws->base.buffer_is_suballocated = rdn_buffer_false;
   ws->base.buffer_get_handle = rdn_buffer_get_handle;
   ws->base.buffer_commit = rdn_buffer_commit;
   ws->base.buffer_get_virtual_address = rdn_buffer_get_virtual_address;
   ws->base.buffer_get_reloc_offset = rdn_buffer_get_reloc_offset;
   ws->base.buffer_get_initial_domain = rdn_buffer_get_initial_domain;
   ws->base.buffer_get_flags = rdn_buffer_get_flags;

   ws->base.ctx_create = rdn_ctx_create;
   ws->base.ctx_destroy = rdn_ctx_destroy;
   ws->base.ctx_query_reset_status = rdn_ctx_query_reset_status;
   ws->base.cs_create = rdn_cs_create;
   ws->base.cs_destroy = rdn_cs_destroy;
   ws->base.cs_add_buffer = rdn_cs_add_buffer;
   ws->base.cs_lookup_buffer = rdn_cs_lookup_buffer;
   ws->base.cs_validate = rdn_cs_validate;
   ws->base.cs_check_space = rdn_cs_check_space;
   ws->base.cs_get_buffer_list = rdn_cs_get_buffer_list;
   ws->base.cs_flush = rdn_cs_flush;
   ws->base.cs_get_next_fence = rdn_cs_get_next_fence;
   ws->base.cs_is_buffer_referenced = rdn_cs_is_buffer_referenced;
   ws->base.cs_request_feature = rdn_cs_request_feature;
   ws->base.cs_sync_flush = rdn_cs_sync_flush;
   ws->base.fence_wait = rdn_ws_fence_wait;
   ws->base.fence_reference = rdn_ws_fence_reference;

   /* surface_init, from Mesa's radeon_drm_surface.c. */
   radeon_surface_init_functions(ws);

   ws->base.screen = screen_create(&ws->base, config);
   if (!ws->base.screen)
      goto fail;
   return &ws->base;

fail:
   if (ws->surf_man)
      radeon_surface_manager_free(ws->surf_man);
   dev->destroy(dev);
   simple_mtx_destroy(&ws->lock);
   FREE(ws);
   return NULL;
}
