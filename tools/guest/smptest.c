/*
 * smptest: a full-screen CGL context made current in a second thread, the
 * way Quake 3 does with r_smp 1. The main thread makes it current, then
 * not, and starts the render thread, which makes it current and draws
 * frames that change colour (red, green, blue, each for a second) with
 * CGLFlushDrawable. Look at the screen with rdnuc grab or readback.
 *
 * Build on the host:
 *   scripts/darwin.sh powerpc-apple-darwin8-gcc -mmacosx-version-min=10.4 \
 *       -Wall -o build/smptest tools/guest/smptest.c \
 *       -framework ApplicationServices -framework OpenGL -lpthread
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <ApplicationServices/ApplicationServices.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static CGLContextObj ctx;
static int seconds = 9;

static void *render(void *arg)
{
	int frame;
	float col[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

	(void)arg;
	CGLSetCurrentContext(ctx);
	for (frame = 0; frame < seconds * 60; frame++) {
		float *c = col[(frame / 60) % 3];

		glClearColor(c[0], c[1], c[2], 1);
		glClear(GL_COLOR_BUFFER_BIT);
		if (frame == 5)
			printf("render thread: GL_RENDERER %s\n", glGetString(GL_RENDERER));
		CGLFlushDrawable(ctx);
		usleep(16000);
	}
	CGLSetCurrentContext(NULL);
	return NULL;
}

int main(int argc, char **argv)
{
	CGLPixelFormatObj pf;
	GLint n = 0;
	CGDirectDisplayID d = CGMainDisplayID();
	pthread_t t;
	CGLPixelFormatAttribute attr[] = { kCGLPFAFullScreen, kCGLPFADoubleBuffer,
		kCGLPFAColorSize, 32, kCGLPFADepthSize, 16, kCGLPFADisplayMask, 0, 0 };

	if (argc > 1)
		seconds = atoi(argv[1]);
	attr[7] = CGDisplayIDToOpenGLDisplayMask(d);
	if (CGLChoosePixelFormat(attr, &pf, &n) || !pf || CGLCreateContext(pf, NULL, &ctx)) {
		printf("no context\n");
		return 1;
	}
	CGDisplayCapture(d);
	CGLSetFullScreen(ctx);
	CGLSetCurrentContext(ctx);
	printf("main thread: GL_RENDERER %s\n", glGetString(GL_RENDERER));
	CGLSetCurrentContext(NULL);
	pthread_create(&t, NULL, render, NULL);
	pthread_join(t, NULL);
	CGLSetCurrentContext(NULL);
	CGLDestroyContext(ctx);
	CGDisplayRelease(d);
	return 0;
}
