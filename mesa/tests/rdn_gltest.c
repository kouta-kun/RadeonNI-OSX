/*
 * First OpenGL through Mesa on the osx-gpu winsys (Linux host).
 *
 *   rdn_gltest [-n frames] [-a angle] [-b rrggbb] [-D] [-o file.ppm]
 *              [-s] [-L mode]
 *
 * Renders with fixed-function OpenGL into an off-screen buffer: a clear to
 * dark grey, a depth-tested pair of triangles (the red-green-blue one in
 * front of the yellow one, whatever the drawing order) and a checkerboard
 * textured square, turned by -a degrees plus two per frame. -b sets the
 * background, so that a run cannot be mistaken for an earlier one whose
 * picture is still in video memory. Prints the GL strings and a few
 * pixels of the first frame, saves the last frame with -o, and with -s
 * copies every frame to the screen, 400 pixels from its left edge and 128
 * from its top.
 *
 * The library exports only the OSMesa calls; GL functions are looked up
 * with OSMesaGetProcAddress and called here as rglClear and so on.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <GL/gl.h>
#include <GL/osmesa.h>

#include "rdn_target.h"

#define W 512
#define H 512

#define GL_FUNCS(X) \
	X(const GLubyte *, GetString, (GLenum)) \
	X(GLenum, GetError, (void)) \
	X(void, ClearColor, (GLclampf, GLclampf, GLclampf, GLclampf)) \
	X(void, Clear, (GLbitfield)) \
	X(void, Enable, (GLenum)) \
	X(void, Disable, (GLenum)) \
	X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
	X(void, MatrixMode, (GLenum)) \
	X(void, LoadIdentity, (void)) \
	X(void, Ortho, (GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble)) \
	X(void, Rotatef, (GLfloat, GLfloat, GLfloat, GLfloat)) \
	X(void, Translatef, (GLfloat, GLfloat, GLfloat)) \
	X(void, Begin, (GLenum)) \
	X(void, End, (void)) \
	X(void, Color3f, (GLfloat, GLfloat, GLfloat)) \
	X(void, Vertex3f, (GLfloat, GLfloat, GLfloat)) \
	X(void, TexCoord2f, (GLfloat, GLfloat)) \
	X(void, GenTextures, (GLsizei, GLuint *)) \
	X(void, BindTexture, (GLenum, GLuint)) \
	X(void, TexParameteri, (GLenum, GLenum, GLint)) \
	X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *)) \
	X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid *)) \
	X(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
	X(GLuint, GenLists, (GLsizei)) \
	X(void, NewList, (GLuint, GLenum)) \
	X(void, EndList, (void)) \
	X(void, CallList, (GLuint)) \
	X(void, Normal3f, (GLfloat, GLfloat, GLfloat)) \
	X(void, Finish, (void))

#define X(ret, name, args) static ret (*rgl##name) args;
GL_FUNCS(X)
#undef X

static int load_gl(void)
{
#define X(ret, name, args) \
	rgl##name = (ret (*) args)OSMesaGetProcAddress("gl" #name); \
	if (!rgl##name) { fprintf(stderr, "no gl" #name "\n"); return -1; }
	GL_FUNCS(X)
#undef X
	return 0;
}

/* Background colour, 0xRRGGBB; -b changes it. */
static unsigned background = 0x333333;
/* -D: draw without the depth test. */
static int no_depth;

static void draw(float angle, GLuint tex)
{
	rglViewport(0, 0, W, H);
	rglClearColor(((background >> 16) & 0xff) / 255.0f,
		      ((background >> 8) & 0xff) / 255.0f,
		      (background & 0xff) / 255.0f, 1.0f);
	rglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	if (no_depth)
		rglDisable(GL_DEPTH_TEST);
	else
		rglEnable(GL_DEPTH_TEST);

	rglMatrixMode(GL_PROJECTION);
	rglLoadIdentity();
	rglOrtho(-1, 1, -1, 1, -1, 1);
	rglMatrixMode(GL_MODELVIEW);
	rglLoadIdentity();
	rglTranslatef(-0.4f, 0.3f, 0.0f);
	rglRotatef(angle, 0, 0, 1);

	rglDisable(GL_TEXTURE_2D);
	rglBegin(GL_TRIANGLES);
	/* In front (glOrtho makes z = 0.5 the nearer one), drawn first. */
	rglColor3f(1, 0, 0); rglVertex3f(-0.5f, -0.4f, 0.5f);
	rglColor3f(0, 1, 0); rglVertex3f(0.5f, -0.4f, 0.5f);
	rglColor3f(0, 0, 1); rglVertex3f(0.0f, 0.5f, 0.5f);
	/* Behind, drawn second: must not cover the first. */
	rglColor3f(1, 1, 0); rglVertex3f(-0.2f, -0.5f, -0.5f);
	rglColor3f(1, 1, 0); rglVertex3f(0.7f, -0.2f, -0.5f);
	rglColor3f(1, 1, 0); rglVertex3f(0.1f, 0.6f, -0.5f);
	rglEnd();

	rglLoadIdentity();
	rglTranslatef(0.45f, -0.45f, 0.0f);
	rglRotatef(-angle, 0, 0, 1);
	rglEnable(GL_TEXTURE_2D);
	rglBindTexture(GL_TEXTURE_2D, tex);
	rglColor3f(1, 1, 1);
	rglBegin(GL_QUADS);
	rglTexCoord2f(0, 0); rglVertex3f(-0.35f, -0.35f, 0);
	rglTexCoord2f(1, 0); rglVertex3f(0.35f, -0.35f, 0);
	rglTexCoord2f(1, 1); rglVertex3f(0.35f, 0.35f, 0);
	rglTexCoord2f(0, 1); rglVertex3f(-0.35f, 0.35f, 0);
	rglEnd();

	/* A green patch made with a scissored clear: no vertices of ours. */
	rglEnable(GL_SCISSOR_TEST);
	rglScissor(16, 16, 48, 48);
	rglClearColor(0.0f, 1.0f, 0.0f, 1.0f);
	rglClear(GL_COLOR_BUFFER_BIT);
	rglDisable(GL_SCISSOR_TEST);
	rglFinish();
}

/*
 * -L mode: a display list with mixed vertex formats (what Apple's Chess
 * builds its pieces from) instead of the usual scene. A red square in the
 * middle of the view, its left half a quad strip, its right half a plain
 * quad. Bits of mode: 1 the strip, 2 the quad, 4 normals and 8 texture
 * coordinates in the strip.
 */
static void draw_list(int mode)
{
	GLuint list = rglGenLists(1);

	rglNewList(list, GL_COMPILE);
	rglColor3f(1, 0, 0);
	if (mode & 1) {
		rglBegin(GL_QUAD_STRIP);
		if (mode & 4) rglNormal3f(0, 0, 1);
		if (mode & 8) rglTexCoord2f(0, 0);
		rglVertex3f(-0.5f, -0.5f, 0);
		if (mode & 8) rglTexCoord2f(0, 1);
		rglVertex3f(-0.5f, 0.5f, 0);
		if (mode & 4) rglNormal3f(0, 0, 1);
		if (mode & 8) rglTexCoord2f(1, 0);
		rglVertex3f(0, -0.5f, 0);
		if (mode & 8) rglTexCoord2f(1, 1);
		rglVertex3f(0, 0.5f, 0);
		rglEnd();
	}
	if (mode & 2) {
		rglBegin(GL_QUADS);
		rglVertex3f(0, -0.5f, 0);
		rglVertex3f(0.5f, -0.5f, 0);
		rglVertex3f(0.5f, 0.5f, 0);
		rglVertex3f(0, 0.5f, 0);
		rglEnd();
	}
	rglEndList();

	rglViewport(0, 0, W, H);
	rglDisable(GL_DEPTH_TEST);
	rglDisable(GL_TEXTURE_2D);
	rglMatrixMode(GL_PROJECTION);
	rglLoadIdentity();
	rglMatrixMode(GL_MODELVIEW);
	rglLoadIdentity();
	rglClearColor(0, 0, 1, 1);
	rglClear(GL_COLOR_BUFFER_BIT);
	rglCallList(list);
	rglFinish();
}

/* The same pixel as pixel() but asked of GL, which knows the surface. */
static uint32_t gl_pixel(int x, int y)
{
	uint8_t p[4] = { 0, 0, 0, 0 };

	rglReadPixels(x, H - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
	return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

/*
 * The buffer holds one 32-bit word per pixel, 0xAARRGGBB in the host's
 * byte order (what OSMESA_BGRA with GL_UNSIGNED_BYTE means), bottom row
 * first.
 */
static uint32_t pixel(const uint8_t *buf, int x, int y)
{
	uint32_t v;

	memcpy(&v, buf + ((H - 1 - y) * W + x) * 4, 4);
	return v & 0xffffff;
}

int main(int argc, char **argv)
{
	const char *out = NULL;
	int frames = 1, to_screen = 0, spitch = 0, opt, f, x, y, list_mode = 0;
	float start_angle = 0.0f;
	volatile uint32_t *scan = NULL;
	uint8_t *buf, tex_data[8 * 8 * 4];
	OSMesaContext ctx;
	GLuint tex;

	while ((opt = getopt(argc, argv, "n:o:sa:b:DL:")) != -1) {
		switch (opt) {
		case 'L': list_mode = atoi(optarg); break;
		case 'n': frames = atoi(optarg); break;
		case 'o': out = optarg; break;
		case 'D': no_depth = 1; break;
		case 'a': start_angle = (float)atof(optarg); break;
		case 'b': background = (unsigned)strtoul(optarg, NULL, 16); break;
		case 's': to_screen = 1; break;
		default: return 2;
		}
	}

	ctx = OSMesaCreateContextExt(OSMESA_BGRA, 24, 8, 0, NULL);
	buf = calloc(W * H, 4);
	if (!ctx || !buf || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
		fprintf(stderr, "no context\n");
		return 1;
	}
	if (load_gl())
		return 1;
	printf("GL_VENDOR:   %s\n", rglGetString(GL_VENDOR));
	printf("GL_RENDERER: %s\n", rglGetString(GL_RENDERER));
	printf("GL_VERSION:  %s\n", rglGetString(GL_VERSION));

	if (list_mode) {
		draw_list(list_mode);
		printf("list mode %d: left half %06x, right half %06x (want ff0000 where drawn), outside %06x (want 0000ff)\n",
		       list_mode, (unsigned)gl_pixel(180, 256), (unsigned)gl_pixel(330, 256),
		       (unsigned)gl_pixel(40, 40));
		OSMesaDestroyContext(ctx);
		return 0;
	}

	if (to_screen) {
		uint32_t w, h, pitch;

		if (!rdn_target_screen(&scan, &w, &h, &pitch) || w < W + 400 ||
		    h < H + 128) {
			fprintf(stderr, "cannot use the screen\n");
			return 1;
		}
		spitch = (int)pitch;
	}

	/* 8x8 checkerboard, magenta and white. */
	for (y = 0; y < 8; y++)
		for (x = 0; x < 8; x++) {
			uint8_t *p = tex_data + (y * 8 + x) * 4;

			p[0] = 255;
			p[1] = ((x ^ y) & 1) ? 255 : 0;
			p[2] = 255;
			p[3] = 255;
		}
	rglGenTextures(1, &tex);
	rglBindTexture(GL_TEXTURE_2D, tex);
	rglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	rglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	rglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE,
		     tex_data);

	for (f = 0; f < frames; f++) {
		draw(start_angle + f * 2.0f, tex);
		if (f == 0) {
			static const int probe[][2] = {
				{ 10, 10 }, { 154, 200 }, { 250, 120 }, { 180, 220 },
				{ 371, 371 }, { 40, 471 },
			};
			unsigned i;

			printf("GL error 0x%x\n", (unsigned)rglGetError());
			printf("glReadPixels:");
			for (i = 0; i < sizeof(probe) / sizeof(probe[0]); i++)
				printf(" (%d,%d)=%06x", probe[i][0], probe[i][1],
				       (unsigned)gl_pixel(probe[i][0], probe[i][1]));
			printf("\n");
			printf("pixel (40,471)  = %06x  (scissored clear, 00ff00)\n",
			       (unsigned)pixel(buf, 40, 471));
			printf("pixel (10,10)   = %06x  (background, %06x)\n",
			       (unsigned)pixel(buf, 10, 10), background);
			printf("pixel (154,200) = %06x  (front triangle alone, mixed colours)\n",
			       (unsigned)pixel(buf, 154, 200));
			printf("pixel (250,120) = %06x  (yellow triangle alone, ffff00)\n",
			       (unsigned)pixel(buf, 250, 120));
			printf("pixel (180,220) = %06x  (overlap: front triangle, not yellow)\n",
			       (unsigned)pixel(buf, 180, 220));
			printf("pixel (371,371) = %06x  (checkerboard: ff00ff or ffffff)\n",
			       (unsigned)pixel(buf, 371, 371));
		}
		if (scan)
			for (y = 0; y < H; y++)
				for (x = 0; x < W; x++)
					scan[(y + 128) * spitch + x + 400] = pixel(buf, x, y);
	}

	if (out) {
		FILE *fp = fopen(out, "wb");

		if (!fp)
			return 1;
		fprintf(fp, "P6\n%d %d\n255\n", W, H);
		for (y = 0; y < H; y++)
			for (x = 0; x < W; x++) {
				uint32_t v = pixel(buf, x, y);

				fputc((v >> 16) & 0xff, fp);
				fputc((v >> 8) & 0xff, fp);
				fputc(v & 0xff, fp);
			}
		fclose(fp);
	}
	OSMesaDestroyContext(ctx);
	return 0;
}
