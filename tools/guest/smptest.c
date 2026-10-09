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
#include <OpenGL/glext.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static CGLContextObj ctx;
static int seconds = 9;
static int menu, lock;

/* A frame like Quake 3's menu: client arrays, unsigned int indices, depth test on and never cleared, blending, ortho, a texture. */
static void menu_frame(int frame)
{
	static float xyz[4 * 4];
	static unsigned char col[4 * 4];
	static float st[4 * 2];
	static unsigned int idx[6] = { 0, 1, 2, 0, 2, 3 };
	static GLuint tex;
	int i;
	float x0 = 400, y0 = 200, x1 = 1500, y1 = 800;

	if (!tex) {
		unsigned char img[64 * 64 * 4];

		for (i = 0; i < 64 * 64; i++) {
			int on = ((i % 64) / 8 + (i / 64) / 8) & 1;

			img[i * 4] = on ? 255 : 40;
			img[i * 4 + 1] = on ? 255 : 40;
			img[i * 4 + 2] = on ? 255 : 200;
			img[i * 4 + 3] = 255;
		}
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
	glDrawBuffer(GL_BACK);
	glViewport(0, 0, 1920, 1080);
	glScissor(0, 0, 1920, 1080);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, 1920, 1080, 0, 0, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glDisable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, tex);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	for (i = 0; i < 4; i++) {
		xyz[i * 4] = (i == 0 || i == 3) ? x0 : x1;
		xyz[i * 4 + 1] = (i < 2) ? y0 : y1;
		xyz[i * 4 + 2] = 0;
		xyz[i * 4 + 3] = 1;
		col[i * 4] = 255;
		col[i * 4 + 1] = (frame / 30) & 1 ? 128 : 255;
		col[i * 4 + 2] = 255;
		col[i * 4 + 3] = 255;
		st[i * 2] = (i == 0 || i == 3) ? 0 : 1;
		st[i * 2 + 1] = (i < 2) ? 0 : 1;
	}
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(3, GL_FLOAT, 16, xyz);
	glColorPointer(4, GL_UNSIGNED_BYTE, 4, col);
	glTexCoordPointer(2, GL_FLOAT, 0, st);
	if (lock)
		glLockArraysEXT(0, 4);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, idx);
	if (lock)
		glUnlockArraysEXT();
	glFinish();
}

static void *render(void *arg)
{
	int frame;
	float col[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

	(void)arg;
	CGLSetCurrentContext(ctx);
	for (frame = 0; frame < seconds * 60; frame++) {
		float *c = col[(frame / 60) % 3];

		if (menu) {
			if (frame < 60) {
				glClearColor(c[0], c[1], c[2], 1);
				glClear(GL_COLOR_BUFFER_BIT);
			}
			menu_frame(frame);
			CGLFlushDrawable(ctx);
			usleep(16000);
			continue;
		}
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
	menu = argc > 2 && !strncmp(argv[2], "menu", 4);
	lock = argc > 2 && !strcmp(argv[2], "menulock");
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
