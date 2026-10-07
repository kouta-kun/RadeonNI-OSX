/*
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 *
 * aglfull: the full-screen AGL context a 2006 game asks for, step by step
 * with the error after each. Call of Duty 2's request is the default.
 *
 *   gcc -arch ppc -isysroot /Developer/SDKs/MacOSX10.4u.sdk -o aglfull \
 *       aglfull.c -framework AGL -framework OpenGL -framework Carbon
 *
 *   aglfull [depth N] [stencil N] [aux N] [auxds] [accel] [norecovery]
 *           [pixel N] [nofull] [set] [seconds N]
 *
 * With no arguments: RGBA, double buffer, full screen, depth 24. "set"
 * also takes the screen (aglSetFullScreen) and clears it to green.
 */
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	GLint attrs[40], v;
	int n = 0, i, full = 1, set = 0, seconds = 3;
	int depth = 24, stencil = 0, aux = 0, auxds = 0, accel = 0, norec = 0, pixel = 0;
	GDHandle gd = GetMainDevice();
	AGLPixelFormat fmt;
	AGLContext ctx;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "depth") && i + 1 < argc) depth = atoi(argv[++i]);
		else if (!strcmp(argv[i], "stencil") && i + 1 < argc) stencil = atoi(argv[++i]);
		else if (!strcmp(argv[i], "aux") && i + 1 < argc) aux = atoi(argv[++i]);
		else if (!strcmp(argv[i], "pixel") && i + 1 < argc) pixel = atoi(argv[++i]);
		else if (!strcmp(argv[i], "seconds") && i + 1 < argc) seconds = atoi(argv[++i]);
		else if (!strcmp(argv[i], "auxds")) auxds = 1;
		else if (!strcmp(argv[i], "accel")) accel = 1;
		else if (!strcmp(argv[i], "norecovery")) norec = 1;
		else if (!strcmp(argv[i], "nofull")) full = 0;
		else if (!strcmp(argv[i], "set")) set = 1;
	}
	attrs[n++] = AGL_RGBA;
	attrs[n++] = AGL_DOUBLEBUFFER;
	if (norec) attrs[n++] = AGL_NO_RECOVERY;
	if (full) attrs[n++] = AGL_FULLSCREEN;
	if (pixel) { attrs[n++] = AGL_PIXEL_SIZE; attrs[n++] = pixel; }
	if (accel) attrs[n++] = AGL_ACCELERATED;
	attrs[n++] = AGL_DEPTH_SIZE; attrs[n++] = depth;
	if (stencil) { attrs[n++] = AGL_STENCIL_SIZE; attrs[n++] = stencil; }
	if (aux) { attrs[n++] = AGL_AUX_BUFFERS; attrs[n++] = aux; }
	if (auxds) attrs[n++] = AGL_AUX_DEPTH_STENCIL;
	attrs[n] = AGL_NONE;

	fmt = aglChoosePixelFormat(&gd, 1, attrs);
	printf("aglChoosePixelFormat -> %p, error %d\n", (void *)fmt, (int)aglGetError());
	if (!fmt)
		return 1;
	{
		static const struct { GLint attr; const char *name; } d[] = {
			{ AGL_RENDERER_ID, "renderer" }, { AGL_ACCELERATED, "accel" },
			{ AGL_FULLSCREEN, "full" }, { AGL_PIXEL_SIZE, "pixel" },
			{ AGL_DEPTH_SIZE, "depth" }, { AGL_STENCIL_SIZE, "stencil" },
			{ AGL_AUX_BUFFERS, "aux" }, { AGL_ALPHA_SIZE, "alpha" },
		};
		unsigned k;

		for (k = 0; k < sizeof(d) / sizeof(d[0]); k++) {
			v = -1;
			aglDescribePixelFormat(fmt, d[k].attr, &v);
			printf("  %s 0x%x", d[k].name, (unsigned)v);
		}
		printf("\n");
	}
	ctx = aglCreateContext(fmt, NULL);
	printf("aglCreateContext -> %p, error %d\n", (void *)ctx, (int)aglGetError());
	aglDestroyPixelFormat(fmt);
	if (!ctx)
		return 2;
	if (set) {
		GLboolean ok = aglSetFullScreen(ctx, CGDisplayPixelsWide(kCGDirectMainDisplay),
						CGDisplayPixelsHigh(kCGDirectMainDisplay), 0, 0);

		printf("aglSetFullScreen -> %d, error %d\n", ok, (int)aglGetError());
		if (ok) {
			aglSetCurrentContext(ctx);
			printf("GL_RENDERER %s\n", (const char *)glGetString(GL_RENDERER));
			glGetIntegerv(GL_DEPTH_BITS, &v); printf("depth bits %d", (int)v);
			glGetIntegerv(GL_STENCIL_BITS, &v); printf(" stencil bits %d", (int)v);
			glGetIntegerv(GL_AUX_BUFFERS, &v); printf(" aux %d\n", (int)v);
			for (i = 0; i < seconds * 10; i++) {
				glClearColor(0, 1, 0, 1);
				glClear(GL_COLOR_BUFFER_BIT);
				aglSwapBuffers(ctx);
				usleep(100000);
			}
			printf("glGetError 0x%x\n", (unsigned)glGetError());
		}
		aglSetCurrentContext(NULL);
	}
	aglDestroyContext(ctx);
	return 0;
}
