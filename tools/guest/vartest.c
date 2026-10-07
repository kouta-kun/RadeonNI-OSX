/*
 * vartest: GL_APPLE_vertex_array_range as the bundle gives it.
 *
 * Draws a triangle from memory of its own named with
 * glVertexArrayRangeAPPLE, into a texture, and reads a pixel back after
 * each step:
 *   1. flushed, drawn: red
 *   2. colours changed in memory, no flush: green if the GPU reads the
 *      memory (RDN_VAR=2), still red with copies (RDN_VAR=1)
 *   3. flushed: green
 *   4. the memory freed, the same amount taken again (malloc gives the
 *      same address back as a rule), blue vertices, flushed, drawn: blue
 *   5. a fence set, finished, tested
 *
 *   RDN_VAR=2 vartest [renderer id, default 0x21a00]
 *
 * Build on the host:
 *   scripts/darwin.sh powerpc-apple-darwin8-gcc -O2 -o build/vartest \
 *       tools/guest/vartest.c -framework OpenGL -framework ApplicationServices
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

struct vertex {
	float x, y, z;
	unsigned char r, g, b, a;
};

#define BYTES (1 << 20)
#define FIRST (BYTES / sizeof(struct vertex) - 3)

static void pixel(const char *what)
{
	unsigned char px[4] = { 0, 0, 0, 0 };

	glFinish();
	glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("%-52s %3u %3u %3u   GL error 0x%x\n", what, px[0], px[1], px[2],
	       glGetError());
}

static struct vertex *triangle(unsigned char r, unsigned char g, unsigned char b)
{
	static const float xy[3][2] = { { -3, -3 }, { 3, -3 }, { 0, 3 } };
	struct vertex *v = malloc(BYTES);
	int i;

	memset(v, 0, BYTES);
	for (i = 0; i < 3; i++) {
		v[FIRST + i].x = xy[i][0];
		v[FIRST + i].y = xy[i][1];
		v[FIRST + i].r = r;
		v[FIRST + i].g = g;
		v[FIRST + i].b = b;
		v[FIRST + i].a = 255;
	}
	return v;
}

static void point(struct vertex *v)
{
	glVertexArrayRangeAPPLE(BYTES, v);
	glFlushVertexArrayRangeAPPLE(BYTES, v);
	glVertexPointer(3, GL_FLOAT, sizeof(*v), &v->x);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(*v), &v->r);
}

int main(int argc, char **argv)
{
	long renderer = argc > 1 ? strtol(argv[1], NULL, 0) : 0x21a00;
	CGLPixelFormatAttribute attrs[8];
	CGLPixelFormatObj pix = NULL;
	CGLContextObj ctx = NULL;
	long npix = 0;
	int n = 0, i;
	static unsigned char picture[64 * 64 * 4];
	struct vertex *v, *w;
	GLuint tex = 0, fbo = 0, fence = 0;

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
	printf("GL_APPLE_vertex_array_range %s, GL_APPLE_fence %s\n",
	       strstr((const char *)glGetString(GL_EXTENSIONS), "GL_APPLE_vertex_array_range") ?
	       "named" : "not named",
	       strstr((const char *)glGetString(GL_EXTENSIONS), "GL_APPLE_fence") ?
	       "named" : "not named");

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);
	glGenFramebuffersEXT(1, &fbo);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
				  GL_TEXTURE_2D, tex, 0);
	glViewport(0, 0, 64, 64);
	glClearColor(0, 0, 0, 1);
	glEnableClientState(GL_VERTEX_ARRAY_RANGE_APPLE);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	v = triangle(255, 0, 0);
	printf("memory at %p\n", (void *)v);
	point(v);
	glDrawArrays(GL_TRIANGLES, FIRST, 3);
	pixel("1. flushed and drawn (red):");

	for (i = 0; i < 3; i++) {
		v[FIRST + i].r = 0;
		v[FIRST + i].g = 255;
	}
	glDrawArrays(GL_TRIANGLES, FIRST, 3);
	pixel("2. changed, not flushed (green: GPU reads; red: copy):");

	glFlushVertexArrayRangeAPPLE(BYTES, v);
	glDrawArrays(GL_TRIANGLES, FIRST, 3);
	pixel("3. flushed (green):");

	free(v);
	w = triangle(0, 0, 255);
	printf("memory freed; new memory at %p (%s)\n", (void *)w,
	       w == v ? "the same address" : "another address");
	point(w);
	glDrawArrays(GL_TRIANGLES, FIRST, 3);
	pixel("4. other memory, flushed and drawn (blue):");

	glGenFencesAPPLE(1, &fence);
	glSetFenceAPPLE(fence);
	glFinishFenceAPPLE(fence);
	printf("5. fence %u set and finished; tested: %d\n", fence, glTestFenceAPPLE(fence));

	CGLSetCurrentContext(NULL);
	CGLDestroyContext(ctx);
	CGLDestroyPixelFormat(pix);
	return 0;
}
