/*
 * listwin: a display list that changes matrix modes inside, in a window.
 *
 *   listwin [mode]     bits: 1 the strip, 2 the plain quad, 4 normals and
 *                      8 texture coordinates in the strip (default 15)
 *
 * Builds a list the way Apple's Chess builds its pieces: a scale on the
 * texture matrix, then geometry under the modelview matrix, each pushed
 * and popped inside the list. Draws it and reads pixels back: the square
 * must cover the middle of the window only. If the scale lands on the
 * modelview matrix instead, it covers everything.
 *
 * Build in the guest:
 *   gcc -isysroot /Developer/SDKs/MacOSX10.4u.sdk -Wall -o listwin listwin.c \
 *       -framework GLUT -framework OpenGL
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <GLUT/glut.h>

static GLuint list;
static int frames;
/* Bits: 1 the strip, 2 the plain quad, 4 normals and 8 texture coordinates in the strip. */
static int mode = 15;

static unsigned pixel(int x, int y)
{
	unsigned char p[4] = { 0, 0, 0, 0 };

	glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
	return (p[0] << 16) | (p[1] << 8) | p[2];
}

static void display(void)
{
	if (!list) {
		list = glGenLists(1);
		glNewList(list, GL_COMPILE);
		glMatrixMode(GL_TEXTURE);
		glPushMatrix();
		glScalef(2.0f, 2.0f, 1.0f);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glRotatef(0.0f, 1.0f, 0.0f, 0.0f);
		glColor3f(1.0f, 0.0f, 0.0f);
		/*
		 * The square in two halves with different vertex formats,
		 * as the pieces have: normals and texture coordinates in
		 * the first strip, positions alone in the second.
		 */
		if (mode & 1) {
		glBegin(GL_QUAD_STRIP);
		if (mode & 4)
			glNormal3f(0.0f, 0.0f, 1.0f);
		if (mode & 8)
		glTexCoord2f(0.0f, 0.0f);
		glVertex3f(-0.5f, -0.5f, 0.0f);
		if (mode & 8)
		glTexCoord2f(0.0f, 1.0f);
		glVertex3f(-0.5f, 0.5f, 0.0f);
		if (mode & 4)
			glNormal3f(0.0f, 0.0f, 1.0f);
		if (mode & 8)
		glTexCoord2f(1.0f, 0.0f);
		glVertex3f(0.0f, -0.5f, 0.0f);
		if (mode & 8)
		glTexCoord2f(1.0f, 1.0f);
		glVertex3f(0.0f, 0.5f, 0.0f);
		glEnd();
		}
		if (mode & 2) {
		glBegin(GL_QUADS);
		glVertex3f(0.0f, -0.5f, 0.0f);
		glVertex3f(0.5f, -0.5f, 0.0f);
		glVertex3f(0.5f, 0.5f, 0.0f);
		glVertex3f(0.0f, 0.5f, 0.0f);
		glEnd();
		}
		glPopMatrix();
		glMatrixMode(GL_TEXTURE);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glEndList();
	}
	glViewport(0, 0, 200, 200);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glCallList(list);
	glFinish();
	if (++frames == 3) {
		GLfloat m[16];

		printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
		printf("middle (100,100): %06x, want ff0000\n", pixel(100, 100));
		printf("corner (20,20):   %06x, want 0000ff\n", pixel(20, 20));
		printf("edge (100,180):   %06x, want 0000ff\n", pixel(100, 180));
		printf("left half (70,100):  %06x, want ff0000\n", pixel(70, 100));
		printf("right half (130,100): %06x, want ff0000\n", pixel(130, 100));
		printf("far right (180,100): %06x, want 0000ff\n", pixel(180, 100));
		glGetFloatv(GL_MODELVIEW_MATRIX, m);
		printf("modelview diagonal after the list: %g %g %g\n", m[0], m[5], m[10]);
		glGetFloatv(GL_TEXTURE_MATRIX, m);
		printf("texture diagonal after the list:   %g %g %g\n", m[0], m[5], m[10]);
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
	glutCreateWindow("listwin");
	glutDisplayFunc(display);
	glutMainLoop();
	return 0;
}
