/*
 * First OpenGL through Mesa on the osx-gpu winsys (Linux host).
 *
 *   rdn_gltest [-n frames] [-a angle] [-b rrggbb] [-D] [-o file.ppm]
 *              [-s] [-L mode] [-P]
 *
 * Renders with fixed-function OpenGL into an off-screen buffer: a clear to
 * dark grey, a depth-tested pair of triangles (the red-green-blue one in
 * front of the yellow one, whatever the drawing order) and a checkerboard
 * textured square, turned by -a degrees plus two per frame. -b sets the
 * background, so that a run cannot be mistaken for an earlier one whose
 * picture is still in video memory. Prints the GL strings and a few
 * pixels of the first frame, saves the last frame with -o, and with -s
 * copies every frame to the screen, 400 pixels from its left edge and 128
 * from its top. -P instead checks a store as a drawable and as a texture,
 * and a share list (store_test()); with RDN_SOFT=1 on Linux that runs
 * without the card, on Mesa's software rasteriser (rdn_soft.c).
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
	X(void, GetTexImage, (GLenum, GLint, GLenum, GLenum, GLvoid *)) \
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

/*
 * -P: a store as a drawable and as a texture, and a share list. Context a
 * draws four colours into a store (video memory of ours, bottom row
 * first); b, which shares with a, has the store as a texture's image and
 * draws it on the left half of its own drawable, and on the right half a
 * texture a made. Reads b's picture back and says PASS or FAIL per check.
 */
#define STORE_SIDE 256

static int check(const char *what, int x, int y, uint32_t want)
{
	uint32_t got = gl_pixel(x, y);

	printf("%s: %s (%d,%d is %06x, want %06x)\n", got == want ? "PASS" : "FAIL",
	       what, x, y, (unsigned)got, (unsigned)want);
	return got != want;
}

static void quad(float x0, float x1)
{
	rglBegin(GL_QUADS);
	rglTexCoord2f(0, 0); rglVertex3f(x0, -1, 0);
	rglTexCoord2f(1, 0); rglVertex3f(x1, -1, 0);
	rglTexCoord2f(1, 1); rglVertex3f(x1, 1, 0);
	rglTexCoord2f(0, 1); rglVertex3f(x0, 1, 0);
	rglEnd();
}

static int store_test(OSMesaContext a, uint8_t *buf, const char *out)
{
	static const float quarter[4][3] = {
		{ 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 1, 1 }
	};
	uint8_t solid[8 * 8 * 4];
	OSMesaContext b;
	GLuint shared, image;
	uint32_t offset;
	int i, bad = 0;

	b = OSMesaCreateContextExt(OSMESA_BGRA, 24, 8, 0, a);
	if (!b || !rdn_target_vram_alloc(STORE_SIDE * STORE_SIDE * 4, &offset)) {
		fprintf(stderr, "no second context or no memory for the store\n");
		return 1;
	}

	/* a: a texture of its own, magenta, for b to use. */
	for (i = 0; i < 8 * 8; i++) {
		solid[i * 4] = 255; solid[i * 4 + 1] = 0;
		solid[i * 4 + 2] = 255; solid[i * 4 + 3] = 255;
	}
	rglGenTextures(1, &shared);
	rglBindTexture(GL_TEXTURE_2D, shared);
	rglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	rglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	rglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA,
		      GL_UNSIGNED_BYTE, solid);

	/* a: the store, a colour in each quarter, red at the bottom left. */
	if (!OSMesaMakeCurrentStore(a, RDN_TARGET_VRAM_HANDLE, STORE_SIDE * 4,
				    offset, STORE_SIDE, STORE_SIDE,
				    OSMESA_STORE_BOTTOM_UP)) {
		printf("FAIL: OSMesaMakeCurrentStore\n");
		return 1;
	}
	rglViewport(0, 0, STORE_SIDE, STORE_SIDE);
	rglEnable(GL_SCISSOR_TEST);
	for (i = 0; i < 4; i++) {
		rglScissor((i & 1) * STORE_SIDE / 2, (i >> 1) * STORE_SIDE / 2,
			   STORE_SIDE / 2, STORE_SIDE / 2);
		rglClearColor(quarter[i][0], quarter[i][1], quarter[i][2], 1);
		rglClear(GL_COLOR_BUFFER_BIT);
	}
	rglDisable(GL_SCISSOR_TEST);
	rglFinish();

	/* b: its own drawable, the store on the left, a's texture on the right. */
	if (!OSMesaMakeCurrent(b, buf, GL_UNSIGNED_BYTE, W, H)) {
		printf("FAIL: the second context has no drawable\n");
		return 1;
	}
	rglViewport(0, 0, W, H);
	rglClearColor(0.2f, 0.2f, 0.2f, 1);
	rglClear(GL_COLOR_BUFFER_BIT);
	rglGenTextures(1, &image);
	rglBindTexture(GL_TEXTURE_2D, image);
	rglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	rglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	if (!OSMesaTexStoreImage(b, GL_TEXTURE_2D, RDN_TARGET_VRAM_HANDLE,
				 STORE_SIDE * 4, offset, STORE_SIDE, STORE_SIDE, 0)) {
		printf("FAIL: OSMesaTexStoreImage\n");
		bad++;
	}
	rglEnable(GL_TEXTURE_2D);
	rglColor3f(1, 1, 1);
	quad(-1, 0);
	rglBindTexture(GL_TEXTURE_2D, shared);
	quad(0, 1);
	rglFinish();

	bad += check("store, bottom left", 64, 384, 0xff0000);
	bad += check("store, bottom right", 192, 384, 0x00ff00);
	bad += check("store, top left", 64, 128, 0x0000ff);
	bad += check("store, top right", 192, 128, 0xffffff);
	bad += check("the other context's texture", 384, 256, 0xff00ff);

	if (out) {
		FILE *f = fopen(out, "wb");
		int x, y;

		if (f) {
			fprintf(f, "P6\n%d %d\n255\n", W, H);
			for (y = 0; y < H; y++)
				for (x = 0; x < W; x++) {
					uint32_t v = pixel(buf, x, y);

					fputc((int)(v >> 16) & 255, f);
					fputc((int)(v >> 8) & 255, f);
					fputc((int)v & 255, f);
				}
			fclose(f);
		}
	}

	/* The store goes back only when nothing uses it any more. */
	rglBindTexture(GL_TEXTURE_2D, image);
	OSMesaTexStoreImage(b, GL_TEXTURE_2D, 0, 0, 0, 0, 0, 0);
	OSMesaDestroyContext(b);
	OSMesaDestroyContext(a);
	rdn_target_vram_free(offset);
	printf("%s\n", bad ? "FAILED" : "all passed");
	return bad != 0;
}

/*
 * -K: upload one texel of every packed pixel type as RGBA8 and read it
 * back as bytes (piglit's teximage-colors, 2026-10-08, failed for these on
 * the G5). The data are written as host-order words with the channels
 * R=0x11/0xff.. distinct, so a swapped or reversed type shows at once.
 */
static int packed_test(void)
{
	/* Red 10 (of 15), green 5, blue 15, alpha 0 for 4_4_4_4 and so on. */
	static const struct {
		const char *name;
		GLenum format, type;
		unsigned bits;
		uint32_t word;
		uint8_t want[4];	/* RGBA, 8 bits, +-4 */
	} t[] = {
		{ "RGBA 4_4_4_4", GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, 16, 0xa5f0, { 170, 85, 255, 0 } },
		{ "RGBA 4_4_4_4_REV", GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4_REV, 16, 0x0f5a, { 170, 85, 255, 0 } },
		{ "BGRA 4_4_4_4", GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4, 16, 0xf5a0, { 170, 85, 255, 0 } },
		{ "RGBA 5_5_5_1", GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 16, 0xf801, { 255, 0, 0, 255 } },
		{ "RGBA 1_5_5_1_REV", GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, 16, 0x801f, { 255, 0, 0, 255 } },
		{ "BGRA 5_5_5_1", GL_BGRA, GL_UNSIGNED_SHORT_5_5_5_1, 16, 0xf801, { 0, 0, 255, 255 } },
		{ "RGB 5_6_5", GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 16, 0xf800, { 255, 0, 0, 255 } },
		{ "RGB 5_6_5_REV", GL_RGB, GL_UNSIGNED_SHORT_5_6_5_REV, 16, 0x001f, { 255, 0, 0, 255 } },
		{ "RGBA 10_10_10_2", GL_RGBA, GL_UNSIGNED_INT_10_10_10_2, 32, 0xffc00003, { 255, 0, 0, 255 } },
		{ "RGBA 2_10_10_10_REV", GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, 32, 0xc00003ff, { 255, 0, 0, 255 } },
		{ "RGBA 8_8_8_8", GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, 32, 0x11223344, { 0x11, 0x22, 0x33, 0x44 } },
		{ "RGBA 8_8_8_8_REV", GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, 32, 0x44332211, { 0x11, 0x22, 0x33, 0x44 } },
		{ "RGB 3_3_2", GL_RGB, GL_UNSIGNED_BYTE_3_3_2, 8, 0xe0, { 255, 0, 0, 255 } },
		{ "RGB 2_3_3_REV", GL_RGB, GL_UNSIGNED_BYTE_2_3_3_REV, 8, 0x07, { 255, 0, 0, 255 } },
	};
	int i, k, bad = 0;
	GLuint tex;

	rglGenTextures(1, &tex);
	rglBindTexture(GL_TEXTURE_2D, tex);
	rglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	rglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	for (i = 0; i < (int)(sizeof t / sizeof t[0]); i++) {
		uint8_t src[4] = { 0 }, got[4] = { 0 };
		uint16_t w16 = (uint16_t)t[i].word;
		uint32_t w32 = t[i].word;
		uint8_t w8 = (uint8_t)t[i].word;
		int ok = 1;

		if (t[i].bits == 16) memcpy(src, &w16, 2);
		else if (t[i].bits == 32) memcpy(src, &w32, 4);
		else memcpy(src, &w8, 1);
		rglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, t[i].format, t[i].type, src);
		rglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, got);
		for (k = 0; k < 4; k++)
			if (abs((int)got[k] - (int)t[i].want[k]) > 8)
				ok = 0;
		printf("%-22s %s  want %3d %3d %3d %3d  got %3d %3d %3d %3d\n", t[i].name,
		       ok ? "ok  " : "FAIL", t[i].want[0], t[i].want[1], t[i].want[2],
		       t[i].want[3], got[0], got[1], got[2], got[3]);
		bad += !ok;
	}
	printf("%s\n", bad ? "FAILED" : "all passed");
	return bad != 0;
}

int main(int argc, char **argv)
{
	const char *out = NULL;
	int frames = 1, to_screen = 0, spitch = 0, opt, f, x, y, list_mode = 0;
	int store_mode = 0, packed_mode = 0;
	float start_angle = 0.0f;
	volatile uint32_t *scan = NULL;
	uint8_t *buf, tex_data[8 * 8 * 4];
	OSMesaContext ctx;
	GLuint tex;

	while ((opt = getopt(argc, argv, "n:o:sa:b:DL:PK")) != -1) {
		switch (opt) {
		case 'L': list_mode = atoi(optarg); break;
		case 'P': store_mode = 1; break;
		case 'K': packed_mode = 1; break;
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

	if (packed_mode)
		return packed_test();
	if (store_mode)
		return store_test(ctx, buf, out);

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
