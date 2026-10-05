/*
 * prelist: does GL state made before a context has a drawable survive?
 *
 *   prelist [renderer-id]
 *
 * Makes a context current with no drawable, as applications do while
 * their window does not exist yet, and there builds a display list (a
 * triangle that covers the lower left half of the view) and sets the
 * clear colour. Then attaches a 64x64 off-screen buffer, sets the view,
 * clears, calls the list and reads four pixels: inside the triangle
 * (red), outside it (blue, the clear colour), and both again through the
 * buffer itself.
 *
 * Build in the guest:
 *   gcc -isysroot /Developer/SDKs/MacOSX10.4u.sdk -Wall -o prelist prelist.c \
 *       -framework OpenGL
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>

#define W 64
#define H 64

static unsigned gl_pixel(int x, int y)
{
	unsigned char p[4] = { 0, 0, 0, 0 };

	glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
	return (p[0] << 16) | (p[1] << 8) | p[2];
}

int main(int argc, char **argv)
{
	long renderer = argc > 1 ? strtol(argv[1], NULL, 0) : 0x21a00;
	CGLPixelFormatAttribute attrs[] = {
		kCGLPFAOffScreen, kCGLPFAColorSize, 32, kCGLPFARendererID,
		(CGLPixelFormatAttribute)renderer, 0
	};
	CGLPixelFormatObj pix = NULL;
	CGLContextObj ctx = NULL;
	long npix = 0;
	unsigned *buf = calloc(W * H, 4);
	GLuint list;

	if (CGLChoosePixelFormat(attrs, &pix, &npix) || !pix ||
	    CGLCreateContext(pix, NULL, &ctx) || !ctx) {
		fprintf(stderr, "no context on renderer 0x%lx\n", renderer);
		return 1;
	}
	CGLSetCurrentContext(ctx);

	/* No drawable yet. */
	glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
	list = glGenLists(1);
	glNewList(list, GL_COMPILE);
	glBegin(GL_TRIANGLES);
	glColor3f(1.0f, 0.0f, 0.0f);
	glVertex3f(-1.0f, -1.0f, 0.0f);
	glVertex3f(1.0f, -1.0f, 0.0f);
	glVertex3f(-1.0f, 1.0f, 0.0f);
	glEnd();
	glEndList();
	printf("list %u made before the drawable; GL error 0x%x\n", (unsigned)list,
	       (unsigned)glGetError());

	if (CGLSetOffScreen(ctx, W, H, W * 4, buf)) {
		fprintf(stderr, "CGLSetOffScreen failed\n");
		return 1;
	}
	printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
	glViewport(0, 0, W, H);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glClear(GL_COLOR_BUFFER_BIT);
	glCallList(list);
	glFinish();
	printf("GL error after drawing 0x%x\n", (unsigned)glGetError());

	printf("inside the triangle (8,8):   %06x, want ff0000\n", gl_pixel(8, 8));
	printf("outside it (56,56):          %06x, want 0000ff\n", gl_pixel(56, 56));
	printf("near its long edge (24,24):  %06x, want ff0000\n", gl_pixel(24, 24));
	printf("past its long edge (40,40):  %06x, want 0000ff\n", gl_pixel(40, 40));
	printf("buffer, bottom left and top right words: %08x %08x\n",
	       buf[8 * W + 8], buf[56 * W + 56]);
	CGLSetCurrentContext(NULL);
	CGLDestroyContext(ctx);
	return 0;
}
