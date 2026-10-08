/*
 * packedtest: one texel of every packed pixel type uploaded as GL_RGBA8,
 * read back two ways: glGetTexImage, and drawn on a quad and read from the
 * window with glReadPixels (what piglit's teximage-colors does). A wrong
 * channel order shows as a FAIL. piglit found 5_5_5_1, 4_4_4_4, 10_10_10_2
 * and 5_6_5_REV wrong on the G5 (docs/PIGLIT.md, cluster 1).
 *
 * Build on the host:
 *   scripts/darwin.sh powerpc-apple-darwin8-gcc -mmacosx-version-min=10.4 \
 *       -Wall -o build/packedtest tools/guest/packedtest.c \
 *       -framework GLUT -framework OpenGL
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <OpenGL/gl.h>
#include <GLUT/glut.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_UNSIGNED_INT_8_8_8_8
#define GL_UNSIGNED_INT_8_8_8_8 0x8035
#endif

static const struct {
	const char *name;
	GLenum format, type;
	unsigned bits;
	uint32_t word;
	uint8_t want[4];
	GLenum native;	/* a matching sized internal format, or 0 */
} t[] = {
	{ "RGBA 4_4_4_4", GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, 16, 0xa5f0, { 170, 85, 255, 0 }, GL_RGBA4 },
	{ "RGBA 4_4_4_4_REV", GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4_REV, 16, 0x0f5a, { 170, 85, 255, 0 }, GL_RGBA4 },
	{ "BGRA 4_4_4_4", GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4, 16, 0xf5a0, { 170, 85, 255, 0 }, GL_RGBA4 },
	{ "RGBA 5_5_5_1", GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 16, 0xf801, { 255, 0, 0, 255 }, GL_RGB5_A1 },
	{ "RGBA 1_5_5_5_REV", GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, 16, 0x801f, { 255, 0, 0, 255 }, GL_RGB5_A1 },
	{ "BGRA 5_5_5_1", GL_BGRA, GL_UNSIGNED_SHORT_5_5_5_1, 16, 0xf801, { 0, 0, 255, 255 }, GL_RGB5_A1 },
	{ "RGB 5_6_5", GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 16, 0xf800, { 255, 0, 0, 255 }, 0x8D62 },
	{ "RGB 5_6_5_REV", GL_RGB, GL_UNSIGNED_SHORT_5_6_5_REV, 16, 0x001f, { 255, 0, 0, 255 }, 0x8D62 },
	{ "RGBA 10_10_10_2", GL_RGBA, GL_UNSIGNED_INT_10_10_10_2, 32, 0xffc00003, { 255, 0, 0, 255 }, GL_RGB10_A2 },
	{ "RGBA 2_10_10_10_REV", GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, 32, 0xc00003ff, { 255, 0, 0, 255 }, GL_RGB10_A2 },
	{ "RGBA 8_8_8_8", GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, 32, 0x11223344, { 0x11, 0x22, 0x33, 0x44 }, 0 },
	{ "RGBA 8_8_8_8_REV", GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, 32, 0x44332211, { 0x11, 0x22, 0x33, 0x44 }, 0 },
	{ "RGB 3_3_2", GL_RGB, GL_UNSIGNED_BYTE_3_3_2, 8, 0xe0, { 255, 0, 0, 255 }, 0 },
	{ "RGB 2_3_3_REV", GL_RGB, GL_UNSIGNED_BYTE_2_3_3_REV, 8, 0x07, { 255, 0, 0, 255 }, 0 },
};

static int differs(const uint8_t *a, const uint8_t *b)
{
	int k;

	for (k = 0; k < 4; k++)
		if (abs((int)a[k] - (int)b[k]) > 8)
			return 1;
	return 0;
}

int main(int argc, char **argv)
{
	int i, bad = 0;
	GLuint tex;

	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
	glutInitWindowSize(64, 64);
	glutCreateWindow("packedtest");
	printf("RENDERER %s  VERSION %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
	{
		GLint sw = -1, lsb = -1, al = -1, rl = -1;

		glGetIntegerv(GL_UNPACK_SWAP_BYTES, &sw);
		glGetIntegerv(GL_UNPACK_LSB_FIRST, &lsb);
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &al);
		glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rl);
		printf("unpack: swap_bytes %d lsb_first %d alignment %d row_length %d\n", sw, lsb, al, rl);
	}
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glViewport(0, 0, 64, 64);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, 64, 0, 64, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	for (i = 0; i < (int)(sizeof t / sizeof t[0]); i++) {
		uint8_t src[4] = { 0 }, got[4] = { 0 }, drawn[4] = { 0 };
		uint16_t w16 = (uint16_t)t[i].word;
		uint32_t w32 = t[i].word;
		uint8_t w8 = (uint8_t)t[i].word;
		int b1, b2;
		uint8_t nat[4] = { 0 };

		if (t[i].bits == 16) memcpy(src, &w16, 2);
		else if (t[i].bits == 32) memcpy(src, &w32, 4);
		else memcpy(src, &w8, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, t[i].format, t[i].type, src);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, got);
		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT);
		glBegin(GL_QUADS);
		glTexCoord2f(0, 0); glVertex2f(0, 0);
		glTexCoord2f(1, 0); glVertex2f(64, 0);
		glTexCoord2f(1, 1); glVertex2f(64, 64);
		glTexCoord2f(0, 1); glVertex2f(0, 64);
		glEnd();
		glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, drawn);
		if (t[i].native) {
			glTexImage2D(GL_TEXTURE_2D, 0, t[i].native, 1, 1, 0, t[i].format, t[i].type, src);
			glClear(GL_COLOR_BUFFER_BIT);
			glBegin(GL_QUADS);
			glTexCoord2f(0, 0); glVertex2f(0, 0);
			glTexCoord2f(1, 0); glVertex2f(64, 0);
			glTexCoord2f(1, 1); glVertex2f(64, 64);
			glTexCoord2f(0, 1); glVertex2f(0, 64);
			glEnd();
			glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, nat);
		}
		if (t[i].native) {
			uint8_t back[4] = { 0 };

			glTexImage2D(GL_TEXTURE_2D, 0, t[i].native, 1, 1, 0, t[i].format, t[i].type, src);
			glGetTexImage(GL_TEXTURE_2D, 0, t[i].format, t[i].type, back);
			if (memcmp(back, src, t[i].bits / 8)) {
				printf("%-22s round trip as the same type FAILED: sent %02x%02x%02x%02x got %02x%02x%02x%02x\n",
				       t[i].name, src[0], src[1], src[2], src[3], back[0], back[1], back[2], back[3]);
				bad++;
			}
		}
		b1 = differs(got, t[i].want);
		b2 = differs(drawn, t[i].want);
		/* A texel without alpha reads back with alpha 1 on the quad. */
		printf("%-22s getteximage %s  drawn %s   want %3d %3d %3d %3d  got %3d %3d %3d %3d  drawn %3d %3d %3d %3d  native %3d %3d %3d %3d\n",
		       t[i].name, b1 ? "FAIL" : "ok  ", b2 ? "FAIL" : "ok  ",
		       t[i].want[0], t[i].want[1], t[i].want[2], t[i].want[3],
		       got[0], got[1], got[2], got[3], drawn[0], drawn[1], drawn[2], drawn[3], nat[0], nat[1], nat[2], nat[3]);
		bad += b1 + b2;
	}
	printf("%s\n", bad ? "FAILED" : "all passed");
	return bad != 0;
}
