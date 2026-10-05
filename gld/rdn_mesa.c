/*
 * The driver bundle's Mesa side: one Mesa context per CGL context.
 *
 * Apple's engine stays in charge of the context's life (through the
 * software renderer the bundle forwards to); what changes is where the GL
 * calls go. When the engine asks the driver to set up dispatch, the
 * application's whole table is filled with entry points that call Mesa's
 * r600. Only off-screen drawables (CGLSetOffScreen) so far: the picture is
 * rendered by the card and copied into the application's buffer when it
 * flushes, as the off-screen frontend does.
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

#include "rdn_glue.h"

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

/* An off-screen drawable as gldAttachDrawable describes it. */
struct offscreen_drawable {
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
	struct offscreen_drawable drawable;
	int bound;
};

#define MAX_CONTEXTS 64
static struct context contexts[MAX_CONTEXTS];
static int resolved, usable = -1;

void *rdn_current_rend;

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
	memset(c, 0, sizeof(*c));
}

void rdn_mesa_attach(void *gld_ctx, long type, const void *drawable)
{
	struct context *c = find(gld_ctx);

	if (!c)
		return;
	c->type = type;
	c->bound = 0;
	if (type == DRAWABLE_OFFSCREEN && drawable)
		memcpy(&c->drawable, drawable, sizeof(c->drawable));
	else
		memset(&c->drawable, 0, sizeof(c->drawable));
	/* The next GL call binds the new drawable. */
	if (c->rend && c->rend == rdn_current_rend)
		rdn_current_rend = NULL;
	rdn_log("attach: context %p, type 0x%lx, %ux%u, rowbytes %u, base %p",
		gld_ctx, type, (unsigned)c->drawable.width,
		(unsigned)c->drawable.height, (unsigned)c->drawable.rowbytes,
		c->drawable.base);
}

static void *lookup(const char *name)
{
	return (void *)OSMesaGetProcAddress(name);
}

static void missing(const char *name)
{
	rdn_log("Mesa has no %s; the engine keeps that entry", name);
}

int rdn_mesa_dispatch(void *gld_ctx, void *engine_table)
{
	struct context *c = find(gld_ctx);
	void *app_table;
	unsigned n;

	if (!c || c->type != DRAWABLE_OFFSCREEN || !c->drawable.base)
		return 0;
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
		n = rdn_dispatch_resolve(lookup, missing);
		rdn_log("Mesa provides %u of the %u GL entry points", n,
			rdn_dispatch_entries);
		resolved = 1;
	}
	c->rend = (char *)engine_table - ENGINE_CTX_TABLE_OFFSET;
	app_table = *(void **)((char *)engine_table - APP_DISPATCH_BACK);
	if (!app_table)
		return 0;
	n = rdn_dispatch_install(app_table, APP_DISPATCH_ENTRIES);
	rdn_log("context %p (engine %p): %u entries of table %p are Mesa's",
		gld_ctx, c->rend, n, app_table);
	return 1;
}

void rdn_make_current(void *rend)
{
	struct context *c = NULL;
	int i;

	for (i = 0; i < MAX_CONTEXTS; i++)
		if (contexts[i].gld_ctx && contexts[i].rend == rend)
			c = &contexts[i];
	if (!c || !c->drawable.base) {
		rdn_log("GL call on engine context %p, which has no drawable of ours", rend);
		return;
	}
	if (!c->mesa)
		c->mesa = OSMesaCreateContextExt(OSMESA_BGRA, 24, 8, 0, NULL);
	if (!c->mesa)
		return;
	/*
	 * 32-bit ARGB words in the host's byte order, which is what both
	 * CGL's off-screen buffers and OSMESA_BGRA with GL_UNSIGNED_BYTE are.
	 */
	if (!OSMesaMakeCurrent(c->mesa, c->drawable.base, GL_UNSIGNED_BYTE,
			       (GLsizei)c->drawable.width, (GLsizei)c->drawable.height)) {
		rdn_log("OSMesaMakeCurrent failed for context %p", c->gld_ctx);
		return;
	}
	OSMesaPixelStore(OSMESA_ROW_LENGTH, (GLint)(c->drawable.rowbytes / 4));
	c->bound = 1;
	rdn_current_rend = rend;
}
