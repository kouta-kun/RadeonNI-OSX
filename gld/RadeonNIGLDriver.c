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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mach/mach.h>
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
#define RECORD_FULLSCREEN	0x2
/* Bit 11 of the pixel format's word 2: kCGLPFAAuxDepthStencil (57). */
#define RECORD_AUX_DEPTH_STENCIL	0x800
/* Depth modes (word 6 of the renderer info): a buffer of 24 bits. */
#define DEPTH_MODE_24		0x800
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

/*
 * Every log line starts with the process and the calling thread: the name
 * of the thread's Mach port in this process, a short number that is the
 * thread's for as long as it lives.
 */
#define TAG		"[%d:%x]"
#define TAG_ARGS	(int)getpid(), thread_tag()

static unsigned thread_tag(void)
{
	return (unsigned)pthread_mach_thread_np(pthread_self());
}

/*
 * More of everything in the log: set when /tmp/rdngld.trace exists (and
 * always in the pass-through build, which is only ever run to read its log).
 */
static int log_all;

#ifdef RDN_MESA
#include <stdarg.h>
#include "rdn_glue.h"

int rdn_logging;
int rdn_trace;

void rdn_log(const char *fmt, ...)
{
	va_list ap;

	if (!logf)
		return;
	/* One line, also when another thread logs at the same time. */
	flockfile(logf);
	fprintf(logf, TAG " ", TAG_ARGS);
	va_start(ap, fmt);
	vfprintf(logf, fmt, ap);
	va_end(ap);
	fprintf(logf, "\n");
	fflush(logf);
	funlockfile(logf);
}

static void kept_at_exit(void)
{
	rdn_dispatch_kept_report("exit");
}
#endif

int rdn_trace_wanted(const char *name)
{
	static const char *only;
	static int looked;
	const char *at;
	size_t len = strlen(name);

	if (!looked) {
		only = getenv("RDN_GLD_TRACE_ONLY");
		looked = 1;
	}
	if (!only)
		return 1;
	for (at = only; (at = strstr(at, name)) != NULL; at += len)
		if ((at == only || at[-1] == ',') &&
		    (at[len] == 0 || at[len] == ','))
			return 1;
	return 0;
}

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
	log_all = rdn_trace;
	/* The entries Mesa lacks are counted while there is a log. */
	rdn_logging = logf != NULL;
	if (logf)
		atexit(kept_at_exit);
#else
	logf = fopen(path ? path : DEFAULT_LOG, "a");
	log_all = 1;
#endif
	handle = dlopen(REAL_BUNDLE, RTLD_NOW | RTLD_LOCAL);
	if (logf)
		fprintf(logf, TAG " RadeonNIGLDriver loaded in %s, software renderer %s\n",
			TAG_ARGS, getprogname(), handle ? "opened" : dlerror());
	for (i = 0; handle && i < GLD_COUNT; i++) {
		real[i] = (gld_fn)dlsym(handle, names[i]);
		if (!real[i] && logf)
			fprintf(logf, TAG " missing %s\n", TAG_ARGS, names[i]);
	}
	if (logf)
		fflush(logf);
#ifdef RDN_MESA
	/* See rdn_hook.c. The window server's context always has a drawable. */
	if (strcmp(getprogname(), "WindowServer") != 0) {
		rdn_hook_set_current(rdn_mesa_early_all);
		if (!getenv("RDN_NO_PBUFFER")) {
			rdn_hook_tex_image_pbuffer(rdn_mesa_tex_image_pbuffer);
			rdn_hook_destroy_pbuffer(rdn_mesa_pbuffer_destroyed);
		}
		if (logf)
			rdn_hook_cgl_log();
	} else if (rdn_ws_core_image()) {
		/* Core Image in the window server: cglsTexImagePBuffer. */
		rdn_hook_cgls_tex_image(rdn_mesa_tex_image_pbuffer_ws);
		rdn_hook_cgls_set_integer(rdn_mesa_cgls_set_integer);
		rdn_hook_destroy_pbuffer(rdn_mesa_pbuffer_destroyed);
	}
#endif
}

/*
 * Read memory that may not be there, for the log: through the kernel, which
 * refuses where reading it here would end the process. All of it or, if
 * that fails, as far as the end of the page it starts in. Returns how many
 * bytes were read.
 */
#define DUMP_MAX	0x100

static int peek(long addr, void *to, int bytes)
{
	int in_page = (int)(vm_page_size - ((unsigned long)addr & (vm_page_size - 1)));
	vm_size_t got = 0;

	if (vm_read_overwrite(mach_task_self(), (vm_address_t)addr,
			      (vm_size_t)bytes, (vm_address_t)to, &got) == KERN_SUCCESS)
		return (int)got;
	if (in_page < bytes &&
	    vm_read_overwrite(mach_task_self(), (vm_address_t)addr,
			      (vm_size_t)in_page, (vm_address_t)to, &got) == KERN_SUCCESS)
		return (int)got;
	return 0;
}

/* An argument that may be the address of something: aligned, not small. */
static int pointer_like(long value)
{
	return (value & 3) == 0 && (unsigned long)value >= 0x1000;
}

/* `quiet`: say nothing if there is nothing to read at the address. */
static void dump_from(const char *what, long addr, int bytes, int quiet)
{
	unsigned long words[DUMP_MAX / 4];
	int i, got;

	if (!logf || !addr)
		return;
	if (bytes > DUMP_MAX)
		bytes = DUMP_MAX;
	got = peek(addr, words, bytes);
	if (got < 4) {
		if (!quiet)
			fprintf(logf, TAG "   %s: nothing to read at %lx\n",
				TAG_ARGS, what, addr);
		return;
	}
	flockfile(logf);
	for (i = 0; i < got / 4; i++) {
		if (i % 8 == 0)
			fprintf(logf, "%s" TAG "   %s+%03x:", i ? "\n" : "",
				TAG_ARGS, what, i * 4);
		fprintf(logf, " %08lx", words[i]);
	}
	fprintf(logf, "\n");
	fflush(logf);
	funlockfile(logf);
}

static void dump(const char *what, long addr, int bytes)
{
	dump_from(what, addr, bytes, 0);
}

/*
 * The objects the driver made, remembered while there is a log so that the
 * arguments of later calls can be recognised: which of gldCreateContext's
 * is the shared object, what a texture call is given first.
 */
#define MAX_KNOWN	64
static struct {
	long value;
	const char *what;
	unsigned number;
} known[MAX_KNOWN];

static void known_add(const char *what, long value)
{
	static unsigned made;
	int i, at = -1;

	if (!logf || !value)
		return;
	for (i = 0; i < MAX_KNOWN; i++) {
		if (known[i].value == value)
			at = i;
		else if (at < 0 && !known[i].value)
			at = i;
	}
	if (at < 0)
		at = made % MAX_KNOWN;
	known[at].value = value;
	known[at].what = what;
	known[at].number = ++made;
	fprintf(logf, TAG "   %lx is %s %u from here on\n", TAG_ARGS, value, what,
		known[at].number);
}

static int known_find(long value)
{
	int i;

	for (i = 0; value && i < MAX_KNOWN; i++)
		if (known[i].value == value)
			return i;
	return -1;
}

static void known_remove(long value)
{
	int i = known_find(value);

	if (i >= 0)
		known[i].value = 0;
}

/* " [0 context 3, 2 shared 1]": the arguments that are known objects. */
static void known_args(char *text, size_t size, const long *args)
{
	size_t used = 0;
	int i, k;

	text[0] = 0;
	for (i = 0; i < 8; i++) {
		k = known_find(args[i]);
		if (k < 0 || used + 40 > size)
			continue;
		used += snprintf(text + used, size - used, "%s%d %s %u",
				 used ? ", " : " [", i, known[k].what, known[k].number);
	}
	if (used)
		snprintf(text + used, size - used, "]");
}

/*
 * What an argument points at: the first `bytes` of it, and any known object
 * whose address is among them.
 */
static void dump_arg(int arg, long value, int bytes)
{
	unsigned long words[DUMP_MAX / 4];
	char what[16];
	int i, k, got;

	if (!logf || !pointer_like(value) || known_find(value) >= 0)
		return;
	if (bytes > DUMP_MAX)
		bytes = DUMP_MAX;
	snprintf(what, sizeof(what), "arg%d", arg);
	dump_from(what, value, bytes, 1);
	got = peek(value, words, bytes);
	for (i = 0; i < got / 4; i++) {
		k = known_find((long)words[i]);
		if (k >= 0)
			fprintf(logf, TAG "   arg%d+%03x is %s %u\n", TAG_ARGS, arg,
				i * 4, known[k].what, known[k].number);
	}
}

/* gldAttachDrawable's type is the CGL pixel format attribute of the kind. */
static const char *drawable_name(long type)
{
	switch (type) {
	case 0:    return "none";
	case 0x35: return "off-screen, kCGLPFAOffScreen";
	case 0x36: return "full screen, kCGLPFAFullScreen";
	case 0x50: return "window, kCGLPFAWindow";
	case 0x5a: return "pbuffer, kCGLPFAPBuffer";
	case 0x5b: return "remote pbuffer, kCGLPFARemotePBuffer";
	}
	return "not seen before";
}

static int drawable_understood(long type)
{
	return type == 0x35 || type == 0x36 || type == 0x50;
}

/*
 * A drawable of a kind the bundle does nothing with yet: the whole record,
 * and what its first words point at, where they point at anything.
 */
static void dump_drawable(const char *when, long type, long record)
{
	unsigned long words[16];
	char what[24];
	int i, got;

	if (!logf)
		return;
	fprintf(logf, TAG "   drawable type 0x%lx (%s), record %lx %s\n", TAG_ARGS,
		type, drawable_name(type), record, when);
	if (!pointer_like(record))
		return;
	dump("drawable", record, 0x100);
	got = peek(record, words, sizeof(words));
	for (i = 0; i < got / 4; i++) {
		if (!pointer_like((long)words[i]) || known_find((long)words[i]) >= 0)
			continue;
		snprintf(what, sizeof(what), "drawable[%d]", i);
		dump_from(what, (long)words[i], 0x40, 1);
	}
}

/*
 * gldGetInteger and gldSetInteger: the parameter's number, with CGL's name
 * for it if CGL has a context parameter of that number (not known to be
 * the same numbers), and the first words of the values.
 */
static void dump_parameter(const char *call, long parameter, long values)
{
	static const struct { long number; const char *name; } cgl[] = {
		{ 200, "kCGLCPSwapRectangle" }, { 222, "kCGLCPSwapInterval" },
		{ 224, "kCGLCPDispatchTableSize" }, { 226, "kCGLCPClientStorage" },
		{ 228, "kCGLCPSurfaceTexture" }, { 235, "kCGLCPSurfaceOrder" },
		{ 236, "kCGLCPSurfaceOpacity" }, { 304, "kCGLCPSurfaceBackingSize" },
		{ 306, "kCGLCPSurfaceSurfaceVolatile" },
		{ 308, "kCGLCPReclaimResources" }, { 309, "kCGLCPCurrentRendererID" },
		{ 310, "kCGLCPGPUVertexProcessing" },
		{ 311, "kCGLCPGPUFragmentProcessing" },
	};
	const char *name = "";
	unsigned i;

	if (!logf)
		return;
	for (i = 0; i < sizeof(cgl) / sizeof(cgl[0]); i++)
		if (cgl[i].number == parameter)
			name = cgl[i].name;
	fprintf(logf, TAG "   %s parameter %ld (0x%lx) %s\n", TAG_ARGS, call,
		parameter, parameter, name);
	if (pointer_like(values))
		dump_from("values", values, 0x20, 1);
}

/* gldCreateContext's sixth argument, less this, is the engine's context. */
#define ENGINE_CTX_CREATE_ARG	0x360

/* Not more than this many detailed descriptions of a call that is frequent. */
#define DETAILS_LOGGED	64

/*
 * What is known about each call's arguments, shown after the call. `args`
 * are the eight register arguments; a call that takes fewer leaves whatever
 * was in the registers in the rest.
 */
static void describe(int idx, const long *args, long ret)
{
	static unsigned details[GLD_COUNT];
	long a = args[0], b = args[1], c = args[2], d = args[3];
	long made = 0;
	int i;

	if (!logf)
		return;
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
		if (drawable_understood(b))
			dump("drawable", c, 0x80);
		else
			dump_drawable("after the call", b, c);
		dump("ctx", a, 0x60);
		break;
	case IDX_gldCreateShared:
		/*
		 * The first argument is where the new object goes; what the
		 * next three are is not known.
		 */
		dump("out", a, 4);
		if (ret == 0 && peek(a, &made, sizeof(made)) == sizeof(made) && made) {
			known_add("shared", made);
			dump_from("shared", made, 0x80, 1);
		}
		for (i = 1; i < 4; i++)
			dump_arg(i, args[i], 0x40);
		break;
	case IDX_gldDestroyShared:
		dump_from("shared", a, 0x80, 1);
		break;
	case IDX_gldCreateContext:
		/*
		 * Where the new context goes, the pixel format's renderer ID
		 * word, and pointers into the engine's context (the sixth
		 * for certain). One of them should lead to the shared object.
		 */
		dump("out", a, 4);
		if (ret == 0 && peek(a, &made, sizeof(made)) == sizeof(made) && made) {
			known_add("context", made);
			dump_from("context", made, 0x40, 1);
		}
		if (pointer_like(args[5]))
			fprintf(logf, TAG "   engine context %lx, if arg5 is what it was\n",
				TAG_ARGS, args[5] - ENGINE_CTX_CREATE_ARG);
		for (i = 1; i < 8; i++)
			dump_arg(i, args[i], 0x40);
		break;
	case IDX_gldGetInteger:
		dump_parameter("gldGetInteger got", b, c);
		break;
	case IDX_gldCreateTexture:
	case IDX_gldModifyTexture:
	case IDX_gldModifyTextureLevel:
	case IDX_gldGetTextureLevel:
		/*
		 * Which argument is the texture's record is not known: what
		 * each of the first four points at, unless it is an object
		 * named above.
		 */
		if (!log_all && details[idx]++ >= DETAILS_LOGGED)
			break;
		for (i = 0; i < 4; i++)
			dump_arg(i, args[i], 0x80);
		break;
	case IDX_gldInitDispatch:
	case IDX_gldUpdateDispatch:
		dump("arg0", a, 0x40);
		dump("arg1", b, 0x40);
		dump("arg2", c, 0x40);
		break;
	case IDX_gldGetString:
		if (ret)
			fprintf(logf, TAG "   string \"%s\"\n", TAG_ARGS,
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
		fprintf(logf, TAG "   dispatch table %p: clear replaced\n",
			TAG_ARGS, (void *)disp);
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
			fprintf(logf, TAG "   engine context %p +%05x: %08lx\n",
				TAG_ARGS, (void *)base, i * 4, base[i]);
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
	fprintf(logf, TAG " driver table calls:", TAG_ARGS);
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

/*
 * For a context the software renderer never set up (a window taken as a
 * surface), the engine's driver table is empty. Every entry gets a
 * function that says it was called (the first few times, when logging)
 * and does nothing, so that what the engine wants of it can be seen; the
 * one that presents is then replaced by present_hook().
 */
static unsigned stub_calls[33];

/* How many calls of each entry are logged; more with the GL trace on. */
#define STUB_LOGGED		6
#define STUB_LOGGED_TRACE	1000

static long table_stub(int entry, long a, long b, long c, long d)
{
	if (logf && stub_calls[entry]++ < (rdn_trace ? STUB_LOGGED_TRACE : STUB_LOGGED)) {
		fprintf(logf, TAG " driver table entry %d called (%lx, %lx, %lx, %lx)\n",
			TAG_ARGS, entry, a, b, c, d);
		fflush(logf);
	}
	return 0;
}

#define S(n) static long table_stub_##n(long a, long b, long c, long d, long e, \
				       long f, long g, long h) \
	{ return table_stub(n, a, b, c, d); }
S(0) S(1) S(2) S(3) S(4) S(5) S(6) S(7) S(8) S(9) S(10) S(11) S(12) S(13) S(14) S(15) S(16) S(17) S(18) S(19) S(20) S(21) S(22) S(23) S(24) S(25) S(26) S(27) S(28) S(29) S(30) S(31) S(32) 
#undef S

static void table_fill(long table)
{
	static const gld_fn stubs[33] = { table_stub_0, table_stub_1, table_stub_2, table_stub_3, table_stub_4, table_stub_5, table_stub_6, table_stub_7, table_stub_8, table_stub_9, table_stub_10, table_stub_11, table_stub_12, table_stub_13, table_stub_14, table_stub_15, table_stub_16, table_stub_17, table_stub_18, table_stub_19, table_stub_20, table_stub_21, table_stub_22, table_stub_23, table_stub_24, table_stub_25, table_stub_26, table_stub_27, table_stub_28, table_stub_29, table_stub_30, table_stub_31, table_stub_32 };
	gld_fn *t = (gld_fn *)table;
	int i;

	for (i = 0; i < 33; i++)
		if (!t[i])
			t[i] = stubs[i];
}

/* The context of the window server that draws on the screen, if any. */
static void *screen_ctx;

/*
 * Does the window server composite with OpenGL (Quartz Extreme) on some
 * display? Then a window's buffer holds its top row first; without, the
 * bottom row (both seen on the monitor: a GLUT triangle before Quartz
 * Extreme worked, Chess after).
 */
int rdn_windows_top_down(void)
{
	static int known = -1;
	int (*list)(unsigned max, unsigned *displays, unsigned *count) =
		(int (*)(unsigned, unsigned *, unsigned *))
		dlsym(RTLD_DEFAULT, "CGGetActiveDisplayList");
	int (*uses)(unsigned display) =
		(int (*)(unsigned))dlsym(RTLD_DEFAULT, "CGDisplayUsesOpenGLAcceleration");
	unsigned displays[8], count = 0, i;

	if (known >= 0)
		return known;
	known = 0;
	if (list && uses && !list(8, displays, &count))
		for (i = 0; i < count; i++)
			if (uses(displays[i]))
				known = 1;
	return known;
}

/*
 * A program started in the login session, with Quartz Extreme working,
 * gets a window drawable described by connection, window and surface
 * alone (the record's fourth word is 2, and no buffer follows). The
 * bundle cannot draw on that yet, so such a context has to stay with the
 * software renderer, and so no context may be given to Mesa before its
 * drawable is known. RDN_GLD_EARLY=1, or a file /tmp/rdngld.early, does it
 * anyway: right for programs
 * whose record names a buffer (those started from an ssh session), which
 * otherwise lose what they set up before their window existed.
 */
static int app_surfaces(void);

static int early_takeover(void)
{
	/* The file is for programs whose environment is not ours to set. */
	return app_surfaces() || getenv("RDN_GLD_EARLY") != NULL ||
	       access("/tmp/rdngld.early", F_OK) == 0;
}

/*
 * Tell the window server that a surface has a new picture
 * (CGSFlushSurface with no region: all of it; what OpenGL's own
 * glcDoNonSimpleFlush does). It then either draws that part of the screen
 * again or, more often, flushes the surface through the kext's surface
 * client and leaves the showing to the driver.
 */
void rdn_surface_flush(unsigned long cid, unsigned long wid, unsigned long sid)
{
	int (*flush)(unsigned long, unsigned long, unsigned long, void *) =
		(int (*)(unsigned long, unsigned long, unsigned long, void *))
		dlsym(RTLD_DEFAULT, "CGSFlushSurface");

	if (flush)
		flush(cid, wid, sid, NULL);
}

static long (*bind_surface)(long, long, long, long, long);

/*
 * The size of a window's surface, from the window server
 * (CGSGetSurfaceBounds, which the software renderer uses too; its last
 * argument is taken to be a rectangle of four floats). The kext only
 * learns the surface's place a moment after it is bound, and a program
 * may draw its one frame before that.
 */
int rdn_surface_size(unsigned long cid, unsigned long wid, unsigned long sid,
		     unsigned *width, unsigned *height)
{
	int (*bounds)(unsigned long, unsigned long, unsigned long, float *) =
		(int (*)(unsigned long, unsigned long, unsigned long, float *))
		dlsym(RTLD_DEFAULT, "CGSGetSurfaceBounds");
	float r[4] = { 0, 0, 0, 0 };

	if (!bounds || bounds(cid, wid, sid, r) || r[2] < 1 || r[3] < 1 ||
	    r[2] > 16384 || r[3] > 16384)
		return 0;
	*width = (unsigned)r[2];
	*height = (unsigned)r[3];
	return 1;
}

/*
 * Windows as surfaces (see forward()), and full-screen contexts with them.
 * On unless RDN_GLD_NOSURFACE is set or a file /tmp/rdngld.nosurface
 * exists, which give the older path through the software renderer's
 * buffer.
 */
static int app_surfaces(void)
{
	static int known = -1;

	if (known < 0)
		known = getenv("RDN_GLD_NOSURFACE") == NULL &&
			access("/tmp/rdngld.nosurface", F_OK) != 0;
	return known;
}
static const long *display_words;

/* Samples of the last pixel format a program asked for; 0: none. */
static int asked_samples;
/* The last request had kCGLPFAAuxDepthStencil. */
static int asked_aux_depth_stencil;

static int in_window_server(void)
{
	static int known = -1;

	if (known < 0)
		known = strcmp(getprogname(), "WindowServer") == 0;
	return known;
}
#endif

#ifdef RDN_MESA
/*
 * The window server takes GL_ARB_fragment_program in its list to mean
 * that Core Image works on the display: that one name is what System
 * Profiler prints as "Core Image: Supported" (CGSServerOperationState
 * 0xf) and what makes the Dock ask for Dashboard's ripple. It does not
 * work: Core Image does not use a renderer it does not know, and a filter
 * put on a window changes nothing on the screen (tools/guest/wsfilter.c).
 * So the window server is not told the name, and reports no Core Image.
 * Fragment programs themselves stay as they are, for it and for every
 * program. RDN_CI_FILE, there when the window server starts: the name
 * stays in.
 *
 * The window server asks twice. First right after it has made its
 * context, before the table is Mesa's (tools/guest/earlyext.c shows that
 * moment): OpenGL's engine answers then, with a list it makes itself from
 * a bit for each extension, in the record gldCreateContext's fifth
 * argument points to and the software renderer fills in. So that bit is
 * cleared when the context has been made (forward()). Later questions get
 * Mesa's list, which leaves the name out as well (gen_dispatch.py).
 */
#define RDN_CI_FILE "/Library/Application Support/RadeonNI/coreimage"
/* The words of extension bits in that record, and the bit. */
#define ENGINE_FEATURES			0x124
#define ENGINE_FEATURE_FRAGMENT_PROGRAM	0x8000ul

int rdn_ws_no_core_image(void)
{
	static int no = -1;

	if (no < 0)
		no = in_window_server() && access(RDN_CI_FILE, F_OK) != 0;
	return no;
}

/*
 * The window server with RDN_CI_FILE: Core Image works on the card, and
 * what it needs of the renderer (A6, docs/CORE-IMAGE-TODO.md) is true
 * for the window server's contexts: the extension names client storage
 * and float pixels, a renderer name it knows, pbuffers.
 */
int rdn_ws_core_image(void)
{
	static int yes = -1;

	if (yes < 0)
		yes = in_window_server() && access(RDN_CI_FILE, F_OK) == 0;
	return yes;
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
			((unsigned long *)a)[2] |= RECORD_ACCELERATED | RECORD_FULLSCREEN;
			/* Words 12 and 13: video and texture memory, in bytes. */
			((unsigned long *)a)[12] = RDN_REPORTED_MEMORY;
			((unsigned long *)a)[13] = RDN_REPORTED_MEMORY;
			/*
			 * The software renderer's depth modes are 32 bits
			 * only. Mesa's depth buffer has 24, and Call of Duty
			 * 2 takes no display whose renderer lacks that mode.
			 */
			((unsigned long *)a)[6] |= DEPTH_MODE_24;
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
				/*
				 * Word 9 is sample buffers (high half) and
				 * samples (low half), as the software renderer
				 * fills it when asked: 0x00010004 for four.
				 * CGL takes a format without it as not
				 * satisfying a request for samples.
				 */
				if (asked_samples > 1 && !in_window_server())
					fmt[9] = 0x10000 | (unsigned long)asked_samples;
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
			rdn_mesa_context_created(*(void **)a, (void *)d);
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
	long args[8] = { a, b, c, d, e, f, g, h };
	long ret = -1;
	int restore_id = 0;

	setup();
	if (logf) {
		char objects[160];

		known_args(objects, sizeof(objects), args);
		fprintf(logf, TAG " %s(%lx, %lx, %lx, %lx, %lx, %lx, %lx, %lx)%s\n",
			TAG_ARGS, names[idx], a, b, c, d, e, f, g, h, objects);
		fflush(logf);
		if (idx == IDX_gldSetInteger)
			dump_parameter("gldSetInteger sets", b, c);
		/*
		 * A drawable the bundle does nothing with yet, in whatever
		 * way the call is answered below, and before the call as
		 * well: the engine fills some records only afterwards.
		 */
		if (idx == IDX_gldAttachDrawable && !drawable_understood(b))
			dump_drawable("before the call", b, c);
		if (idx == IDX_gldDestroyShared || idx == IDX_gldDestroyContext)
			known_remove(a);
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
	/* What the context's program called of the entries Mesa lacks. */
	if (idx == IDX_gldDestroyContext && logf)
		rdn_dispatch_kept_report("a context is destroyed");
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
	/*
	 * A program's window, the way a hardware driver takes it: the record
	 * is connection, window, surface; OpenGL's callback has the window
	 * server manage the surface in the kext (it then tells the kext
	 * where the window is and leaves that part of the screen alone), and
	 * the card puts Mesa's picture there. The software renderer is not
	 * involved. 0x24 is the mode the window server gives its own
	 * surface: a window, 32 bits.
	 */
	if (!in_window_server() && app_surfaces()) {
		if (idx == IDX_gldAttachDrawable && b == 0x50 && c &&
		    bind_surface && display_words) {
			const unsigned long *r = (const unsigned long *)c;
			long err = bind_surface(r[0], r[1], r[2], 0x24,
						display_words[0]);

			if (logf)
				fprintf(logf, TAG "   surface 0x%lx of window 0x%lx bound -> %lx\n",
					TAG_ARGS, r[2], r[1], err);
			if (!err && rdn_mesa_attach_surface((void *)a, r[0], r[1], r[2]))
				return 2;
		} else if (idx == IDX_gldAttachDrawable && b == 0x36) {
			/*
			 * kCGLPFAFullScreen: the program has the display to
			 * itself. Mesa draws off-screen and each swap
			 * copies the whole picture to the screen.
			 */
			if (logf) {
				fprintf(logf, TAG "   the whole screen is attached\n", TAG_ARGS);
				if (c)
					dump("drawable", c, 0x40);
			}
			if (rdn_mesa_attach_screen((void *)a, 0))
				return 2;
		} else if (idx == IDX_gldAttachDrawable &&
			   rdn_mesa_is_surface((void *)a)) {
			if (logf)
				fprintf(logf, TAG "   the context's surface is let go, nothing attached\n",
					TAG_ARGS);
			rdn_mesa_detach((void *)a);
			return 0;
		}
		if ((idx == IDX_gldInitDispatch || idx == IDX_gldUpdateDispatch) &&
		    rdn_mesa_is_surface((void *)a)) {
			if (b) {
				table_fill(b);
				if (rdn_mesa_dispatch((void *)a, (void *)b))
					present_hook((void *)a, b);
			}
			return idx == IDX_gldInitDispatch ? 4 : 0;
		}
	}
	/*
	 * Pbuffers (kCGLPFAPBuffer): the software renderer refuses them with
	 * kCGLBadEnumeration, so the program never gets one. Ours are video
	 * memory that Mesa draws into and a texture can show
	 * (CGLTexImagePBuffer is hooked, rdn_hook.c). RDN_NO_PBUFFER=1 leaves the refusal.
	 */
	if (!in_window_server() && !getenv("RDN_NO_PBUFFER")) {
		if (idx == IDX_gldAttachDrawable && b == 0x5a && c) {
			rdn_mesa_attach((void *)a, b, (const void *)c);
			if (logf)
				fprintf(logf, TAG "   pbuffer attached by us -> 1\n", TAG_ARGS);
			return 1;
		}
	}
	/*
	 * The window server's Core Image makes contexts that share with its
	 * own and draws into pbuffers (cglsCreatePBuffer, cglsAttachPBuffer):
	 * the record is its own pbuffer object, with the size in words 8 and 9.
	 */
	if (rdn_ws_core_image() && idx == IDX_gldAttachDrawable && b == 0x5a && c) {
		rdn_mesa_pbuffer_layout(1);
		rdn_mesa_attach((void *)a, b, (const void *)c);
		if (logf)
			fprintf(logf, TAG "   pbuffer of the window server attached by us -> 2\n", TAG_ARGS);
		return 2;
	}
	if (in_window_server()) {
		if (idx == IDX_gldAttachDrawable) {
			/* The record's third word is the surface ID. */
			rdn_window_server = 1;
			ret = rdn_mesa_attach_screen((void *)a,
				c ? ((const unsigned long *)c)[2] : 0) ? 2 : -1;
			screen_ctx = ret == 2 ? (void *)a : NULL;
			if (logf) {
				fprintf(logf, TAG "   attached by us -> %lx\n", TAG_ARGS, ret);
				dump("drawable", c, 0x80);
			}
			return ret;
		}
		if ((idx == IDX_gldInitDispatch || idx == IDX_gldUpdateDispatch) &&
		    rdn_ws_core_image() && screen_ctx != (void *)a &&
		    rdn_mesa_is_pbuffer((void *)a)) {
			/* Core Image's own context: Mesa's, nothing is presented. */
			if (b)
				table_fill(b);
			if (b)
				rdn_mesa_dispatch((void *)a, (void *)b);
			if (logf) {
				fprintf(logf, TAG "   dispatch of a pbuffer context set up by us\n", TAG_ARGS);
				fflush(logf);
			}
			return idx == IDX_gldInitDispatch ? 4 : 0;
		}
		if ((idx == IDX_gldInitDispatch || idx == IDX_gldUpdateDispatch) &&
		    screen_ctx == (void *)a) {
			if (b)
				table_fill(b);
			if (b && rdn_mesa_dispatch((void *)a, (void *)b))
				present_hook((void *)a, b);
			if (logf) {
				fprintf(logf, TAG "   dispatch set up by us\n", TAG_ARGS);
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

		asked_samples = 0;
		asked_aux_depth_stencil = 0;

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
			if ((*in == 55 || *in == 56) && !getenv("RDN_GLD_KEEP_SAMPLES")) {
				/*
				 * With Mesa inside it does: the request is
				 * kept from the software renderer as before,
				 * Mesa's buffers get the samples
				 * (rdn_mesa_samples) and the record that goes
				 * back says so (word 9, below). The picture is
				 * resolved when it is copied to the screen.
				 */
				if (*in == 56 && in[1] > 1)
					asked_samples = (int)in[1];
				in += 2;
				continue;
			}
			/*
			 * kCGLPFAAuxDepthStencil, no value: the software
			 * renderer has no format for it, and CGL wants the
			 * flag in the record that comes back.
			 */
			if (*in == 57)
				asked_aux_depth_stencil = 1;
			if (*in != 73 && *in != 72)
				filtered[n++] = *in;
			in++;
		}
		filtered[n] = 0;
		b = (long)filtered;
#ifdef RDN_MESA
		if (!in_window_server())
			rdn_mesa_samples(asked_samples ? asked_samples : 1);
#else
		asked_samples = 0;
#endif
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
#ifdef RDN_MESA
	/* No Core Image to report: the engine's own list (rdn_ws_no_core_image). */
	if (idx == IDX_gldCreateContext && ret == 0 && e && rdn_ws_no_core_image()) {
		unsigned long *features = (unsigned long *)(e + ENGINE_FEATURES);

		if (logf)
			fprintf(logf, TAG "   extension bits %08lx: GL_ARB_fragment_program's cleared\n",
				TAG_ARGS, *features);
		*features &= ~ENGINE_FEATURE_FRAGMENT_PROGRAM;
	}
#endif
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
			if (!in_window_server()) {
				/*
				 * A program: the software renderer has no
				 * format for the whole screen
				 * (kCGLPFAFullScreen, 54). The record is its
				 * record for a window (a depth buffer of 24
				 * bits, a stencil buffer of 8, the words it
				 * has at 3, 5 and 8) with the full screen
				 * flag as well.
				 */
				fmt[2] |= RECORD_FULLSCREEN;
				fmt[3] = 8;
				fmt[5] = 0x20000000;
				fmt[6] = 0x1000;
				fmt[7] = 0x80;
				fmt[8] = 4;
				if (asked_aux_depth_stencil)
					fmt[2] |= RECORD_AUX_DEPTH_STENCIL;
				if (asked_samples > 1)
					fmt[9] = 0x10000 | (unsigned long)asked_samples;
			}
			*(unsigned long **)a = fmt;
			own_format = fmt;
		}
		if (logf) {
			fprintf(logf, TAG "   made a format of our own -> %lx, %lx\n",
				TAG_ARGS, ret, *(long *)a);
			if (ret == 0 && *(long *)a)
				dump("pixfmt", *(long *)a, 0x80);
		}
	}
	if (logf) {
		fprintf(logf, TAG "   %s -> %lx\n", TAG_ARGS, names[idx], ret);
		fflush(logf);
	}
	/* The attribute list as the software renderer was given it. */
	args[1] = b;
	describe(idx, args, ret);
	ret = adjust(idx, a, b, c, d, ret);
#ifdef RDN_MESA
	/*
	 * gldInitializeLibrary's fifth argument is OpenGL's glcBindSurface
	 * (by its address): bind(connection, window, surface, kernel surface
	 * ID, renderer) has the window server take the window's surface as
	 * an accelerated one, through CGSBindSurface.
	 */
	if (idx == IDX_gldInitializeLibrary && e) {
		bind_surface = (long (*)(long, long, long, long, long))e;
		/*
		 * The first argument is an array with one word per display
		 * (a Mach port name by its look: the display's accelerator
		 * or framebuffer); the callback wants the word of the
		 * display the surface is on.
		 */
		display_words = (const long *)a;
	}
	/*
	 * The sixth argument of gldCreateContext points 0x360 bytes into
	 * the engine's context (10.4.11, by comparison with the table
	 * gldInitDispatch is given later).
	 */
	if (idx == IDX_gldCreateContext && ret == 0 && a && f &&
	    !in_window_server() && early_takeover())
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
