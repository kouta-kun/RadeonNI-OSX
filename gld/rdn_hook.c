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
#include <string.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

#include "rdn_glue.h"

#define HOOKED "_CGLSetCurrentContext"
#define HOOKED_LOOKUP "_CFBundleGetFunctionPointerForName"
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
