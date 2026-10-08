/*
 * doubletest: front and back buffers of a double-buffered GLUT window.
 * Draws blue (bottom half) and green (top half) on the back buffer, reads
 * both buffers (the front must still be untouched), swaps, and reads both
 * again (the front must now hold the picture; the back keeps it or not).
 * Then draws red on the front buffer and reads it back. Prints what was
 * read; piglit read-front, swapbuffers-behavior and drawbuffer-modes ask
 * the same (docs/PIGLIT.md).
 *
 * Build on the host:
 *   scripts/darwin.sh powerpc-apple-darwin8-gcc -mmacosx-version-min=10.4 \
 *       -Wall -o build/doubletest tools/guest/doubletest.c \
 *       -framework GLUT -framework OpenGL
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <OpenGL/gl.h>
#include <GLUT/glut.h>
#include <stdio.h>
#include <string.h>

static void rect(float x0, float y0, float x1, float y1)
{
	glBegin(GL_QUADS);
	glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
	glEnd();
}

static void probe(const char *what, GLenum buffer)
{
	unsigned char lo[4] = { 0 }, hi[4] = { 0 };

	glReadBuffer(buffer);
	glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, lo);
	glReadPixels(10, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, hi);
	printf("%-28s bottom %3d %3d %3d   top %3d %3d %3d\n", what, lo[0], lo[1], lo[2],
	       hi[0], hi[1], hi[2]);
}

int main(int argc, char **argv)
{
	glutInit(&argc, argv);
	/* "rgb": the pixel format of piglit's read-front (no alpha, depth, stencil),
	 * and the front buffer is not touched before the swap. */
	int rgb = argc > 1 && !strcmp(argv[1], "rgb");

	glutInitDisplayMode((rgb ? GLUT_RGB | GLUT_DEPTH | GLUT_STENCIL : GLUT_RGBA) | GLUT_DOUBLE);
	glutInitWindowSize(160, 160);
	glutCreateWindow("doubletest");
	printf("RENDERER %s\n", glGetString(GL_RENDERER));
	glViewport(0, 0, 160, 160);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, 160, 0, 160, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	if (!rgb) {
		glDrawBuffer(GL_FRONT);
		glClearColor(1, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		glFlush();
	}
	/* "readfirst" is piglit's read-front: it never sets the draw buffer, the default must be the back one. */
	if (!(argc > 2 && !strcmp(argv[2], "readfirst")))
		glDrawBuffer(GL_BACK);
	{
		GLint db = -1;

		glGetIntegerv(GL_DRAW_BUFFER, &db);
		printf("default draw buffer 0x%x (GL_BACK is 0x405)\n", (unsigned)db);
	}
	glClearColor(0, 0, 1, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glColor3f(0, 1, 0);
	rect(0, 80, 160, 160);
	if (!(argc > 2 && !strcmp(argv[2], "readfirst"))) {
		probe("before swap, back  (b/g)", GL_BACK);
		probe("before swap, front (r/r)", GL_FRONT);
	}
	if (argc > 2 && !strcmp(argv[2], "readfirst")) {
		/* piglit read-front: the read buffer is the front one before the swap. */
		unsigned char lo[4] = { 0 }, hi[4] = { 0 };

		if (argc > 3 && !strcmp(argv[3], "finish"))
			glFinish();
		glReadBuffer(GL_FRONT);
		glutSwapBuffers();
		glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, lo);
		glReadPixels(10, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, hi);
		{
			static float f[64 * 32 * 4];

			glReadPixels(0, 0, 64, 32, GL_RGBA, GL_FLOAT, f);
			printf("float rect read, first pixel %g %g %g %g (0 0 1 1)\n", f[0], f[1], f[2], f[3]);
		}
		printf("read buffer front before swap: bottom %d %d %d top %d %d %d (0 0 255, 0 255 0)\n",
		       lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
		return 0;
	}
	glutSwapBuffers();
	probe("after swap, front  (b/g)", GL_FRONT);
	probe("after swap, back   (b/g?)", GL_BACK);
	glDrawBuffer(GL_FRONT);
	glClearColor(1, 1, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glFlush();
	probe("front cleared yellow", GL_FRONT);
	probe("back is not yellow", GL_BACK);
	return 0;
}
