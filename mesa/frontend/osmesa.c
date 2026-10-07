/*
 * osx-gpu: this is src/gallium/frontends/osmesa/osmesa.c from Mesa 25.0.7,
 * the last release that had the OSMesa frontend, adapted to build against
 * current Mesa. Changes are limited to what the newer interfaces require
 * (include paths, the removed post-processing module).
 */

/*
 * Copyright (c) 2013  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*
 * Off-Screen rendering into client memory.
 * OpenGL gallium frontend for softpipe and llvmpipe.
 *
 * Notes:
 *
 * If Gallium is built with LLVM support we use the llvmpipe driver.
 * Otherwise we use softpipe.  The GALLIUM_DRIVER environment variable
 * may be set to "softpipe" or "llvmpipe" to override.
 *
 * With softpipe we could render directly into the user's buffer by using a
 * display target resource.  However, softpipe doesn't support "upside-down"
 * rendering which would be needed for the OSMESA_Y_UP=TRUE case.
 *
 * With llvmpipe we could only render directly into the user's buffer when its
 * width and height is a multiple of the tile size (64 pixels).
 *
 * Because of these constraints we always render into ordinary resources then
 * copy the results to the user's buffer in the flush_front() function which
 * is called when the app calls glFlush/Finish.
 *
 * In general, the OSMesa interface is pretty ugly and not a good match
 * for Gallium.  But we're interested in doing the best we can to preserve
 * application portability.  With a little work we could come up with a
 * much nicer, new off-screen Gallium interface...
 */

/**
 * The following block is for avoid windows.h to be included
 * and osmesa require APIENTRY to be defined
 */
#include "util/glheader.h"
#ifndef APIENTRY
#define APIENTRY GLAPIENTRY
#endif
#include "GL/osmesa.h"

#include <stdio.h>
#include <unistd.h>
#include <c11/threads.h>

#include "state_tracker/st_context.h"
#include "main/extensions.h"
#include "main/glthread.h"

#include "glapi/glapi/glapi.h"  /* for OSMesaGetProcAddress below */

#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"

#include "util/u_atomic.h"
#include "util/box.h"
#include "util/u_debug.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_process.h"


#include "frontend/api.h"
#include "frontend/winsys_handle.h"



extern struct pipe_screen *
osmesa_create_screen(void);



struct osmesa_buffer
{
   struct pipe_frontend_drawable base;
   struct st_visual visual;
   unsigned width, height;

   struct pipe_resource *textures[ST_ATTACHMENT_COUNT];

   void *map;

   /*
    * OSMesaMakeCurrentDirect: what is drawn is shown on this surface of
    * the device, imported with resource_from_handle. The context draws on
    * a texture of its own and the GPU copies from it when the context is
    * flushed, so that nothing half drawn is ever shown.
    */
   bool direct;
   unsigned direct_handle, direct_stride, direct_offset;
   struct pipe_resource *direct_res;
   /* That surface's size, and where on it this drawable is shown. */
   unsigned target_width, target_height;
   int target_x, target_y;
   /*
    * OSMesaSurfaceStorage: every finished picture of the drawable is
    * also copied into memory the caller named (imported like the target),
    * where others can read it. Handle 0: nowhere.
    */
   unsigned own_handle, own_stride, own_offset;
   struct pipe_resource *store_res;

   struct osmesa_buffer *next;  /**< next in linked list */
};


struct osmesa_context
{
   struct st_context *st;

   bool ever_used;     /*< Has this context ever been current? */

   struct osmesa_buffer *current_buffer;

   /* Storage for depth/stencil, if the user has requested access.  The backing
    * driver always has its own storage for the actual depth/stencil, which we
    * have to transfer in and out.
    */
   void *zs;
   unsigned zs_stride;

   enum pipe_format depth_stencil_format, accum_format;

   GLenum format;         /*< User-specified context format */
   GLenum type;           /*< Buffer's data type */
   GLint user_row_length; /*< user-specified number of pixels per row */
   GLboolean y_up;        /*< TRUE  -> Y increases upward */
                          /*< FALSE -> Y increases downward */
   /* OSMesaSurfaceStorage, for the next OSMesaMakeCurrentSurface. */
   unsigned own_handle, own_stride, own_offset;
   /* OSMesaReadbackRects: the only parts of the color buffer to copy out. */
   GLint num_rects;
   GLint *rects;

};

/*
 * glthread calls this in its own thread before anything else. A loader
 * with per-thread state would tell it about the thread here; this
 * frontend finds its context through Mesa's (st->frontend_context), so
 * there is nothing to do, but the call must have somewhere to go.
 */
static void
osmesa_set_background_context(struct st_context *st,
                              struct util_queue_monitoring *queue_info)
{
   (void)st;
   (void)queue_info;
}

/**
 * Called from the ST manager.
 */
static int
osmesa_st_get_param(struct pipe_frontend_screen *fscreen, enum st_manager_param param)
{
   /* no-op */
   return 0;
}

static struct pipe_frontend_screen *global_fscreen = NULL;

static void
destroy_st_manager(void)
{
   if (global_fscreen) {
      if (global_fscreen->screen)
         global_fscreen->screen->destroy(global_fscreen->screen);
      FREE(global_fscreen);
   }
}

static void
create_st_manager(void)
{
   if (atexit(destroy_st_manager) != 0)
      return;

   global_fscreen = CALLOC_STRUCT(pipe_frontend_screen);
   if (global_fscreen) {
      global_fscreen->screen = osmesa_create_screen();
      global_fscreen->get_param = osmesa_st_get_param;
      global_fscreen->get_egl_image = NULL;
      global_fscreen->set_background_context = osmesa_set_background_context;
   }
}

/**
 * Create/return a singleton st_manager object.
 */
static struct pipe_frontend_screen *
get_st_manager(void)
{
   static once_flag create_once_flag = ONCE_FLAG_INIT;

   call_once(&create_once_flag, create_st_manager);

   return global_fscreen;
}

/* Reads the color or depth buffer from the backing context to either the user storage
 * (color buffer) or our temporary (z/s)
 */
static void
osmesa_read_buffer(OSMesaContext osmesa, struct pipe_resource *res, void *dst,
                   int dst_stride, bool y_up, bool use_rects)
{
   struct pipe_context *pipe = osmesa->st->pipe;

   struct pipe_box box;
   u_box_2d(0, 0, res->width0, res->height0, &box);

   struct pipe_transfer *transfer = NULL;
   uint8_t *src = pipe->texture_map(pipe, res, 0, PIPE_MAP_READ, &box,
                                   &transfer);

   /*
    * Copy the color buffer from the resource to the user's buffer.
    */

   if (y_up) {
      /* need to flip image upside down */
      dst = (uint8_t *)dst + (res->height0 - 1) * dst_stride;
      dst_stride = -dst_stride;
   }

   unsigned bpp = util_format_get_blocksize(res->format);

   if (use_rects && osmesa->num_rects > 0) {
      /* The rectangles' rows count from the first row of the user's buffer. */
      for (int i = 0; i < osmesa->num_rects; i++) {
         const GLint *r = osmesa->rects + i * 4;
         int x0 = MAX2(r[0], 0), y0 = MAX2(r[1], 0);
         int x1 = MIN2(r[0] + r[2], (int)res->width0);
         int y1 = MIN2(r[1] + r[3], (int)res->height0);

         if (x1 <= x0)
            continue;
         for (int y = y0; y < y1; y++) {
            int src_y = y_up ? (int)res->height0 - 1 - y : y;

            /* dst and dst_stride already follow the rows of the source. */
            memcpy((uint8_t *)dst + src_y * dst_stride + (int)(x0 * bpp),
                   src + src_y * transfer->stride + x0 * bpp,
                   (x1 - x0) * bpp);
         }
      }
      pipe->texture_unmap(pipe, transfer);
      return;
   }

   for (unsigned y = 0; y < res->height0; y++)
   {
      memcpy(dst, src, bpp * res->width0);
      dst = (uint8_t *)dst + dst_stride;
      src += transfer->stride;
   }

   pipe->texture_unmap(pipe, transfer);
}


/*
 * Copy one rectangle of a direct drawable (its own coordinates, y from the
 * top) to its place on the target surface, as far as both hold it.
 */
/*
 * OSMesaSetSamples: how many samples per pixel the contexts and buffers
 * made from now on have (1: no multisampling). A multisampled buffer is
 * only ever a source here: what the device shows, and what a surface's
 * owner reads, is the resolved picture.
 */
static unsigned osmesa_samples = 1;

/* Samples the visual of a new context or buffer gets: what the GPU can do. */
static unsigned
osmesa_usable_samples(enum pipe_format color_format, enum pipe_format ds_format)
{
   struct pipe_screen *screen = get_st_manager()->screen;
   unsigned n = osmesa_samples;

   for (; n > 1; n /= 2) {
      if (screen->is_format_supported(screen, color_format, PIPE_TEXTURE_2D,
                                      n, n, PIPE_BIND_RENDER_TARGET) &&
          (ds_format == PIPE_FORMAT_NONE ||
           screen->is_format_supported(screen, ds_format, PIPE_TEXTURE_2D,
                                       n, n, PIPE_BIND_DEPTH_STENCIL)))
         break;
   }
   return MAX2(n, 1);
}

/*
 * Copy a rectangle between two of our surfaces. With multisampling on
 * either side it is a blit, which resolves the samples on the way.
 */
static void
osmesa_copy(struct pipe_context *pipe, struct pipe_resource *dst,
            int dx, int dy, struct pipe_resource *src,
            const struct pipe_box *box)
{
   struct pipe_blit_info info;

   if (src->nr_samples <= 1 && dst->nr_samples <= 1) {
      pipe->resource_copy_region(pipe, dst, 0, dx, dy, 0, src, 0, box);
      return;
   }
   memset(&info, 0, sizeof(info));
   info.src.resource = src;
   info.src.format = src->format;
   info.src.box = *box;
   info.dst.resource = dst;
   info.dst.format = dst->format;
   info.dst.box = *box;
   info.dst.box.x = dx;
   info.dst.box.y = dy;
   info.mask = PIPE_MASK_RGBA;
   info.filter = PIPE_TEX_FILTER_NEAREST;
   pipe->blit(pipe, &info);
}

static void
osmesa_show_rect(struct pipe_context *pipe, struct osmesa_buffer *osbuffer,
                 struct pipe_resource *res, int x, int y, int w, int h)
{
   int x0 = MAX2(x, 0), y0 = MAX2(y, 0);
   int x1 = MIN2(x + w, (int)osbuffer->width);
   int y1 = MIN2(y + h, (int)osbuffer->height);
   struct pipe_box box;

   /* The part that falls on the target. */
   x0 = MAX2(x0, -osbuffer->target_x);
   y0 = MAX2(y0, -osbuffer->target_y);
   x1 = MIN2(x1, (int)osbuffer->target_width - osbuffer->target_x);
   y1 = MIN2(y1, (int)osbuffer->target_height - osbuffer->target_y);
   if (x1 <= x0 || y1 <= y0)
      return;
   u_box_2d(x0, y0, x1 - x0, y1 - y0, &box);
   osmesa_copy(pipe, osbuffer->direct_res, osbuffer->target_x + x0,
               osbuffer->target_y + y0, res, &box);
}


/**
 * Given an OSMESA_x format and a GL_y type, return the best
 * matching PIPE_FORMAT_z.
 * Note that we can't exactly match all user format/type combinations
 * with gallium formats.  If we find this to be a problem, we can
 * implement more elaborate format/type conversion in the flush_front()
 * function.
 */
static enum pipe_format
osmesa_choose_format(GLenum format, GLenum type)
{
   switch (format) {
   case OSMESA_RGBA:
      if (type == GL_UNSIGNED_BYTE) {
#if UTIL_ARCH_LITTLE_ENDIAN
         return PIPE_FORMAT_R8G8B8A8_UNORM;
#else
         return PIPE_FORMAT_A8B8G8R8_UNORM;
#endif
      }
      else if (type == GL_UNSIGNED_SHORT) {
         return PIPE_FORMAT_R16G16B16A16_UNORM;
      }
      else if (type == GL_FLOAT) {
         return PIPE_FORMAT_R32G32B32A32_FLOAT;
      }
      else {
         return PIPE_FORMAT_NONE;
      }
      break;
   case OSMESA_BGRA:
      if (type == GL_UNSIGNED_BYTE) {
#if UTIL_ARCH_LITTLE_ENDIAN
         return PIPE_FORMAT_B8G8R8A8_UNORM;
#else
         return PIPE_FORMAT_A8R8G8B8_UNORM;
#endif
      }
      else if (type == GL_UNSIGNED_SHORT) {
         return PIPE_FORMAT_R16G16B16A16_UNORM;
      }
      else if (type == GL_FLOAT) {
         return PIPE_FORMAT_R32G32B32A32_FLOAT;
      }
      else {
         return PIPE_FORMAT_NONE;
      }
      break;
   case OSMESA_ARGB:
      if (type == GL_UNSIGNED_BYTE) {
#if UTIL_ARCH_LITTLE_ENDIAN
         return PIPE_FORMAT_A8R8G8B8_UNORM;
#else
         return PIPE_FORMAT_B8G8R8A8_UNORM;
#endif
      }
      else if (type == GL_UNSIGNED_SHORT) {
         return PIPE_FORMAT_R16G16B16A16_UNORM;
      }
      else if (type == GL_FLOAT) {
         return PIPE_FORMAT_R32G32B32A32_FLOAT;
      }
      else {
         return PIPE_FORMAT_NONE;
      }
      break;
   case OSMESA_RGB:
      if (type == GL_UNSIGNED_BYTE) {
         return PIPE_FORMAT_R8G8B8_UNORM;
      }
      else if (type == GL_UNSIGNED_SHORT) {
         return PIPE_FORMAT_R16G16B16_UNORM;
      }
      else if (type == GL_FLOAT) {
         return PIPE_FORMAT_R32G32B32_FLOAT;
      }
      else {
         return PIPE_FORMAT_NONE;
      }
      break;
   case OSMESA_BGR:
      /* No gallium format for this one */
      return PIPE_FORMAT_NONE;
   case OSMESA_RGB_565:
      if (type != GL_UNSIGNED_SHORT_5_6_5)
         return PIPE_FORMAT_NONE;
      return PIPE_FORMAT_B5G6R5_UNORM;
   default:
      return PIPE_FORMAT_NONE;
   }
}


/**
 * Initialize an st_visual object.
 */
static void
osmesa_init_st_visual(struct st_visual *vis,
                      enum pipe_format color_format,
                      enum pipe_format ds_format,
                      enum pipe_format accum_format)
{
   vis->buffer_mask = ST_ATTACHMENT_FRONT_LEFT_MASK;

   if (ds_format != PIPE_FORMAT_NONE)
      vis->buffer_mask |= ST_ATTACHMENT_DEPTH_STENCIL_MASK;
   if (accum_format != PIPE_FORMAT_NONE)
      vis->buffer_mask |= ST_ATTACHMENT_ACCUM;

   vis->color_format = color_format;
   vis->depth_stencil_format = ds_format;
   vis->accum_format = accum_format;
   vis->samples = osmesa_usable_samples(color_format, ds_format);
}


/**
 * Return the osmesa_buffer that corresponds to an pipe_frontend_drawable.
 */
static inline struct osmesa_buffer *
drawable_to_osbuffer(struct pipe_frontend_drawable *drawable)
{
   return (struct osmesa_buffer *)drawable;
}


/**
 * Called via glFlush/glFinish.  This is where we copy the contents
 * of the driver's color buffer into the user-specified buffer.
 */
static bool
osmesa_st_framebuffer_flush_front(struct st_context *st,
                                  struct pipe_frontend_drawable *drawable,
                                  enum st_attachment_type statt)
{
   /* Not this thread's current context: with glthread this is another thread. */
   OSMesaContext osmesa = st ? (OSMesaContext)st->frontend_context : OSMesaGetCurrentContext();
   struct osmesa_buffer *osbuffer = drawable_to_osbuffer(drawable);
   struct pipe_resource *res = osbuffer->textures[statt];
   unsigned bpp;
   int dst_stride;

   if (statt != ST_ATTACHMENT_FRONT_LEFT)
      return false;

   if (osbuffer->direct) {
      struct pipe_context *pipe = osmesa->st->pipe;
      struct pipe_box box;

      if (getenv("RDN_DEBUG_DIRECT"))
         fprintf(stderr, "flush_front: direct_res %p, %dx%d at %d,%d of %ux%u, %d rects (%d %d %d %d)\n",
                 (void *)osbuffer->direct_res, osbuffer->width, osbuffer->height,
                 osbuffer->target_x, osbuffer->target_y, osbuffer->target_width,
                 osbuffer->target_height, osmesa->num_rects,
                 osmesa->num_rects ? osmesa->rects[0] : 0, osmesa->num_rects ? osmesa->rects[1] : 0,
                 osmesa->num_rects ? osmesa->rects[2] : 0, osmesa->num_rects ? osmesa->rects[3] : 0);

      if (!osbuffer->direct_res)
         return false;
      if (osbuffer->store_res) {
         u_box_2d(0, 0, osbuffer->width, osbuffer->height, &box);
         osmesa_copy(pipe, osbuffer->store_res, 0, 0, res, &box);
      }
      if (osmesa->num_rects > 0) {
         for (int i = 0; i < osmesa->num_rects; i++) {
            const GLint *r = osmesa->rects + i * 4;

            osmesa_show_rect(pipe, osbuffer, res, r[0], r[1], r[2], r[3]);
         }
      } else {
         osmesa_show_rect(pipe, osbuffer, res, 0, 0, osbuffer->width,
                          osbuffer->height);
      }
      pipe->flush(pipe, NULL, 0);
      return true;
   }

   /* Snapshot the color buffer to the user's buffer. */
   bpp = util_format_get_blocksize(osbuffer->visual.color_format);
   if (osmesa->user_row_length)
      dst_stride = bpp * osmesa->user_row_length;
   else
      dst_stride = bpp * osbuffer->width;

   osmesa_read_buffer(osmesa, res, osbuffer->map, dst_stride, osmesa->y_up,
                      true);

   /* If the user has requested the Z/S buffer, then snapshot that one too. */
   if (osmesa->zs) {
      osmesa_read_buffer(osmesa, osbuffer->textures[ST_ATTACHMENT_DEPTH_STENCIL],
                         osmesa->zs, osmesa->zs_stride, true, false);
   }

   return true;
}


/**
 * Called by the st manager to validate the framebuffer (allocate
 * its resources).
 */
static bool
osmesa_st_framebuffer_validate(struct st_context *st,
                               struct pipe_frontend_drawable *drawable,
                               const enum st_attachment_type *statts,
                               unsigned count,
                               struct pipe_resource **out,
                               struct pipe_resource **resolve)
{
   struct pipe_screen *screen = get_st_manager()->screen;
   enum st_attachment_type i;
   struct osmesa_buffer *osbuffer = drawable_to_osbuffer(drawable);
   struct pipe_resource templat;

   memset(&templat, 0, sizeof(templat));
   templat.target = PIPE_TEXTURE_RECT;
   templat.format = 0; /* setup below */
   templat.last_level = 0;
   templat.width0 = osbuffer->width;
   templat.height0 = osbuffer->height;
   templat.depth0 = 1;
   templat.array_size = 1;
   templat.usage = PIPE_USAGE_DEFAULT;
   templat.bind = 0; /* setup below */
   templat.flags = 0;

   for (i = 0; i < count; i++) {
      enum pipe_format format = PIPE_FORMAT_NONE;
      unsigned bind = 0;

      /*
       * At this time, we really only need to handle the front-left color
       * attachment, since that's all we specified for the visual in
       * osmesa_init_st_visual().
       */
      if (statts[i] == ST_ATTACHMENT_FRONT_LEFT) {
         format = osbuffer->visual.color_format;
         bind = PIPE_BIND_RENDER_TARGET;
      }
      else if (statts[i] == ST_ATTACHMENT_DEPTH_STENCIL) {
         format = osbuffer->visual.depth_stencil_format;
         bind = PIPE_BIND_DEPTH_STENCIL;
      }
      else if (statts[i] == ST_ATTACHMENT_ACCUM) {
         format = osbuffer->visual.accum_format;
         bind = PIPE_BIND_RENDER_TARGET;
      }
      else {
         debug_warning("Unexpected attachment type in "
                       "osmesa_st_framebuffer_validate()");
      }

      templat.format = format;
      templat.bind = bind;
      /* Multisampled: the colour and depth buffers Mesa draws on. */
      if (osbuffer->visual.samples > 1 && statts[i] != ST_ATTACHMENT_ACCUM) {
         templat.target = PIPE_TEXTURE_2D;
         templat.nr_samples = templat.nr_storage_samples = osbuffer->visual.samples;
      } else {
         templat.target = PIPE_TEXTURE_RECT;
         templat.nr_samples = templat.nr_storage_samples = 0;
      }
      pipe_resource_reference(&out[i], NULL);
      if (osbuffer->direct && statts[i] == ST_ATTACHMENT_FRONT_LEFT &&
          !osbuffer->direct_res) {
         struct winsys_handle whandle;
         struct pipe_resource target = templat;

         target.target = PIPE_TEXTURE_RECT;
         target.nr_samples = target.nr_storage_samples = 0;
         target.width0 = osbuffer->target_width;
         target.height0 = osbuffer->target_height;

         memset(&whandle, 0, sizeof(whandle));
         whandle.type = WINSYS_HANDLE_TYPE_KMS;
         whandle.handle = osbuffer->direct_handle;
         whandle.stride = osbuffer->direct_stride;
         whandle.offset = osbuffer->direct_offset;
         osbuffer->direct_res =
            screen->resource_from_handle(screen, &target, &whandle,
                                         PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE);
      }
      if (osbuffer->direct && osbuffer->own_handle && !osbuffer->store_res &&
          statts[i] == ST_ATTACHMENT_FRONT_LEFT) {
         struct winsys_handle whandle;

         memset(&whandle, 0, sizeof(whandle));
         whandle.type = WINSYS_HANDLE_TYPE_KMS;
         whandle.handle = osbuffer->own_handle;
         whandle.stride = osbuffer->own_stride;
         whandle.offset = osbuffer->own_offset;
         {
            struct pipe_resource store = templat;

            store.target = PIPE_TEXTURE_RECT;
            store.nr_samples = store.nr_storage_samples = 0;
            osbuffer->store_res =
               screen->resource_from_handle(screen, &store, &whandle,
                                            PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE);
         }
      }
      out[i] = osbuffer->textures[statts[i]] =
         screen->resource_create(screen, &templat);
   }

   return true;
}

static uint32_t osmesa_fb_ID = 0;


/**
 * Create new buffer and add to linked list.
 */
static struct osmesa_buffer *
osmesa_create_buffer(enum pipe_format color_format,
                     enum pipe_format ds_format,
                     enum pipe_format accum_format)
{
   struct osmesa_buffer *osbuffer = CALLOC_STRUCT(osmesa_buffer);
   if (osbuffer) {
      osbuffer->base.flush_front = osmesa_st_framebuffer_flush_front;
      osbuffer->base.validate = osmesa_st_framebuffer_validate;
      p_atomic_set(&osbuffer->base.stamp, 1);
      osbuffer->base.ID = p_atomic_inc_return(&osmesa_fb_ID);
      osbuffer->base.fscreen = get_st_manager();
      osbuffer->base.visual = &osbuffer->visual;

      osmesa_init_st_visual(&osbuffer->visual, color_format,
                            ds_format, accum_format);
   }

   return osbuffer;
}


static void
osmesa_destroy_buffer(struct osmesa_buffer *osbuffer)
{
   /*
    * Notify the state manager that the associated framebuffer interface
    * is no longer valid.
    */
   st_api_destroy_drawable(&osbuffer->base);
   pipe_resource_reference(&osbuffer->direct_res, NULL);
   pipe_resource_reference(&osbuffer->store_res, NULL);

   FREE(osbuffer);
}



/**********************************************************************/
/*****                    Public Functions                        *****/
/**********************************************************************/


/**
 * Create an Off-Screen Mesa rendering context.  The only attribute needed is
 * an RGBA vs Color-Index mode flag.
 *
 * Input:  format - Must be GL_RGBA
 *         sharelist - specifies another OSMesaContext with which to share
 *                     display lists.  NULL indicates no sharing.
 * Return:  an OSMesaContext or 0 if error
 */
/*
 * With glthread (RDN_GLTHREAD=1) the OpenGL calls a program makes are only
 * recorded by its own thread, and a second one runs them. Everything here
 * that touches the context from the program's thread waits for that
 * second thread to catch up first. Without glthread this does nothing.
 */
static void
osmesa_sync(OSMesaContext osmesa)
{
   if (osmesa && osmesa->st)
      _mesa_glthread_finish(osmesa->st->ctx);
}

/*
 * Which programs run with glthread. RDN_GLTHREAD in the environment
 * decides if it is set (0: no, anything else: yes). Otherwise the file
 * RDN_GLTHREAD_LIST does: a program is in if a line of it is the
 * program's name as the system has it (Quake3, Doom 3 Demo), or "*".
 * No file, no glthread: it has only been tried with a few programs.
 */
#define RDN_GLTHREAD_LIST "/Library/Application Support/RadeonNI/glthread"

static bool
osmesa_want_glthread(void)
{
   const char *env = getenv("RDN_GLTHREAD");
   const char *name = util_get_process_name();
   char line[256];
   bool want = false;
   FILE *f;

   if (env)
      return strcmp(env, "0") != 0;
   f = fopen(RDN_GLTHREAD_LIST, "r");
   if (!f)
      return false;
   while (!want && fgets(line, sizeof(line), f)) {
      size_t n = strcspn(line, "\r\n");

      line[n] = 0;
      want = !strcmp(line, "*") || (name && !strcmp(line, name));
   }
   fclose(f);
   return want;
}

/*
 * The list of extensions a program reads with glGetString().
 *
 * Mesa finds out which extensions a context has by reading a structure of
 * one-byte flags (struct gl_extensions) through a pointer to bool
 * (_mesa_extension_supported() in extensions.c). bool has four bytes on
 * 32-bit Darwin PowerPC, so there every flag is looked for at four times
 * its distance, and the list a program gets has little to do with what the
 * context can do: on the Radeon it lacked GL_ARB_vertex_program,
 * GL_ARB_vertex_shader and GL_EXT_stencil_two_side, which the context has
 * and which Mesa itself, asking each flag by name, knows it has. Doom 3
 * then draws with the fixed-function path it keeps for a GeForce 256.
 *
 * This makes the list the way Mesa means to, in Mesa's order (by year,
 * then by name), and puts it where glGetString() looks before it makes
 * its own. glGetStringi() and GL_NUM_EXTENSIONS still go the wrong way;
 * they agree with each other, and programs of Tiger's time do not use
 * them.
 */
static int
osmesa_extension_compare(const void *a, const void *b)
{
   const struct mesa_extension *ea = *(const struct mesa_extension *const *)a;
   const struct mesa_extension *eb = *(const struct mesa_extension *const *)b;
   int d = (int)ea->year - (int)eb->year;

   return d ? d : strcmp(ea->name, eb->name);
}

static void
osmesa_extension_string(struct gl_context *ctx)
{
   const GLboolean *have = (const GLboolean *)&ctx->Extensions;
   const struct mesa_extension *list[MESA_EXTENSION_COUNT];
   unsigned count = 0, i;
   size_t length = 1;
   char *s, *p;

   for (i = 0; i < MESA_EXTENSION_COUNT; i++) {
      const struct mesa_extension *e = &_mesa_extension_table[i];

      if (ctx->Version >= e->version[ctx->API] && have[e->offset]) {
         list[count++] = e;
         length += strlen(e->name) + 1;
      }
   }
   s = malloc(length);
   if (!s)
      return;
   qsort(list, count, sizeof(list[0]), osmesa_extension_compare);
   for (p = s, i = 0; i < count; i++)
      p += sprintf(p, "%s ", list[i]->name);
   *p = 0;
   /* Mesa frees it with the context. */
   free((void *)ctx->Extensions.String);
   ctx->Extensions.String = (const GLubyte *)s;
}

/*
 * Who gets the true list: every program, unless RDN_EXTENSIONS=mesa is in
 * its environment (Mesa's own list then, to compare). The window server
 * has only ever been seen with Mesa's list and what it does with another
 * one is not known, so it keeps that until the file RDN_EXTENSIONS_FILE
 * exists.
 */
#define RDN_EXTENSIONS_FILE "/Library/Application Support/RadeonNI/true-extensions"

static bool
osmesa_true_extensions(void)
{
   const char *env = getenv("RDN_EXTENSIONS");
   const char *name = util_get_process_name();

   if (env)
      return strcmp(env, "mesa") != 0;
   if (name && !strcmp(name, "WindowServer"))
      return access(RDN_EXTENSIONS_FILE, F_OK) == 0;
   return true;
}

GLAPI OSMesaContext GLAPIENTRY
OSMesaCreateContext(GLenum format, OSMesaContext sharelist)
{
   return OSMesaCreateContextExt(format, 24, 8, 0, sharelist);
}


/**
 * New in Mesa 3.5
 *
 * Create context and specify size of ancillary buffers.
 */
GLAPI OSMesaContext GLAPIENTRY
OSMesaCreateContextExt(GLenum format, GLint depthBits, GLint stencilBits,
                       GLint accumBits, OSMesaContext sharelist)
{
   int attribs[100], n = 0;

   attribs[n++] = OSMESA_FORMAT;
   attribs[n++] = format;
   attribs[n++] = OSMESA_DEPTH_BITS;
   attribs[n++] = depthBits;
   attribs[n++] = OSMESA_STENCIL_BITS;
   attribs[n++] = stencilBits;
   attribs[n++] = OSMESA_ACCUM_BITS;
   attribs[n++] = accumBits;
   attribs[n++] = 0;

   return OSMesaCreateContextAttribs(attribs, sharelist);
}


/**
 * New in Mesa 11.2
 *
 * Create context with attribute list.
 */
GLAPI OSMesaContext GLAPIENTRY
OSMesaCreateContextAttribs(const int *attribList, OSMesaContext sharelist)
{
   OSMesaContext osmesa;
   struct st_context *st_shared;
   enum st_context_error st_error = 0;
   struct st_context_attribs attribs;
   GLenum format = GL_RGBA;
   int depthBits = 0, stencilBits = 0, accumBits = 0;
   int profile = OSMESA_COMPAT_PROFILE, version_major = 1, version_minor = 0;
   int i;

   if (sharelist) {
      st_shared = sharelist->st;
   }
   else {
      st_shared = NULL;
   }

   for (i = 0; attribList[i]; i += 2) {
      switch (attribList[i]) {
      case OSMESA_FORMAT:
         format = attribList[i+1];
         switch (format) {
         case OSMESA_COLOR_INDEX:
         case OSMESA_RGBA:
         case OSMESA_BGRA:
         case OSMESA_ARGB:
         case OSMESA_RGB:
         case OSMESA_BGR:
         case OSMESA_RGB_565:
            /* legal */
            break;
         default:
            return NULL;
         }
         break;
      case OSMESA_DEPTH_BITS:
         depthBits = attribList[i+1];
         if (depthBits < 0)
            return NULL;
         break;
      case OSMESA_STENCIL_BITS:
         stencilBits = attribList[i+1];
         if (stencilBits < 0)
            return NULL;
         break;
      case OSMESA_ACCUM_BITS:
         accumBits = attribList[i+1];
         if (accumBits < 0)
            return NULL;
         break;
      case OSMESA_PROFILE:
         profile = attribList[i+1];
         if (profile != OSMESA_CORE_PROFILE &&
             profile != OSMESA_COMPAT_PROFILE)
            return NULL;
         break;
      case OSMESA_CONTEXT_MAJOR_VERSION:
         version_major = attribList[i+1];
         if (version_major < 1)
            return NULL;
         break;
      case OSMESA_CONTEXT_MINOR_VERSION:
         version_minor = attribList[i+1];
         if (version_minor < 0)
            return NULL;
         break;
      case 0:
         /* end of list */
         break;
      default:
         fprintf(stderr, "Bad attribute in OSMesaCreateContextAttribs()\n");
         return NULL;
      }
   }

   osmesa = (OSMesaContext) CALLOC_STRUCT(osmesa_context);
   if (!osmesa)
      return NULL;

   /* Choose depth/stencil/accum buffer formats */
   if (accumBits > 0) {
      osmesa->accum_format = PIPE_FORMAT_R16G16B16A16_SNORM;
   }
   if (depthBits > 0 && stencilBits > 0) {
      osmesa->depth_stencil_format = PIPE_FORMAT_Z24_UNORM_S8_UINT;
   }
   else if (stencilBits > 0) {
      osmesa->depth_stencil_format = PIPE_FORMAT_S8_UINT;
   }
   else if (depthBits >= 24) {
      osmesa->depth_stencil_format = PIPE_FORMAT_Z24X8_UNORM;
   }
   else if (depthBits >= 16) {
      osmesa->depth_stencil_format = PIPE_FORMAT_Z16_UNORM;
   }

   /*
    * Create the rendering context
    */
   memset(&attribs, 0, sizeof(attribs));
   attribs.profile = (profile == OSMESA_CORE_PROFILE)
      ? API_OPENGL_CORE : API_OPENGL_COMPAT;
   attribs.major = version_major;
   attribs.minor = version_minor;
   attribs.flags = 0;  /* ST_CONTEXT_FLAG_x */
   attribs.options.force_glsl_extensions_warn = false;
   attribs.options.disable_blend_func_extended = false;
   attribs.options.disable_glsl_line_continuations = false;
   attribs.options.force_glsl_version = 0;

   osmesa_init_st_visual(&attribs.visual,
                         PIPE_FORMAT_NONE,
                         osmesa->depth_stencil_format,
                         osmesa->accum_format);

   osmesa->st = st_api_create_context(get_st_manager(),
                                         &attribs, &st_error, st_shared);
   if (!osmesa->st) {
      FREE(osmesa);
      return NULL;
   }

   osmesa->st->frontend_context = osmesa;

   if (osmesa_true_extensions())
      osmesa_extension_string(osmesa->st->ctx);

   /*
    * Two processors: let the second one do Mesa's work for each call
    * (state validation, the draws) while the program goes on.
    */
   if (osmesa_want_glthread()) {
      _mesa_glthread_init(osmesa->st->ctx);
      if (getenv("RDN_GLTHREAD_LOG"))
         fprintf(stderr, "rdn: glthread %s\n",
                 osmesa->st->ctx->GLThread.enabled ? "enabled" : "refused");
   }

   osmesa->format = format;
   osmesa->user_row_length = 0;
   osmesa->y_up = GL_TRUE;

   return osmesa;
}



/**
 * Destroy an Off-Screen Mesa rendering context.
 *
 * \param osmesa  the context to destroy
 */
GLAPI void GLAPIENTRY
OSMesaDestroyContext(OSMesaContext osmesa)
{
   osmesa_sync(osmesa);
   if (osmesa) {
      st_destroy_context(osmesa->st);
      free(osmesa->zs);
      free(osmesa->rects);
      FREE(osmesa);
   }
}


/**
 * Bind an OSMesaContext to an image buffer.  The image buffer is just a
 * block of memory which the client provides.  Its size must be at least
 * as large as width*height*pixelSize.  Its address should be a multiple
 * of 4 if using RGBA mode.
 *
 * By default, image data is stored in the order of glDrawPixels: row-major
 * order with the lower-left image pixel stored in the first array position
 * (ie. bottom-to-top).
 *
 * If the context's viewport hasn't been initialized yet, it will now be
 * initialized to (0,0,width,height).
 *
 * Input:  osmesa - the rendering context
 *         buffer - the image buffer memory
 *         type - data type for pixel components
 *                GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT_5_6_5, GL_UNSIGNED_SHORT
 *                or GL_FLOAT.
 *         width, height - size of image buffer in pixels, at least 1
 * Return:  GL_TRUE if success, GL_FALSE if error because of invalid osmesa,
 *          invalid type, invalid size, etc.
 */
GLAPI GLboolean GLAPIENTRY
OSMesaMakeCurrent(OSMesaContext osmesa, void *buffer, GLenum type,
                  GLsizei width, GLsizei height)
{
   osmesa_sync(osmesa);
   enum pipe_format color_format;

   if (!osmesa && !buffer) {
      st_api_make_current(NULL, NULL, NULL);
      return GL_TRUE;
   }

   if (!osmesa || !buffer || width < 1 || height < 1) {
      return GL_FALSE;
   }

   color_format = osmesa_choose_format(osmesa->format, type);
   if (color_format == PIPE_FORMAT_NONE) {
      fprintf(stderr, "OSMesaMakeCurrent(unsupported format/type)\n");
      return GL_FALSE;
   }

   /* See if we already have a buffer that uses these pixel formats */
   if (osmesa->current_buffer &&
       (osmesa->current_buffer->direct ||
        osmesa->current_buffer->visual.color_format != color_format ||
        osmesa->current_buffer->visual.depth_stencil_format != osmesa->depth_stencil_format ||
        osmesa->current_buffer->visual.accum_format != osmesa->accum_format ||
        osmesa->current_buffer->width != width ||
        osmesa->current_buffer->height != height)) {
      osmesa_destroy_buffer(osmesa->current_buffer);
      osmesa->current_buffer = NULL;
   }

   if (!osmesa->current_buffer) {
      osmesa->current_buffer = osmesa_create_buffer(color_format,
                                      osmesa->depth_stencil_format,
                                      osmesa->accum_format);
   }

   struct osmesa_buffer *osbuffer = osmesa->current_buffer;

   osbuffer->width = width;
   osbuffer->height = height;
   osbuffer->map = buffer;

   osmesa->type = type;

   st_api_make_current(osmesa->st, &osbuffer->base, &osbuffer->base);

   /* XXX: We should probably load the current color value into the buffer here
    * to match classic swrast behavior (context's fb starts with the contents of
    * your pixel buffer).
    */

   osmesa->ever_used = true;

   return GL_TRUE;
}



GLAPI void GLAPIENTRY
OSMesaShowStore(OSMesaContext osmesa, GLuint handle, GLsizei stride,
                GLuint offset, GLsizei width, GLsizei height, GLint x, GLint y,
                GLint count, const GLint *rects)
{
   osmesa_sync(osmesa);
   struct osmesa_buffer *osbuffer = osmesa ? osmesa->current_buffer : NULL;
   struct pipe_context *pipe;
   struct pipe_screen *screen;
   struct pipe_resource templat, *src;
   struct winsys_handle whandle;
   struct pipe_box box;

   if (!osbuffer || !osbuffer->direct || !osbuffer->direct_res || width < 1 ||
       height < 1)
      return;
   pipe = osmesa->st->pipe;
   screen = pipe->screen;
   memset(&templat, 0, sizeof(templat));
   templat.target = PIPE_TEXTURE_RECT;
   templat.format = osbuffer->visual.color_format;
   templat.width0 = width;
   templat.height0 = height;
   templat.depth0 = 1;
   templat.array_size = 1;
   templat.usage = PIPE_USAGE_DEFAULT;
   templat.bind = PIPE_BIND_RENDER_TARGET;
   memset(&whandle, 0, sizeof(whandle));
   whandle.type = WINSYS_HANDLE_TYPE_KMS;
   whandle.handle = handle;
   whandle.stride = stride;
   whandle.offset = offset;
   src = screen->resource_from_handle(screen, &templat, &whandle,
                                      PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE);
   if (!src)
      return;
   for (int i = 0; i < count; i++) {
      const GLint *r = rects + i * 4;
      int x0 = MAX2(MAX2(r[0], 0), -x), y0 = MAX2(MAX2(r[1], 0), -y);
      int x1 = MIN2(MIN2(r[0] + r[2], width), (int)osbuffer->target_width - x);
      int y1 = MIN2(MIN2(r[1] + r[3], height), (int)osbuffer->target_height - y);

      if (x1 <= x0 || y1 <= y0)
         continue;
      u_box_2d(x0, y0, x1 - x0, y1 - y0, &box);
      pipe->resource_copy_region(pipe, osbuffer->direct_res, 0, x + x0, y + y0,
                                 0, src, 0, &box);
   }
   pipe->flush(pipe, NULL, 0);
   pipe_resource_reference(&src, NULL);
}


GLAPI GLboolean GLAPIENTRY
OSMesaTexStore(OSMesaContext osmesa, GLenum target, GLuint handle,
               GLsizei stride, GLuint offset, GLsizei width, GLsizei height)
{
   osmesa_sync(osmesa);
   struct osmesa_buffer *osbuffer = osmesa ? osmesa->current_buffer : NULL;
   struct pipe_screen *screen;
   struct pipe_resource templat, *res;
   struct winsys_handle whandle;
   enum pipe_format format;
   bool ok;

   if (!osbuffer || width < 1 || height < 1)
      return GL_FALSE;
   /* The same layout without alpha: a surface is opaque. */
   format = osbuffer->visual.color_format;
   if (format == PIPE_FORMAT_B8G8R8A8_UNORM)
      format = PIPE_FORMAT_B8G8R8X8_UNORM;
   else if (format == PIPE_FORMAT_A8R8G8B8_UNORM)
      format = PIPE_FORMAT_X8R8G8B8_UNORM;
   screen = osmesa->st->pipe->screen;
   memset(&templat, 0, sizeof(templat));
   templat.target = PIPE_TEXTURE_RECT;
   templat.format = format;
   templat.width0 = width;
   templat.height0 = height;
   templat.depth0 = 1;
   templat.array_size = 1;
   templat.usage = PIPE_USAGE_DEFAULT;
   templat.bind = PIPE_BIND_SAMPLER_VIEW;
   memset(&whandle, 0, sizeof(whandle));
   whandle.type = WINSYS_HANDLE_TYPE_KMS;
   whandle.handle = handle;
   whandle.stride = stride;
   whandle.offset = offset;
   res = screen->resource_from_handle(screen, &templat, &whandle,
                                      PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE);
   if (!res)
      return GL_FALSE;
   ok = st_context_teximage(osmesa->st, target, 0, format, res, false);
   pipe_resource_reference(&res, NULL);
   return ok ? GL_TRUE : GL_FALSE;
}


GLAPI void GLAPIENTRY
OSMesaDrawStore(OSMesaContext osmesa, GLuint handle, GLsizei stride,
                GLuint offset, GLsizei width, GLsizei height, GLint sx,
                GLint sy, GLsizei sw, GLsizei sh, GLint dx, GLint dy)
{
   osmesa_sync(osmesa);
   struct osmesa_buffer *osbuffer = osmesa ? osmesa->current_buffer : NULL;
   struct pipe_context *pipe;
   struct pipe_screen *screen;
   struct pipe_resource templat, *src, *dst;
   struct winsys_handle whandle;
   struct pipe_box box;

   if (!osbuffer || width < 1 || height < 1)
      return;
   dst = osbuffer->textures[ST_ATTACHMENT_FRONT_LEFT];
   if (!dst)
      return;
   /* The part that is inside both. */
   if (sx < 0) { dx -= sx; sw += sx; sx = 0; }
   if (sy < 0) { dy -= sy; sh += sy; sy = 0; }
   if (dx < 0) { sx -= dx; sw += dx; dx = 0; }
   if (dy < 0) { sy -= dy; sh += dy; dy = 0; }
   sw = MIN2(MIN2(sw, width - sx), (int)dst->width0 - dx);
   sh = MIN2(MIN2(sh, height - sy), (int)dst->height0 - dy);
   if (sw <= 0 || sh <= 0)
      return;

   /* What GL has drawn so far goes first. */
   st_context_flush(osmesa->st, 0, NULL, NULL, NULL);
   pipe = osmesa->st->pipe;
   screen = pipe->screen;
   memset(&templat, 0, sizeof(templat));
   templat.target = PIPE_TEXTURE_RECT;
   templat.format = osbuffer->visual.color_format;
   templat.width0 = width;
   templat.height0 = height;
   templat.depth0 = 1;
   templat.array_size = 1;
   templat.usage = PIPE_USAGE_DEFAULT;
   templat.bind = PIPE_BIND_RENDER_TARGET;
   memset(&whandle, 0, sizeof(whandle));
   whandle.type = WINSYS_HANDLE_TYPE_KMS;
   whandle.handle = handle;
   whandle.stride = stride;
   whandle.offset = offset;
   src = screen->resource_from_handle(screen, &templat, &whandle,
                                      PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE);
   if (!src)
      return;
   u_box_2d(sx, sy, sw, sh, &box);
   osmesa_copy(pipe, dst, dx, dy, src, &box);
   pipe_resource_reference(&src, NULL);
}


GLAPI void GLAPIENTRY
OSMesaSurfaceStorage(OSMesaContext osmesa, GLuint handle, GLsizei stride,
                     GLuint offset)
{
   osmesa_sync(osmesa);
   if (!osmesa)
      return;
   osmesa->own_handle = handle;
   osmesa->own_stride = stride;
   osmesa->own_offset = offset;
}


GLAPI GLboolean GLAPIENTRY
OSMesaMakeCurrentDirect(OSMesaContext osmesa, GLuint handle, GLsizei width,
                        GLsizei height, GLsizei stride, GLuint offset)
{
   osmesa_sync(osmesa);
   return OSMesaMakeCurrentSurface(osmesa, handle, width, height, stride,
                                   offset, 0, 0, width, height);
}


GLAPI GLboolean GLAPIENTRY
OSMesaMakeCurrentSurface(OSMesaContext osmesa, GLuint handle,
                         GLsizei target_width, GLsizei target_height,
                         GLsizei stride, GLuint offset, GLint x, GLint y,
                         GLsizei width, GLsizei height)
{
   osmesa_sync(osmesa);
   enum pipe_format color_format;
   struct osmesa_buffer *osbuffer;

   if (!osmesa || width < 1 || height < 1)
      return GL_FALSE;
   color_format = osmesa_choose_format(osmesa->format, GL_UNSIGNED_BYTE);
   if (color_format == PIPE_FORMAT_NONE)
      return GL_FALSE;

   osbuffer = osmesa->current_buffer;
   if (osbuffer &&
       (!osbuffer->direct ||
        osbuffer->visual.color_format != color_format ||
        osbuffer->width != (unsigned)width ||
        osbuffer->height != (unsigned)height ||
        osbuffer->direct_handle != handle ||
        osbuffer->direct_stride != (unsigned)stride ||
        osbuffer->direct_offset != offset ||
        osbuffer->target_width != (unsigned)target_width ||
        osbuffer->target_height != (unsigned)target_height ||
        osbuffer->own_handle != osmesa->own_handle ||
        osbuffer->own_stride != osmesa->own_stride ||
        osbuffer->own_offset != osmesa->own_offset)) {
      osmesa_destroy_buffer(osbuffer);
      osmesa->current_buffer = NULL;
   }
   if (!osmesa->current_buffer) {
      osmesa->current_buffer = osmesa_create_buffer(color_format,
                                      osmesa->depth_stencil_format,
                                      osmesa->accum_format);
      if (!osmesa->current_buffer)
         return GL_FALSE;
   }
   osbuffer = osmesa->current_buffer;
   osbuffer->width = width;
   osbuffer->height = height;
   osbuffer->map = NULL;
   osbuffer->direct = true;
   osbuffer->direct_handle = handle;
   osbuffer->direct_stride = stride;
   osbuffer->direct_offset = offset;
   osbuffer->target_width = target_width;
   osbuffer->target_height = target_height;
   osbuffer->own_handle = osmesa->own_handle;
   osbuffer->own_stride = osmesa->own_stride;
   osbuffer->own_offset = osmesa->own_offset;
   /* Only the place changes when a window moves: the buffer is kept. */
   osbuffer->target_x = x;
   osbuffer->target_y = y;
   osmesa->type = GL_UNSIGNED_BYTE;

   st_api_make_current(osmesa->st, &osbuffer->base, &osbuffer->base);
   osmesa->ever_used = true;
   return GL_TRUE;
}


GLAPI void GLAPIENTRY
OSMesaSetSamples(GLint samples)
{
   osmesa_samples = samples > 1 ? MIN2(samples, 8) : 1;
}

/*
 * glthread records a call with its data in a batch if both fit in one
 * (MARSHAL_MAX_CMD_SIZE); anything larger it runs at once in the program's
 * thread, after waiting for the other thread to finish all that came
 * before. The call's own record takes a few words of that.
 */
GLAPI GLint GLAPIENTRY
OSMesaAsyncDataLimit(OSMesaContext osmesa)
{
   if (!osmesa || !osmesa->st || !osmesa->st->ctx->GLThread.enabled)
      return 0;
   return MARSHAL_MAX_CMD_SIZE - 256;
}

GLAPI OSMesaContext GLAPIENTRY
OSMesaGetCurrentContext(void)
{
   struct st_context *st = st_api_get_current();
   return st ? (OSMesaContext) st->frontend_context : NULL;
}



GLAPI void GLAPIENTRY
OSMesaReadbackRects(OSMesaContext osmesa, GLint count, const GLint *rects)
{
   osmesa_sync(osmesa);
   if (!osmesa)
      return;
   free(osmesa->rects);
   osmesa->rects = NULL;
   osmesa->num_rects = 0;
   if (count <= 0 || !rects)
      return;
   osmesa->rects = malloc(count * 4 * sizeof(GLint));
   if (!osmesa->rects)
      return;
   memcpy(osmesa->rects, rects, count * 4 * sizeof(GLint));
   osmesa->num_rects = count;
}


GLAPI void GLAPIENTRY
OSMesaPixelStore(GLint pname, GLint value)
{
   OSMesaContext osmesa = OSMesaGetCurrentContext();

   switch (pname) {
   case OSMESA_ROW_LENGTH:
      osmesa->user_row_length = value;
      break;
   case OSMESA_Y_UP:
      osmesa->y_up = value ? GL_TRUE : GL_FALSE;
      break;
   default:
      fprintf(stderr, "Invalid pname in OSMesaPixelStore()\n");
      return;
   }
}


GLAPI void GLAPIENTRY
OSMesaGetIntegerv(GLint pname, GLint *value)
{
   OSMesaContext osmesa = OSMesaGetCurrentContext();
   struct osmesa_buffer *osbuffer = osmesa ? osmesa->current_buffer : NULL;

   switch (pname) {
   case OSMESA_WIDTH:
      *value = osbuffer ? osbuffer->width : 0;
      return;
   case OSMESA_HEIGHT:
      *value = osbuffer ? osbuffer->height : 0;
      return;
   case OSMESA_FORMAT:
      *value = osmesa->format;
      return;
   case OSMESA_TYPE:
      /* current color buffer's data type */
      *value = osmesa->type;
      return;
   case OSMESA_ROW_LENGTH:
      *value = osmesa->user_row_length;
      return;
   case OSMESA_Y_UP:
      *value = osmesa->y_up;
      return;
   case OSMESA_MAX_WIDTH:
      FALLTHROUGH;
   case OSMESA_MAX_HEIGHT:
      {
         struct pipe_screen *screen = get_st_manager()->screen;
         *value = screen->caps.max_texture_2d_size;
      }
      return;
   default:
      fprintf(stderr, "Invalid pname in OSMesaGetIntegerv()\n");
      return;
   }
}


/**
 * Return information about the depth buffer associated with an OSMesa context.
 * Input:  c - the OSMesa context
 * Output:  width, height - size of buffer in pixels
 *          bytesPerValue - bytes per depth value (2 or 4)
 *          buffer - pointer to depth buffer values
 * Return:  GL_TRUE or GL_FALSE to indicate success or failure.
 */
GLAPI GLboolean GLAPIENTRY
OSMesaGetDepthBuffer(OSMesaContext c, GLint *width, GLint *height,
                     GLint *bytesPerValue, void **buffer)
{
   osmesa_sync(c);
   struct osmesa_buffer *osbuffer = c->current_buffer;
   struct pipe_resource *res = osbuffer->textures[ST_ATTACHMENT_DEPTH_STENCIL];

   if (!res) {
      *width = 0;
      *height = 0;
      *bytesPerValue = 0;
      *buffer = NULL;
      return GL_FALSE;
   }

   *width = res->width0;
   *height = res->height0;
   *bytesPerValue = util_format_get_blocksize(res->format);

   if (!c->zs) {
      c->zs_stride = *width * *bytesPerValue;
      c->zs = calloc(c->zs_stride, *height);
      if (!c->zs)
         return GL_FALSE;

      osmesa_read_buffer(c, res, c->zs, c->zs_stride, true, false);
   }

   *buffer = c->zs;

   return GL_TRUE;
}


/**
 * Return the color buffer associated with an OSMesa context.
 * Input:  c - the OSMesa context
 * Output:  width, height - size of buffer in pixels
 *          format - the pixel format (OSMESA_FORMAT)
 *          buffer - pointer to color buffer values
 * Return:  GL_TRUE or GL_FALSE to indicate success or failure.
 */
GLAPI GLboolean GLAPIENTRY
OSMesaGetColorBuffer(OSMesaContext osmesa, GLint *width,
                      GLint *height, GLint *format, void **buffer)
{
   osmesa_sync(osmesa);
   struct osmesa_buffer *osbuffer = osmesa->current_buffer;

   if (osbuffer) {
      *width = osbuffer->width;
      *height = osbuffer->height;
      *format = osmesa->format;
      *buffer = osbuffer->map;
      return GL_TRUE;
   }
   else {
      *width = 0;
      *height = 0;
      *format = 0;
      *buffer = 0;
      return GL_FALSE;
   }
}


struct name_function
{
   const char *Name;
   OSMESAproc Function;
};

static struct name_function functions[] = {
   { "OSMesaCreateContext", (OSMESAproc) OSMesaCreateContext },
   { "OSMesaCreateContextExt", (OSMESAproc) OSMesaCreateContextExt },
   { "OSMesaCreateContextAttribs", (OSMESAproc) OSMesaCreateContextAttribs },
   { "OSMesaDestroyContext", (OSMESAproc) OSMesaDestroyContext },
   { "OSMesaMakeCurrent", (OSMESAproc) OSMesaMakeCurrent },
   { "OSMesaGetCurrentContext", (OSMESAproc) OSMesaGetCurrentContext },
   { "OSMesaPixelStore", (OSMESAproc) OSMesaPixelStore },
   { "OSMesaGetIntegerv", (OSMESAproc) OSMesaGetIntegerv },
   { "OSMesaGetDepthBuffer", (OSMESAproc) OSMesaGetDepthBuffer },
   { "OSMesaGetColorBuffer", (OSMESAproc) OSMesaGetColorBuffer },
   { "OSMesaGetProcAddress", (OSMESAproc) OSMesaGetProcAddress },
   { "OSMesaColorClamp", (OSMESAproc) OSMesaColorClamp },
   { "OSMesaPostprocess", (OSMESAproc) OSMesaPostprocess },
   { NULL, NULL }
};


GLAPI OSMESAproc GLAPIENTRY
OSMesaGetProcAddress(const char *funcName)
{
   int i;
   for (i = 0; functions[i].Name; i++) {
      if (strcmp(functions[i].Name, funcName) == 0)
         return functions[i].Function;
   }
   return _mesa_glapi_get_proc_address(funcName);
}


GLAPI void GLAPIENTRY
OSMesaColorClamp(GLboolean enable)
{
   extern void GLAPIENTRY _mesa_ClampColor(GLenum target, GLenum clamp);

   _mesa_ClampColor(GL_CLAMP_FRAGMENT_COLOR_ARB,
                    enable ? GL_TRUE : GL_FIXED_ONLY_ARB);
}


GLAPI void GLAPIENTRY
OSMesaPostprocess(OSMesaContext osmesa, const char *filter,
                  unsigned enable_value)
{
   /* osx-gpu: current Mesa has no post-processing filters. */
   (void)osmesa;
   (void)filter;
   (void)enable_value;
}
