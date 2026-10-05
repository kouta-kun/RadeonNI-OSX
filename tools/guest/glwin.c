/*
 * glwin: a windowed OpenGL program for Tiger (GLUT).
 *
 *   glwin [seconds] [x y] [skip]
 *
 * Opens a 320x240 window at (x, y) (default 100, 100), prints the GL
 * strings, and for the given time (default 5 s) draws a turning
 * red-green-blue triangle on dark blue with a white frame, then exits and
 * prints how many frames it drew.
 *
 * Build in the guest:
 *   gcc -isysroot /Developer/SDKs/MacOSX10.4u.sdk -Wall -o glwin glwin.c \
 *       -framework GLUT -framework OpenGL
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <GLUT/glut.h>

static double start, duration = 5.0;
static int frames;
/* Bits: 1 no clear, 2 no frame, 4 no triangle. For telling calls apart. */
static int skip;

static double now(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
}

static void display(void)
{
	double t = now() - start;

	if (frames == 0) {
		printf("GL_VENDOR:   %s\n", glGetString(GL_VENDOR));
		printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
		printf("GL_VERSION:  %s\n", glGetString(GL_VERSION));
		fflush(stdout);
	}
	if (!(skip & 1)) {
		glClearColor(0.0f, 0.0f, 0.4f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	if (!(skip & 2)) {
		glColor3f(1, 1, 1);
		glBegin(GL_LINE_LOOP);
		glVertex2f(-0.95f, -0.95f);
		glVertex2f(0.95f, -0.95f);
		glVertex2f(0.95f, 0.95f);
		glVertex2f(-0.95f, 0.95f);
		glEnd();
	}

	glRotatef((float)(t * 60.0), 0, 0, 1);
	if (!(skip & 4)) {
		glBegin(GL_TRIANGLES);
		glColor3f(1, 0, 0); glVertex2f(-0.6f, -0.5f);
		glColor3f(0, 1, 0); glVertex2f(0.6f, -0.5f);
		glColor3f(0, 0, 1); glVertex2f(0.0f, 0.7f);
		glEnd();
	}
	glutSwapBuffers();
	frames++;

	if (t > duration) {
		printf("%d frames in %.1f s\n", frames, t);
		exit(0);
	}
}

static void idle(void)
{
	glutPostRedisplay();
}

int main(int argc, char **argv)
{
	int x = 100, y = 100;

	glutInit(&argc, argv);
	if (argc > 1)
		duration = atof(argv[1]);
	if (argc > 3) {
		x = atoi(argv[2]);
		y = atoi(argv[3]);
	}
	if (argc > 4)
		skip = atoi(argv[4]);
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
	glutInitWindowSize(320, 240);
	glutInitWindowPosition(x, y);
	glutCreateWindow("glwin");
	glutDisplayFunc(display);
	glutIdleFunc(idle);
	start = now();
	glutMainLoop();
	return 0;
}
