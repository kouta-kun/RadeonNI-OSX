/*
 * earlyext: the list of extensions a context gives before it has ever
 * been made current, and after.
 *
 * The window server asks its context for GL_EXTENSIONS right after it has
 * made it, through the context's own table (CGL macros), and decides from
 * that whether the display can do Core Image. At that moment the table is
 * still the one OpenGL filled in by itself.
 *
 *   earlyext [renderer id, default 0x21a00]
 *
 * EARLYEXT_PEEK=1 shows where the engine keeps the bits that list is made
 * from, =2 also clears GL_ARB_fragment_program's and asks again.
 * EARLYEXT_LIST=1 prints the lists.
 *
 * Build on the host:
 *   scripts/darwin.sh powerpc-apple-darwin8-gcc -O2 -o build/earlyext \
 *       tools/guest/earlyext.c -framework OpenGL
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/CGLContext.h>

/* Is the name in the list, as a whole name? */
static int named(const char *list, const char *name)
{
	size_t len = strlen(name);
	const char *at;

	for (at = list; at && (at = strstr(at, name)) != NULL; at += len)
		if ((at == list || at[-1] == ' ') && (at[len] == ' ' || !at[len]))
			return 1;
	return 0;
}

static void say(const char *when, CGLContextObj ctx)
{
	const char *list = (const char *)ctx->disp.get_string(ctx->rend, GL_EXTENSIONS);
	const char *renderer = (const char *)ctx->disp.get_string(ctx->rend, GL_RENDERER);
	unsigned names = 0;
	const char *at;

	for (at = list; at && *at; at++)
		names += *at == ' ' && at[1];
	printf("%s: renderer \"%s\", %u names, GL_ARB_fragment_program %s, "
	       "GL_APPLE_float_pixels %s\n", when, renderer ? renderer : "(none)",
	       list && *list ? names + 1 : 0,
	       named(list, "GL_ARB_fragment_program") ? "named" : "not named",
	       named(list, "GL_APPLE_float_pixels") ? "named" : "not named");
	if (getenv("EARLYEXT_LIST") && list)
		printf("%s\n", list);
}

int main(int argc, char **argv)
{
	long renderer = argc > 1 ? strtol(argv[1], NULL, 0) : 0x21a00;
	CGLPixelFormatAttribute attrs[8];
	CGLPixelFormatObj pix = NULL;
	CGLContextObj ctx = NULL;
	long npix = 0;
	int n = 0;
	static unsigned char picture[64 * 64 * 4];

	attrs[n++] = kCGLPFAOffScreen;
	attrs[n++] = kCGLPFAColorSize;
	attrs[n++] = 32;
	attrs[n++] = kCGLPFARendererID;
	attrs[n++] = renderer;
	attrs[n] = 0;
	if (CGLChoosePixelFormat(attrs, &pix, &npix) || !pix ||
	    CGLCreateContext(pix, NULL, &ctx)) {
		printf("no context on renderer 0x%lx\n", renderer);
		return 1;
	}
	say("just made", ctx);
	if (getenv("EARLYEXT_PEEK")) {
		/*
		 * Where OpenGL's engine keeps what the renderer can do: a
		 * pointer 12 bytes before the context's table, and at 0x124
		 * from there a bit for each extension it may name (bit 15,
		 * 0x8000 of the first word, is GL_ARB_fragment_program).
		 */
		unsigned long **slot = (unsigned long **)((char *)ctx->rend + 0x468c);
		unsigned long *bits = (unsigned long *)((char *)*slot + 0x124);

		printf("rend %p, table %p, features at %p: %08lx %08lx %08lx\n",
		       (void *)ctx->rend, (void *)&ctx->disp, (void *)*slot,
		       bits[0], bits[1], bits[2]);
		if (atoi(getenv("EARLYEXT_PEEK")) > 1) {
			bits[0] &= ~0x8000ul;
			say("bit 15 cleared", ctx);
		}
	}
	CGLSetOffScreen(ctx, 64, 64, 64 * 4, picture);
	say("with a drawable", ctx);
	CGLSetCurrentContext(ctx);
	say("made current", ctx);
	CGLSetCurrentContext(NULL);
	CGLDestroyContext(ctx);
	CGLDestroyPixelFormat(pix);
	return 0;
}
