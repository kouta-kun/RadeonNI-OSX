/*
 * sharetest: contexts that share objects, as Core Image's three do.
 *
 *   A is an off-screen context with its own memory. B is made sharing
 *   with A (CGLCreateContext's second argument) and has its own memory. C
 *   shares with nothing.
 *
 *   1. in A: a texture of one colour and a fragment program that swaps red
 *      and blue; in B: both are there (glIsTexture, glIsProgramARB) and a
 *      quad drawn with them gives the swapped colour, in B's memory and
 *      through glReadPixels; A's memory is still untouched
 *   2. in C: neither exists
 *   3. back in A: the quad is drawn there too, the swapped colour
 *   4. a texture made in B after A existed is seen in A
 *
 *   sharetest [renderer id, default 0x21a00]
 *
 * Build on the host:
 *   scripts/darwin.sh powerpc-apple-darwin8-gcc -O2 -o build/sharetest \
 *       tools/guest/sharetest.c -framework OpenGL
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

#define SIDE 32

static int failed;

static void check(const char *what, int ok)
{
	printf("%-62s %s\n", what, ok ? "PASS" : "FAIL");
	if (!ok)
		failed++;
}

static CGLContextObj make(CGLPixelFormatObj pix, CGLContextObj share,
			  unsigned *memory)
{
	CGLContextObj ctx = NULL;

	if (CGLCreateContext(pix, share, &ctx) || !ctx ||
	    CGLSetOffScreen(ctx, SIDE, SIDE, SIDE * 4, memory)) {
		printf("cannot make a context\n");
		exit(1);
	}
	return ctx;
}

static void quad(void)
{
	glViewport(0, 0, SIDE, SIDE);
	glDisable(GL_DEPTH_TEST);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(-1, -1);
	glTexCoord2f(1, 0); glVertex2f(1, -1);
	glTexCoord2f(1, 1); glVertex2f(1, 1);
	glTexCoord2f(0, 1); glVertex2f(-1, 1);
	glEnd();
	glFinish();
}

int main(int argc, char **argv)
{
	static const char swap[] =
		"!!ARBfp1.0\n"
		"TEMP t;\n"
		"TEX t, fragment.texcoord[0], texture[0], 2D;\n"
		"MOV result.color, t.zyxw;\n"
		"END\n";
	long renderer = argc > 1 ? strtol(argv[1], NULL, 0) : 0x21a00;
	CGLPixelFormatAttribute attrs[8];
	CGLPixelFormatObj pix = NULL;
	CGLContextObj a, b, c;
	static unsigned ma[SIDE * SIDE], mb[SIDE * SIDE], mc[SIDE * SIDE];
	static unsigned char picture[4 * 4 * 4];
	unsigned char px[4];
	GLuint tex = 0, prog = 0, tex2 = 0;
	long npix = 0;
	int n = 0, i;

	attrs[n++] = kCGLPFAOffScreen;
	attrs[n++] = kCGLPFAColorSize;
	attrs[n++] = 32;
	attrs[n++] = kCGLPFARendererID;
	attrs[n++] = renderer;
	attrs[n] = 0;
	if (CGLChoosePixelFormat(attrs, &pix, &npix) || !pix) {
		printf("no pixel format on renderer 0x%lx\n", renderer);
		return 1;
	}

	a = make(pix, NULL, ma);
	b = make(pix, a, mb);
	c = make(pix, NULL, mc);

	/* A: the texture and the program. */
	CGLSetCurrentContext(a);
	printf("renderer: %s\n", glGetString(GL_RENDERER));
	for (i = 0; i < 16; i++) {
		picture[i * 4 + 0] = 0x20;	/* R */
		picture[i * 4 + 1] = 0x40;	/* G */
		picture[i * 4 + 2] = 0x80;	/* B */
		picture[i * 4 + 3] = 0xff;
	}
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, picture);
	glGenProgramsARB(1, &prog);
	glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, prog);
	glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB,
			   strlen(swap), swap);
	glFinish();

	/* B: they are there, and draw. */
	CGLSetCurrentContext(b);
	check("1. B sees A's texture (glIsTexture)", glIsTexture(tex));
	check("   B sees A's program (glIsProgramARB)", glIsProgramARB(prog));
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glBindTexture(GL_TEXTURE_2D, tex);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_FRAGMENT_PROGRAM_ARB);
	glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, prog);
	quad();
	memset(px, 0, 4);
	glReadPixels(SIDE / 2, SIDE / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("   read back RGBA %u %u %u %u (GL error 0x%x)\n", px[0], px[1],
	       px[2], px[3], (unsigned)glGetError());
	check("   the quad in B is red and blue swapped (glReadPixels)",
	      px[0] == 0x80 && px[1] == 0x40 && px[2] == 0x20);
	printf("   B's memory: %08x at the middle, %08x at the corner\n",
	       mb[(SIDE / 2) * SIDE + SIDE / 2], mb[0]);
	check("   and in B's own memory (ARGB 0xff804020)",
	      mb[(SIDE / 2) * SIDE + SIDE / 2] == 0xff804020u);
	check("   A's memory is untouched", ma[(SIDE / 2) * SIDE + SIDE / 2] == 0);

	/* C shares nothing. */
	CGLSetCurrentContext(c);
	check("2. C does not see A's texture", !glIsTexture(tex));
	check("   C does not see A's program", !glIsProgramARB(prog));

	/* A draws with the same objects. */
	CGLSetCurrentContext(a);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_FRAGMENT_PROGRAM_ARB);
	glBindTexture(GL_TEXTURE_2D, tex);
	glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, prog);
	quad();
	memset(px, 0, 4);
	glReadPixels(SIDE / 2, SIDE / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	check("3. the quad in A is swapped too",
	      px[0] == 0x80 && px[1] == 0x40 && px[2] == 0x20);
	check("   A's own memory has it (ARGB 0xff804020)",
	      ma[(SIDE / 2) * SIDE + SIDE / 2] == 0xff804020u);

	/* A texture made in B now. */
	CGLSetCurrentContext(b);
	glGenTextures(1, &tex2);
	glBindTexture(GL_TEXTURE_2D, tex2);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, picture);
	glFinish();
	CGLSetCurrentContext(a);
	check("4. A sees the texture B made after it", glIsTexture(tex2));

	CGLSetCurrentContext(NULL);
	CGLDestroyContext(c);
	CGLDestroyContext(b);
	CGLDestroyContext(a);
	CGLDestroyPixelFormat(pix);
	printf(failed ? "%d failed\n" : "all passed\n", failed);
	return failed != 0;
}
