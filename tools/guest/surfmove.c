/*
 * surfmove: a GL window that draws one frame and then moves itself.
 *
 *   surfmove [steps [milliseconds]]
 *
 * Draws once (a red field with a green square in the middle), like a
 * program that only redraws when it has to, then moves its window 60
 * pixels right and 30 down every so often (default: 6 steps, 700 ms
 * apart) without drawing again, waits three seconds and exits. For
 * checking, without a mouse, what happens on the screen to a window whose
 * contents the card shows when the window is moved.
 *
 * Build in the guest:
 *   gcc -isysroot /Developer/SDKs/MacOSX10.4u.sdk -Wall -o surfmove surfmove.c \
 *       -framework GLUT -framework OpenGL
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <GLUT/glut.h>

static int steps = 6, interval = 700, step, drawn;

static void display(void)
{
	glViewport(0, 0, 300, 200);
	glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glColor3f(0.0f, 1.0f, 0.0f);
	glBegin(GL_QUADS);
	glVertex2f(-0.3f, -0.3f);
	glVertex2f(0.3f, -0.3f);
	glVertex2f(0.3f, 0.3f);
	glVertex2f(-0.3f, 0.3f);
	glEnd();
	glutSwapBuffers();
	printf("frame %d drawn\n", ++drawn);
	fflush(stdout);
}

static void tick(int value)
{
	if (step == steps) {
		printf("moved %d times, %d frames drawn in all\n", steps, drawn);
		exit(0);
	}
	if (++step < steps + 1 && step <= steps) {
		glutPositionWindow(100 + step * 60, 100 + step * 30);
		printf("moved to %d,%d\n", 100 + step * 60, 100 + step * 30);
		fflush(stdout);
	}
	glutTimerFunc(step == steps ? 3000 : interval, tick, 0);
}

int main(int argc, char **argv)
{
	glutInit(&argc, argv);
	if (argc > 1)
		steps = atoi(argv[1]);
	if (argc > 2)
		interval = atoi(argv[2]);
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
	glutInitWindowSize(300, 200);
	glutInitWindowPosition(100, 100);
	glutCreateWindow("surfmove");
	glutDisplayFunc(display);
	glutTimerFunc(1500, tick, 0);
	glutMainLoop();
	return 0;
}
