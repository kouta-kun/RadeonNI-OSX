/*
 * pfsamples: ask CGL for a window pixel format with and without sample
 * buffers and say what it gives. With RDN_GLD_LOG=file the driver bundle
 * logs the records behind the answers; with RDN_GLD_KEEP_SAMPLES=1 it lets
 * the request for samples through to Apple's software renderer.
 *
 * Build on Tiger:
 *   gcc -isysroot /Developer/SDKs/MacOSX10.4u.sdk -Wall -o pfsamples \
 *       pfsamples.c -framework OpenGL
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <OpenGL/OpenGL.h>

static void ask(int samples, int renderer)
{
	CGLPixelFormatAttribute a[24];
	CGLPixelFormatObj pf = NULL;
	long n = 0, v = 0, sb = 0, rid = 0;
	int i = 0;
	CGLError e;

	a[i++] = kCGLPFAWindow;
	a[i++] = kCGLPFADoubleBuffer;
	a[i++] = kCGLPFAColorSize; a[i++] = 32;
	a[i++] = kCGLPFADepthSize; a[i++] = 24;
	if (renderer) { a[i++] = kCGLPFARendererID; a[i++] = renderer; }
	if (samples) {
		a[i++] = kCGLPFASampleBuffers; a[i++] = 1;
		a[i++] = kCGLPFASamples; a[i++] = samples;
	}
	a[i] = 0;
	e = CGLChoosePixelFormat(a, &pf, &n);
	if (pf) {
		CGLDescribePixelFormat(pf, 0, kCGLPFASamples, &v);
		CGLDescribePixelFormat(pf, 0, kCGLPFASampleBuffers, &sb);
		CGLDescribePixelFormat(pf, 0, kCGLPFARendererID, &rid);
	}
	printf("samples %d, renderer 0x%x: error %d, %ld formats, first: renderer 0x%lx, sample buffers %ld, samples %ld\n",
	       samples, renderer, (int)e, n, rid, sb, v);
	if (pf)
		CGLDestroyPixelFormat(pf);
}

int main(int argc, char **argv)
{
	int renderer = argc > 1 ? (int)strtol(argv[1], NULL, 0) : 0;

	ask(0, renderer);
	ask(4, renderer);
	ask(2, renderer);
	return 0;
}
