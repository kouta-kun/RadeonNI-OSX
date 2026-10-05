/*
 * glprobe: list the OpenGL renderers Tiger offers and exercise one.
 *
 *   glprobe              list renderers for every display
 *   glprobe draw [id]    create an off-screen context (optionally on the
 *                        renderer with that ID), clear to a colour, read a
 *                        pixel back and print the GL strings
 *
 * Build in the guest:
 *   gcc -isysroot /Developer/SDKs/MacOSX10.4u.sdk -o glprobe glprobe.c \
 *       -framework OpenGL -framework ApplicationServices
 * (the guest's installed OpenGL.framework has no headers; the SDK does)
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
#include <ApplicationServices/ApplicationServices.h>

static void list(void)
{
	CGDirectDisplayID displays[8];
	CGDisplayCount n = 0, d;

	CGGetActiveDisplayList(8, displays, &n);
	for (d = 0; d < n; d++) {
		CGOpenGLDisplayMask mask = CGDisplayIDToOpenGLDisplayMask(displays[d]);
		CGLRendererInfoObj info;
		long count = 0, i;
		CGLError err = CGLQueryRendererInfo(mask, &info, &count);

		printf("display %u (0x%x) mask 0x%x: %ld renderers (err %d)\n",
		       (unsigned)d, (unsigned)displays[d], (unsigned)mask, count, err);
		if (err)
			continue;
		for (i = 0; i < count; i++) {
			long id = 0, accel = 0, vram = 0, window = 0, full = 0, off = 0;

			CGLDescribeRenderer(info, i, kCGLRPRendererID, &id);
			CGLDescribeRenderer(info, i, kCGLRPAccelerated, &accel);
			CGLDescribeRenderer(info, i, kCGLRPVideoMemory, &vram);
			CGLDescribeRenderer(info, i, kCGLRPWindow, &window);
			CGLDescribeRenderer(info, i, kCGLRPFullScreen, &full);
			CGLDescribeRenderer(info, i, kCGLRPOffScreen, &off);
			printf("  renderer 0x%08lx accel %ld vram %ld window %ld fullscreen %ld offscreen %ld\n",
			       id, accel, vram, window, full, off);
		}
		CGLDestroyRendererInfo(info);
	}
}

static int draw(long renderer)
{
	CGLPixelFormatAttribute attrs[16];
	CGLPixelFormatObj pix = NULL;
	CGLContextObj ctx = NULL;
	long npix = 0;
	int n = 0, w = 64, h = 64;
	unsigned char *buf, px[4] = { 0, 0, 0, 0 };
	CGLError err;

	attrs[n++] = kCGLPFAOffScreen;
	attrs[n++] = kCGLPFAColorSize;
	attrs[n++] = 32;
	if (renderer) {
		attrs[n++] = kCGLPFARendererID;
		attrs[n++] = renderer;
	}
	attrs[n] = 0;

	err = CGLChoosePixelFormat(attrs, &pix, &npix);
	printf("CGLChoosePixelFormat: err %d, %ld formats\n", err, npix);
	if (err || !pix)
		return 1;
	err = CGLCreateContext(pix, NULL, &ctx);
	printf("CGLCreateContext: err %d\n", err);
	if (err)
		return 1;
	buf = calloc(w * h, 4);
	err = CGLSetOffScreen(ctx, w, h, w * 4, buf);
	printf("CGLSetOffScreen: err %d\n", err);
	CGLSetCurrentContext(ctx);

	/* The context object is public: where its GL dispatch table is. */
	printf("context %p, rend %p, disp %p (%u entries), accum %p, clear %p\n",
	       (void *)ctx, (void *)ctx->rend, (void *)&ctx->disp,
	       (unsigned)(sizeof(ctx->disp) / sizeof(void *)),
	       (void *)ctx->disp.accum, (void *)ctx->disp.clear);

	printf("GL_VENDOR:   %s\n", glGetString(GL_VENDOR));
	printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
	printf("GL_VERSION:  %s\n", glGetString(GL_VERSION));

	glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glFinish();
	glReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("cleared to green, read back RGBA %u %u %u %u (GL error 0x%x)\n",
	       px[0], px[1], px[2], px[3], (unsigned)glGetError());

	printf("after drawing: clear %p\n", (void *)ctx->disp.clear);

	CGLSetCurrentContext(NULL);
	CGLDestroyContext(ctx);
	CGLDestroyPixelFormat(pix);
	free(buf);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "draw"))
		return draw(argc > 2 ? strtol(argv[2], NULL, 0) : 0);
	list();
	return 0;
}
