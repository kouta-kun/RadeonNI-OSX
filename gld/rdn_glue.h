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

#include <stdint.h>

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
/*
 * While the bundle logs (rdn_logging), install also wraps the entries Mesa
 * has no function for, which stay the engine's, to count their calls and
 * log the first few of each. This logs the counts, with `when` to tell the
 * reports apart.
 */
void rdn_dispatch_kept_report(const char *when);

/* rdn_mesa.c: the engine context whose Mesa context is current. */
/* Per thread: Mesa's current context is too. */
extern __thread void *rdn_current_rend;
void rdn_make_current(void *rend);

/* rdn_mesa.c: what the bundle tells it about, as the gld* calls go by. */
void rdn_mesa_context_created(void *gld_ctx, void *share);
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
/*
 * rdn_hook.c: let `own` answer a program's lookups of functions by name
 * in a bundle (a function, or NULL for a name that is not ours). Returns 0
 * when the program cannot be hooked.
 */
int rdn_hook_function_lookup(void *(*own)(const char *name));
/*
 * rdn_hook.c: let `handler` answer CGLTexImagePBuffer (it returns 1 and
 * the CGL error in *result, or 0 to leave the call to OpenGL).
 */
void rdn_hook_tex_image_pbuffer(int (*handler)(void *cgl_ctx, void *pbuffer,
					       long source, long *result));
/* rdn_hook.c: log CGLCreatePBuffer, CGLSetPBuffer and CGLChoosePixelFormat. */
void rdn_hook_cgl_log(void);
/* rdn_hook.c: the same for the window server's cglsTexImagePBuffer. */
void rdn_hook_cgls_tex_image(int (*handler)(void *ctx, void *pbuffer, long source,
					    long *result));
/*
 * rdn_hook.c: let `handler` see the window server's cglsSetInteger (ctx,
 * parameter, values); it returns 1 and the result for a call that is
 * ours, 0 to leave it to OpenGL's engine. 0 when it cannot be hooked.
 */
int rdn_hook_cgls_set_integer(int (*handler)(void *ctx, long pname, long *vals, long *result));
int rdn_mesa_cgls_set_integer(void *ctx, long pname, long *vals, long *result);
/*
 * rdn_hook.c: `handler` is called with a pbuffer object just before
 * CGLDestroyPBuffer (or the window server's cglsDestroyPBuffer) destroys it.
 */
void rdn_hook_destroy_pbuffer(void (*handler)(void *pbuffer));
void rdn_mesa_pbuffer_destroyed(void *pbuffer);
/*
 * rdn_hook.c: `idle` is called whenever any thread calls mach_msg only to wait
 * for a message; it returns how many milliseconds to wait first (0: none),
 * and `timeout` is called if they pass with no message.
 */
void rdn_hook_mach_msg(unsigned (*idle)(void), void (*timeout)(void));
/*
 * glFlush and glFinish of a program's window (a surface of the window
 * server's) that has not swapped: a single-buffered window shows what is
 * drawn at a flush, and the window server is only told of a new picture
 * by rdn_swap otherwise. Core Image Fun House draws that way.
 */
void rdn_flush_surface(void *rend);
/* The pbuffer records are the window server's (size in words 8 and 9). */
void rdn_mesa_pbuffer_layout(int window_server);
int rdn_mesa_is_pbuffer(void *gld_ctx);
int rdn_mesa_tex_image_pbuffer_ws(void *cgls_ctx, void *pbuffer, long source, long *result);
/* CGLTexImagePBuffer for a context of ours (rdn_mesa.c). */
int rdn_mesa_tex_image_pbuffer(void *cgl_ctx, void *pbuffer, long source, long *result);
/* 0 if a pbuffer's memory cannot be had: the attach is to fail. */
int rdn_mesa_attach(void *gld_ctx, long type, const void *drawable);
/*
 * Where the current context's window coordinates start on Mesa's drawable,
 * when that is bigger than what the context was given (the window server
 * draws on a part of the screen and Mesa on the screen). Added to x and y
 * of the GL calls that take window coordinates.
 */
extern int rdn_origin_x, rdn_origin_y;

/*
 * The current context runs with glthread, and a glBufferData or
 * glBufferSubData with more than rdn_async_limit bytes would make the
 * program's thread wait for everything recorded so far (once a frame in
 * Doom 3, a third of its time). The entry points hand such data over in
 * pieces of rdn_async_piece bytes instead. 0: nothing to do.
 */
extern long rdn_async_limit, rdn_async_piece;
/*
 * The current context runs with glthread and its picture is copied to
 * memory the program or the engine reads (an off-screen drawable, a
 * window that is no surface). There glFlush must not return before the
 * copy is made, which with glthread only glFinish does.
 */
extern int rdn_flush_waits;

/* The bundle's log has a file (RDN_GLD_LOG, or /tmp/rdngld.on exists). */
extern int rdn_logging;
/*
 * The table being installed into is a finished context's (rdn_mesa.c).
 * While a context is still being made the engine keeps other things in
 * the slots Mesa has no function for, and a wrapper there crashes
 * cglAssignDispatch.
 */
extern int rdn_kept_now;
/* Log every GL call that reaches Mesa (set when /tmp/rdngld.trace exists). */
extern int rdn_trace;

/*
 * RDN_GLD_TRACE_ONLY in the environment limits the trace to the calls it
 * names ("glDrawElements,glBindTexture"); without it every call is wanted.
 */
int rdn_trace_wanted(const char *name);

/*
 * The window server is not to find GL_ARB_fragment_program in its list
 * (RadeonNIGLDriver.c).
 */
int rdn_ws_no_core_image(void);
/* The window server with Core Image on the card (the file, RadeonNIGLDriver.c). */
int rdn_ws_core_image(void);

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
void rdn_mesa_samples(int samples);
void rdn_mesa_double(int yes);

/* RadeonNIGLDriver.c: one line into the bundle's log, if it has one. */
void rdn_log(const char *fmt, ...);

#endif /* RDN_GLUE_H */
