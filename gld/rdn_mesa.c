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
 * One Mesa context is current per process, not per thread, for now.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <GL/gl.h>
#include <GL/osmesa.h>

#include <unistd.h>

#include "rdn_glue.h"
#include "rdn_target.h"

/* gldAttachDrawable's type for CGLSetOffScreen: kCGLPFAOffScreen. */
#define DRAWABLE_OFFSCREEN	0x35

/*
 * The engine's context (10.4.11's GLEngine): the driver's own table is at
 * this offset in it, and six words before that table is a pointer to the
 * application's GLIFunctionDispatch. See docs/GLD-INTERFACE.md.
 */
#define ENGINE_CTX_TABLE_OFFSET	0x4698
#define APP_DISPATCH_BACK	0x18
/*
 * The application's table in 10.4.11 has 684 entries; the 10.4u SDK's
 * header declares 686. The CGL context's private fields follow the table,
 * so writing the last two entries corrupts them (seen as a crash in
 * CGLDestroyContext).
 */
#define APP_DISPATCH_ENTRIES	684

/* gldAttachDrawable's type for a window: kCGLPFAWindow. */
#define DRAWABLE_WINDOW		0x50
/* Not Apple's: the card's own screen, for the window server's context. */
#define DRAWABLE_SCREEN		0x7570
/*
 * Not Apple's either: a program's window as a surface of the window
 * server's, shown by the card itself at the place the kext is told.
 */
#define DRAWABLE_SURFACE	0x7571
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

struct context {
	void *gld_ctx;
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
	int stored;
	int bound;
	/*
	 * The engine's context as gldCreateContext was told, so that the
	 * program's GL calls can be given to Mesa before the first drawable
	 * is attached; and whether they have been.
	 */
	void *early_rend;
	int dispatched;
	/* Bound to nothing that is shown (no drawable yet). */
	int nowhere;
	unsigned swaps;
};

#define MAX_CONTEXTS 64
static struct context contexts[MAX_CONTEXTS];
static int resolved, usable = -1;

static int surface_direct(void);

void *rdn_current_rend;
int rdn_origin_x, rdn_origin_y;

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

void rdn_mesa_context_created(void *gld_ctx)
{
	int i;

	if (!gld_ctx || find(gld_ctx))
		return;
	for (i = 0; i < MAX_CONTEXTS; i++)
		if (!contexts[i].gld_ctx) {
			memset(&contexts[i], 0, sizeof(contexts[i]));
			contexts[i].gld_ctx = gld_ctx;
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
		rdn_current_rend = NULL;
	}
	if (c->mesa)
		OSMesaDestroyContext(c->mesa);
	if (c->stored) {
		rdn_target_surface_buffer((uint32_t)c->surface, 0, 0, 0, 0);
		rdn_target_vram_free(c->store_offset);
	}
	memset(c, 0, sizeof(*c));
}

/* Read the engine's record; false if there is nothing to draw into. */
/*
 * A surface's picture is kept in a linear buffer in video memory, 64
 * pixels to the row's multiple as the card wants of a linear render
 * target, and the kext is told where: the window server reads it there
 * when it draws the window itself. A new one when the size changes; none
 * (width 0) to give it up.
 */
static void surface_store(struct context *c, uint32_t width, uint32_t height)
{
	uint32_t row_bytes = ((width + 63) & ~63u) * 4;
	uint32_t rows = (height + 63) & ~63u;

	if (c->stored && c->store_width == width && c->store_height == height)
		return;
	if (c->stored) {
		rdn_target_surface_buffer((uint32_t)c->surface, 0, 0, 0, 0);
		/* Mesa lets go of it at the next bind; nothing reads it now. */
		rdn_target_vram_free(c->store_offset);
		c->stored = 0;
	}
	if (!width || !height ||
	    !rdn_target_vram_alloc((row_bytes * rows + 4095) & ~4095u, &c->store_offset))
		return;
	c->store_row_bytes = row_bytes;
	c->store_width = width;
	c->store_height = height;
	c->stored = 1;
	if (!rdn_target_surface_buffer((uint32_t)c->surface, c->store_offset,
				       row_bytes, width, height))
		rdn_log("the kext did not take surface 0x%lx's buffer", c->surface);
	else
		rdn_log("surface 0x%lx keeps its picture at aperture offset 0x%x, %ux%u, %u bytes a row",
			c->surface, (unsigned)c->store_offset, (unsigned)width,
			(unsigned)height, (unsigned)row_bytes);
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
			       &c->screen_pitch) ||
	    !rdn_surface_size(c->connection, c->window, c->surface, &width, &height))
		return 0;
	d->width = width;
	d->height = height;
	d->base = (void *)pixels;

	/*
	 * The place and the visible part come from the kext, once the
	 * window server has told it. Until then the program can draw; nothing
	 * is shown.
	 */
	c->screen_rects = 0;
	c->placed = 0;
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

	if (c->type == DRAWABLE_SCREEN) {
		*d = c->screen;
		return d->base != NULL;
	}
	if (c->type == DRAWABLE_SURFACE)
		return surface_drawable((struct context *)c, d);
	memset(d, 0, sizeof(*d));
	if (!r || (c->type != DRAWABLE_OFFSCREEN && c->type != DRAWABLE_WINDOW))
		return 0;
	d->width = r[REC_WIDTH];
	d->height = r[REC_HEIGHT];
	d->rowbytes = (r[REC_ROW] >> 16) * (r[REC_ROW] & 0xffff);
	d->base = (void *)(uintptr_t)r[REC_BASE];
	return d->base && d->width && d->height && (r[REC_ROW] & 0xffff) == 4 &&
	       d->rowbytes >= d->width * 4;
}

void rdn_mesa_attach(void *gld_ctx, long type, const void *drawable)
{
	struct context *c = find(gld_ctx);
	struct drawable d;

	if (!c)
		return;
	c->type = type;
	c->record = drawable;
	c->bound = 0;
	/* The next GL call binds the new drawable. */
	if (c->rend && c->rend == rdn_current_rend)
		rdn_current_rend = NULL;
	read_record(c, &d);
	rdn_log("attach: context %p, type 0x%lx, %ux%u, rowbytes %u, base %p",
		gld_ctx, type, (unsigned)d.width, (unsigned)d.height,
		(unsigned)d.rowbytes, d.base);
}

int rdn_mesa_attach_surface(void *gld_ctx, unsigned long connection,
			    unsigned long window, unsigned long surface)
{
	struct context *c = find(gld_ctx);

	if (!c)
		return 0;
	/* The device is opened with the first Mesa context. */
	if (!c->mesa)
		c->mesa = OSMesaCreateContextExt(OSMESA_BGRA, 24, 8, 0, NULL);
	if (!c->mesa)
		return 0;
	c->type = DRAWABLE_SURFACE;
	c->connection = connection;
	c->window = window;
	c->surface = surface;
	c->record = NULL;
	c->bound = 0;
	if (c->rend && c->rend == rdn_current_rend)
		rdn_current_rend = NULL;
	rdn_log("attach: context %p draws on surface 0x%lx", gld_ctx, surface);
	return 1;
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
	if (c->type == DRAWABLE_SURFACE && c->stored) {
		/* Off the store before it is freed. */
		if (c->mesa && OSMesaGetCurrentContext() == c->mesa)
			OSMesaMakeCurrent(NULL, NULL, GL_UNSIGNED_BYTE, 0, 0);
		surface_store(c, 0, 0);
	}
	c->type = 0;
	c->record = NULL;
	c->bound = 0;
	if (c->rend && c->rend == rdn_current_rend)
		rdn_current_rend = NULL;
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
		c->mesa = OSMesaCreateContextExt(OSMESA_BGRA, 24, 8, 0, NULL);
	if (!c->mesa || !rdn_target_screen(&pixels, &width, &height, &pitch)) {
		rdn_log("attach: context %p cannot reach the screen", gld_ctx);
		return 0;
	}
	c->type = DRAWABLE_SCREEN;
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
		rdn_current_rend = NULL;
	rdn_log("attach: context %p draws on the screen at %d,%d %dx%d (surface %lu, %u rectangles, %s)",
		gld_ctx, (int)b[0], (int)b[1], (int)b[2], (int)b[3], surface,
		(unsigned)count, c->direct ? "direct" : "copied");
	return 1;
}

static void *lookup(const char *name)
{
	return (void *)OSMesaGetProcAddress(name);
}

static void missing(const char *name)
{
	rdn_log("Mesa has no %s; the engine keeps that entry", name);
}

/* Mesa's functions looked up, once. False if there is no card to use. */
static int mesa_ready(void)
{
	unsigned n;

	if (usable < 0) {
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
	void *app_table = *(void **)((char *)engine_table - APP_DISPATCH_BACK);
	unsigned n;

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
		static void *before[APP_DISPATCH_ENTRIES];
		void **t = app_table;
		unsigned i, changed = 0, first = 0;

		memcpy(before, t, sizeof(before));
		rdn_dispatch_install(app_table, APP_DISPATCH_ENTRIES);
		for (i = 0; i < APP_DISPATCH_ENTRIES; i++)
			if (before[i] != t[i] && !changed++)
				first = i;
		if (changed)
			rdn_log("  the engine had taken back %u entries, the first at index %u (%s)",
				changed, first, when);
	}
	n = rdn_dispatch_install(app_table, APP_DISPATCH_ENTRIES);
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

void rdn_mesa_context_engine(void *gld_ctx, void *rend)
{
	struct context *c = find(gld_ctx);

	if (c && !c->early_rend)
		c->early_rend = rend;
}

void rdn_mesa_early_all(void *cgl_ctx)
{
	int i;

	(void)cgl_ctx;
	for (i = 0; i < MAX_CONTEXTS; i++)
		if (contexts[i].gld_ctx)
			rdn_mesa_early(contexts[i].gld_ctx);
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
	install(c, (char *)c->early_rend + ENGINE_CTX_TABLE_OFFSET, "before a drawable");
}

void rdn_make_current(void *rend)
{
	struct context *c = NULL;
	struct drawable d;
	int i;

	for (i = 0; i < MAX_CONTEXTS; i++)
		if (contexts[i].gld_ctx && contexts[i].rend == rend)
			c = &contexts[i];
	if (!c) {
		rdn_log("GL call on engine context %p, which is not ours", rend);
		return;
	}
	if (!c->mesa)
		c->mesa = OSMesaCreateContextExt(OSMESA_BGRA, 24, 8, 0, NULL);
	if (!c->mesa)
		return;
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
		rdn_current_rend = NULL;
		if (!OSMesaMakeCurrent(c->mesa, nowhere, GL_UNSIGNED_BYTE, side, side))
			return;
		OSMesaPixelStore(OSMESA_ROW_LENGTH, side);
		OSMesaReadbackRects(c->mesa, 0, NULL);
		c->bound = 0;
		c->nowhere = 1;
		return;
	}
	rdn_origin_x = rdn_origin_y = 0;
	if (c->type == DRAWABLE_SURFACE) {
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
		rdn_current_rend = rend;
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
		rdn_current_rend = rend;
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
	rdn_current_rend = rend;
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
}

void rdn_watch_tex_sub_image2D(unsigned target, int level, int xoffset,
			       int yoffset, int width, int height,
			       unsigned format, unsigned type, const void *pixels)
{
	if (target == GL_TEXTURE_RECTANGLE_ARB)
		watch_texture_has_image = 1;
}

void rdn_watch_tex_parameterf(unsigned target, unsigned pname, float param)
{
	uint32_t list[32][5], n, i, id;
	struct context *c = NULL;
	static unsigned noted;
	int j;

	if (target != GL_TEXTURE_RECTANGLE_ARB || watch_texture_has_image ||
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
	n = rdn_target_surface_list(list, 32);
	for (i = 0; i < n; i++) {
		if (list[i][0] != id)
			continue;
		if (OSMesaTexStore(c->mesa, GL_TEXTURE_RECTANGLE_ARB,
				   RDN_TARGET_VRAM_HANDLE, (GLsizei)list[i][2],
				   list[i][1], (GLsizei)list[i][3], (GLsizei)list[i][4]))
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

	if (!watch.in_quads || watch.vertices != 4 || watch.texcoords != 4 ||
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

int rdn_swap(void *rend)
{
	int i, tries;

	for (i = 0; i < MAX_CONTEXTS; i++)
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
			 * window server is told: it draws the window's area
			 * again and the picture goes into its frame
			 * (rdn_watch_end). Slower than putting it on the
			 * screen from here, which RDN_GLD_DIRECT_SWAP=1
			 * does as well, but right when the window is
			 * moving or partly covered.
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
void rdn_mesa_samples(int samples)
{
	if (getenv("RDN_GLD_NO_MSAA"))
		samples = 1;
	if (samples > 1)
		rdn_log("pixel format with %d samples asked for", samples);
	OSMesaSetSamples(samples);
}

void rdn_mesa_present(void *gld_ctx)
{
	struct context *c = find(gld_ctx);
	struct drawable d;

	if (!c || !c->rend || !mesa_finish || !read_record(c, &d))
		return;
	/* A surface may have moved or been covered since the last frame. */
	if (c->rend != rdn_current_rend || !c->bound || drawable_changed(c) ||
	    c->type == DRAWABLE_SURFACE)
		rdn_make_current(c->rend);
	if (c->rend != rdn_current_rend || !c->bound)
		return;
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
