/*
 * pinned: does the GPU draw from the program's own memory?
 *
 * An off-screen context on our renderer; a buffer object over page-aligned
 * memory of the program (GL_AMD_pinned_memory, which is how Mesa takes
 * memory behind the GART); a triangle that covers the middle of the
 * picture, drawn from it; then the colours in that memory changed with no
 * OpenGL call and the triangle drawn again. Prints the pixel read back
 * each time and OK or FAILED.
 *
 *   pinned [renderer id, default 0x21a00] [megabytes, default 1] [freefirst]
 *
 * freefirst: free the memory before the buffer object is deleted.
 *
 * Build on the host:
 *   scripts/darwin.sh powerpc-apple-darwin8-gcc -O2 -o build/pinned \
 *       tools/guest/pinned.c -framework OpenGL -framework ApplicationServices
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <ApplicationServices/ApplicationServices.h>

#ifndef GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD
#define GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD 0x9160
#endif

struct vertex {
	float x, y, z;
	unsigned char r, g, b, a;
};

static int pixel(const char *what, int r, int g, int b)
{
	unsigned char px[4] = { 0, 0, 0, 0 };

	glFinish();
	glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("%s: pixel %u %u %u, wanted %d %d %d, GL error 0x%x\n", what,
	       px[0], px[1], px[2], r, g, b, glGetError());
	return px[0] == r && px[1] == g && px[2] == b;
}

int main(int argc, char **argv)
{
	long renderer = argc > 1 ? strtol(argv[1], NULL, 0) : 0x21a00;
	size_t bytes = (argc > 2 ? atoi(argv[2]) : 1) << 20;
	CGLPixelFormatAttribute attrs[8];
	CGLPixelFormatObj pix = NULL;
	CGLContextObj ctx = NULL;
	long npix = 0;
	int n = 0, i, ok = 1;
	static unsigned char picture[64 * 64 * 4];
	struct vertex *v;
	GLuint buffer = 0;
	/* The vertices are put far into the memory: every page must be there. */
	size_t first;

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
	if (!strstr((const char *)glGetString(GL_EXTENSIONS), "GL_AMD_pinned_memory")) {
		printf("FAILED: no GL_AMD_pinned_memory\n");
		return 1;
	}

	v = valloc(bytes);
	memset(v, 0, bytes);
	first = bytes / sizeof(*v) - 3;
	for (i = 0; i < 3; i++) {
		static const float xy[3][2] = { { -3, -3 }, { 3, -3 }, { 0, 3 } };

		v[first + i].x = xy[i][0];
		v[first + i].y = xy[i][1];
		v[first + i].r = 255;
		v[first + i].a = 255;
	}

	/*
	 * Into a texture: what an off-screen context of our renderer draws
	 * does not come back from glReadPixels on the G5 (glprobe reads
	 * zeroes there too).
	 */
	{
		GLuint tex = 0, fbo = 0;

		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA,
			     GL_UNSIGNED_BYTE, NULL);
		glBindTexture(GL_TEXTURE_2D, 0);
		glGenFramebuffersEXT(1, &fbo);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
					  GL_TEXTURE_2D, tex, 0);
		printf("framebuffer status 0x%x\n",
		       glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT));
	}
	glViewport(0, 0, 64, 64);
	glClearColor(0, 0, 1, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	ok &= pixel("cleared", 0, 0, 255);

	glGenBuffersARB(1, &buffer);
	glBindBufferARB(GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD, buffer);
	glBufferDataARB(GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD, bytes, v, GL_DYNAMIC_DRAW_ARB);
	glBindBufferARB(GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD, 0);
	printf("buffer over %p, %lu bytes: GL error 0x%x\n", (void *)v,
	       (unsigned long)bytes, glGetError());

	glBindBufferARB(GL_ARRAY_BUFFER_ARB, buffer);
	glVertexPointer(3, GL_FLOAT, sizeof(*v), (char *)0);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(*v), (char *)12);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	glDrawArrays(GL_TRIANGLES, first, 3);
	ok &= pixel("drawn from the program's memory (red)", 255, 0, 0);

	/* No OpenGL call tells of this. */
	for (i = 0; i < 3; i++) {
		v[first + i].r = 0;
		v[first + i].g = 255;
	}
	glDrawArrays(GL_TRIANGLES, first, 3);
	ok &= pixel("after the memory changed (green)", 0, 255, 0);

	if (argc > 3 && !strcmp(argv[3], "freefirst")) {
		char *other;

		/*
		 * Freed while the buffer object is still there, as a game may;
		 * then other memory taken and written, and the triangle drawn
		 * again: do the pages the GPU reads still hold the vertices?
		 */
		free(v);
		other = malloc(64 << 20);
		memset(other, 0x55, 64 << 20);
		printf("memory freed with the buffer object alive; 64 MB taken at %p and written\n",
		       (void *)other);
		glClear(GL_COLOR_BUFFER_BIT);
		glDrawArrays(GL_TRIANGLES, first, 3);
		ok &= pixel("drawn from the freed memory (green)", 0, 255, 0);
		free(other);
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_COLOR_ARRAY);
		glDeleteBuffersARB(1, &buffer);
	} else {
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_COLOR_ARRAY);
		glDeleteBuffersARB(1, &buffer);
		glFinish();
		free(v);
	}
	glClear(GL_COLOR_BUFFER_BIT);
	ok &= pixel("cleared after the buffer was deleted", 0, 0, 255);

	CGLSetCurrentContext(NULL);
	CGLDestroyContext(ctx);
	CGLDestroyPixelFormat(pix);
	printf("%s\n", ok ? "OK" : "FAILED");
	return !ok;
}
