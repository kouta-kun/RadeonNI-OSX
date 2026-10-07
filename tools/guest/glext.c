/*
 * glext: print the OpenGL strings a program gets from a window's context:
 * vendor, renderer, version and the list of extensions.
 *
 * Build in the guest (the G5 has no OpenGL headers), then copy it over:
 *   gcc -isysroot /Developer/SDKs/MacOSX10.4u.sdk -Wall -o glext glext.c \
 *       -framework GLUT -framework OpenGL
 *
 * RDN_EXTENSIONS=mesa in its environment shows the list as Mesa makes it.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <OpenGL/gl.h>
#include <GLUT/glut.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH | GLUT_STENCIL);
	glutCreateWindow("glext");
	printf("VENDOR %s\nRENDERER %s\nVERSION %s\nEXTENSIONS %s\n",
	       glGetString(GL_VENDOR), glGetString(GL_RENDERER),
	       glGetString(GL_VERSION), glGetString(GL_EXTENSIONS));
	return 0;
}
