/*
 * The driver bundle's Mesa side: one Mesa context per CGL context.
 *
 * Apple's engine stays in charge of the context's life (through the
 * software renderer the bundle forwards to); what changes is where the GL
 * calls go. When the engine asks the driver to set up dispatch, the
 * application's whole table is filled with entry points that call Mesa's
 * r600. Off-screen drawables (CGLSetOffScreen) and windows: the picture is
 * rendered by the card and copied into the buffer Apple's code expects it
 * in, when the application flushes or presents. Slow for being a copy, but
 * it needs nothing from the window server.
 *
 * Mesa context binding is local to each thread.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <GL/gl.h>
#include <GL/osmesa.h>

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "rdn_engine.h"
#include "rdn_glue.h"
#include "rdn_target.h"

/* gldAttachDrawable's type for CGLSetOffScreen: kCGLPFAOffScreen. */
#define DRAWABLE_OFFSCREEN	0x35

/*
 * The engine's context (10.4.11's GLEngine): the driver's own table is at
 * this offset in it, and six words before that table is a pointer to the
 * application's GLIFunctionDispatch. See docs/GLD-INTERFACE.md.
 */
/*
 * Where the engine keeps its driver table and the pointer to the
 * application's table, and how long that table is, depend on the system:
 * gld/rdn_engine.h. On a system that is not known the engine is left alone.
 *
 * On 10.4.11 the application's table has 684 entries; the 10.4u SDK's
 * header declares 686. The CGL context's private fields follow the table,
 * so writing the last two entries corrupts them (seen as a crash in
 * CGLDestroyContext). On 10.5.9 it has 759, as its header does.
 */
#define ENGINE_CTX_TABLE_OFFSET	(engine->table_off)
#define APP_DISPATCH_BACK	(engine->disp_back)
#define APP_DISPATCH_ENTRIES	(engine->disp_entries)

/* gldAttachDrawable's type for a window: kCGLPFAWindow. */
#define DRAWABLE_WINDOW		0x50
/* gldAttachDrawable's type for CGLSetPBuffer: kCGLPFAPBuffer. */
#define DRAWABLE_PBUFFER	0x5a
/* Not Apple's: the card's own screen, for the window server's context. */
#define DRAWABLE_SCREEN		0x7570
/*
 * Not Apple's either: a program's window as a surface of the window
 * server's, shown by the card itself at the place the kext is told.
 */
#define DRAWABLE_SURFACE	0x7571
#define DRAWABLE_BACKING	0x7572
#define MAX_SCREEN_RECTS	256

/*
 * What gldAttachDrawable's record says, as far as it is understood, for
 * off-screen drawables and windows alike (docs/GLD-INTERFACE.md): words
 * 4 and 5 are width and height, word 11 the base address of the buffer to
 * draw into, word 27 the row length in pixels (the width rounded up to 16
 * for a window) in its high half and the bytes per pixel in its low half.
 * The engine owns the record and changes it in place when a window is
 * resized, so it is read again before every use.
 */
enum {
	REC_WIDTH = 4,
	REC_HEIGHT = 5,
	REC_BASE = 11,
	REC_ROW = 27
};

struct drawable {
	uint32_t width;
	uint32_t height;
	uint32_t rowbytes;
	void *base;
};

/*
 * Everything rdn_make_current binds Mesa from, as it was when it last did, so
 * that a call that would bind the same again can be left out (below).
 */
struct bind_sig {
	struct drawable d;
	OSMesaContext mesa;
	const uint32_t *record;
	long type;
	unsigned long connection, window, surface;
	int origin_x, origin_y, placed, direct, stored, backing_extent_set;
	uint32_t screen_width, screen_height, screen_pitch, screen_rects, rect_hash;
	uint32_t store_offset, store_row_bytes, store_width, store_height;
	/* From here on what the binding left in the globals; not compared. */
	int global_origin_x, global_origin_y, watch, flush_waits;
};

struct context {
	void *gld_ctx;
	/* The thread Mesa's context was last made current in. */
	pthread_t thread;
	int thread_set;
	int unpack_row_bytes;
	/* The context it shares objects with (gldCreateContext's fourth argument). */
	void *share;
	void *rend;
	OSMesaContext mesa;
	long type;
	const uint32_t *record;
	/* What Mesa is bound to now; compared with the record. */
	struct drawable drawable;
	/*
	 * For DRAWABLE_SCREEN: the part of the screen to draw on, and the
	 * rectangles of it (relative to it) that are copied there. None
	 * means all of it.
	 */
	struct drawable screen;
	uint32_t screen_rects;
	GLint screen_rect[MAX_SCREEN_RECTS][4];
	/*
	 * Or, for DRAWABLE_SCREEN, Mesa draws on the whole screen's own
	 * surface and the context's window coordinates are moved by this
	 * much, to the bottom left corner of that part (rdn_origin_x).
	 */
	int direct;
	uint32_t screen_width, screen_height, screen_pitch;
	int origin_x, origin_y;
	/*
	 * For DRAWABLE_SURFACE: the window server's names for it; origin_*
	 * is its top left on the screen once the kext knows (placed).
	 */
	unsigned long connection, window, surface;
	int placed;
	/* Its picture: video memory of our own, registered with the kext. */
	uint32_t store_offset, store_row_bytes, store_width, store_height;
	int stored, store_new, backing_extent_set;
	int bound;
	/* The window server's Core Image context has had its start state set. */
	int ws_state;
	/*
	 * The engine's context as gldCreateContext was told, so that the
	 * program's GL calls can be given to Mesa before the first drawable
	 * is attached; and whether they have been.
	 */
	void *early_rend;
	int early_ready;
	int dispatched;
	/* Bound to nothing that is shown (no drawable yet). */
	int nowhere;
	unsigned swaps;
	/* The pixel format asked for a double-buffered context (OSMesaDoubleBuffer). */
	int dbl;
	/* What Mesa was last bound from, when it can be told again (rdn_make_current). */
	struct bind_sig sig;
	int sig_valid;
	unsigned sig_age;
};

#define MAX_CONTEXTS 64
static struct context contexts[MAX_CONTEXTS];
static int resolved, usable = -1;

/* Core Animation can bind one context to several independently live surfaces. */
struct surface_store {
	uint32_t id, offset, row_bytes, width, height;
	struct surface_store *next;
};
static struct surface_store *surface_stores;
static pthread_mutex_t surface_store_lock = PTHREAD_MUTEX_INITIALIZER;


static int surface_direct(void);

/*
 * Pbuffers. CGLCreatePBuffer's object is the record gldAttachDrawable
 * gets for type 0x5a: word 2 is its surface ID, word 3 the texture
 * target, word 4 the format, words 6 and 7 the size. Its pixels are video
 * memory of ours, 64 pixels to the row's multiple, bottom row first (a
 * texture's order), made when a context first draws into it or takes it
 * as a texture. CGLDestroyPBuffer tells the driver nothing, so the memory
 * is given back when the object's address comes back with another ID, and
 * when the program ends.
 */
#define MAX_PBUFFERS 64
struct pbuffer {
	const void *key;
	uint32_t id;
	uint32_t offset, row_bytes, width, height;
	/* OSMESA_STORE_* of the pixel format, from CGLCreatePBuffer's. */
	uint32_t flags, bytes_per_pixel;
};
static struct pbuffer pbuffers[MAX_PBUFFERS];
/* The window server's records have the size in words 8 and 9, not 6 and 7. */
static int pbuffer_ws;

void rdn_mesa_pbuffer_layout(int window_server)
{
	pbuffer_ws = window_server;
}

__thread void *rdn_current_rend;

/*
 * The thread that draws, found without the thread-local lookup (which on
 * Tiger is a call to __emutls_get_address for every GL call of the program:
 * rdn_current_rend is read by every entry, RDN_ENTER). The first thread to
 * make a context current takes this, with the range of its stack; its
 * entries compare the stack pointer to the range (no other thread's is in
 * it) and the context to .rend, which is what the thread's variable holds:
 * every write to rdn_current_rend goes through rdn_set_current_rend, which
 * updates both. A thread that is not in the range, or a context that is not
 * .rend, takes the variable. The owner gives the place up by making no
 * context current. A thread that ends while it owns it leaves its range
 * behind; a thread made later with the same stack sets its own contexts
 * through the same function, and has the same answers as its variable.
 * On for The Sims 2, or RDN_FAST_REND=1; RDN_FAST_REND=0 off.
 */
struct rdn_fast_rend rdn_fast_rend;
static pthread_mutex_t fast_rend_lock = PTHREAD_MUTEX_INITIALIZER;

#if defined(__APPLE__) && defined(__ppc__)
#include <sys/resource.h>

static int fast_rend_enabled(void)
{
	static int on = -1;

	if (on < 0) {
		const char *name = getprogname(), *env = getenv("RDN_FAST_REND");

		on = name && strcmp(name, "WindowServer") &&
		     (env ? *env != '0' : !strcmp(name, "The Sims 2"));
	}
	return on;
}

static int fast_rend_mine(void)
{
	uintptr_t sp;

	__asm__("mr %0,r1" : "=r"(sp));
	return sp - rdn_fast_rend.low < rdn_fast_rend.len;
}

/* The calling thread's stack as [*low, *high); nothing if the stack pointer is not in what the C library says. */
static int fast_rend_stack(uintptr_t *low, uintptr_t *high)
{
	pthread_t self = pthread_self();
	uintptr_t top = (uintptr_t)pthread_get_stackaddr_np(self);
	uintptr_t size = pthread_get_stacksize_np(self);
	uintptr_t sp;
	struct rlimit limit;

	__asm__("mr %0,r1" : "=r"(sp));
	/* The first thread's real limit is the process's (at most 64 MB, the kernel's). */
	if (pthread_main_np() && !getrlimit(RLIMIT_STACK, &limit) &&
	    limit.rlim_cur > size && limit.rlim_cur <= (64u << 20))
		size = (uintptr_t)limit.rlim_cur;
	if (size > top || sp < top - size || sp >= top)
		return 0;
	*low = top - size;
	*high = top;
	return 1;
}

void rdn_set_current_rend(void *rend)
{
	uintptr_t low, high;

	rdn_current_rend = rend;
	if (fast_rend_mine()) {
		if (rend)
			rdn_fast_rend.rend = rend;
		else {
			rdn_fast_rend.len = 0;
			rdn_fast_rend.rend = NULL;
		}
		return;
	}
	if (!rend || rdn_fast_rend.len || !fast_rend_enabled())
		return;
	pthread_mutex_lock(&fast_rend_lock);
	if (!rdn_fast_rend.len && fast_rend_stack(&low, &high)) {
		rdn_fast_rend.rend = rend;
		rdn_fast_rend.low = low;
		__sync_synchronize();
		/* Last: from here on this thread's entries use it. */
		rdn_fast_rend.len = high - low;
	}
	pthread_mutex_unlock(&fast_rend_lock);
}
#else
void rdn_set_current_rend(void *rend)
{
	rdn_current_rend = rend;
}
#endif
int rdn_origin_x, rdn_origin_y;
long rdn_async_limit, rdn_async_piece;
int rdn_flush_waits;
int rdn_kept_now;
/* The context being made current runs with glthread. */
static int glthread;

/*
 * How the entry points hand large buffer data to this context (see
 * rdn_glue.h). Two pieces fill one of glthread's batches. RDN_GLD_NO_SPLIT
 * in the environment leaves the data whole.
 */
static void async_data(OSMesaContext mesa)
{
	static int split = -1;
	long limit = OSMesaAsyncDataLimit(mesa);

	if (split < 0)
		split = getenv("RDN_GLD_NO_SPLIT") == NULL;
	glthread = limit > 0;
	if (!split)
		limit = 0;
	rdn_async_limit = limit;
	rdn_async_piece = limit / 2;
	/* Until the drawable turns out to be memory (rdn_make_current). */
	rdn_flush_waits = 0;
}

/*
 * Draw on the screen's surface itself, unless /tmp/rdngld.copy exists:
 * then, as for windows, off-screen with a copy for every flush.
 */
static int screen_direct(void)
{
	static int known = -1;

	if (known < 0)
		known = access("/tmp/rdngld.copy", F_OK) != 0;
	return known;
}
static void (*mesa_finish)(void);
static void (*mesa_flush)(void);

static struct context *find(void *gld_ctx)
{
	int i;

	for (i = 0; i < MAX_CONTEXTS; i++)
		if (contexts[i].gld_ctx == gld_ctx)
			return gld_ctx ? &contexts[i] : NULL;
	return NULL;
}

/*
 * Mesa's context for ours, made now if it is not yet. A context that
 * shares (CGLCreateContext's second argument) is made with the other's
 * Mesa context as its share list, so that textures, programs and buffers
 * are the same objects in both; the other's is made first when it does
 * not exist yet (contexts are made on first use). Core Image does this:
 * its two contexts for pbuffers share with the program's.
 */
/* The last pixel format chosen asked for double buffering (rdn_mesa_double). */
static int pending_double;
int rdn_modern_contexts;

static OSMesaContext modern_context(OSMesaContext share)
{
	const int attribs[] = { OSMESA_FORMAT, OSMESA_BGRA,
		OSMESA_DEPTH_BITS, 24, OSMESA_STENCIL_BITS, 8,
		OSMESA_PROFILE, OSMESA_COMPAT_PROFILE,
		OSMESA_CONTEXT_MAJOR_VERSION, 4, OSMESA_CONTEXT_MINOR_VERSION, 6, 0 };
	return OSMesaCreateContextAttribs(attribs, share);
}

/* Opt in before creating any CGL contexts. The desktop keeps its legacy path. */
__attribute__((visibility("default")))
int RDNGLRequestCompatibilityContext(int major, int minor)
{
	OSMesaContext probe;
	int i;
	if (major != 4 || minor != 6 || rdn_window_server)
		return 0;
	for (i = 0; i < MAX_CONTEXTS; ++i)
		if (contexts[i].gld_ctx)
			return 0;
	probe = modern_context(NULL);
	if (!probe)
		return 0;
	OSMesaDestroyContext(probe);
	rdn_modern_contexts = 1;
	return 1;
}

__attribute__((visibility("default")))
int RDNGLMakeCurrent(void *cgl_context)
{
	GLint major = 0, minor = 0;
	void (*get)(GLenum, GLint *);
	if (!rdn_modern_contexts)
		return 0;
	rdn_mesa_early_all(cgl_context);
	if (!cgl_context)
		return OSMesaGetCurrentContext() == NULL;
	if (!OSMesaGetCurrentContext())
		return 0;
	get = (void (*)(GLenum, GLint *))OSMesaGetProcAddress("glGetIntegerv");
	if (!get)
		return 0;
	get(GL_MAJOR_VERSION, &major);
	get(GL_MINOR_VERSION, &minor);
	return major > 4 || (major == 4 && minor >= 6);
}

__attribute__((visibility("default")))
void *RDNGLGetProcAddress(const char *name)
{
	if (!rdn_modern_contexts || !name || name[0] != 'g' || name[1] != 'l' ||
					!OSMesaGetCurrentContext())
		return NULL;
	return (void *)OSMesaGetProcAddress(name);
}

static OSMesaContext mesa_for(struct context *c)
{
	OSMesaContext list = NULL;

	if (c->mesa)
		return c->mesa;
	if (c->share && c->share != c->gld_ctx) {
		struct context *o = find(c->share);

		if (o)
			list = mesa_for(o);
	}
	OSMesaDoubleBuffer(c->dbl ? GL_TRUE : GL_FALSE);

 if (rdn_modern_contexts) {
  c->mesa = modern_context(list);
 } else {
  c->mesa = OSMesaCreateContextExt(OSMESA_BGRA, 24, 8, 0, list);
 }
	OSMesaDoubleBuffer(GL_FALSE);
	if (c->mesa && list && rdn_trace)
		rdn_log("context %p shares with %p", c->gld_ctx, c->share);
	return c->mesa;
}

void rdn_mesa_context_created(void *gld_ctx, void *share)
{
	int i;

	if (!gld_ctx || find(gld_ctx))
		return;
	for (i = 0; i < MAX_CONTEXTS; i++)
		if (!contexts[i].gld_ctx) {
			memset(&contexts[i], 0, sizeof(contexts[i]));
			contexts[i].gld_ctx = gld_ctx;
			contexts[i].share = share;
			contexts[i].dbl = pending_double;
			return;
		}
	rdn_log("no room for another context; it stays with the software renderer");
}

void rdn_mesa_context_destroyed(void *gld_ctx)
{
	struct context *c = find(gld_ctx);

	if (!c)
		return;
	if (c->rend && c->rend == rdn_current_rend) {
		OSMesaMakeCurrent(NULL, NULL, GL_UNSIGNED_BYTE, 0, 0);
		rdn_set_current_rend(NULL);
	}
	if (c->mesa)
		OSMesaDestroyContext(c->mesa);
	rdn_dispatch_forget(c->rend);
	/* WindowServer owns the surface lifetime, independently of this context. */
	memset(c, 0, sizeof(*c));
}


/* Imported Mesa resources retain storage until their final GPU use. */
void rdn_mesa_pbuffer_destroyed(void *record)
{
	int i, found = 0;

	if (record)
		rdn_target_surface_buffer(((const uint32_t *)record)[2],
			0xffffffffU, 0, 0, 0);

	for (i = 0; i < MAX_CONTEXTS; i++) {
		struct context *c = &contexts[i];

		if (c->gld_ctx && c->type == DRAWABLE_PBUFFER && c->record == record) {
			c->type = 0;
			c->record = NULL;
			c->bound = 0;
			if (c->rend && c->rend == rdn_current_rend)
				rdn_set_current_rend(NULL);
		}
	}
	for (i = 0; i < MAX_PBUFFERS; i++) {
		struct pbuffer *p = &pbuffers[i];

		if (p->key != record)
			continue;
		found = 1;
		rdn_target_vram_free(p->offset);
		memset(p, 0, sizeof(*p));
	}
	if (rdn_logging) {
		unsigned live = 0, bytes = 0;

		for (i = 0; i < MAX_PBUFFERS; i++)
			if (pbuffers[i].key) {
				live++;
				bytes += pbuffers[i].row_bytes * pbuffers[i].height;
			}
		rdn_log("pbuffer %p destroyed (ours: %d); %u live, %u MB", record, found, live, bytes >> 20);
	}
}

static struct pbuffer *pbuffer_find(uint32_t id)
{
	int i;

	for (i = 0; i < MAX_PBUFFERS; i++)
		if (pbuffers[i].key && pbuffers[i].id == id)
			return &pbuffers[i];
	return NULL;
}

static int reserve_surface(uint32_t surface)
{
	uint32_t info[5];
	bool found;

	if (!rdn_target_surface_query(surface, info, &found))
		return 0;
	if (found)
		return 1;
	return rdn_target_surface_buffer(surface, 0, 0, 0, 0);
}

void rdn_mesa_prepare_pbuffer(void *cgl_ctx, void *record)
{
	void *rend = cgl_ctx ? *(void **)cgl_ctx : NULL;
	const uint32_t *r = record;
	int i;

	if (!r)
		return;
	if (!cgl_ctx && rdn_window_server) {
		rdn_log("WindowServer pbuffer 0x%x registered before attach: %d", r[2],
			reserve_surface(r[2]));
		return;
	}
	if (!rend)
		return;
	for (i = 0; i < MAX_CONTEXTS; ++i) {
		struct context *c = &contexts[i];
		if (c->gld_ctx && (c->rend == rend || c->early_rend == rend)) {
			/* Leopard creates the accelerator surface before attaching GL. */
			if (mesa_for(c))
				rdn_log("pbuffer 0x%x registered before attach: %d", r[2],
					reserve_surface(r[2]));
			return;
		}
	}
}

/*
 * The pixels of a pbuffer by its internal format (CGLCreatePBuffer's
 * fourth argument, word 4 of the record): Core Image asks for GL_RGBA16
 * (0x805b) for its intermediate pictures, and for floating point when a
 * filter wants it. Everything else is 8 bits a channel.
 */
static void pbuffer_pixels(uint32_t format, uint32_t *flags, uint32_t *bytes)
{
	switch (format) {
	case 0x805b:	/* GL_RGBA16 */
		*flags = OSMESA_STORE_RGBA16;
		*bytes = 8;
		break;
	case 0x881a:	/* GL_RGBA16F_ARB, GL_RGBA_FLOAT16_APPLE */
		*flags = OSMESA_STORE_FLOAT16;
		*bytes = 8;
		break;
	case 0x8814:	/* GL_RGBA32F_ARB, GL_RGBA_FLOAT32_APPLE */
		*flags = OSMESA_STORE_FLOAT32;
		*bytes = 16;
		break;
	default:
		*flags = 0;
		*bytes = 4;
	}
}

/* The pbuffer a record describes, its memory made if it is not yet. */
static struct pbuffer *pbuffer_for(const uint32_t *r)
{
	struct pbuffer *p = NULL, *free_slot = NULL;
	uint32_t width = r[pbuffer_ws ? 8 : 6], height = r[pbuffer_ws ? 9 : 7];
	uint32_t row_bytes, bytes, flags, per_pixel;
	int i;

	if (!width || !height)
		return NULL;
	pbuffer_pixels(r[4], &flags, &per_pixel);
	for (i = 0; i < MAX_PBUFFERS; i++) {
		struct pbuffer *q = &pbuffers[i];

		if (!q->key) {
			if (!free_slot)
				free_slot = q;
			continue;
		}
		if (q->key == r && q->id != r[2]) {
			/* The object's address is another pbuffer's now. */
			rdn_target_vram_free(q->offset);
			memset(q, 0, sizeof(*q));
			if (!free_slot)
				free_slot = q;
		} else if (q->id == r[2]) {
			p = q;
		}
	}
	if (p && p->width == width && p->height == height && p->flags == flags)
		return p;
	if (p) {
		rdn_target_vram_free(p->offset);
		memset(p, 0, sizeof(*p));
		free_slot = p;
	}
	if (!free_slot)
		return NULL;
	/* Nothing bigger than the card draws (16384 a side) or than fits 32 bits. */
	if (width > 16384 || height > 16384 ||
	    (unsigned long long)((width + 63) & ~63u) * per_pixel *
	    ((height + 63) & ~63u) > 0x7ffff000ull) {
		rdn_log("pbuffer %ux%u: too big", (unsigned)width, (unsigned)height);
		return NULL;
	}
	row_bytes = ((width + 63) & ~63u) * per_pixel;
	bytes = row_bytes * ((height + 63) & ~63u);
	bytes = (bytes + 4095) & ~4095u;
	if (!rdn_target_vram_alloc_hidden(bytes, &free_slot->offset) &&
	    !rdn_target_vram_alloc(bytes, &free_slot->offset)) {
		rdn_log("pbuffer %ux%u: no video memory", (unsigned)width, (unsigned)height);
		return NULL;
	}
	free_slot->key = r;
	free_slot->id = r[2];
	free_slot->row_bytes = row_bytes;
	free_slot->width = width;
	free_slot->height = height;
	free_slot->flags = flags;
	free_slot->bytes_per_pixel = per_pixel;

	if (rdn_trace)
		rdn_log("pbuffer 0x%x (record %p): %ux%u, format 0x%x, %u bytes a pixel, in video memory at 0x%x",
			(unsigned)r[2], (const void *)r, (unsigned)width,
			(unsigned)height, (unsigned)r[4], (unsigned)per_pixel,
			(unsigned)free_slot->offset);
	return free_slot;
}

/*
 * CGLTexImagePBuffer (hooked, rdn_hook.c): the texture bound to the
 * pbuffer's target in this context then shows the pbuffer's memory, the
 * way a texture does that is bound to a pbuffer. `pbuffer` is the record
 * gldAttachDrawable got. Returns 1 if the call is ours, with the CGL
 * error (0 if it worked) in *result.
 */
int rdn_mesa_tex_image_pbuffer(void *cgl_ctx, void *pbuffer, int source, int *result)
{
	const uint32_t *r = pbuffer;
	struct context *c = NULL;
	struct pbuffer *p;
	void *rend = cgl_ctx ? *(void **)cgl_ctx : NULL;
	int i;

	(void)source;
	for (i = 0; rend && i < MAX_CONTEXTS; i++)
		if (contexts[i].gld_ctx && contexts[i].rend == rend)
			c = &contexts[i];
	if (!c || !r || !c->mesa)
		return 0;
	p = pbuffer_find(r[2]);
	if (!p) {
		/* Not one of ours, or never drawn into or attached. */
		p = pbuffer_for(r);
		if (!p)
			return 0;
	}
	rdn_make_current(c->rend);
	if (!OSMesaTexStoreImage(c->mesa, (GLenum)r[3], RDN_TARGET_VRAM_HANDLE,
				 (GLsizei)p->row_bytes, p->offset,
				 (GLsizei)p->width, (GLsizei)p->height,
				 p->flags | OSMESA_STORE_BOTTOM_UP | OSMESA_STORE_ALPHA)) {
		rdn_log("pbuffer 0x%x as a texture: Mesa refuses (target 0x%x)",
			(unsigned)r[2], (unsigned)r[3]);
		*result = 0x2717;
		return 1;
	}
	if (rdn_trace)
		rdn_log("pbuffer 0x%x is the texture bound to 0x%x of context %p",
			(unsigned)r[2], (unsigned)r[3], c->gld_ctx);
	*result = 0;
	return 1;
}

/* The window server's cglsTexImagePBuffer: the same, with its record. */
int rdn_mesa_tex_image_pbuffer_ws(void *cgls_ctx, void *pbuffer, int source, int *result)
{
	int ret;

	rdn_mesa_pbuffer_layout(1);
	ret = rdn_mesa_tex_image_pbuffer(cgls_ctx, pbuffer, source, result);
	if (rdn_trace)
		rdn_log("cglsTexImagePBuffer(%p, %p) -> %s, %ld", cgls_ctx, pbuffer,
			ret ? "ours" : "left to OpenGL", ret ? *result : 0L);
	return ret;
}

/*
 * The window server's cglsSetInteger. Parameter 0x3e6 makes the texture
 * bound in the context (its own, in the engine, which Mesa's glBindTexture
 * never reaches) the image of a surface by ID: a pbuffer's (this is also
 * what cglsTexImagePBuffer does), or the screen's, which a layer's filter
 * wants as its backdrop. The values are the ID, the target, the format,
 * the width and height, 0x8367, the buffer (0x400 and 0x404 seen) and a
 * word.
 */
static int backing_store(struct context *c, void *backing, uint32_t stride,
                         uint32_t width, uint32_t height);

int rdn_surface_texture_hook;

int rdn_mesa_cgls_set_integer(void *cgls_ctx, int pname, int *vals, int *result)
{
	struct context *c = NULL;
	void *rend = cgls_ctx ? *(void **)cgls_ctx : NULL;
	struct pbuffer *p;
	int i;

	if (rdn_trace)
		rdn_log("cglsSetInteger(%p, 0x%lx): first value 0x%lx", cgls_ctx,
			pname, vals ? vals[0] : 0);
	if ((pname != 0x3e5 && pname != 0x3e6 && pname != 999) || !vals)
		return 0;
	if (rdn_trace)
		rdn_log("cglsSetInteger(%p, 0x%lx): %lx %lx %lx %lx %lx", cgls_ctx,
			pname, vals[0], vals[1], vals[2], vals[3], vals[4]);
	for (i = 0; rend && i < MAX_CONTEXTS; i++)
		if (contexts[i].gld_ctx && contexts[i].rend == rend)
			c = &contexts[i];
	if (!c || !c->mesa)
		return 0;
	void *backing;
	uint32_t backing_stride, backing_width, backing_height;
	int is_backing = rdn_target_surface_backing((uint32_t)vals[0], &backing,
	                                           &backing_stride, &backing_width,
	                                           &backing_height);
	if (pname == 999 && !is_backing && !pbuffer_find((uint32_t)vals[0]))
		return 0;
	if (pname == 0x3e5 || ((pname == 0x3e6 || pname == 999) && is_backing &&
	                      (uint32_t)vals[0] != c->surface)) {
		uint32_t info[5];
		bool found;
		if (is_backing) {
			struct context storage = {0};
			storage.surface = (uint32_t)vals[0];
			if (!backing_store(&storage, backing, backing_stride,
			                   backing_width, backing_height)) {
				*result = 0x2717;
				return 1;
			}
		}
		rdn_make_current(c->rend);
		if (rdn_target_surface_query((uint32_t)vals[0], info, &found) &&
		    found && info[3] && info[4]) {
			*result = (is_backing ?
				OSMesaTexStoreImage(c->mesa, (GLenum)vals[1], RDN_TARGET_VRAM_HANDLE,
				                   info[2], info[1], info[3], info[4],
				                   (vals[2] == GL_RGB || vals[2] == GL_RGB8) ?
				                   0 : OSMESA_STORE_ALPHA) :
				OSMesaTexStore(c->mesa, (GLenum)vals[1], RDN_TARGET_VRAM_HANDLE,
				               info[2], info[1], info[3], info[4])) ? 0 : 0x2717;
			rdn_log("surface 0x%lx explicitly bound to texture target 0x%lx: %ld",
				vals[0], vals[1], *result);
			return 1;
		}
		*result = 0x2717;
		return 1;
	}
	p = pbuffer_find((uint32_t)vals[0]);
	if (p) {
		/* A pbuffer: its memory is the texture. */
		rdn_make_current(c->rend);
		if (!OSMesaTexStoreImage(c->mesa, (GLenum)vals[1], RDN_TARGET_VRAM_HANDLE,
					 (GLsizei)p->row_bytes, p->offset,
					 (GLsizei)p->width, (GLsizei)p->height,
					 p->flags | OSMESA_STORE_BOTTOM_UP | OSMESA_STORE_ALPHA)) {
			*result = 0x2717;
			return 1;
		}
		*result = 0;
		return 1;
	}
	if ((c->type == DRAWABLE_SCREEN || c->type == DRAWABLE_BACKING) &&
	    (unsigned long)vals[0] == c->surface) {
		/* The screen's surface: what is drawn so far, as a texture. */
		rdn_make_current(c->rend);
		*result = OSMesaTexCopyDrawable(c->mesa, (GLenum)vals[1], c->origin_x,
						(GLint)c->screen_height - c->origin_y - (GLint)vals[4],
						(GLsizei)vals[3], (GLsizei)vals[4]) ? 0 : 0x2717;
		if (rdn_logging)
			rdn_log("the screen as a texture of context %p (%ldx%ld at %d,%d of %ux%u): %s",
				c->gld_ctx, vals[3], vals[4], c->origin_x, c->origin_y,
				(unsigned)c->screen_width, (unsigned)c->screen_height,
				*result ? "failed" : "done");
		return 1;
	}
	return 0;
}

/* Read the engine's record; false if there is nothing to draw into. */
/*
 * A surface's picture is kept in a linear buffer in video memory, 64
 * pixels to the row's multiple as the card wants of a linear render
 * target. WindowServer reads it until the surface is destroyed; resizing
 * replaces the backing without tying its lifetime to the current context.
 */
static void clear_surface_store(struct surface_store *store)
{
	store->id = store->offset = store->row_bytes = store->width = store->height = 0;
}

static void surface_store(struct context *c, uint32_t width, uint32_t height)
{
	uint32_t row_bytes = ((width + 63) & ~63u) * 4;
	uint32_t rows = (height + 63) & ~63u;
	uint32_t info[5];
	struct surface_store *store = NULL, *empty = NULL;
	bool found;

	c->stored = 0;
	c->store_new = 0;
	if (!c->surface || !width || !height || width > 16384 || height > 16384)
		return;
	pthread_mutex_lock(&surface_store_lock);
	/* Reclaim only after WindowServer has removed the surface. */
	for (struct surface_store *s = surface_stores; s; s = s->next) {
		if (s->id && s->id != c->surface &&
		    rdn_target_surface_query(s->id, info, &found) && !found) {
			rdn_target_vram_free(s->offset);
			clear_surface_store(s);
		}
		if (s->id == c->surface)
			store = s;
		else if (!s->id && !empty)
			empty = s;
	}
	if (store && (store->width != width || store->height != height)) {
		if (!rdn_target_surface_buffer(store->id, 0, 0, 0, 0))
			goto out;
		rdn_target_vram_free(store->offset);
		clear_surface_store(store);
	}
	if (!store)
		store = empty;
	if (!store) {
		store = calloc(1, sizeof(*store));
		if (!store)
			goto out;
		store->next = surface_stores;
		surface_stores = store;
	}
	if (!store->id) {
		if (!rdn_target_vram_alloc((row_bytes * rows + 4095) & ~4095u, &store->offset))
			goto out;
		c->store_new = 1;
		store->id = (uint32_t)c->surface;
		store->row_bytes = row_bytes;
		store->width = width;
		store->height = height;
	}
	if (!rdn_target_surface_buffer(store->id, store->offset,
				      store->row_bytes, width, height)) {
		if (c->store_new) {
			rdn_target_vram_free(store->offset);
			clear_surface_store(store);
		}
		goto out;
	}
	c->store_offset = store->offset;
	c->store_row_bytes = store->row_bytes;
	c->store_width = width;
	c->store_height = height;
	c->stored = 1;
out:
	pthread_mutex_unlock(&surface_store_lock);
}

/* CPU-drawn backings also need storage when first imported as textures. */
static int backing_store(struct context *c, void *backing, uint32_t stride,
                         uint32_t width, uint32_t height)
{
	surface_store(c, width, height);
	if (!c->stored) return 0;
	if (c->store_new) {
		uint8_t *gpu = rdn_target_vram_map(c->store_offset);
		if (!gpu) return 0;
		for (uint32_t y = 0; y < height; ++y)
			memcpy(gpu + y * c->store_row_bytes,
			       (uint8_t *)backing + y * stride, width * 4);
		__sync_synchronize();
	}
	return 1;
}

/*
 * Where the window server has the surface now, from the kext: size as the
 * drawable, place and rectangles kept in the context. The base only says
 * that there is one.
 */
static int surface_drawable(struct context *c, struct drawable *d)
{
	static int16_t rects[MAX_SCREEN_RECTS][4];
	volatile uint32_t *pixels;
	uint32_t count = 0, i;
	unsigned width, height;
	int32_t b[4];

	memset(d, 0, sizeof(*d));
	if (!c->mesa ||
	    !rdn_target_screen(&pixels, &c->screen_width, &c->screen_height,
			       &c->screen_pitch))
		return 0;
	if (!c->window) {
		/* Quartz-owned surfaces have no CGS window to query. */
		if (!rdn_target_surface_region((uint32_t)c->surface, b, rects,
		                              MAX_SCREEN_RECTS, &count) ||
		    b[2] <= 0 || b[3] <= 0)
			return 0;
		width = b[2];
		height = b[3];
	} else if (!rdn_surface_size(c->connection, c->window, c->surface,
	                             &width, &height)) {
		return 0;
	}
	d->width = !c->window && c->backing_extent_set ? c->screen.width : width;
	d->height = !c->window && c->backing_extent_set ? c->screen.height : height;
	d->base = (void *)pixels;

	/*
	 * The place and the visible part come from the kext, once the
	 * window server has told it. Until then the program can draw; nothing
	 * is shown.
	 */
	c->screen_rects = 0;
	c->placed = 0;
	if (!c->window)
		return 1;
	if (!rdn_target_surface_region((uint32_t)c->surface, b, rects,
				       MAX_SCREEN_RECTS, &count) ||
	    b[2] <= 0 || b[3] <= 0 || count > MAX_SCREEN_RECTS)
		return 1;
	c->origin_x = b[0];
	c->origin_y = b[1];
	/* Experiment: RDN_GLD_SURFACE_AT=x shows the surface x pixels further right. */
	if (getenv("RDN_GLD_SURFACE_AT"))
		c->origin_x += atoi(getenv("RDN_GLD_SURFACE_AT"));
	for (i = 0; i < count; i++) {
		c->screen_rect[i][0] = rects[i][0] - b[0];
		c->screen_rect[i][1] = rects[i][1] - b[1];
		c->screen_rect[i][2] = rects[i][2];
		c->screen_rect[i][3] = rects[i][3];
	}
	c->screen_rects = count;
	c->placed = 1;
	return 1;
}

static int read_record(const struct context *c, struct drawable *d)
{
	const uint32_t *r = c->record;

	if (c->type == DRAWABLE_SCREEN || c->type == DRAWABLE_BACKING) {
		*d = c->screen;
		return d->base != NULL;
	}
	if (c->type == DRAWABLE_SURFACE)
		return surface_drawable((struct context *)c, d);
	memset(d, 0, sizeof(*d));
	if (c->type == DRAWABLE_PBUFFER) {
		struct pbuffer *p = r ? pbuffer_for(r) : NULL;

		if (!p)
			return 0;
		d->width = p->width;
		d->height = p->height;
		d->rowbytes = p->row_bytes;
		d->base = p;
		return 1;
	}
	if (!r || (c->type != DRAWABLE_OFFSCREEN && c->type != DRAWABLE_WINDOW))
		return 0;
	if (c->type == DRAWABLE_OFFSCREEN) {
		/*
		 * CGLSetOffScreen's record is the program's own: width,
		 * height, row bytes and base in its first four words. The
		 * engine's later words (REC_*) are not filled for it.
		 */
		d->width = r[0];
		d->height = r[1];
		d->rowbytes = r[2];
		d->base = *(void *const *)((const char *)r + (sizeof(void *) == 8 ? 16 : 12));
		return d->base && d->width && d->height &&
		       d->rowbytes >= d->width * 4;
	}
	d->width = r[REC_WIDTH];
	d->height = r[REC_HEIGHT];
	d->rowbytes = (r[REC_ROW] >> 16) * (r[REC_ROW] & 0xffff);
	d->base = (void *)(uintptr_t)r[REC_BASE];
	return d->base && d->width && d->height && (r[REC_ROW] & 0xffff) == 4 &&
	       d->rowbytes >= d->width * 4;
}

int rdn_mesa_attach(void *gld_ctx, long type, const void *drawable)
{
	struct context *c = find(gld_ctx);
	struct drawable d;

	if (!c)
		return 1;
	c->type = type;
	c->record = drawable;
	c->bound = 0;
	/* The next GL call binds the new drawable. */
	if (c->rend && c->rend == rdn_current_rend)
		rdn_set_current_rend(NULL);
	read_record(c, &d);
	rdn_log("attach: context %p, type 0x%lx, %ux%u, rowbytes %u, base %p",
		gld_ctx, type, (unsigned)d.width, (unsigned)d.height,
		(unsigned)d.rowbytes, d.base);
	if (type == DRAWABLE_PBUFFER && !d.base) {
		/*
		 * No memory for it (or a size no card has): the call fails
		 * (kCGLBadAlloc) and the context keeps what it had, instead
		 * of drawing into a dummy where nothing is ever seen.
		 */
		c->type = 0;
		c->record = NULL;
		return 0;
	}
	return 1;
}

int rdn_mesa_expect_surface(void *gld_ctx, unsigned long surface)
{
	struct context *c = find(gld_ctx);

	if (!c) {
		rdn_log("surface 0x%lx: no Mesa context for %p", surface, gld_ctx);
		return 0;
	}
	if (!c->mesa && !mesa_for(c)) {
		rdn_log("surface 0x%lx: Mesa context creation failed", surface);
		return 0;
	}
	/* Rebinding must preserve the image used by the kernel's direct flush. */
	if (!reserve_surface((uint32_t)surface)) {
		rdn_log("surface 0x%lx: buffer announcement failed", surface);
		return 0;
	}
	return 1;
}

int rdn_mesa_attach_surface(void *gld_ctx, unsigned long connection,
			    unsigned long window, unsigned long surface)
{
	struct context *c = find(gld_ctx);

	if (!c)
		return 0;
	/* The device is opened with the first Mesa context. */
	if (!c->mesa)
		mesa_for(c);
	if (!c->mesa)
		return 0;
	/* Windowless Quartz contexts draw into their registered CPU backing. */
	if (!window && surface > 1 && rdn_mesa_attach_screen(gld_ctx, surface))
		return 1;
	if (c->surface != surface)
		c->stored = 0;
	c->type = DRAWABLE_SURFACE;
	c->backing_extent_set = 0;
	c->connection = connection;
	c->window = window;
	c->surface = surface;
	c->record = NULL;
	c->bound = 0;
	if (c->rend && c->rend == rdn_current_rend)
		rdn_set_current_rend(NULL);
	rdn_log("attach: context %p draws on surface 0x%lx", gld_ctx, surface);
	return 1;
}

int rdn_mesa_is_pbuffer(void *gld_ctx)
{
	struct context *c = find(gld_ctx);

	return c && c->type == DRAWABLE_PBUFFER;
}

int rdn_mesa_is_surface(void *gld_ctx)
{
	struct context *c = find(gld_ctx);

	/* A surface, or the whole screen in a program's hands. */
	return c && (c->type == DRAWABLE_SURFACE ||
		     (c->type == DRAWABLE_SCREEN && !rdn_window_server));
}

void rdn_mesa_detach(void *gld_ctx)
{
	struct context *c = find(gld_ctx);

	if (!c)
		return;
	if (c->type == DRAWABLE_PBUFFER && c->mesa && c->bound) {
		/* An imported pbuffer can outlive its producer's drawable binding. */
		if (OSMesaAdoptContext(c->mesa)) {
			OSMesaFlushRender(c->mesa, GL_TRUE);
			rdn_set_current_rend(NULL);
		}
	}
	if (c->type == DRAWABLE_SURFACE && c->stored) {
		/* The compositor retains this surface across context detach/reattach. */
		if (c->mesa && OSMesaGetCurrentContext() == c->mesa)
			OSMesaMakeCurrent(NULL, NULL, GL_UNSIGNED_BYTE, 0, 0);
	}
	c->type = 0;
	c->record = NULL;
	c->bound = 0;
	if (c->rend && c->rend == rdn_current_rend)
		rdn_set_current_rend(NULL);
}

/*
 * The window server composes a frame in several passes, each ended by a
 * glFlush that copies to the screen, and the frame is only whole after the
 * last: the pass with the filter's backdrop is shown without the widget
 * that is drawn over it next, for as long as that pass takes (a few
 * milliseconds: a flicker). So the copies are put off and merged
 * (OSMesaDeferPresent) and made when a few milliseconds have passed
 * with the window server's thread waiting for a message (mach_msg with a
 * timeout, rdn_hook_mach_msg; it has no run loop for a timer), or when it
 * calls for a message and the copies are older than that. Until that
 * thread has been seen to wait many times, every flush copies at once as
 * before.
 */
#include <pthread.h>

#include <sys/time.h>

#define DEFER_MS 6

static pthread_t defer_thread;
static int defer_set, defer_on, defer_pending;
static unsigned defer_idle_calls;
static struct timeval defer_first;
static OSMesaContext defer_ctx;

static void defer_hook(OSMesaContext m)
{
	defer_ctx = m;
	if (!defer_pending) {
		defer_pending = 1;
		gettimeofday(&defer_first, NULL);
	}
}

static void defer_now(void)
{
	int i;

	defer_pending = 0;
	for (i = 0; defer_ctx && i < MAX_CONTEXTS; i++)
		if (contexts[i].gld_ctx && contexts[i].mesa == defer_ctx) {
			rdn_make_current(contexts[i].rend);
			OSMesaPresentPending(defer_ctx);
			break;
		}
}

/* The window server's thread is about to wait for a message. */
static unsigned defer_idle(void)
{
	struct timeval now;
	long age_us;

	if (!defer_set || !pthread_equal(pthread_self(), defer_thread))
		return 0;
	if (!defer_on) {
		/* Seen to go idle between frames, often enough. */
		if (++defer_idle_calls == 10) {
			defer_on = 1;
			OSMesaDeferPresent(GL_TRUE, defer_hook);
			rdn_log("the window server's thread goes idle: copies to the screen are put off for up to %d ms", DEFER_MS);
		}
		return 0;
	}
	if (!defer_pending)
		return 0;
	gettimeofday(&now, NULL);
	age_us = (now.tv_sec - defer_first.tv_sec) * 1000000L + (now.tv_usec - defer_first.tv_usec);
	if (age_us >= DEFER_MS * 1000L) {
		defer_now();
		return 0;
	}
	return (unsigned)((DEFER_MS * 1000L - age_us + 999) / 1000);
}

/* The time ran out with no message: the frame is whole. */
static void defer_timeout(void)
{
	if (defer_pending)
		defer_now();
}

static void defer_setup(void)
{
	if (defer_set || access("/Library/Application Support/RadeonNI/nodefer", F_OK) == 0)
		return;
	defer_thread = pthread_self();
	defer_set = 1;
	rdn_hook_mach_msg(defer_idle, defer_timeout);
}

/* Quartz's first viewport gives the unpadded backing extent. */
void rdn_mesa_backing_viewport(void *rend, int x, int y, int width, int height)
{
	int i;
	if (x || y || width <= 0 || height <= 0) return;
	for (i = 0; i < MAX_CONTEXTS; ++i) {
		struct context *c = &contexts[i];
		if (!c->gld_ctx || c->rend != rend ||
		    (c->type != DRAWABLE_BACKING &&
		     (c->type != DRAWABLE_SURFACE || c->window)) ||
		    c->backing_extent_set || (uint32_t)width > c->store_width ||
		    (uint32_t)height > c->store_height)
			continue;
		c->backing_extent_set = 1;
		if ((uint32_t)width == c->screen.width && (uint32_t)height == c->screen.height)
			return;
		c->screen.width = width;
		c->screen.height = height;
		if (c->type == DRAWABLE_BACKING) {
			c->screen_width = width;
			c->screen_height = height;
		}
		rdn_set_current_rend(NULL);
		rdn_make_current(rend);
		return;
	}
}

int rdn_mesa_attach_screen(void *gld_ctx, unsigned long surface)
{
	static int16_t rects[MAX_SCREEN_RECTS][4];
	uint32_t count = 0, i;
	int32_t b[4];
	struct context *c = find(gld_ctx);
	volatile uint32_t *pixels;
	uint32_t width, height, pitch;

	if (!c)
		return 0;
	/* The device is opened with the first Mesa context. */
	if (!c->mesa)
		mesa_for(c);
	if (!c->mesa || !rdn_target_screen(&pixels, &width, &height, &pitch)) {
		rdn_log("attach: context %p cannot reach the screen", gld_ctx);
		return 0;
	}
	void *backing;
	uint32_t backing_stride, backing_width, backing_height;
	if (surface != 1 && rdn_target_surface_backing((uint32_t)surface, &backing,
	                                              &backing_stride, &backing_width,
	                                              &backing_height)) {
		c->surface = surface;
		if (!backing_store(c, backing, backing_stride, backing_width, backing_height))
			return 0;
		c->type = DRAWABLE_BACKING;
		c->backing_extent_set = 0;
		c->record = NULL;
		c->bound = 0;
		c->origin_x = c->origin_y = 0;
		c->screen_rects = 0;
		c->screen.width = c->screen_width = backing_width;
		c->screen.height = c->screen_height = backing_height;
		c->screen.rowbytes = backing_stride;
		c->screen.base = backing;
		c->screen_pitch = c->store_row_bytes / 4;
		if (c->rend == rdn_current_rend) rdn_set_current_rend(NULL);
		rdn_log("attach: context %p draws into backing surface %lu, %ux%u",
		        gld_ctx, surface, backing_width, backing_height);
		return 1;
	}
	/* Unknown surfaces must never alias the display's scanout. */
	if (surface != 1 && (surface || rdn_window_server)) return 0;
	c->type = DRAWABLE_SCREEN;
	c->surface = surface;
	if (rdn_window_server && rdn_ws_core_image())
		defer_setup();
	c->record = NULL;
	c->bound = 0;
	c->screen_rects = 0;
	if (!rdn_target_surface_region((uint32_t)surface, b, rects,
				       MAX_SCREEN_RECTS, &count) ||
	    b[0] < 0 || b[1] < 0 || b[2] <= 0 || b[3] <= 0 ||
	    (uint32_t)(b[0] + b[2]) > width || (uint32_t)(b[1] + b[3]) > height) {
		b[0] = b[1] = 0;
		b[2] = (int32_t)width;
		b[3] = (int32_t)height;
	} else if (count <= MAX_SCREEN_RECTS) {
		/*
		 * The window server draws only inside the shape; the rest
		 * of the box around it must stay as it is on the screen.
		 */
		for (i = 0; i < count; i++) {
			/* Screen coordinates when drawing direct. */
			c->screen_rect[i][0] = rects[i][0] - (screen_direct() ? 0 : b[0]);
			c->screen_rect[i][1] = rects[i][1] - (screen_direct() ? 0 : b[1]);
			c->screen_rect[i][2] = rects[i][2];
			c->screen_rect[i][3] = rects[i][3];
		}
		c->screen_rects = count;
	}
	c->direct = screen_direct();
	c->screen_width = width;
	c->screen_height = height;
	c->screen_pitch = pitch;
	c->origin_x = b[0];
	c->origin_y = (int)height - (b[1] + b[3]);
	c->screen.width = (uint32_t)b[2];
	c->screen.height = (uint32_t)b[3];
	c->screen.rowbytes = pitch * 4;
	c->screen.base = (void *)(pixels + (uint32_t)b[1] * pitch + (uint32_t)b[0]);
	if (c->rend && c->rend == rdn_current_rend)
		rdn_set_current_rend(NULL);
	rdn_log("attach: context %p draws on the screen at %d,%d %dx%d (surface %lu, %u rectangles, %s)",
		gld_ctx, (int)b[0], (int)b[1], (int)b[2], (int)b[3], surface,
		(unsigned)count, c->direct ? "direct" : "copied");
	return 1;
}

static void *lookup(const char *name)
{
	return (void *)OSMesaGetProcAddress(name);
}

void *rdn_mesa_lookup_missing(const char *name, void *cgl_ctx)
{
	void *function;
	int i;

	if (!name || name[0] != 'g' || name[1] != 'l' || !cgl_ctx)
		return NULL;
	function = lookup(name);
	if (!function)
		return NULL;
	for (i = 0; i < MAX_CONTEXTS; ++i) {
		struct context *c = &contexts[i];
		if (c->gld_ctx && c->rend == *(void **)cgl_ctx) {
			rdn_make_current(c->rend);
			return OSMesaGetCurrentContext() == c->mesa ? function : NULL;
		}
	}
	return NULL;
}

static void missing(const char *name)
{
	rdn_log("Mesa has no %s; the engine keeps that entry", name);
}

/* Mesa's functions looked up, once. False if there is no card to use. */
extern void (*osmesa_present_log)(int rects, unsigned long sum, const uint8_t *rgb, int w, int h);
extern int osmesa_present_area[4];
static void present_log(int rects, unsigned long sum, const uint8_t *rgb, int w, int h)
{
	static int count = -1;
	char name[64];
	FILE *f;
	int i;

	if (count < 0)
		count = access("/tmp/rdngld.dump", F_OK) == 0 ? 0 : -2;
	if (rects < 0) {
		rdn_log("FLUSH of the screen context, %d rectangles", -rects);
		return;
	}
	rdn_log("PRESENT to the screen, %d copies, sum %lu, frame %d", rects, sum, count >= 0 ? count : -1);
	if (count < 0 || !rgb || w <= 0 || h <= 0)
		return;
	snprintf(name, sizeof(name), "/tmp/rdnframes/%05d.ppm", count++);
	f = fopen(name, "wb");
	if (!f)
		return;
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (i = 0; i < h; i++)
		fwrite(rgb + i * 160 * 3, 3, (size_t)w, f);
	fclose(f);
}

/*
 * Programs written for Apple's OpenGL 2.1 that need more of it when Mesa
 * says 4.6: Gecko (PowerFox) asks for glGetStringi from a 3.0 or newer
 * context, and Leopard's OpenGL framework does not export it, so the
 * browser stops itself at start. A program listed in the file
 * RDN_GLVERSION_LIST (a line each: the program's name as the system has
 * it; "-name" and "*" as for glthread) sees 2.1 and GLSL 1.20, which is
 * what Apple's engine reports there. RDN_GL_VERSION21=1 or 0 in a
 * program's environment decides for it. No other program is changed.
 */
#define RDN_GLVERSION_LIST "/Library/Application Support/RadeonNI/glversion21"

/* Is this program named in the file, or has the variable (1 or 0) been set? */
static int program_listed(const char *var, const char *path)
{
	const char *env = getenv(var);
	const char *name = getprogname();
	int named = -1, all = -1;
	char line[256];
	FILE *f;

	if (env)
		return strcmp(env, "0") != 0;
	f = fopen(path, "r");
	if (!f)
		return 0;
	while (fgets(line, sizeof(line), f)) {
		const char *who = line;
		int on = 1;

		line[strcspn(line, "\r\n")] = 0;
		if (*who == '-') {
			on = 0;
			who++;
		}
		if (!strcmp(who, "*"))
			all = on;
		else if (name && !strcmp(who, name))
			named = on;
	}
	fclose(f);
	return named >= 0 ? named : all > 0;
}

static int want_version21(void)
{
	return program_listed("RDN_GL_VERSION21", RDN_GLVERSION_LIST);
}

/*
 * A program named in RDN_SOFTWARE_LIST (same lines as the list above), or
 * started with RDN_SOFTWARE=1, is left to Apple's software renderer: Mesa
 * and the card are not used for it. To tell a program's trouble from ours.
 */
#define RDN_SOFTWARE_LIST "/Library/Application Support/RadeonNI/software"

static int mesa_ready(void)
{
	unsigned n;

	if (usable < 0) {
		if (program_listed("RDN_SOFTWARE", RDN_SOFTWARE_LIST)) {
			usable = 0;
			rdn_log("Mesa is not used for this program (RDN_SOFTWARE): Apple's software renderer");
		}
	}
	if (usable < 0) {
		/* Before Mesa makes its first context: it reads the override once. */
		if (want_version21()) {
			setenv("MESA_GL_VERSION_OVERRIDE", "2.1", 0);
			setenv("MESA_GLSL_VERSION_OVERRIDE", "120", 0);
			rdn_log("GL 2.1 and GLSL 1.20 for this program (%s)", RDN_GLVERSION_LIST);
		}
		/* A context to find out whether the card is there at all. */
		OSMesaContext probe = OSMesaCreateContextExt(OSMESA_BGRA, 24, 8, 0, NULL);

		usable = probe != NULL;
		if (probe)
			OSMesaDestroyContext(probe);
		rdn_log("Mesa on the card: %s", usable ? "available" : "not available");
	}
	if (!usable)
		return 0;
	if (!resolved) {
		mesa_finish = (void (*)(void))OSMesaGetProcAddress("glFinish");
		if (rdn_logging) {
			osmesa_present_log = present_log;
			/* Summing a part of every frame (and dumping it) slows a present down: only with /tmp/rdngld.dump. */
			if (access("/tmp/rdngld.dump", F_OK) == 0) {
				osmesa_present_area[0] = 560;
				osmesa_present_area[1] = 380;
				osmesa_present_area[2] = 640;
				osmesa_present_area[3] = 330;
			}
		}
		if (rdn_ws_core_image())
			OSMesaPresentOnSwitch(GL_FALSE);
		mesa_flush = (void (*)(void))OSMesaGetProcAddress("glFlush");
		n = rdn_dispatch_resolve(lookup, missing);
		rdn_log("Mesa provides %u of the %u GL entry points", n,
			rdn_dispatch_entries);
		resolved = 1;
	}
	return 1;
}

static int install(struct context *c, void *engine_table, const char *when)
{
	const struct rdn_engine_layout *engine = rdn_engine_layout();
	void *app_table;
	unsigned n;

	if (!engine)
		return 0;	/* a system whose engine is not known: leave it alone */
	app_table = *(void **)((char *)engine_table - APP_DISPATCH_BACK);
	if (!app_table || !mesa_ready())
		return 0;
	c->rend = (char *)engine_table - ENGINE_CTX_TABLE_OFFSET;
	if (getenv("RDN_GLD_MASTER")) {
		/*
		 * Experiment: does the engine keep, inside its context, the
		 * table it fills the program's from? Look for the program's
		 * first entries there.
		 */
		void **t = app_table, **r = (void **)c->rend;
		unsigned i;

		rdn_log("table %p starts %p %p %p %p (%s)", app_table, t[0], t[1],
			t[2], t[3], when);
		for (i = 0; i + 4 <= (ENGINE_CTX_TABLE_OFFSET + 0x80) / 4; i++)
			if (r[i] == t[0] && r[i + 1] == t[1] && r[i + 2] == t[2] &&
			    r[i + 3] == t[3] && (void **)&r[i] != t)
				rdn_log("  the same four at engine context + 0x%x", i * 4);
	}
	if (c->dispatched && getenv("RDN_GLD_MASTER")) {
		/* Experiment: which entries has the engine taken back? */
		static void *before[RDN_MAX_DISP_ENTRIES];
		void **t = app_table;
		unsigned i, changed = 0, first = 0;

		memcpy(before, t, APP_DISPATCH_ENTRIES * sizeof(void *));
		rdn_dispatch_install(app_table, APP_DISPATCH_ENTRIES);
		for (i = 0; i < APP_DISPATCH_ENTRIES; i++)
			if (before[i] != t[i] && !changed++)
				first = i;
		if (changed)
			rdn_log("  the engine had taken back %u entries, the first at index %u (%s)",
				changed, first, when);
	}
	/* Not before the context has a drawable: see rdn_glue.h. */
	rdn_kept_now = c->type != 0;
	n = rdn_dispatch_install(app_table, APP_DISPATCH_ENTRIES);
	rdn_kept_now = 0;
	if (!c->dispatched || rdn_trace)
		rdn_log("context %p (engine %p): %u entries of table %p are Mesa's (%s)",
			c->gld_ctx, c->rend, n, app_table, when);
	c->dispatched = 1;
	return 1;
}

int rdn_mesa_dispatch(void *gld_ctx, void *engine_table)
{
	struct context *c = find(gld_ctx);
	struct drawable d;

	/*
	 * A context taken over early stays Mesa's whatever its drawable,
	 * and a surface is always ours.
	 */
	if (!c || (!c->dispatched && c->type != DRAWABLE_SURFACE &&
		   !read_record(c, &d)))
		return 0;
	return install(c, engine_table, "dispatch set-up");
}

int rdn_mesa_unpack_row_bytes(void *rend, int value)
{
	int i;
	for (i = 0; i < MAX_CONTEXTS; ++i)
		if (contexts[i].gld_ctx && contexts[i].rend == rend) {
			if (value >= 0) contexts[i].unpack_row_bytes = value;
			return contexts[i].unpack_row_bytes;
		}
	return 0;
}

void rdn_mesa_context_engine(void *gld_ctx, void *rend)
{
	struct context *c = find(gld_ctx);

	if (c && !c->early_rend)
		c->early_rend = rend;
}

void rdn_mesa_early_all(void *cgl_ctx)
{
	int i;

	/* What the glue holds back for the context that is current now is made first. */
	rdn_dispatch_flush_pending();
	if (!cgl_ctx) {
		OSMesaContext m = OSMesaGetCurrentContext();

		/*
		 * The thread lets go of its context. Mesa's current context
		 * is per thread: let go of it here too, which makes glthread
		 * run what this thread recorded while it is still the
		 * context's thread (a program that goes on in another
		 * thread, like Quake 3 with r_smp, would else have the
		 * second thread run it, with no current context there).
		 * Not when another thread has taken the context since: Mesa
		 * would unbind it there.
		 */
		for (i = 0; m && i < MAX_CONTEXTS; i++)
			if (contexts[i].gld_ctx && contexts[i].mesa == m) {
				if (pthread_equal(contexts[i].thread, pthread_self()))
					OSMesaMakeCurrent(NULL, NULL, GL_UNSIGNED_BYTE, 0, 0);
				break;
			}
		rdn_set_current_rend(NULL);
		return;
	}
	for (i = 0; i < MAX_CONTEXTS; i++)
		if (contexts[i].gld_ctx) {
			if (contexts[i].early_rend == *(void **)cgl_ctx)
				contexts[i].early_ready = 1;
			rdn_mesa_early(contexts[i].gld_ctx);
		}
	/* By-name entrypoints bypass the engine's per-call context wrapper. */
	if (!rdn_window_server) {
		for (i = 0; i < MAX_CONTEXTS; ++i)
			if (contexts[i].gld_ctx && contexts[i].rend == *(void **)cgl_ctx) {
				rdn_make_current(contexts[i].rend);
				return;
			}
		OSMesaMakeCurrent(NULL, NULL, GL_UNSIGNED_BYTE, 0, 0);
		rdn_set_current_rend(NULL);
	}
}

void rdn_mesa_early(void *gld_ctx)
{
	struct context *c = find(gld_ctx);

	/*
	 * Again at every call until a drawable is attached: the engine is
	 * still filling the program's table while the context is being
	 * made.
	 */
	if (!c || !c->early_rend || c->type)
		return;
	/* Leopard 10.5.8 publishes the GL table after gldCreateContext returns. */
	if (rdn_engine_layout() && !strcmp(rdn_engine_layout()->os, "10.5.8") &&
	    !c->early_ready) {
		static void *(*get_current)(void);
		void *current;

		/* Clients may cache CGLSetCurrentContext before this bundle loads. */
		if (!get_current)
			get_current = (void *(*)(void))dlsym(RTLD_DEFAULT, "CGLGetCurrentContext");
		current = get_current ? get_current() : NULL;
		if (!current || *(void **)current != c->early_rend)
			return;
		c->early_ready = 1;
	}
	if (rdn_engine_layout())
		install(c, (char *)c->early_rend + rdn_engine_layout()->table_off,
			"before a drawable");
}

static void make_current_slow(void *rend)
{
	struct context *c = NULL;
	struct drawable d;
	int i;

	for (i = 0; i < MAX_CONTEXTS; i++)
		if (contexts[i].gld_ctx && contexts[i].rend == rend)
			c = &contexts[i];
	/*
	 * A mode set changes the screen the window server's contexts draw
	 * on (size and pitch; the device asks the kext at most every 20 ms):
	 * attach it again, or the picture goes into the old shape.
	 */
	if (c && c->type == DRAWABLE_SCREEN && rdn_window_server) {
		volatile uint32_t *px;
		uint32_t w, h, p;

		if (rdn_target_screen(&px, &w, &h, &p) &&
		    (w != c->screen_width || h != c->screen_height || p != c->screen_pitch)) {
			rdn_log("the screen is now %ux%u, pitch %u: attached again", (unsigned)w,
				(unsigned)h, (unsigned)p);
			rdn_mesa_attach_screen(c->gld_ctx, c->surface);
		}
	}
	if (!c) {
		rdn_log("GL call on engine context %p, which is not ours", rend);
		return;
	}
	if (!c->mesa)
		mesa_for(c);
	if (!c->mesa)
		return;
	async_data(c->mesa);
	/*
	 * Another thread of the program takes the context over while it is
	 * attached already (Quake 3's render thread; its main thread draws
	 * the intro): this thread only becomes Mesa's current one.
	 */
	if (c->bound && !c->nowhere && c->thread_set && !pthread_equal(c->thread, pthread_self()) &&
	    !rdn_window_server && !getenv("RDN_GLD_NO_ADOPT")) {
		if (OSMesaAdoptContext(c->mesa)) {
			c->thread = pthread_self();
			if (rdn_trace)
				rdn_log("Mesa adopted in this thread for context %p", c->gld_ctx);
			rdn_set_current_rend(rend);
			return;
		}
	}
	c->thread = pthread_self();
	c->thread_set = 1;
	if (rdn_trace)
		rdn_log("Mesa made current in this thread for context %p", c->gld_ctx);
	if (rdn_trace) {
		int ok = read_record(c, &d);

		rdn_log("make current: context %p type 0x%lx record %p: %s, %ux%u base %p row %u",
			c->gld_ctx, c->type, (const void *)c->record,
			ok ? "drawable" : "no drawable", (unsigned)d.width,
			(unsigned)d.height, d.base, (unsigned)d.rowbytes);
		if (c->record) {
			int k;

			for (k = 0; k < 32; k += 8)
				rdn_log("  record+%02x: %08x %08x %08x %08x %08x %08x %08x %08x",
					k * 4, c->record[k], c->record[k + 1],
					c->record[k + 2], c->record[k + 3],
					c->record[k + 4], c->record[k + 5],
					c->record[k + 6], c->record[k + 7]);
		}
	}
	if (!read_record(c, &d)) {
		/*
		 * No drawable yet: programs load textures and build display
		 * lists before their window exists. Mesa needs something
		 * to be bound to; nothing drawn now is ever shown.
		 */
		static uint32_t *nowhere;
		static int side = 16;

		if (!nowhere) {
			/* Experiment: RDN_GLD_DUMMY=n makes it n pixels a side. */
			if (getenv("RDN_GLD_DUMMY"))
				side = atoi(getenv("RDN_GLD_DUMMY"));
			nowhere = calloc((size_t)side * side, 4);
			if (!nowhere)
				return;
		}

		/*
		 * rdn_current_rend stays as it is, so that every GL call
		 * comes back here and finds the drawable as soon as there
		 * is one (the engine fills an off-screen record only after
		 * gldAttachDrawable has returned).
		 */
		if (c->nowhere && OSMesaGetCurrentContext() == c->mesa)
			return;
		rdn_origin_x = rdn_origin_y = 0;
		rdn_set_current_rend(NULL);
		if (!OSMesaMakeCurrent(c->mesa, nowhere, GL_UNSIGNED_BYTE, side, side))
			return;
		OSMesaPixelStore(OSMESA_ROW_LENGTH, side);
		OSMesaReadbackRects(c->mesa, 0, NULL);
		c->bound = 0;
		c->nowhere = 1;
		return;
	}
	rdn_origin_x = rdn_origin_y = 0;
	if (c->type == DRAWABLE_BACKING) {
		if (!OSMesaMakeCurrentStore(c->mesa, RDN_TARGET_VRAM_HANDLE,
		                            c->store_row_bytes, c->store_offset,
		                            d.width, d.height, 0)) return;
		if (!OSMesaStoreBacking(c->mesa, d.base, d.rowbytes)) return;
		OSMesaReadbackRects(c->mesa, 0, NULL);
		c->drawable = d;
		c->bound = 1;
		c->nowhere = 0;
		rdn_watch = 0;
		rdn_set_current_rend(rend);
		return;
	}
	if (c->type == DRAWABLE_SURFACE) {
		if (!c->window && c->backing_extent_set)
			surface_store(c, c->store_width, c->store_height);
		else
			surface_store(c, d.width, d.height);
		OSMesaSurfaceStorage(c->mesa, c->stored ? RDN_TARGET_VRAM_HANDLE : 0,
				     (GLsizei)c->store_row_bytes, c->store_offset);
		if (!OSMesaMakeCurrentSurface(c->mesa, RDN_TARGET_SCREEN_HANDLE,
					      (GLsizei)c->screen_width,
					      (GLsizei)c->screen_height,
					      (GLsizei)(c->screen_pitch * 4), 0,
					      c->origin_x, c->origin_y,
					      (GLsizei)d.width, (GLsizei)d.height)) {
			rdn_log("OSMesaMakeCurrentSurface failed for context %p",
				c->gld_ctx);
			return;
		}
		/* Only what the window server lets show of the window. */
		if (surface_direct() && c->placed && c->screen_rects) {
			OSMesaReadbackRects(c->mesa, (GLint)c->screen_rects,
					    &c->screen_rect[0][0]);
		} else {
			GLint none[4] = { 0, 0, 0, 0 };

			OSMesaReadbackRects(c->mesa, 1, none);
		}
		c->drawable = d;
		c->bound = 1;
		c->nowhere = 0;
		rdn_set_current_rend(rend);
		return;
	}
	if (c->type == DRAWABLE_SCREEN && c->direct) {
		if (!OSMesaMakeCurrentDirect(c->mesa, RDN_TARGET_SCREEN_HANDLE,
					     (GLsizei)c->screen_width,
					     (GLsizei)c->screen_height,
					     (GLsizei)(c->screen_pitch * 4), 0)) {
			rdn_log("OSMesaMakeCurrentDirect failed for context %p",
				c->gld_ctx);
			return;
		}
		rdn_origin_x = c->origin_x;
		rdn_origin_y = c->origin_y;
		rdn_watch = rdn_window_server;
		/* What is shown when the context is flushed: the region. */
		if (c->screen_rects) {
			OSMesaReadbackRects(c->mesa, (GLint)c->screen_rects,
					    &c->screen_rect[0][0]);
		} else {
			GLint box[4];

			box[0] = c->origin_x;
			box[1] = (GLint)c->screen_height - c->origin_y -
				 (GLint)c->screen.height;
			box[2] = (GLint)c->screen.width;
			box[3] = (GLint)c->screen.height;
			OSMesaReadbackRects(c->mesa, 1, box);
		}
		c->drawable = d;
		c->bound = 1;
		c->nowhere = 0;
		rdn_set_current_rend(rend);
		return;
	}
	if (c->type == DRAWABLE_PBUFFER) {
		const struct pbuffer *p = d.base;

		/*
		 * The window server's first calls on its Core Image context
		 * (bind a texture, set its parameters) come before the pbuffer
		 * is attached and carry the screen context's engine context, so
		 * they land in the context that is current, not here. The new
		 * context takes over the rectangle texture bound there.
		 */
		GLint inherit = 0;

		if (pbuffer_ws && !c->ws_state && OSMesaGetCurrentContext() &&
		    OSMesaGetCurrentContext() != c->mesa) {
			void (*get_int)(GLenum, GLint *) = (void (*)(GLenum, GLint *))
				OSMesaGetProcAddress("glGetIntegerv");

			if (get_int)
				get_int(0x84f6, &inherit);	/* GL_TEXTURE_BINDING_RECTANGLE */
		}
		if (!OSMesaMakeCurrentStore(c->mesa, RDN_TARGET_VRAM_HANDLE,
					    (GLsizei)p->row_bytes, p->offset,
					    (GLsizei)p->width, (GLsizei)p->height,
					    p->flags | OSMESA_STORE_BOTTOM_UP)) {
			rdn_log("OSMesaMakeCurrentStore failed for context %p",
				c->gld_ctx);
			return;
		}
		OSMesaReadbackRects(c->mesa, 0, NULL);
		c->drawable = d;
		c->bound = 1;
		c->nowhere = 0;
		rdn_set_current_rend(rend);
		if (pbuffer_ws && !c->ws_state) {
			/*
			 * The window server's contexts start with rectangle
			 * texturing on: it draws the backdrop copy into its
			 * pbuffer without ever enabling it.
			 */
			void (*enable)(GLenum) = (void (*)(GLenum))OSMesaGetProcAddress("glEnable");
			void (*bind)(GLenum, GLuint) = (void (*)(GLenum, GLuint))
				OSMesaGetProcAddress("glBindTexture");

			c->ws_state = 1;
			if (enable)
				enable(0x84f5);
			if (inherit > 0 && bind)
				bind(0x84f5, (GLuint)inherit);
			if (rdn_logging)
				rdn_log("window server context %p: rectangle texturing on, texture %d bound",
					c->gld_ctx, (int)inherit);
		}
		return;
	}
	/*
	 * 32-bit ARGB words in the host's byte order, which is what both
	 * CGL's buffers and OSMESA_BGRA with GL_UNSIGNED_BYTE are.
	 */
	if (!OSMesaMakeCurrent(c->mesa, d.base, GL_UNSIGNED_BYTE,
			       (GLsizei)d.width, (GLsizei)d.height)) {
		rdn_log("OSMesaMakeCurrent failed for context %p", c->gld_ctx);
		return;
	}
	OSMesaPixelStore(OSMESA_ROW_LENGTH, (GLint)(d.rowbytes / 4));
	/* CGL's buffers hold the bottom row first; the screen the top row. */
	OSMesaPixelStore(OSMESA_Y_UP, c->type == DRAWABLE_OFFSCREEN ||
				      (c->type == DRAWABLE_WINDOW &&
				       !rdn_windows_top_down()));
	OSMesaReadbackRects(c->mesa,
			    c->type == DRAWABLE_SCREEN ? (GLint)c->screen_rects : 0,
			    &c->screen_rect[0][0]);
	c->drawable = d;
	c->bound = 1;
	c->nowhere = 0;
	rdn_flush_waits = glthread;
	rdn_set_current_rend(rend);
}

/*
 * The bindings that can be told again: a program's window, off-screen buffer
 * or surface. The others (the screen, pbuffers, backings) are bound anew.
 */
static int bind_sig_type(const struct context *c)
{
	return c->type == DRAWABLE_WINDOW || c->type == DRAWABLE_OFFSCREEN ||
	       c->type == DRAWABLE_SURFACE;
}

/* What the binding of `c` to `d` is made from, in the context's present state. */
static void bind_sig_of(const struct context *c, const struct drawable *d, struct bind_sig *sig)
{
	uint32_t hash = 2166136261u, i;
	const uint32_t *words = (const uint32_t *)c->screen_rect;

	memset(sig, 0, sizeof(*sig));
	sig->d = *d;
	sig->mesa = c->mesa;
	sig->record = c->record;
	sig->type = c->type;
	sig->connection = c->connection;
	sig->window = c->window;
	sig->surface = c->surface;
	sig->origin_x = c->origin_x;
	sig->origin_y = c->origin_y;
	sig->placed = c->placed;
	sig->direct = c->direct;
	sig->stored = c->stored;
	sig->backing_extent_set = c->backing_extent_set;
	sig->screen_width = c->screen_width;
	sig->screen_height = c->screen_height;
	sig->screen_pitch = c->screen_pitch;
	sig->screen_rects = c->screen_rects;
	for (i = 0; i < c->screen_rects && i < MAX_SCREEN_RECTS * 4; i++)
		hash = (hash ^ words[i]) * 16777619u;
	sig->rect_hash = hash;
	sig->store_offset = c->store_offset;
	sig->store_row_bytes = c->store_row_bytes;
	sig->store_width = c->store_width;
	sig->store_height = c->store_height;
}

/*
 * Make the engine context's Mesa context current in this thread, bound to the
 * program's drawable.
 *
 * Programs call aglSetCurrentContext (and for a surface the swap comes
 * here) every frame, with the same context and drawable as in the last. Doing
 * it anew each time is not free: OSMesaMakeCurrent* and OSMesaSurfaceStorage
 * wait for glthread's thread to run everything the program has called (the
 * calls that follow cannot be queued while it does), and the surface's
 * storage is registered with the kext again. When the context is still the
 * one Mesa has current in this thread and nothing it is bound from has
 * changed (the drawable's size, base and row length, a surface's place,
 * visible rectangles and storage, the screen's mode), only what the slow
 * way leaves in the globals is put back. Every 128th call takes the slow way
 * anyway. RDN_GLD_NO_REBIND_SKIP=1 in the environment always does.
 */
void rdn_make_current(void *rend)
{
	struct context *c = NULL;
	struct drawable d;
	struct bind_sig now;
	static int never = -1;
	int i;

	if (never < 0)
		never = getenv("RDN_GLD_NO_REBIND_SKIP") != NULL;
	for (i = 0; i < MAX_CONTEXTS; i++)
		if (contexts[i].gld_ctx && contexts[i].rend == rend)
			c = &contexts[i];
	if (c && c->sig_valid && !never && !rdn_trace && c->bound && !c->nowhere && c->mesa &&
	    (++c->sig_age & 127) && pthread_equal(c->thread, pthread_self()) &&
	    OSMesaGetCurrentContext() == c->mesa && read_record(c, &d)) {
		bind_sig_of(c, &d, &now);
		if (!memcmp(&now, &c->sig, offsetof(struct bind_sig, global_origin_x))) {
			async_data(c->mesa);
			rdn_origin_x = c->sig.global_origin_x;
			rdn_origin_y = c->sig.global_origin_y;
			rdn_watch = c->sig.watch;
			rdn_flush_waits = c->sig.flush_waits;
			rdn_set_current_rend(rend);
			return;
		}
	}
	if (c)
		c->sig_valid = 0;
	make_current_slow(rend);
	if (c && !never && c->mesa && c->bound && !c->nowhere && bind_sig_type(c) &&
	    rdn_current_rend == rend) {
		bind_sig_of(c, &c->drawable, &c->sig);
		c->sig.global_origin_x = rdn_origin_x;
		c->sig.global_origin_y = rdn_origin_y;
		c->sig.watch = rdn_watch;
		c->sig.flush_waits = rdn_flush_waits;
		c->sig_valid = 1;
	}
}

/* True if the engine's record no longer says what Mesa is bound to. */
static int drawable_changed(const struct context *c)
{
	struct drawable d;

	return !read_record(c, &d) || memcmp(&d, &c->drawable, sizeof(d)) != 0;
}

/*
 * A program's surface inside the window server's picture.
 *
 * Where a program's OpenGL surface is, the window server draws a plain
 * white rectangle: one quad, with texturing off, white, whose texture
 * coordinates are nevertheless the part of the surface it stands for, and
 * leaves the surface itself to the driver. The calls are watched as they
 * go by (gen_dispatch.py, WATCHED); when such a quad has been drawn, the
 * surface's picture is copied over it in the window server's own drawing
 * buffer, there and then, so that whatever the window server draws next
 * (windows in front, shadows) comes out on top of it and the whole goes to
 * the screen in one piece. The window server's own coordinates are used,
 * not the shape the kext was told, which lags behind while a window is
 * dragged.
 */
int rdn_watch;
int rdn_window_server;

static struct {
	/* glOrtho's left and top: the desktop position of the drawable's corner. */
	double left, top;
	int unit, rect_on[2];
	int white, in_quads, vertices, texcoords;
	float v[4][2], t[4][2], s_now, t_now;
} watch;

void rdn_watch_ortho(double left, double right, double bottom, double top,
		     double z_near, double z_far)
{
	watch.left = left;
	watch.top = top;
}

void rdn_watch_active_texture(unsigned unit)
{
	watch.unit = unit == GL_TEXTURE1;
}

void rdn_watch_enable(unsigned cap)
{
	if (cap == GL_TEXTURE_RECTANGLE_ARB)
		watch.rect_on[watch.unit] = 1;
}

void rdn_watch_disable(unsigned cap)
{
	if (cap == GL_TEXTURE_RECTANGLE_ARB)
		watch.rect_on[watch.unit] = 0;
}

void rdn_watch_color4ub(unsigned char r, unsigned char g, unsigned char b,
			unsigned char a)
{
	watch.white = (r & g & b & a) == 0xff;
}

void rdn_watch_begin(unsigned mode)
{
	watch.in_quads = mode == GL_QUADS;
	watch.vertices = 0;
	watch.texcoords = 0;
}

void rdn_watch_tex_coord2f(float s, float t)
{
	watch.s_now = s;
	watch.t_now = t;
	watch.texcoords++;
}

void rdn_watch_vertex2f(float x, float y)
{
	if (watch.in_quads && watch.vertices < 4) {
		watch.v[watch.vertices][0] = x;
		watch.v[watch.vertices][1] = y;
		watch.t[watch.vertices][0] = watch.s_now;
		watch.t[watch.vertices][1] = watch.t_now;
	}
	watch.vertices++;
}

/*
 * A surface as a texture.
 *
 * The better case: the window server makes a rectangle texture for a
 * program's surface, sets its filters and gives it no image, because it
 * has told Apple's engine (a private context parameter, 997) that the
 * texture is the surface; it then draws the surface as an ordinary
 * textured quad. The engine keeps that to itself, but the window server
 * does all of it while it holds the surface locked for reading, and the
 * kext knows which surface that is. So: a rectangle texture whose
 * anisotropy is set before it has an image, while a surface is locked,
 * gets that surface's picture in video memory as its image, uncopied.
 */
static int watch_texture_has_image;

/* Opt-in, bounded snapshots of CPU uploads before compositor drawing. */
static void watch_upload(unsigned target, int width, int height,
			 unsigned format, unsigned type, const void *pixels)
{
	static unsigned count;
	void (*get)(GLenum, GLint *);
	GLint pbo = 0, row = 0, skip_x = 0, skip_y = 0, texture = 0;
	char path[128];
	FILE *fp;
	int y;

	if (count >= 24 || target != GL_TEXTURE_RECTANGLE_ARB ||
	    width < 128 || width > 512 || height < 128 || height > 512 ||
	    format != GL_BGRA || type != GL_UNSIGNED_INT_8_8_8_8_REV || !pixels ||
	    access("/tmp/rdngld.uploads", F_OK))
		return;
	get = (void (*)(GLenum, GLint *))OSMesaGetProcAddress("glGetIntegerv");
	if (!get)
		return;
	get(0x88EF, &pbo); /* GL_PIXEL_UNPACK_BUFFER_BINDING */
	if (pbo)
		return;
	get(GL_UNPACK_ROW_LENGTH, &row);
	get(GL_UNPACK_SKIP_PIXELS, &skip_x);
	get(GL_UNPACK_SKIP_ROWS, &skip_y);
	get(GL_TEXTURE_BINDING_RECTANGLE_ARB, &texture);
	if (!row)
		row = width;
	if (row < width || row > 16384 || skip_x < 0 || skip_y < 0)
		return;
	snprintf(path, sizeof(path), "/tmp/rdn-upload-%d-%02u-%x-%dx%d.argb",
		 (int)getpid(), count++, (unsigned)texture, width, height);
	fp = fopen(path, "wb");
	if (!fp)
		return;
	for (y = 0; y < height; ++y)
		if (fwrite((const char *)pixels + ((size_t)(skip_y + y) * row + skip_x) * 4,
			   4, width, fp) != (size_t)width)
			break;
	fclose(fp);
}

void rdn_watch_bind_texture(unsigned target, unsigned texture)
{
	if (target == GL_TEXTURE_RECTANGLE_ARB)
		watch_texture_has_image = 0;
}

void rdn_watch_tex_image2D(unsigned target, int level, int internalformat,
			   int width, int height, int border, unsigned format,
			   unsigned type, const void *pixels)
{
	if (target == GL_TEXTURE_RECTANGLE_ARB)
		watch_texture_has_image = 1;
	watch_upload(target, width, height, format, type, pixels);
}

void rdn_watch_tex_sub_image2D(unsigned target, int level, int xoffset,
			       int yoffset, int width, int height,
			       unsigned format, unsigned type, const void *pixels)
{
	if (target == GL_TEXTURE_RECTANGLE_ARB)
		watch_texture_has_image = 1;
	watch_upload(target, width, height, format, type, pixels);
}

void rdn_watch_tex_parameterf(unsigned target, unsigned pname, float param)
{
	uint32_t info[5], id;
	bool found;
	struct context *c = NULL;
	static unsigned noted;
	int j;

	if (rdn_surface_texture_hook || target != GL_TEXTURE_RECTANGLE_ARB || watch_texture_has_image ||
	    pname != 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY_EXT */)
		return;
	id = rdn_target_surface_locked();
	if (!id)
		return;
	for (j = 0; j < MAX_CONTEXTS; j++)
		if (contexts[j].gld_ctx && contexts[j].rend == rdn_current_rend)
			c = &contexts[j];
	if (!c || !c->mesa)
		return;
	if (rdn_target_surface_query(id, info, &found) && found && info[3] && info[4]) {
		if (OSMesaTexStore(c->mesa, GL_TEXTURE_RECTANGLE_ARB,
				   RDN_TARGET_VRAM_HANDLE, (GLsizei)info[2],
				   info[1], (GLsizei)info[3], (GLsizei)info[4]))
			watch_texture_has_image = 1;
		if (noted++ < 20)
			rdn_log("surface 0x%x is the image of a texture of the window server's: %s",
				(unsigned)id, watch_texture_has_image ? "done" : "failed");
		return;
	}
	if (noted++ < 20)
		rdn_log("surface 0x%x is locked for a texture but has no buffer", (unsigned)id);
}

void rdn_watch_end(void)
{
	uint32_t list[32][5], n, i, best = 32;
	struct context *c = NULL;
	int sx, sy, sw, sh, dx, dy, j;
	static unsigned noted;
	/* Where each surface's corner was when it was drawn last. */
	enum { MAX_DRAWN = 32 };
	static struct { uint32_t id; long x, y; } drawn[MAX_DRAWN];
	long best_d = 0;

	if (rdn_surface_texture_hook || !watch.in_quads || watch.vertices != 4 || watch.texcoords != 4 ||
	    !watch.white || watch.rect_on[0] || watch.rect_on[1])
		return;
	for (j = 0; j < MAX_CONTEXTS; j++)
		if (contexts[j].gld_ctx && contexts[j].rend == rdn_current_rend)
			c = &contexts[j];
	if (!c || c->type != DRAWABLE_SCREEN || !c->direct || !c->bound)
		return;
	/* Corners go round from the top left: 0 and 2 are opposite. */
	sx = (int)(watch.t[0][0] + 0.5f);
	sy = (int)(watch.t[0][1] + 0.5f);
	sw = (int)(watch.t[2][0] + 0.5f) - sx;
	sh = (int)(watch.t[2][1] + 0.5f) - sy;
	if (sx < 0 || sy < 0 || sw <= 0 || sh <= 0 ||
	    (int)(watch.v[2][0] - watch.v[0][0] + 0.5f) != sw ||
	    (int)(watch.v[2][1] - watch.v[0][1] + 0.5f) != sh)
		return;
	/*
	 * The drawable's corner is at origin_x from the screen's left and
	 * origin_y from its bottom, and at glOrtho's left, top on the
	 * desktop.
	 */
	dx = (int)(watch.v[0][0] - watch.left) + c->origin_x;
	dy = (int)(watch.v[0][1] - watch.top) +
	     ((int)c->screen_height - c->origin_y - (int)c->screen.height);

	/*
	 * Whose is it? Nothing in the quad says. Of the surfaces it fits,
	 * the one whose corner would be nearest to where that surface was
	 * drawn last or to where the kext has it (which is right except for
	 * a step's lag during a drag), whichever is nearer.
	 */
	n = rdn_target_surface_list(list, 32);
	for (i = 0; i < n; i++) {
		static int16_t rects[MAX_SCREEN_RECTS][4];
		long ox = dx - sx, oy = dy - sy, d, bx, by;
		uint32_t count, k;
		int32_t b[4];

		if ((uint32_t)(sx + sw) > list[i][3] || (uint32_t)(sy + sh) > list[i][4])
			continue;
		for (k = 0; k < MAX_DRAWN; k++)
			if (drawn[k].id == list[i][0])
				break;
		d = -1;
		if (k < MAX_DRAWN) {
			bx = drawn[k].x;
			by = drawn[k].y;
			d = labs(ox - bx) + labs(oy - by);
		}
		/* The nearer of the two counts. */
		if (rdn_target_surface_region(list[i][0], b, rects,
					      MAX_SCREEN_RECTS, &count)) {
			long e = labs(ox - b[0]) + labs(oy - b[1]);

			if (d < 0 || e < d)
				d = e;
		}
		if (d < 0)
			continue;
		if (best == 32 || d < best_d) {
			best = i;
			best_d = d;
		}
	}
	if (best != 32) {
		uint32_t k, slot = 0;

		for (k = 0; k < MAX_DRAWN; k++) {
			if (drawn[k].id == list[best][0]) {
				slot = k;
				break;
			}
			/* Otherwise a free place, or the one of a surface that is gone. */
			if (!drawn[k].id)
				slot = k;
		}
		if (drawn[slot].id != list[best][0]) {
			for (k = 0; k < MAX_DRAWN; k++) {
				uint32_t m;

				for (m = 0; m < n; m++)
					if (list[m][0] == drawn[k].id)
						break;
				if (m == n) {
					slot = k;
					break;
				}
			}
		}
		drawn[slot].id = list[best][0];
		drawn[slot].x = dx - sx;
		drawn[slot].y = dy - sy;
	}
	if (best == 32) {
		if (noted++ < 40)
			rdn_log("a white quad %dx%d (from %d,%d) fits none of %u surfaces",
				sw, sh, sx, sy, (unsigned)n);
		return;
	}
	if (rdn_trace || noted++ < 40)
		rdn_log("surface 0x%x drawn into the window server's picture: %d,%d %dx%d of it at %d,%d",
			(unsigned)list[best][0], sx, sy, sw, sh, dx, dy);
	OSMesaDrawStore(c->mesa, RDN_TARGET_VRAM_HANDLE, (GLsizei)list[best][2],
			list[best][1], (GLsizei)list[best][3], (GLsizei)list[best][4],
			sx, sy, sw, sh, dx, dy);
}

static int surface_direct(void)
{
	static int known = -1;

	if (known < 0)
		known = getenv("RDN_GLD_DIRECT_SWAP") != NULL;
	return known;
}

void rdn_flush_surface(void *rend)
{
	int i;

	for (i = 0; i < MAX_CONTEXTS; i++)
		if (contexts[i].gld_ctx && contexts[i].rend == rend &&
		    contexts[i].type == DRAWABLE_SURFACE) {
			if (contexts[i].dbl)
				return;	/* only a swap shows the back buffer */
			if (contexts[i].swaps == 0 && contexts[i].bound) {
				/* As for a swap: the picture is complete, then the window server knows. */
				rdn_mesa_present(contexts[i].gld_ctx);
				rdn_surface_flush(contexts[i].connection, contexts[i].window,
						  contexts[i].surface);
			}
			return;
		}
}

int rdn_swap(void *rend)
{
	int i, tries;

	for (i = 0; i < MAX_CONTEXTS; i++) {
		if (contexts[i].gld_ctx && contexts[i].rend == rend &&
		    contexts[i].type == DRAWABLE_SCREEN && !rdn_window_server) {
			/* The whole screen: the copy is all there is to do. */
			rdn_mesa_present(contexts[i].gld_ctx);
			return 1;
		}
		if (contexts[i].gld_ctx && contexts[i].rend == rend &&
		    contexts[i].type == DRAWABLE_SURFACE) {
			if (contexts[i].swaps++ % 100 < 2)
				rdn_log("swap: surface 0x%lx at %d,%d, %u rectangles, bound %d",
					contexts[i].surface, contexts[i].origin_x,
					contexts[i].origin_y,
					(unsigned)contexts[i].screen_rects,
					contexts[i].bound);
			/*
			 * The picture goes to the surface's buffer, and the
			 * window server is told. It decides how the picture
			 * gets to the screen: if nothing else changed and
			 * nothing translucent lies over the window, it has
			 * the kext copy the buffer there
			 * (RadeonNIAccel::flushSurface); otherwise it draws
			 * the window's area again with the buffer as a
			 * texture. RDN_GLD_DIRECT_SWAP=1 also puts it on
			 * the screen from here, which is wrong when the
			 * window is moving or partly covered.
			 */
			if (surface_direct()) {
				for (tries = 0; tries < 50; tries++) {
					struct drawable d;

					if (surface_drawable(&contexts[i], &d) &&
					    contexts[i].placed)
						break;
					usleep(10000);
				}
			}
			rdn_mesa_present(contexts[i].gld_ctx);
			rdn_surface_flush(contexts[i].connection, contexts[i].window,
					  contexts[i].surface);
			return 1;
		}
	}
	return 0;
}

static int swap_finish(void)
{
	static int known = -1;

	if (known < 0)
		known = getenv("RDN_GLD_SWAP_FINISH") != NULL;
	return known;
}

/*
 * A program asked for a multisampled pixel format. Contexts made after
 * this get that many samples; RDN_GLD_NO_MSAA in the environment keeps
 * them at one.
 */
/*
 * A program chose a double-buffered pixel format (kCGLPFADoubleBuffer). The
 * contexts made with it draw on a back buffer that a swap shows; with
 * RDN_GLD_NO_BACKBUFFER=1 in the program's environment they do not, and
 * GL_BACK is GL_FRONT, which is shown at every flush, as before.
 */
void rdn_mesa_double(int yes)
{
	if (getenv("RDN_GLD_NO_BACKBUFFER"))
		yes = 0;
	pending_double = yes != 0;
	if (yes)
		rdn_log("pixel format with a back buffer asked for");
}

void rdn_mesa_samples(int samples)
{
	if (getenv("RDN_GLD_NO_MSAA"))
		samples = 1;
	if (samples > 1)
		rdn_log("pixel format with %d samples asked for", samples);
	OSMesaSetSamples(samples);
}

/*
 * RDN_FPS=1 in the environment, or the file /tmp/rdngld.fps (for programs
 * whose environment is not ours to set): frames a second, once a second, in
 * /tmp/rdn-fps.log. A frame is one present of the program's own drawable.
 */
static void fps_count(void)
{
	static int on = -1;
	static unsigned frames;
	static double last, first;
	struct timeval tv;
	double now;

	if (on < 0)
		on = getenv("RDN_FPS") != NULL || access("/tmp/rdngld.fps", F_OK) == 0;
	if (!on)
		return;
	gettimeofday(&tv, NULL);
	now = tv.tv_sec + tv.tv_usec / 1e6;
	if (last == 0) {
		last = first = now;
		return;
	}
	frames++;
	if (now - last >= 1.0) {
		FILE *f = fopen("/tmp/rdn-fps.log", "a");

		if (f) {
			fprintf(f, "rdn: fps %.1f (frame %.2f ms) at %.0f s\n",
				frames / (now - last), 1000.0 * (now - last) / frames,
				now - first);
			fclose(f);
		}
		frames = 0;
		last = now;
	}
}

void rdn_mesa_present(void *gld_ctx)
{
	struct context *c = find(gld_ctx);
	struct drawable d;

	if (!c || !c->rend || !mesa_finish || !read_record(c, &d))
		return;
	fps_count();
	/* A surface may have moved or been covered since the last frame. */
	if (c->rend != rdn_current_rend || !c->bound || drawable_changed(c) ||
	    c->type == DRAWABLE_SURFACE)
		rdn_make_current(c->rend);
	if (c->rend != rdn_current_rend || !c->bound)
		return;
	if (c->dbl) {
		/*
		 * A full-screen program does not wait for its picture: the swap
		 * is queued behind the frame in glthread's thread. A surface is
		 * read by the window server at once, so that swap is waited for.
		 */
		if (c->type == DRAWABLE_SCREEN && !rdn_window_server && !swap_finish())
			OSMesaSwapBuffersAsync(c->mesa);
		else
			OSMesaSwapBuffers(c->mesa);
	}
	/*
	 * A program that has the whole screen need not wait for its picture:
	 * the copy to the screen is queued behind the drawing, and the
	 * winsys keeps the program from running far ahead of the GPU. A
	 * surface's picture is read by the window server, so that one is
	 * waited for. RDN_GLD_SWAP_FINISH=1 waits in both cases.
	 */
	if (c->type == DRAWABLE_SCREEN && !rdn_window_server && mesa_flush &&
	    !swap_finish())
		mesa_flush();
	else
		mesa_finish();
}
