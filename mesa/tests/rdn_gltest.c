/*
 * First OpenGL through Mesa on the osx-gpu winsys (Linux host).
 *
 *   rdn_gltest [-n frames] [-a angle] [-b rrggbb] [-o file.ppm]
 *              [-s width height pitch]
 *
 * Renders with fixed-function OpenGL into an off-screen buffer: a clear to
 * dark grey, a depth-tested pair of triangles (the red-green-blue one in
 * front of the yellow one, whatever the drawing order) and a checkerboard
 * textured square, turned by -a degrees plus two per frame. -b sets the
 * background, so that a run cannot be mistaken for an earlier one whose
 * picture is still in video memory. Prints the GL strings and a few
 * pixels of the first frame, saves the last frame with -o, and with -s
 * copies every frame to the scanout surface at aperture offset 0
 * (width, height and pitch as `rdn_tool accel` prints them).
 *
 * The library exports only the OSMesa calls; GL functions are looked up
 * with OSMesaGetProcAddress and called here as rglClear and so on.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <GL/gl.h>
#include <GL/osmesa.h>

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

static void draw(float angle, GLuint tex)
{
	rglViewport(0, 0, W, H);
	rglClearColor(((background >> 16) & 0xff) / 255.0f,
		      ((background >> 8) & 0xff) / 255.0f,
		      (background & 0xff) / 255.0f, 1.0f);
	rglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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
	rglFinish();
}

/* The buffer is BGRA bytes, bottom row first. */
static uint32_t pixel(const uint8_t *buf, int x, int y)
{
	const uint8_t *p = buf + ((H - 1 - y) * W + x) * 4;

	return ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

int main(int argc, char **argv)
{
	const char *out = NULL, *addr = getenv("RDN_PCI_ADDR");
	int frames = 1, sw = 0, sh = 0, spitch = 0, opt, f, x, y;
	float start_angle = 0.0f;
	volatile uint32_t *scan = NULL;
	uint8_t *buf, tex_data[8 * 8 * 4];
	OSMesaContext ctx;
	GLuint tex;

	while ((opt = getopt(argc, argv, "n:o:sa:b:")) != -1) {
		switch (opt) {
		case 'n': frames = atoi(optarg); break;
		case 'o': out = optarg; break;
		case 'a': start_angle = (float)atof(optarg); break;
		case 'b': background = (unsigned)strtoul(optarg, NULL, 16); break;
		case 's':
			if (optind + 2 >= argc + 0 && optind + 3 > argc)
				return 2;
			sw = atoi(argv[optind]);
			sh = atoi(argv[optind + 1]);
			spitch = atoi(argv[optind + 2]);
			optind += 3;
			break;
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

	if (sw) {
		char path[256];
		struct stat st;
		int fd;

		snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/resource0",
			 addr ? addr : "0000:10:00.0");
		fd = open(path, O_RDWR);
		if (fd < 0 || fstat(fd, &st) || sw < W + 400 || sh < H + 128 ||
		    (uint64_t)spitch * sh * 4 > (uint64_t)st.st_size) {
			fprintf(stderr, "cannot use the scanout surface\n");
			return 1;
		}
		scan = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (scan == MAP_FAILED)
			return 1;
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
			printf("GL error 0x%x\n", (unsigned)rglGetError());
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
