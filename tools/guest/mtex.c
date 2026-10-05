/*
 * mtex: two textures multiplied, from client vertex arrays, as Quake 3
 * draws a lightmapped wall.
 *
 *   mtex [mode]
 *
 * A square of texture 0 (grey 200) times texture 1 (grey 200) times a
 * colour array of 255: every pixel of it should come out near 9d9d9d
 * (200 * 200 / 255 = 157). Bits of mode leave things out to find what
 * breaks it: 1 no colour array (glColor instead), 2 no second texture,
 * 4 indices as GL_UNSIGNED_INT like Quake (default GL_UNSIGNED_SHORT),
 * 8 the second texture's coordinates as glMultiTexCoord, not an array,
 * 16 the two coordinate sets as separate arrays with stride 0, 32 with
 * glLockArraysEXT, 64 the second set's array left enabled afterwards,
 * 128 drawn twice with the colour array's contents changed in between.
 * Texture 1 is black on its left half and every vertex points at its
 * right half, so a square that is black on the left got the first set.
 *
 * Build in the guest:
 *   gcc -isysroot /Developer/SDKs/MacOSX10.4u.sdk -Wall -o mtex mtex.c \
 *       -framework GLUT -framework OpenGL
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GLUT/glut.h>
#include <OpenGL/glext.h>

static int mode, frames;

static unsigned pixel(int x, int y)
{
	unsigned char p[4] = { 0, 0, 0, 0 };

	glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
	return (p[0] << 16) | (p[1] << 8) | p[2];
}

static void display(void)
{
	static GLuint tex[2];
	static const GLfloat xyz[4][4] = {
		{ -0.8f, -0.8f, 0, 0 }, { 0.8f, -0.8f, 0, 0 },
		{ 0.8f, 0.8f, 0, 0 }, { -0.8f, 0.8f, 0, 0 },
	};
	/* Quake's layout: both sets interleaved, 16 bytes a vertex. */
	/*
	 * The second set points every vertex at the bright half of texture
	 * 1; the first set, used for it by mistake, would give a black left
	 * half.
	 */
	static const GLfloat st[4][2][2] = {
		{ { 0, 0 }, { 0.8f, 0.5f } }, { { 1, 0 }, { 0.8f, 0.5f } },
		{ { 1, 1 }, { 0.8f, 0.5f } }, { { 0, 1 }, { 0.8f, 0.5f } },
	};
	/* The same as two arrays of their own, as Quake has them. */
	static const GLfloat st0[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	static const GLfloat st1[1000][2] = { { 0.8f, 0.5f }, { 0.8f, 0.5f },
					      { 0.8f, 0.5f }, { 0.8f, 0.5f } };
	static GLubyte colors[4][4];
	static const GLuint idx32[6] = { 0, 1, 2, 0, 2, 3 };
	static const GLushort idx16[6] = { 0, 1, 2, 0, 2, 3 };

	if (!tex[0]) {
		static GLubyte grey[8 * 8 * 4];
		int i;

		memset(grey, 200, sizeof(grey));
		memset(colors, 255, sizeof(colors));
		glGenTextures(2, tex);
		for (i = 0; i < 2; i++) {
			int k;

			/* Texture 1: black on its left half, 200 on its right. */
			if (i == 1)
				for (k = 0; k < 64; k++)
					if (k % 8 < 4)
						memset(grey + k * 4, 0, 3);
			glBindTexture(GL_TEXTURE_2D, tex[i]);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA,
				     GL_UNSIGNED_BYTE, grey);
		}
	}
	glViewport(0, 0, 200, 200);
	glClearColor(0, 0, 1, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, 16, xyz);
	if (mode & 1) {
		glColor4ub(255, 255, 255, 255);
	} else {
		glEnableClientState(GL_COLOR_ARRAY);
		glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);
	}

	glActiveTextureARB(GL_TEXTURE0_ARB);
	glClientActiveTextureARB(GL_TEXTURE0_ARB);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, tex[0]);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	if (mode & 16)
		glTexCoordPointer(2, GL_FLOAT, 0, st0);
	else
		glTexCoordPointer(2, GL_FLOAT, 16, st[0][0]);

	if (!(mode & 2)) {
		glActiveTextureARB(GL_TEXTURE1_ARB);
		glClientActiveTextureARB(GL_TEXTURE1_ARB);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, tex[1]);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		if (mode & 8) {
			glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.5f, 0.5f);
		} else {
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			if (mode & 16)
				glTexCoordPointer(2, GL_FLOAT, 0, st1);
			else
				glTexCoordPointer(2, GL_FLOAT, 16, st[0][1]);
		}
	}
	if (mode & 32)
		glLockArraysEXT(0, 4);
	if (mode & 128) {
		/*
		 * Quake's several stages: the same arrays drawn twice with
		 * their contents changed in between. The first, black, must
		 * be covered by the second.
		 */
		memset(colors, 0, sizeof(colors));
		glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, idx32);
		memset(colors, 255, sizeof(colors));
		glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);
	}
	if (mode & 4)
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, idx32);
	else
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx16);
	if (mode & 32)
		glUnlockArraysEXT();
	if (!(mode & 2)) {
		/* Quake leaves this array enabled (64). */
		if (!(mode & 64))
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisable(GL_TEXTURE_2D);
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glClientActiveTextureARB(GL_TEXTURE0_ARB);
	}
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisable(GL_TEXTURE_2D);
	glFinish();
	if (++frames == 3) {
		printf("mode %d: middle %06x, left %06x, right %06x, top %06x, bottom %06x (want %s), outside %06x\n",
		       mode, pixel(100, 100), pixel(40, 100), pixel(160, 100),
		       pixel(100, 160), pixel(100, 40),
		       (mode & 2) ? "c8c8c8" : "9d9d9d", pixel(5, 5));
		fflush(stdout);
		exit(0);
	}
	glutPostRedisplay();
}

int main(int argc, char **argv)
{
	glutInit(&argc, argv);
	if (argc > 1)
		mode = atoi(argv[1]);
	glutInitDisplayMode(GLUT_RGBA | GLUT_SINGLE);
	glutInitWindowSize(200, 200);
	glutCreateWindow("mtex");
	glutDisplayFunc(display);
	glutMainLoop();
	return 0;
}
