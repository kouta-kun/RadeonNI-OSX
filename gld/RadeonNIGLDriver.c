/*
 * RadeonNIGLDriver: the OpenGL driver bundle of the Radeon HD 7570 driver.
 *
 * Built two ways. In the guest, by gld/Makefile: the pass-through and call
 * logger described below, for learning the interface. On the host, with
 * RDN_MESA defined and linked with Mesa (mesa/target/meson.build): the
 * same pass-through keeps Apple's engine supplied with a driver, while
 * rdn_mesa.c gives each context's GL entry points to Mesa's r600.
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
#include <string.h>
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

/* Bit 8 of word 2, in the renderer info and in the pixel format alike. */
#define RECORD_ACCELERATED	0x100
#ifdef RDN_MESA
#define RDN_CLAIMS_ACCELERATED	1
#else
#define RDN_CLAIMS_ACCELERATED	0
#endif

/* What the card's aperture holds; the kext hands out most of it. */
#define RDN_REPORTED_MEMORY	(256ul << 20)

/* The entry of the driver's own table the engine calls to present. */
#define DRIVER_TABLE_PRESENT	24

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
/* A pixel format record this bundle allocated itself, if any. */
static void *own_format;
static FILE *logf;
static int ready;
/*
 * RDN_GLD_PATCH: 1 changes the renderer ID in the renderer info, 2 also in
 * the pixel format. With 2, CGLCreateContext fails with kCGLBadPixelFormat
 * before the bundle is called, so it is off by default.
 */
#ifdef RDN_MESA
/* With Mesa inside, the bundle is a renderer of its own, and accelerated. */
static int patch = 7;
#else
static int patch = 1;
#endif

#ifdef RDN_MESA
#include <stdarg.h>
#include "rdn_glue.h"

int rdn_trace;

void rdn_log(const char *fmt, ...)
{
	va_list ap;

	if (!logf)
		return;
	fprintf(logf, "[%d] ", (int)getpid());
	va_start(ap, fmt);
	vfprintf(logf, fmt, ap);
	va_end(ap);
	fprintf(logf, "\n");
	fflush(logf);
}
#endif

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
#ifdef RDN_MESA
	/*
	 * Logging every call is slow; with Mesa only when asked for, by the
	 * environment or, for processes whose environment is not ours to
	 * set (the window server), by the existence of /tmp/rdngld.on,
	 * which gives each process its own /tmp/rdngld.<pid>.log.
	 */
	if (path) {
		logf = fopen(path, "a");
	} else if (access("/tmp/rdngld.on", F_OK) == 0) {
		char name[64];

		snprintf(name, sizeof(name), "/tmp/rdngld.%d.log", (int)getpid());
		logf = fopen(name, "a");
	}
	rdn_trace = logf && access("/tmp/rdngld.trace", F_OK) == 0;
#else
	logf = fopen(path ? path : DEFAULT_LOG, "a");
#endif
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
#ifdef RDN_MESA
	/* See rdn_hook.c. The window server's context always has a drawable. */
	if (strcmp(getprogname(), "WindowServer") != 0)
		rdn_hook_set_current(rdn_mesa_early_all);
#endif
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
		dump("attrs", b, 0xa0);
		if (a) {
			dump("pixfmt*", a, 4);
			dump("pixfmt", *(long *)a, 0x80);
		}
		break;
	case IDX_gldAttachDrawable:
		dump("drawable", c, 0x80);
		dump("ctx", a, 0x60);
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

/*
 * Experiment (RDN_GLD_TABLE=1): count the engine's calls into the table
 * the driver fills at gldInitDispatch, to learn what each entry is for.
 * The wrappers only count before passing the call on, so that arguments
 * in floating-point registers survive.
 */
#define DRIVER_TABLE_ENTRIES 33
static gld_fn table_real[DRIVER_TABLE_ENTRIES];
static unsigned long table_calls[DRIVER_TABLE_ENTRIES];

#define TABLE_WRAPPERS(X) \
	X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) \
	X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) \
	X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) X(32)
#define X(n) \
	static long table_wrap_##n(long a, long b, long c, long d, long e, \
				   long f, long g, long h) \
	{ \
		table_calls[n]++; \
		return table_real[n](a, b, c, d, e, f, g, h); \
	}
TABLE_WRAPPERS(X)
#undef X
static const gld_fn table_wrap[DRIVER_TABLE_ENTRIES] = {
#define X(n) table_wrap_##n,
	TABLE_WRAPPERS(X)
#undef X
};

static void table_report(void)
{
	int i;

	if (!logf)
		return;
	fprintf(logf, "[%d] driver table calls:", (int)getpid());
	for (i = 0; i < DRIVER_TABLE_ENTRIES; i++)
		if (table_calls[i])
			fprintf(logf, " [%d]=%lu", i, table_calls[i]);
	fprintf(logf, "\n");
	fflush(logf);
}

static void table_hook(long table)
{
	gld_fn *t = (gld_fn *)table;
	static int registered;
	int i;

	if (!getenv("RDN_GLD_TABLE") || !t)
		return;
	for (i = 0; i < DRIVER_TABLE_ENTRIES; i++) {
		if (!t[i] || t[i] == table_wrap[i])
			continue;
		table_real[i] = t[i];
		t[i] = table_wrap[i];
	}
	if (!registered) {
		registered = 1;
		atexit(table_report);
	}
}

#ifdef RDN_MESA
/*
 * Presentation. The engine calls the software renderer through the driver
 * table to put a window's buffer on screen. Mesa has to have finished its
 * frame, and copied it into that buffer, before the software renderer
 * hands it to the window server.
 */
static gld_fn present_real;
static void *present_ctx;

static long present_wrap(long a, long b, long c, long d, long e, long f,
			 long g, long h)
{
	rdn_mesa_present(present_ctx);
	/* No software renderer behind the window server's context. */
	if (!present_real)
		return 0;
	return present_real(a, b, c, d, e, f, g, h);
}

static void present_hook(void *gld_ctx, long table)
{
	gld_fn *t = (gld_fn *)table;

	present_ctx = gld_ctx;
	if (t[DRIVER_TABLE_PRESENT] != present_wrap) {
		present_real = t[DRIVER_TABLE_PRESENT];
		t[DRIVER_TABLE_PRESENT] = present_wrap;
	}
}

/* The context of the window server that draws on the screen, if any. */
static void *screen_ctx;

/* gldCreateContext's sixth argument, less this, is the engine's context. */
#define ENGINE_CTX_CREATE_ARG	0x360

static int in_window_server(void)
{
	static int known = -1;

	if (known < 0)
		known = strcmp(getprogname(), "WindowServer") == 0;
	return known;
}
#endif

/* Changes made to what the software renderer answered. */
static long adjust(int idx, long a, long b, long c, long d, long ret)
{
	switch (idx) {
	case IDX_gldGetVersion:
		/*
		 * The fourth value is the low half of the renderer ID; the
		 * engine files the plug-in under it.
		 */
		if (ret && d && (patch & 4))
			*(long *)d = RDN_RENDERER_ID & 0xffff;
		break;
	case IDX_gldGetRendererInfo:
		/* Word 1 is the low half of the renderer ID. */
		if (ret == 0 && a && (patch & 1))
			((long *)a)[1] = RDN_RENDERER_ID & 0xffff;
		if (ret == 0 && a && RDN_CLAIMS_ACCELERATED) {
			((unsigned long *)a)[2] |= RECORD_ACCELERATED;
			/* Words 12 and 13: video and texture memory, in bytes. */
			((unsigned long *)a)[12] = RDN_REPORTED_MEMORY;
			((unsigned long *)a)[13] = RDN_REPORTED_MEMORY;
		}
		/* Experiment: RDN_GLD_INFO="word:xor[,word:xor]" flips bits. */
		if (ret == 0 && a && getenv("RDN_GLD_INFO")) {
			const char *p = getenv("RDN_GLD_INFO");

			while (*p) {
				char *end;
				long word = strtol(p, &end, 0);
				unsigned long x = *end == ':' ? strtoul(end + 1, &end, 0) : 0;

				if (word >= 0 && word < 16)
					((unsigned long *)a)[word] ^= x;
				p = *end == ',' ? end + 1 : end;
				if (p == end && *end)
					break;
			}
		}
		break;
	case IDX_gldChoosePixelFormat:
		/*
		 * The answer is a chain of records linked through word 0.
		 * Word 1 of each is the whole renderer ID, word 2 its flags.
		 * Every record has to carry our ID: CGL hands one that does
		 * not to the renderer it names, which then frees what it
		 * did not allocate.
		 */
		if (ret == 0 && a) {
			unsigned long *fmt = *(unsigned long **)a;
			int guard = 0;

			for (; fmt && guard < 64; fmt = (unsigned long *)fmt[0], guard++) {
				if (patch & 2)
					fmt[1] = RDN_RENDERER_ID;
				if (RDN_CLAIMS_ACCELERATED)
					fmt[2] |= RECORD_ACCELERATED;
			}
		}
		/* Experiment: RDN_GLD_PF="word:xor" flips bits of the format. */
		if (ret == 0 && a && *(long **)a && getenv("RDN_GLD_PF")) {
			char *end;
			long word = strtol(getenv("RDN_GLD_PF"), &end, 0);

			if (*end == ':' && word >= 0 && word < 16)
				(*(unsigned long **)a)[word] ^= strtoul(end + 1, NULL, 0);
		}
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
		table_hook(b);
#ifdef RDN_MESA
		if (b && rdn_mesa_dispatch((void *)a, (void *)b))
			present_hook((void *)a, b);
#else
		if (b)
			take_over(*(GLIFunctionDispatch **)(b - APP_DISPATCH_BACK));
#endif
		break;
#ifdef RDN_MESA
	case IDX_gldCreateContext:
		if (ret == 0 && a)
			rdn_mesa_context_created(*(void **)a);
		break;
	case IDX_gldAttachDrawable:
		rdn_mesa_attach((void *)a, b, (const void *)c);
		break;
#else
	case IDX_gldGetString:
		if (b == GL_RENDERER)
			return (long)RDN_RENDERER_STRING;
		break;
#endif
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
#ifdef RDN_MESA
	/* Mesa's context goes before the software renderer's own. */
	if (idx == IDX_gldDestroyContext)
		rdn_mesa_context_destroyed((void *)a);
	else if (idx != IDX_gldCreateContext && !in_window_server())
		rdn_mesa_early((void *)a);
	/*
	 * Inside the window server the software renderer cannot attach a
	 * drawable (it waits on the window server, that is, on itself). The
	 * window server's context draws on the screen: attach that
	 * ourselves, and keep the software renderer out of the dispatch
	 * set-up that follows. The return values are the ones the software
	 * renderer gives applications.
	 */
	if (in_window_server()) {
		if (idx == IDX_gldAttachDrawable) {
			/* The record's third word is the surface ID. */
			ret = rdn_mesa_attach_screen((void *)a,
				c ? ((const unsigned long *)c)[2] : 0) ? 2 : -1;
			screen_ctx = ret == 2 ? (void *)a : NULL;
			if (logf) {
				fprintf(logf, "[%d]   attached by us -> %lx\n", (int)getpid(), ret);
				dump("drawable", c, 0x80);
			}
			return ret;
		}
		if ((idx == IDX_gldInitDispatch || idx == IDX_gldUpdateDispatch) &&
		    screen_ctx == (void *)a) {
			if (b && rdn_mesa_dispatch((void *)a, (void *)b))
				present_hook((void *)a, b);
			if (logf) {
				fprintf(logf, "[%d]   dispatch set up by us\n", (int)getpid());
				fflush(logf);
			}
			return idx == IDX_gldInitDispatch ? 4 : 0;
		}
	}
#endif
	/*
	 * The renderer decides itself whether it satisfies an attribute
	 * list, and the software renderer turns down a request for an
	 * accelerated one. Experiment (RDN_GLD_ACCEL=1): take those
	 * attributes out before it sees the list.
	 */
	if (idx == IDX_gldChoosePixelFormat && b &&
	    (RDN_CLAIMS_ACCELERATED || getenv("RDN_GLD_ACCEL"))) {
		static long filtered[64];
		const long *in = (const long *)b;
		int n = 0;

		while (*in && n < 62) {
			/*
			 * kCGLPFAAccelerated and kCGLPFANoRecovery take no
			 * value. 20, 21 and 22 are not public; the window
			 * server asks for them with the value 8 each (taken
			 * to be the red, green and blue sizes), and the
			 * software renderer answers such a list with no
			 * format at all.
			 */
			if (*in == 20 || *in == 21 || *in == 22) {
				in += in[1] ? 2 : 1;
				continue;
			}
			/*
			 * kCGLPFASampleBuffers and kCGLPFASamples, each with
			 * a value: the software renderer answers them with a
			 * buffer twice the window's size each way, which the
			 * copy from Mesa does not handle. No multisampling
			 * until Mesa does it on the card.
			 */
			if (*in == 55 || *in == 56) {
				in += 2;
				continue;
			}
			if (*in != 73 && *in != 72)
				filtered[n++] = *in;
			in++;
		}
		filtered[n] = 0;
		b = (long)filtered;
	}
	if (idx == IDX_gldDestroyPixelFormat && a && (void *)a == own_format) {
		/* Ours, not the software renderer's, to free. */
		free(own_format);
		own_format = NULL;
		ret = 0;
	} else if (real[idx])
		ret = real[idx](a, b, c, d, e, f, g, h);
	if (restore_id)
		*(long *)b = RDN_RENDERER_ID;
	/*
	 * The window server's own request is one the software renderer
	 * answers with no format (seen: window, the three private
	 * attributes, depth 0). Ask it for any window format instead.
	 */
	if (idx == IDX_gldChoosePixelFormat && RDN_CLAIMS_ACCELERATED &&
	    ret == 0 && a && !*(long *)a && real[idx]) {
		/*
		 * Inside the window server the software renderer has no
		 * format for any request. Make the record ourselves, as the
		 * window server's own checks want it: 32-bit ARGB, no depth,
		 * no stencil, no auxiliary buffers, a window, accelerated,
		 * on every display. Thirteen words, as the software
		 * renderer's records are; freed by gldDestroyPixelFormat.
		 */
		unsigned long *fmt = calloc(13, sizeof(*fmt));

		if (fmt) {
			fmt[1] = RDN_RENDERER_ID;
			fmt[2] = 0x24dc | 0x1 | RECORD_ACCELERATED;
			fmt[4] = 0x8000;	/* colour mode: ARGB 8888 */
			fmt[6] = 1;		/* depth mode: none */
			fmt[7] = 1;		/* stencil mode: none */
			fmt[12] = 0xffffffff;	/* display mask */
			*(unsigned long **)a = fmt;
			own_format = fmt;
		}
		if (logf) {
			fprintf(logf, "[%d]   made a format of our own -> %lx, %lx\n",
				(int)getpid(), ret, *(long *)a);
			if (ret == 0 && *(long *)a)
				dump("pixfmt", *(long *)a, 0x80);
		}
	}
	if (logf) {
		fprintf(logf, "[%d]   %s -> %lx\n", (int)getpid(), names[idx], ret);
		fflush(logf);
	}
	describe(idx, a, b, c, d, ret);
	ret = adjust(idx, a, b, c, d, ret);
#ifdef RDN_MESA
	/*
	 * The sixth argument of gldCreateContext points 0x360 bytes into
	 * the engine's context (10.4.11, by comparison with the table
	 * gldInitDispatch is given later).
	 */
	if (idx == IDX_gldCreateContext && ret == 0 && a && f &&
	    !in_window_server())
		rdn_mesa_context_engine(*(void **)a,
					(char *)f - ENGINE_CTX_CREATE_ARG);
#endif
	return ret;
}

#define X(name) \
	__attribute__((visibility("default"))) \
	long name(long a, long b, long c, long d, long e, long f, long g, long h); \
	long name(long a, long b, long c, long d, long e, long f, long g, long h) \
	{ \
		return forward(IDX_##name, a, b, c, d, e, f, g, h); \
	}
GLD_ENTRIES(X)
#undef X
