/*
 * Hooks on CGLSetCurrentContext and CFBundleGetFunctionPointerForName for
 * every image in the process.
 *
 * OpenGL fills a new context's dispatch table itself when the context is
 * complete, with no call to the driver, and the driver is only asked to
 * set its entries up once a drawable is attached. Programs make GL calls
 * before that (textures, display lists), so the bundle has to give the
 * table to Mesa earlier, and the one thing every program does between the
 * two is to make the context current.
 *
 * The hook replaces the pointers through which other images call
 * CGLSetCurrentContext (their lazy and non-lazy symbol pointers), in the
 * images loaded now and in every one loaded later. No code is changed.
 *
 * The second hook is for OpenGL functions that Tiger's framework does not
 * have and a program looks up by name, as World of Warcraft does for
 * GL_APPLE_flush_buffer_range's two: it asks the framework's bundle
 * (CFBundleGetFunctionPointerForName on "com.apple.opengl"), which will
 * never have them. The names the bundle answers for are its own to give
 * (rdn_hook_function_lookup); every other question goes on to Core
 * Foundation.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

#include "rdn_glue.h"

#define HOOKED "_CGLSetCurrentContext"
#define HOOKED_LOOKUP "_CFBundleGetFunctionPointerForName"
#define HOOKED_TEX "_CGLTexImagePBuffer"
#define ASCII 0x0600	/* kCFStringEncodingASCII */

static int (*real_set_current)(void *ctx);
static void (*after_set_current)(void *ctx);

static int hooked_set_current(void *ctx)
{
	int err = real_set_current(ctx);

	if (ctx && !err)
		after_set_current(ctx);
	return err;
}

static void *(*real_lookup)(void *bundle, void *name);
static unsigned char (*string_to_c)(void *string, char *buffer, long size, unsigned encoding);
static void *(*own_function)(const char *name);

static void *hooked_lookup(void *bundle, void *name)
{
	char text[64];
	void *own;

	if (name && string_to_c(name, text, sizeof(text), ASCII) &&
	    (own = own_function(text)) != NULL)
		return own;
	return real_lookup(bundle, name);
}

/*
 * CGLTexImagePBuffer goes to the engine, which wants the texture bound in
 * its own state (Mesa's glBindTexture never reaches it) and refuses the
 * rest with kCGLBadState. A pbuffer of ours is Mesa's to show.
 */
static long (*real_tex_image)(void *ctx, void *pbuffer, long source);
static int (*tex_image_handler)(void *ctx, void *pbuffer, long source, long *result);

static long hooked_tex_image(void *ctx, void *pbuffer, long source)
{
	long result;

	if (tex_image_handler(ctx, pbuffer, source, &result))
		return result;
	return real_tex_image(ctx, pbuffer, source);
}

/*
 * Calls of CGL that the log (RDN_GLD_LOG) shows, with what they were given
 * and what they returned: pbuffers and pixel formats are decided before
 * any driver function is called.
 */
static long (*real_create_pbuffer)(long w, long h, long target, long format,
				   long levels, void **out);
static long (*real_set_pbuffer)(void *ctx, void *pbuffer, long face, long level,
				long screen);
static long (*real_choose)(const long *attrs, void **pix, long *npix);

static long hooked_create_pbuffer(long w, long h, long target, long format,
				  long levels, void **out)
{
	long err = real_create_pbuffer(w, h, target, format, levels, out);

	rdn_log("CGLCreatePBuffer(%ld, %ld, target 0x%lx, format 0x%lx, levels %ld) -> %ld, %p",
		w, h, target, format, levels, err, out ? *out : NULL);
	return err;
}

static long hooked_set_pbuffer(void *ctx, void *pbuffer, long face, long level,
			       long screen)
{
	long err = real_set_pbuffer(ctx, pbuffer, face, level, screen);

	rdn_log("CGLSetPBuffer(%p, %p, face %ld, level %ld, screen %ld) -> %ld",
		ctx, pbuffer, face, level, screen, err);
	return err;
}

static long hooked_choose(const long *attrs, void **pix, long *npix)
{
	long err = real_choose(attrs, pix, npix);
	char text[400];
	int n = 0, i;

	for (i = 0; attrs && attrs[i] && i < 24 && n < (int)sizeof(text) - 12; i++)
		n += snprintf(text + n, sizeof(text) - n, " %ld", attrs[i]);
	text[n] = 0;
	rdn_log("CGLChoosePixelFormat(%s) -> %ld, %ld formats", text, err,
		npix ? *npix : -1L);
	return err;
}

/* The window server's cglsTexImagePBuffer (CoreGraphics), the same way. */
static long (*real_cgls_tex_image)(void *ctx, void *pbuffer, long source);
static int (*cgls_tex_image_handler)(void *ctx, void *pbuffer, long source, long *result);

static long hooked_cgls_tex_image(void *ctx, void *pbuffer, long source)
{
	long result;

	if (cgls_tex_image_handler(ctx, pbuffer, source, &result))
		return result;
	return real_cgls_tex_image(ctx, pbuffer, source);
}

/* CGLDestroyPBuffer and the window server's cglsDestroyPBuffer. */
static long (*real_destroy_pbuffer)(void *pbuffer);
static long (*real_cgls_destroy_pbuffer)(void *pbuffer);
static void (*destroy_pbuffer_handler)(void *pbuffer);

static long hooked_destroy_pbuffer(void *pbuffer)
{
	destroy_pbuffer_handler(pbuffer);
	return real_destroy_pbuffer(pbuffer);
}

static long hooked_cgls_destroy_pbuffer(void *pbuffer)
{
	destroy_pbuffer_handler(pbuffer);
	return real_cgls_destroy_pbuffer(pbuffer);
}

static void rebind(const struct mach_header *mh, intptr_t slide)
{
	const struct load_command *lc = (const struct load_command *)(mh + 1);
	const struct segment_command *linkedit = NULL;
	const struct symtab_command *symtab = NULL;
	const struct dysymtab_command *dysymtab = NULL;
	const struct nlist *symbols;
	const uint32_t *indirect;
	const char *strings;
	uint32_t i, j, k;

	if (mh->magic != MH_MAGIC)
		return;
	for (i = 0; i < mh->ncmds; i++, lc = (const void *)((const char *)lc + lc->cmdsize)) {
		if (lc->cmd == LC_SEGMENT &&
		    !strcmp(((const struct segment_command *)lc)->segname, SEG_LINKEDIT))
			linkedit = (const struct segment_command *)lc;
		else if (lc->cmd == LC_SYMTAB)
			symtab = (const struct symtab_command *)lc;
		else if (lc->cmd == LC_DYSYMTAB)
			dysymtab = (const struct dysymtab_command *)lc;
	}
	if (!linkedit || !symtab || !dysymtab || !dysymtab->nindirectsyms)
		return;
	/* The tables are in __LINKEDIT, given by file offset. */
	symbols = (const void *)(slide + linkedit->vmaddr - linkedit->fileoff + symtab->symoff);
	strings = (const char *)(slide + linkedit->vmaddr - linkedit->fileoff + symtab->stroff);
	indirect = (const void *)(slide + linkedit->vmaddr - linkedit->fileoff +
				  dysymtab->indirectsymoff);

	lc = (const struct load_command *)(mh + 1);
	for (i = 0; i < mh->ncmds; i++, lc = (const void *)((const char *)lc + lc->cmdsize)) {
		const struct segment_command *seg = (const struct segment_command *)lc;
		const struct section *sect = (const struct section *)(seg + 1);

		if (lc->cmd != LC_SEGMENT)
			continue;
		for (j = 0; j < seg->nsects; j++, sect++) {
			uint32_t type = sect->flags & SECTION_TYPE;
			void **pointers = (void **)(slide + sect->addr);

			if (type != S_LAZY_SYMBOL_POINTERS &&
			    type != S_NON_LAZY_SYMBOL_POINTERS)
				continue;
			for (k = 0; k < sect->size / sizeof(void *); k++) {
				uint32_t sym = indirect[sect->reserved1 + k];

				if (sym & (INDIRECT_SYMBOL_LOCAL | INDIRECT_SYMBOL_ABS) ||
				    sym >= symtab->nsyms)
					continue;
				if (real_set_current &&
				    !strcmp(strings + symbols[sym].n_un.n_strx, HOOKED) &&
				    pointers[k] != (void *)hooked_set_current)
					pointers[k] = (void *)hooked_set_current;
				if (real_create_pbuffer &&
				    !strcmp(strings + symbols[sym].n_un.n_strx, "_CGLCreatePBuffer") &&
				    pointers[k] != (void *)hooked_create_pbuffer)
					pointers[k] = (void *)hooked_create_pbuffer;
				if (real_set_pbuffer &&
				    !strcmp(strings + symbols[sym].n_un.n_strx, "_CGLSetPBuffer") &&
				    pointers[k] != (void *)hooked_set_pbuffer)
					pointers[k] = (void *)hooked_set_pbuffer;
				if (real_choose &&
				    !strcmp(strings + symbols[sym].n_un.n_strx, "_CGLChoosePixelFormat") &&
				    pointers[k] != (void *)hooked_choose)
					pointers[k] = (void *)hooked_choose;
				if (real_destroy_pbuffer &&
				    !strcmp(strings + symbols[sym].n_un.n_strx, "_CGLDestroyPBuffer") &&
				    pointers[k] != (void *)hooked_destroy_pbuffer)
					pointers[k] = (void *)hooked_destroy_pbuffer;
				if (real_cgls_destroy_pbuffer &&
				    !strcmp(strings + symbols[sym].n_un.n_strx, "_cglsDestroyPBuffer") &&
				    pointers[k] != (void *)hooked_cgls_destroy_pbuffer)
					pointers[k] = (void *)hooked_cgls_destroy_pbuffer;
				if (real_cgls_tex_image &&
				    !strcmp(strings + symbols[sym].n_un.n_strx, "_cglsTexImagePBuffer") &&
				    pointers[k] != (void *)hooked_cgls_tex_image)
					pointers[k] = (void *)hooked_cgls_tex_image;
				if (real_tex_image &&
				    !strcmp(strings + symbols[sym].n_un.n_strx, HOOKED_TEX) &&
				    pointers[k] != (void *)hooked_tex_image)
					pointers[k] = (void *)hooked_tex_image;
				if (real_lookup &&
				    !strcmp(strings + symbols[sym].n_un.n_strx, HOOKED_LOOKUP) &&
				    pointers[k] != (void *)hooked_lookup)
					pointers[k] = (void *)hooked_lookup;
			}
		}
	}
}

/* dyld calls back for every image there is, and then for each new one. */
static void watch_images(void)
{
	static int watching;

	if (!watching) {
		watching = 1;
		_dyld_register_func_for_add_image(rebind);
	} else {
		uint32_t i;

		/* A hook added later: the images there are, again. */
		for (i = 0; i < _dyld_image_count(); i++)
			rebind(_dyld_get_image_header(i), _dyld_get_image_vmaddr_slide(i));
	}
}

int rdn_hook_function_lookup(void *(*own)(const char *name))
{
	if (real_lookup)
		return 1;
	string_to_c = (unsigned char (*)(void *, char *, long, unsigned))
		dlsym(RTLD_DEFAULT, "CFStringGetCString");
	own_function = own;
	if (string_to_c)
		real_lookup = (void *(*)(void *, void *))
			dlsym(RTLD_DEFAULT, "CFBundleGetFunctionPointerForName");
	if (!real_lookup)
		return 0;	/* a program without Core Foundation */
	watch_images();
	return 1;
}

void rdn_hook_set_current(void (*after)(void *cgl_ctx))
{
	if (real_set_current)
		return;
	real_set_current = (int (*)(void *))dlsym(RTLD_DEFAULT, "CGLSetCurrentContext");
	if (!real_set_current) {
		rdn_log("no CGLSetCurrentContext to hook");
		return;
	}
	after_set_current = after;
	watch_images();
}

void rdn_hook_tex_image_pbuffer(int (*handler)(void *cgl_ctx, void *pbuffer,
					       long source, long *result))
{
	if (real_tex_image)
		return;
	real_tex_image = (long (*)(void *, void *, long))
		dlsym(RTLD_DEFAULT, "CGLTexImagePBuffer");
	if (!real_tex_image) {
		rdn_log("no CGLTexImagePBuffer to hook");
		return;
	}
	tex_image_handler = handler;
	watch_images();
}

/* The CGL calls above go to the log. */
void rdn_hook_cgl_log(void)
{
	if (real_create_pbuffer)
		return;
	real_create_pbuffer = (long (*)(long, long, long, long, long, void **))
		dlsym(RTLD_DEFAULT, "CGLCreatePBuffer");
	real_set_pbuffer = (long (*)(void *, void *, long, long, long))
		dlsym(RTLD_DEFAULT, "CGLSetPBuffer");
	real_choose = (long (*)(const long *, void **, long *))
		dlsym(RTLD_DEFAULT, "CGLChoosePixelFormat");
	if (!real_create_pbuffer || !real_set_pbuffer || !real_choose) {
		real_create_pbuffer = NULL;
		return;
	}
	watch_images();
}

void rdn_hook_cgls_tex_image(int (*handler)(void *ctx, void *pbuffer, long source,
					    long *result))
{
	if (real_cgls_tex_image)
		return;
	real_cgls_tex_image = (long (*)(void *, void *, long))
		dlsym(RTLD_DEFAULT, "cglsTexImagePBuffer");
	if (!real_cgls_tex_image) {
		rdn_log("no cglsTexImagePBuffer to hook");
		return;
	}
	cgls_tex_image_handler = handler;
	watch_images();
}

/*
 * The window server's cglsSetInteger. CoreGraphics calls it by a plain
 * branch inside itself (a filter's backdrop is a texture of the screen's
 * surface, set with parameter 0x3e6), so no symbol pointer can be
 * replaced. The function is twelve instructions long in 10.4.11:
 *
 *   mfspr r0,lr; bcl 20,31,+4; mfspr r10,lr; mtspr lr,r0      (position)
 *   addis r2,r10,0xff0; lwz r2,-0x2d28(r2); add r2,r3,r2      (offset)
 *   lwz r3,0(r3); lwz r2,0xc(r2); lwz r12,0x30(r2)
 *   mtctr r12; bctr                                            (the engine's)
 *
 * Its words are checked, and the four from "addis" on are replaced by a
 * jump to rdn_hook_cgls_set_integer's own function, which does what the
 * rest did unless the handler takes the call. A function that is not
 * these twelve words is left alone.
 */
static int (*cgls_set_handler)(void *ctx, long pname, long *vals, long *result);
static const void *cgls_set_fn;

static long hooked_cgls_set_integer(void *ctx, long pname, long *vals)
{
	long result;
	/* The offset the original reads, from where the original reads it. */
	long off = *(const int *)((const char *)cgls_set_fn + 8 + 0x0ff00000 - 0x2d28);
	void **table = *(void ***)((char *)ctx + off + 0xc);
	long (*engine)(void *rend, long pname, long *vals) = (long (*)(void *, long, long *))table[0x30 / 4];

	if (cgls_set_handler(ctx, pname, vals, &result))
		return result;
	return engine(*(void **)ctx, pname, vals);
}

int rdn_hook_cgls_set_integer(int (*handler)(void *ctx, long pname, long *vals, long *result))
{
	static const uint32_t original[12] = {
		0x7c0802a6, 0x429f0005, 0x7d4802a6, 0x7c0803a6,
		0x3c4a0ff0, 0x8042d2d8, 0x7c431214, 0x80630000,
		0x8042000c, 0x81820030, 0x7d8903a6, 0x4e800420
	};
	volatile uint32_t *code;
	uintptr_t target = (uintptr_t)hooked_cgls_set_integer;
	uintptr_t page;
	kern_return_t kr;
	unsigned i;

	if (cgls_set_fn)
		return 1;
	code = (volatile uint32_t *)dlsym(RTLD_DEFAULT, "cglsSetInteger");
	if (!code) {
		rdn_log("no cglsSetInteger to hook");
		return 0;
	}
	for (i = 0; i < 12; i++)
		if (code[i] != original[i]) {
			rdn_log("cglsSetInteger is not the one hooked for (word %u is 0x%08x)", i, (unsigned)code[i]);
			return 0;
		}
	page = (uintptr_t)code & ~(uintptr_t)(vm_page_size - 1);
	kr = vm_protect(mach_task_self(), page, vm_page_size, FALSE,
			VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE | VM_PROT_COPY);
	if (kr != KERN_SUCCESS)
		/* A mapping whose maximum protection lacks writing: copy the page. */
		kr = vm_protect(mach_task_self(), page, vm_page_size, FALSE,
				VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
	if (kr != KERN_SUCCESS) {
		rdn_log("cglsSetInteger's code cannot be written (vm_protect %d at 0x%lx)",
			(int)kr, (unsigned long)page);
		return 0;
	}
	cgls_set_handler = handler;
	cgls_set_fn = (const void *)code;
	code[4] = 0x3d800000 | (uint32_t)(target >> 16);		/* lis r12,hi */
	code[5] = 0x618c0000 | (uint32_t)(target & 0xffff);		/* ori r12,r12,lo */
	code[6] = 0x7d8903a6;					/* mtctr r12 */
	code[7] = 0x4e800420;					/* bctr */
	for (i = 4; i < 8; i++) {
		__asm__ volatile("dcbst 0,%0; sync; icbi 0,%0; sync; isync" : : "r"(code + i) : "memory");
	}
	vm_protect(mach_task_self(), page, vm_page_size, FALSE,
		   VM_PROT_READ | VM_PROT_EXECUTE);
	rdn_log("cglsSetInteger is ours (jump at %p)", (const void *)code);
	return 1;
}

void rdn_hook_destroy_pbuffer(void (*handler)(void *pbuffer))
{
	if (destroy_pbuffer_handler)
		return;
	real_destroy_pbuffer = (long (*)(void *))dlsym(RTLD_DEFAULT, "CGLDestroyPBuffer");
	real_cgls_destroy_pbuffer = (long (*)(void *))dlsym(RTLD_DEFAULT, "cglsDestroyPBuffer");
	if (!real_destroy_pbuffer && !real_cgls_destroy_pbuffer)
		return;
	destroy_pbuffer_handler = handler;
	watch_images();
}
