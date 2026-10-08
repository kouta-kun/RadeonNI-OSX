/*
 * occtest: ARB_occlusion_query on the card. Draws a 20x20 quad with a query
 * around it and one with the depth test failing, and prints the samples
 * counted (400 and 0 wanted). piglit's occlusion_query got 0 for both
 * (docs/PIGLIT.md, cluster 3).
 *
 * Build on the host:
 *   scripts/darwin.sh powerpc-apple-darwin8-gcc -mmacosx-version-min=10.4 \
 *       -Wall -o build/occtest tools/guest/occtest.c \
 *       -framework GLUT -framework OpenGL
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <OpenGL/gl.h>
#include <GLUT/glut.h>
#include <stdio.h>

static void quad(float z)
{
	glBegin(GL_QUADS);
	glVertex3f(10, 10, z); glVertex3f(30, 10, z);
	glVertex3f(30, 30, z); glVertex3f(10, 30, z);
	glEnd();
}

int main(int argc, char **argv)
{
	GLuint q[3], avail = 0, samples = 0;
	int i, spin;

	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
	glutInitWindowSize(64, 64);
	glutCreateWindow("occtest");
	{
		GLint db = -1;

		glGetIntegerv(GL_DEPTH_BITS, &db);
		printf("RENDERER %s, depth bits %d\n", glGetString(GL_RENDERER), (int)db);
	}
	glViewport(0, 0, 64, 64);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, 64, 0, 64, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glGenQueriesARB(3, q);

	/* 1: nothing in the way. */
	glBeginQueryARB(GL_SAMPLES_PASSED_ARB, q[0]);
	quad(0.0f);
	glEndQueryARB(GL_SAMPLES_PASSED_ARB);
	/* 2: behind the first quad (glOrtho maps z -0.5 to the far side), no pixel passes. */
	glBeginQueryARB(GL_SAMPLES_PASSED_ARB, q[1]);
	quad(-0.5f);
	glEndQueryARB(GL_SAMPLES_PASSED_ARB);
	/* 3: no draw at all. */
	glBeginQueryARB(GL_SAMPLES_PASSED_ARB, q[2]);
	glEndQueryARB(GL_SAMPLES_PASSED_ARB);

	{
		unsigned char px[4] = { 0 };

		/* Depth test sanity: a red quad at depth 0.5, a green one behind it. */
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glColor3f(1, 0, 0); quad(0.0f);
		glColor3f(0, 1, 0); quad(-0.5f);
		glReadPixels(20, 20, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		printf("depth test: pixel %d %d %d (255 0 0 if the far quad is hidden)\n", px[0], px[1], px[2]);
	}
	for (i = 0; i < 3; i++) {
		for (spin = 0; spin < 1000000; spin++) {
			glGetQueryObjectuivARB(q[i], GL_QUERY_RESULT_AVAILABLE_ARB, &avail);
			if (avail)
				break;
		}
		glGetQueryObjectuivARB(q[i], GL_QUERY_RESULT_ARB, &samples);
		printf("query %d: available %u (after %d polls), samples %u\n", i, (unsigned)avail, spin, (unsigned)samples);
	}
	printf("wanted: 400, 0, 0\n");
	return 0;
}
