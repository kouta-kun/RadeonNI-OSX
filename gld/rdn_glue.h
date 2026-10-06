/*
 * What the parts of the driver bundle share. Plain pointers, so that the
 * part that talks to Mesa does not need Apple's OpenGL headers and the part
 * that talks to Apple's does not need Mesa's.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_GLUE_H
#define RDN_GLUE_H

/* The generated glue (gen_dispatch.py). */
/* Look Mesa's functions up by OpenGL name; returns how many exist. */
unsigned rdn_dispatch_resolve(void *(*lookup)(const char *name),
			      void (*missing)(const char *name));
/*
 * Put our entry points into a GLIFunctionDispatch that has `entries`
 * entries (the running system's may be shorter than the SDK header's);
 * returns how many were replaced.
 */
unsigned rdn_dispatch_install(void *table, unsigned entries);
extern const unsigned rdn_dispatch_entries;

/* rdn_mesa.c: the engine context whose Mesa context is current. */
extern void *rdn_current_rend;
void rdn_make_current(void *rend);

/* rdn_mesa.c: what the bundle tells it about, as the gld* calls go by. */
void rdn_mesa_context_created(void *gld_ctx);
void rdn_mesa_context_destroyed(void *gld_ctx);
/*
 * The engine's context that goes with a new driver context, and the
 * moment to try giving the program's GL entry points to Mesa before any
 * drawable is attached (any later driver call for the context will do).
 */
void rdn_mesa_context_engine(void *gld_ctx, void *rend);
void rdn_mesa_early(void *gld_ctx);
/* The same for every context that has no drawable yet. */
void rdn_mesa_early_all(void *cgl_ctx);
/* True if windows' buffers hold their top row first (RadeonNIGLDriver.c). */
int rdn_windows_top_down(void);
/* rdn_hook.c: call `after` whenever a context has been made current. */
void rdn_hook_set_current(void (*after)(void *cgl_ctx));
void rdn_mesa_attach(void *gld_ctx, long type, const void *drawable);
/*
 * Where the current context's window coordinates start on Mesa's drawable,
 * when that is bigger than what the context was given (the window server
 * draws on a part of the screen and Mesa on the screen). Added to x and y
 * of the GL calls that take window coordinates.
 */
extern int rdn_origin_x, rdn_origin_y;

/* Log every GL call that reaches Mesa (set when /tmp/rdngld.trace exists). */
extern int rdn_trace;

/*
 * RDN_GLD_TRACE_ONLY in the environment limits the trace to the calls it
 * names ("glDrawElements,glBindTexture"); without it every call is wanted.
 */
int rdn_trace_wanted(const char *name);

/*
 * The context draws on the card's screen, inside the shape of the window
 * server's surface `surface` (the whole screen if the kernel does not know
 * the surface). For the window server's compositing context. False if the
 * screen cannot be reached.
 */
int rdn_mesa_attach_screen(void *gld_ctx, unsigned long surface);
/*
 * The context draws on a window that the window server manages as surface
 * `surface` in the kext; the card shows it there itself. False if Mesa is
 * not available.
 */
int rdn_mesa_attach_surface(void *gld_ctx, unsigned long connection,
			    unsigned long window, unsigned long surface);
/* RadeonNIGLDriver.c: have the window server draw the surface's area again. */
void rdn_surface_flush(unsigned long cid, unsigned long wid, unsigned long sid);
/* RadeonNIGLDriver.c: the surface's size as the window server has it. */
int rdn_surface_size(unsigned long cid, unsigned long wid, unsigned long sid,
		     unsigned *width, unsigned *height);
int rdn_mesa_is_surface(void *gld_ctx);
/* The context has no drawable any more. */
void rdn_mesa_detach(void *gld_ctx);
/*
 * The program asked for the picture to be shown (glSwapAPPLE, which is
 * what CGLFlushDrawable calls). True if the bundle did it; false if the
 * engine should, as for a window that is a buffer of the software
 * renderer's.
 */
int rdn_swap(void *rend);
/*
 * Set in the window server once its context draws on the screen: the
 * calls below are then reported as they are made (after Mesa has had
 * them). See rdn_watch_end() in rdn_mesa.c.
 */
extern int rdn_watch;
/* Set by the bundle when the process is the window server. */
extern int rdn_window_server;
void rdn_watch_ortho(double left, double right, double bottom, double top,
		     double z_near, double z_far);
void rdn_watch_begin(unsigned mode);
void rdn_watch_end(void);
void rdn_watch_vertex2f(float x, float y);
void rdn_watch_tex_coord2f(float s, float t);
void rdn_watch_color4ub(unsigned char r, unsigned char g, unsigned char b,
			unsigned char a);
void rdn_watch_enable(unsigned cap);
void rdn_watch_disable(unsigned cap);
void rdn_watch_active_texture(unsigned unit);
void rdn_watch_bind_texture(unsigned target, unsigned texture);
void rdn_watch_tex_image2D(unsigned target, int level, int internalformat,
			   int width, int height, int border, unsigned format,
			   unsigned type, const void *pixels);
void rdn_watch_tex_sub_image2D(unsigned target, int level, int xoffset,
			       int yoffset, int width, int height,
			       unsigned format, unsigned type, const void *pixels);
void rdn_watch_tex_parameterf(unsigned target, unsigned pname, float param);
/* True if the context's GL entry points are now Mesa's. */
int rdn_mesa_dispatch(void *gld_ctx, void *engine_table);
/* Finish the frame and put it into the drawable's buffer. */
void rdn_mesa_present(void *gld_ctx);

/* RadeonNIGLDriver.c: one line into the bundle's log, if it has one. */
void rdn_log(const char *fmt, ...);

#endif /* RDN_GLUE_H */
