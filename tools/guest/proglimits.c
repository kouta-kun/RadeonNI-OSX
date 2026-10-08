/*
 * proglimits: the vertex and fragment program limits Core Image asks an
 * OpenGL renderer for before it decides whether to use it, with the GL
 * error each query leaves.
 *
 *   proglimits [renderer id, default 0x21a00; 0x20400 is Apple's software]
 *
 * Apple's renderers answer 0, without an error, when a vertex program is
 * asked for the counts only fragment programs have (the last three).
 *
 * Build on the host:
 *   scripts/darwin.sh powerpc-apple-darwin8-gcc -O2 -o build/proglimits \
 *       tools/guest/proglimits.c -framework OpenGL
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>

int main(int argc, char **argv)
{
	static const struct {
		GLenum pname;
		const char *name;
	} limits[] = {
		{ 0x88AB, "native parameters" },
		{ 0x88A7, "native temporaries" },
		{ 0x88AF, "native attributes" },
		{ 0x88B4, "local parameters" },
		{ 0x88A3, "native instructions" },
		{ 0x880E, "native ALU instructions" },
		{ 0x880F, "native texture instructions" },
		{ 0x8810, "native texture indirections" },
	};
	static const struct {
		GLenum target;
		const char *name;
	} kinds[] = {
		{ GL_VERTEX_PROGRAM_ARB, "vertex" },
		{ GL_FRAGMENT_PROGRAM_ARB, "fragment" },
	};
	long renderer = argc > 1 ? strtol(argv[1], NULL, 0) : 0x21a00;
	CGLPixelFormatAttribute attrs[8];
	CGLPixelFormatObj pix = NULL;
	CGLContextObj ctx = NULL;
	long npix = 0;
	static unsigned char picture[64 * 64 * 4];
	unsigned i, j;
	int n = 0;
	GLint v;

	attrs[n++] = kCGLPFAOffScreen;
	attrs[n++] = kCGLPFAColorSize;
	attrs[n++] = 32;
	attrs[n++] = kCGLPFARendererID;
	attrs[n++] = renderer;
	attrs[n] = 0;
	if (CGLChoosePixelFormat(attrs, &pix, &npix) || !pix ||
	    CGLCreateContext(pix, NULL, &ctx) ||
	    CGLSetOffScreen(ctx, 64, 64, 64 * 4, picture)) {
		printf("no off-screen context on renderer 0x%lx\n", renderer);
		return 1;
	}
	CGLSetCurrentContext(ctx);
	printf("renderer: %s\n", glGetString(GL_RENDERER));
	for (j = 0; j < 2; j++)
		for (i = 0; i < sizeof(limits) / sizeof(limits[0]); i++) {
			v = -1;
			glGetProgramivARB(kinds[j].target, limits[i].pname, &v);
			printf("%-8s %-28s %6ld   GL error 0x%x\n", kinds[j].name,
			       limits[i].name, (long)v, (unsigned)glGetError());
		}
	v = -1;
	glGetIntegerv(GL_MAX_TEXTURE_COORDS_ARB, &v);
	printf("texture coordinates %ld, ", (long)v);
	v = -1;
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS_ARB, &v);
	printf("texture image units %ld\n", (long)v);
	CGLSetCurrentContext(NULL);
	CGLDestroyContext(ctx);
	CGLDestroyPixelFormat(pix);
	return 0;
}
