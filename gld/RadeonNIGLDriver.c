/*
 * RadeonNIGLDriver: the OpenGL driver bundle of the Radeon HD 7570 driver.
 *
 * Milestone A0 state: a pass-through. Every gld* entry point Tiger's
 * OpenGL expects from a driver bundle is forwarded to Apple's software
 * renderer, and each call is logged with its integer arguments and result,
 * to learn the interface. Set RDN_GLD_LOG to a file name to move the log.
 *
 * Three things are changed on the way, to show that a driver bundle can
 * present itself as its own renderer and take over GL entry points:
 *   - the renderer ID becomes RDN_RENDERER_ID;
 *   - the GL_RENDERER string is ours;
 *   - glClear, in the application's GL dispatch table (reached through the
 *     engine context at gldInitDispatch time), is replaced by a function
 *     that clears to magenta whatever colour was asked for.
 *
 * The forwarders pass eight integer arguments, which on PowerPC covers
 * every register argument; floating-point arguments and arguments on the
 * stack would be lost. None has been seen yet.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <OpenGL/gl.h>
#include <OpenGL/gliContext.h>
#include <OpenGL/gliDispatch.h>

/* The next free number after Apple's ATI renderers in CGLRenderers.h. */
#define RDN_RENDERER_ID		0x00021a00
#define SOFTWARE_RENDERER_ID	0x00020400	/* kCGLRendererGenericFloatID */
#define RDN_RENDERER_STRING	"RadeonNI pass-through to Apple Software Renderer"

#define REAL_BUNDLE \
	"/System/Library/Frameworks/OpenGL.framework/Resources/" \
	"GLRendererFloat.bundle/GLRendererFloat"
#define DEFAULT_LOG	"/tmp/rdngld.log"

/* The 62 entry points of a 10.4.11 PowerPC driver bundle. */
#define GLD_ENTRIES(X) \
	X(gldAllocVertexBuffer) X(gldAttachDrawable) X(gldChoosePixelFormat) \
	X(gldCompleteVertexBuffer) X(gldCreateBuffer) X(gldCreateContext) \
	X(gldCreateFence) X(gldCreateFramebuffer) X(gldCreatePipelineProgram) \
	X(gldCreateQuery) X(gldCreateShared) X(gldCreateTexture) \
	X(gldCreateTextureLevel) X(gldCreateVertexArray) X(gldDeleteTexture) \
	X(gldDeleteTextureLevel) X(gldDestroyBuffer) X(gldDestroyContext) \
	X(gldDestroyFence) X(gldDestroyFramebuffer) \
	X(gldDestroyMemoryPluginData) X(gldDestroyPipelineProgram) \
	X(gldDestroyPixelFormat) X(gldDestroyQuery) X(gldDestroyShared) \
	X(gldDestroyVertexArray) X(gldFinish) X(gldFinishMemoryPluginData) \
	X(gldFinishObject) X(gldFlush) X(gldFlushBuffer) \
	X(gldFlushVertexArray) X(gldFreeVertexBuffer) X(gldGetError) \
	X(gldGetInteger) X(gldGetMemoryPluginData) \
	X(gldGetPipelineProgramInfo) X(gldGetQueryInfo) X(gldGetRendererInfo) \
	X(gldGetString) X(gldGetTextureLevel) X(gldGetTextureLevelInfo) \
	X(gldGetVersion) X(gldInitDispatch) X(gldInitializeLibrary) \
	X(gldIsTextureResident) X(gldModifyPipelineProgram) \
	X(gldModifyTexture) X(gldModifyTextureLevel) X(gldModifyVertexArray) \
	X(gldPageoffBuffer) X(gldReclaimBuffer) X(gldReclaimContext) \
	X(gldReclaimFramebuffer) X(gldReclaimTexture) \
	X(gldReclaimVertexArray) X(gldRelatePipelineProgram) X(gldSetInteger) \
	X(gldSetMemoryPluginData) X(gldTerminateLibrary) \
	X(gldTestMemoryPluginData) X(gldTestObject) X(gldUpdateDispatch)

typedef long (*gld_fn)(long, long, long, long, long, long, long, long);

enum {
#define X(name) IDX_##name,
	GLD_ENTRIES(X)
#undef X
	GLD_COUNT
};

static const char *const names[GLD_COUNT] = {
#define X(name) #name,
	GLD_ENTRIES(X)
#undef X
};

static gld_fn real[GLD_COUNT];
static FILE *logf;
static int ready;
/*
 * RDN_GLD_PATCH: 1 changes the renderer ID in the renderer info, 2 also in
 * the pixel format. With 2, CGLCreateContext fails with kCGLBadPixelFormat
 * before the bundle is called, so it is off by default.
 */
static int patch = 1;

static void setup(void)
{
	const char *path = getenv("RDN_GLD_LOG");
	void *handle;
	int i;

	if (ready)
		return;
	ready = 1;
	if (getenv("RDN_GLD_PATCH"))
		patch = atoi(getenv("RDN_GLD_PATCH"));
	logf = fopen(path ? path : DEFAULT_LOG, "a");
	handle = dlopen(REAL_BUNDLE, RTLD_NOW | RTLD_LOCAL);
	if (logf)
		fprintf(logf, "[%d] RadeonNIGLDriver loaded, software renderer %s\n",
			(int)getpid(), handle ? "opened" : dlerror());
	for (i = 0; handle && i < GLD_COUNT; i++) {
		real[i] = (gld_fn)dlsym(handle, names[i]);
		if (!real[i] && logf)
			fprintf(logf, "[%d] missing %s\n", (int)getpid(), names[i]);
	}
	if (logf)
		fflush(logf);
}

static void dump(const char *what, long addr, int bytes)
{
	const unsigned long *p = (const unsigned long *)addr;
	int i;

	if (!logf || !addr)
		return;
	for (i = 0; i < bytes / 4; i++) {
		if (i % 8 == 0)
			fprintf(logf, "%s[%d]   %s+%03x:", i ? "\n" : "",
				(int)getpid(), what, i * 4);
		fprintf(logf, " %08lx", p[i]);
	}
	fprintf(logf, "\n");
	fflush(logf);
}

/* What is known about each call's arguments, shown after the call. */
static void describe(int idx, long a, long b, long c, long d, long ret)
{
	switch (idx) {
	case IDX_gldGetVersion:
		dump("version", a, 4); dump("version", b, 4);
		dump("version", c, 4); dump("version", d, 4);
		break;
	case IDX_gldGetRendererInfo:
		dump("rendinfo", a, 0x100);
		break;
	case IDX_gldChoosePixelFormat:
		dump("attrs", b, 0x40);
		if (a) {
			dump("pixfmt*", a, 4);
			dump("pixfmt", *(long *)a, 0x80);
		}
		break;
	case IDX_gldCreateShared:
	case IDX_gldCreateContext:
		dump("out", a, 4);
		break;
	case IDX_gldInitDispatch:
	case IDX_gldUpdateDispatch:
		dump("arg0", a, 0x40);
		dump("arg1", b, 0x40);
		dump("arg2", c, 0x40);
		break;
	case IDX_gldGetString:
		if (logf && ret)
			fprintf(logf, "[%d]   string \"%s\"\n", (int)getpid(),
				(const char *)ret);
		break;
	}
}

/*
 * The dispatch table takeover. The engine's own entries are kept so that
 * the replacement can still use them.
 */
static void (*engine_clear)(GLIContext, GLbitfield);
static void (*engine_clear_color)(GLIContext, GLclampf, GLclampf, GLclampf,
				  GLclampf);

static void rdn_clear(GLIContext ctx, GLbitfield mask)
{
	engine_clear_color(ctx, 1.0f, 0.0f, 1.0f, 1.0f);
	engine_clear(ctx, mask);
}

static void take_over(GLIFunctionDispatch *disp)
{
	if (!disp || disp->clear == rdn_clear)
		return;
	engine_clear = disp->clear;
	engine_clear_color = disp->clear_color;
	disp->clear = rdn_clear;
	if (logf)
		fprintf(logf, "[%d]   dispatch table %p: clear replaced\n",
			(int)getpid(), (void *)disp);
}

/*
 * Experiment: RDN_GLD_SCAN=<hex value> looks for that word in the engine's
 * context, which the table given to gldInitDispatch lies inside.
 */
#define ENGINE_CTX_TABLE_OFFSET	0x4698
#define APP_DISPATCH_BACK	0x18
#define ENGINE_CTX_SCAN_BYTES	0x1a000

static void scan(long table)
{
	const char *want = getenv("RDN_GLD_SCAN");
	unsigned long value, *base;
	int i;

	if (!want || !logf)
		return;
	value = strtoul(want, NULL, 16);
	base = (unsigned long *)(table - ENGINE_CTX_TABLE_OFFSET);
	for (i = 0; i < ENGINE_CTX_SCAN_BYTES / 4; i++)
		if (base[i] >= value - 4 && base[i] <= value + 0xab8)
			fprintf(logf, "[%d]   engine context %p +%05x: %08lx\n",
				(int)getpid(), (void *)base, i * 4, base[i]);
	fflush(logf);
}

/* Changes made to what the software renderer answered. */
static long adjust(int idx, long a, long b, long ret)
{
	switch (idx) {
	case IDX_gldGetRendererInfo:
		/* Word 1 is the low half of the renderer ID. */
		if (ret == 0 && a && (patch & 1))
			((long *)a)[1] = RDN_RENDERER_ID & 0xffff;
		break;
	case IDX_gldChoosePixelFormat:
		/* Word 1 of the pixel format is the whole renderer ID. */
		if (ret == 0 && a && *(long **)a && (patch & 2))
			(*(long **)a)[1] = RDN_RENDERER_ID;
		break;
	case IDX_gldInitDispatch:
	case IDX_gldUpdateDispatch:
		/*
		 * The table passed here is one the driver fills for the
		 * engine, inside the engine's context. Six words before
		 * it the engine keeps a pointer to the application's GL
		 * dispatch table (the public GLIFunctionDispatch inside
		 * the CGL context object). 10.4.11, found by search.
		 */
		scan(b);
		if (b)
			take_over(*(GLIFunctionDispatch **)(b - APP_DISPATCH_BACK));
		break;
	case IDX_gldGetString:
		if (b == GL_RENDERER)
			return (long)RDN_RENDERER_STRING;
		break;
	}
	return ret;
}

static long forward(int idx, long a, long b, long c, long d, long e, long f,
		    long g, long h)
{
	long ret = -1;
	int restore_id = 0;

	setup();
	if (logf) {
		fprintf(logf, "[%d] %s(%lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx)\n",
			(int)getpid(), names[idx], a, b, c, d, e, f, g, h);
		fflush(logf);
	}
	/*
	 * gldCreateContext gets a pointer to the pixel format's renderer ID
	 * word. The software renderer refuses an ID that is not its own.
	 */
	if (idx == IDX_gldCreateContext && b &&
	    *(long *)b == RDN_RENDERER_ID) {
		*(long *)b = SOFTWARE_RENDERER_ID;
		restore_id = 1;
	}
	if (real[idx])
		ret = real[idx](a, b, c, d, e, f, g, h);
	if (restore_id)
		*(long *)b = RDN_RENDERER_ID;
	if (logf) {
		fprintf(logf, "[%d]   %s -> %lx\n", (int)getpid(), names[idx], ret);
		fflush(logf);
	}
	describe(idx, a, b, c, d, ret);
	ret = adjust(idx, a, b, ret);
	return ret;
}

#define X(name) \
	long name(long a, long b, long c, long d, long e, long f, long g, long h) \
	{ \
		return forward(IDX_##name, a, b, c, d, e, f, g, h); \
	}
GLD_ENTRIES(X)
#undef X
