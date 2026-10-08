/*
 * appletest: the small Apple extensions as the bundle gives them
 * (gld/gen_dispatch.py, APPLE_HELP).
 *
 *   1. which of the names are in the list
 *   2. GL_APPLE_client_storage, GL_APPLE_transform_hint and
 *      GL_COLOR_FLOAT_APPLE: set, asked for again, no GL error
 *   3. GL_APPLE_texture_range with client storage, the way Core Image
 *      gives a picture: a rectangle texture drawn, a pixel read back, and
 *      the range and the hint asked for again
 *   4. a half float texture by GL_APPLE_float_pixels' names
 *   5. GL_APPLE_fence: many large quads, a fence, how long finishing it
 *      takes and how long glFinish takes after it (next to nothing if the
 *      fence was a real wait)
 *   6. GL_APPLE_flush_render
 *   7. GL_EXT_gpu_program_parameters' functions, looked up by name in the
 *      framework's bundle as World of Warcraft does, set the colour a
 *      fragment program draws
 *
 *   appletest [renderer id, default 0x21a00]
 *
 * RDN_NO_APPLE=1 or RDN_NO_PROGPARAMS=1 in the environment show what a
 * program gets without the bundle's own. A copy of this program named
 * WindowServer shows the list the window server gets (no
 * GL_ARB_fragment_program, and none of the bundle's own; the rest of the
 * tests then fail, as they should).
 *
 * Build on the host:
 *   scripts/darwin.sh powerpc-apple-darwin8-gcc -O2 -o build/appletest \
 *       tools/guest/appletest.c -framework OpenGL -framework CoreFoundation
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <CoreFoundation/CoreFoundation.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>

#ifndef GL_COLOR_FLOAT_APPLE
#define GL_COLOR_FLOAT_APPLE 0x8A0F
#endif
#ifndef GL_RGBA_FLOAT16_APPLE
#define GL_RGBA_FLOAT16_APPLE 0x881A
#endif
#ifndef GL_HALF_APPLE
#define GL_HALF_APPLE 0x140B
#endif

#define SIDE 1024

static int failed;

static double now(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1e3 + tv.tv_usec / 1e3;
}

static void check(const char *what, int ok)
{
	GLenum err = glGetError();

	printf("%-62s %s", what, ok && !err ? "PASS" : "FAIL");
	if (err)
		printf("   GL error 0x%x", (unsigned)err);
	printf("\n");
	failed += !ok || err;
}

static void quad(void)
{
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0);
	glVertex2f(-1, -1);
	glTexCoord2f(64, 0);
	glVertex2f(1, -1);
	glTexCoord2f(64, 64);
	glVertex2f(1, 1);
	glTexCoord2f(0, 64);
	glVertex2f(-1, 1);
	glEnd();
}

static int pixel_is(unsigned r, unsigned g, unsigned b)
{
	unsigned char px[4] = { 9, 9, 9, 9 };

	glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	if (px[0] != r || px[1] != g || px[2] != b)
		printf("   read %u %u %u, expected %u %u %u\n", px[0], px[1], px[2], r, g, b);
	return px[0] == r && px[1] == g && px[2] == b;
}

typedef void (*params4fv)(GLenum target, GLuint index, GLsizei count, const GLfloat *params);

static params4fv by_name(const char *name)
{
	CFBundleRef gl = CFBundleGetBundleWithIdentifier(CFSTR("com.apple.opengl"));
	CFStringRef s = CFStringCreateWithCString(NULL, name, kCFStringEncodingASCII);
	params4fv f = gl ? (params4fv)CFBundleGetFunctionPointerForName(gl, s) : NULL;

	CFRelease(s);
	printf("   %s by name: %s\n", name, f ? "found" : "not found");
	return f;
}

int main(int argc, char **argv)
{
	static const char *const names[] = {
		"GL_APPLE_client_storage", "GL_APPLE_float_pixels", "GL_APPLE_texture_range",
		"GL_APPLE_transform_hint", "GL_APPLE_fence", "GL_APPLE_vertex_array_object",
		"GL_APPLE_flush_render", "GL_APPLE_flush_buffer_range",
		"GL_EXT_gpu_program_parameters", "GL_ATI_array_rev_comps_in_4_bytes",
		"GL_ARB_fragment_program",
	};
	static const char env_text[] = "!!ARBfp1.0\nMOV result.color, program.env[1];\nEND\n";
	static const char local_text[] = "!!ARBfp1.0\nMOV result.color, program.local[1];\nEND\n";
	static const GLfloat green[8] = { 1, 0, 0, 1, 0, 1, 0, 1 };
	static const GLfloat blue[8] = { 1, 0, 0, 1, 0, 0, 1, 1 };
	long renderer = argc > 1 ? strtol(argv[1], NULL, 0) : 0x21a00;
	CGLPixelFormatAttribute attrs[8];
	CGLPixelFormatObj pix = NULL;
	CGLContextObj ctx = NULL;
	long npix = 0;
	int n = 0;
	unsigned i;
	static unsigned char picture[64 * 64 * 4];
	static unsigned bgra[64 * 64];
	static unsigned short half[4 * 4 * 4];
	const char *list;
	GLuint tex = 0, fbo = 0, src = 0, fence = 0, prog[2] = { 0, 0 };
	GLint value = -1;
	GLboolean flag = 9;
	GLvoid *pointer = NULL;
	params4fv env, local;
	double t0, t1, t2;

	attrs[n++] = kCGLPFAOffScreen;
	attrs[n++] = kCGLPFAColorSize;
	attrs[n++] = 32;
	attrs[n++] = kCGLPFARendererID;
	attrs[n++] = renderer;
	attrs[n] = 0;
	if (CGLChoosePixelFormat(attrs, &pix, &npix) || !pix ||
	    CGLCreateContext(pix, NULL, &ctx) ||
	    CGLSetOffScreen(ctx, 64, 64, 64 * 4, picture)) {
		printf("no off-screen context on renderer 0x%lx\n", renderer);
		return 1;
	}
	CGLSetCurrentContext(ctx);
	printf("renderer: %s\n", glGetString(GL_RENDERER));

	list = (const char *)glGetString(GL_EXTENSIONS);
	printf("1. names in the list:\n");
	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		const char *at = strstr(list, names[i]);
		size_t len = strlen(names[i]);

		printf("   %-36s %s\n", names[i],
		       at && (at[len] == ' ' || !at[len]) ? "named" : "not named");
	}

	/* Everything is drawn into a texture. */
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SIDE, SIDE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);
	glGenFramebuffersEXT(1, &fbo);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
				  GL_TEXTURE_2D, tex, 0);
	glViewport(0, 0, SIDE, SIDE);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	check("   (a framebuffer object to draw into)",
	      glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT);

	glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, 1);
	glGetIntegerv(GL_UNPACK_CLIENT_STORAGE_APPLE, &value);
	glGetBooleanv(GL_UNPACK_CLIENT_STORAGE_APPLE, &flag);
	check("2. client storage set, and asked for again", value == 1 && flag == 1);
	glHint(GL_TRANSFORM_HINT_APPLE, GL_FASTEST);
	value = -1;
	glGetIntegerv(GL_TRANSFORM_HINT_APPLE, &value);
	check("   transform hint set, and asked for again", value == GL_FASTEST);
	flag = 9;
	glGetBooleanv(GL_COLOR_FLOAT_APPLE, &flag);
	check("   GL_COLOR_FLOAT_APPLE is false", flag == 0);

	for (i = 0; i < 64 * 64; i++)
		bgra[i] = 0xff204080;	/* A R G B: 0x20, 0x40, 0x80 */
	glGenTextures(1, &src);
	glBindTexture(GL_TEXTURE_RECTANGLE_EXT, src);
	glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE_EXT, sizeof(bgra), bgra);
	glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_STORAGE_HINT_APPLE,
			GL_STORAGE_SHARED_APPLE);
	glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, GL_RGBA8, 64, 64, 0, GL_BGRA,
		     GL_UNSIGNED_INT_8_8_8_8_REV, bgra);
	glEnable(GL_TEXTURE_RECTANGLE_EXT);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	quad();
	glFinishObjectAPPLE(GL_TEXTURE, src);
	check("3. a texture with client storage and a range, drawn", pixel_is(0x20, 0x40, 0x80));
	check("   glTestObjectAPPLE of it", glTestObjectAPPLE(GL_TEXTURE, src) == 1);
	glGetTexParameterPointervAPPLE(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_RANGE_POINTER_APPLE,
				       &pointer);
	value = -1;
	glGetTexParameteriv(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_RANGE_LENGTH_APPLE, &value);
	check("   the range asked for again", pointer == bgra && value == (GLint)sizeof(bgra));
	value = -1;
	glGetTexParameteriv(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_STORAGE_HINT_APPLE, &value);
	check("   the storage hint asked for again", value == GL_STORAGE_SHARED_APPLE);
	glDisable(GL_TEXTURE_RECTANGLE_EXT);
	glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, 0);

	for (i = 0; i < 4 * 4 * 4; i++)
		half[i] = 0x3c00;	/* 1.0 */
	glBindTexture(GL_TEXTURE_RECTANGLE_EXT, 0);
	glTexImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, GL_RGBA_FLOAT16_APPLE, 4, 4, 0, GL_RGBA,
		     GL_HALF_APPLE, half);
	check("4. a half float texture", 1);

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	glColor4f(0.001f, 0.001f, 0.001f, 1);
	t0 = now();
	for (i = 0; i < 4000; i++)
		quad();
	glGenFencesAPPLE(1, &fence);
	glSetFenceAPPLE(fence);
	value = glTestFenceAPPLE(fence);
	t0 = now() - t0;
	t1 = now();
	glFinishFenceAPPLE(fence);
	t1 = now() - t1;
	t2 = now();
	glFinish();
	t2 = now() - t2;
	printf("   4000 quads of %dx%d: %.1f ms to ask for; fence tested at once: %d;\n"
	       "   glFinishFenceAPPLE %.1f ms, glFinish after it %.1f ms\n",
	       SIDE, SIDE, t0, (int)value, t1, t2);
	check("5. a fence finished tests true", glTestFenceAPPLE(fence) == 1);
	check("   and is a real wait (glFinish after it finds little to wait for)",
	      t2 < 5 || t2 * 4 < t1);
	glSetFenceAPPLE(fence);
	glFinishObjectAPPLE(GL_FENCE_APPLE, fence);
	check("   glFinishObjectAPPLE and glTestObjectAPPLE of a fence",
	      glTestObjectAPPLE(GL_FENCE_APPLE, fence) == 1);
	glDeleteFencesAPPLE(1, &fence);
	glDisable(GL_BLEND);

	glFlushRenderAPPLE();
	glFinishRenderAPPLE();
	check("6. glFlushRenderAPPLE and glFinishRenderAPPLE", 1);

	printf("7. GL_EXT_gpu_program_parameters' functions:\n");
	env = by_name("glProgramEnvParameters4fvEXT");
	local = by_name("glProgramLocalParameters4fvEXT");
	if (env && local) {
		glGenProgramsARB(2, prog);
		glEnable(GL_FRAGMENT_PROGRAM_ARB);
		glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, prog[0]);
		glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB,
				   sizeof(env_text) - 1, env_text);
		env(GL_FRAGMENT_PROGRAM_ARB, 0, 2, green);
		quad();
		check("   two environment parameters set, the second drawn (green)",
		      pixel_is(0, 255, 0));
		glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, prog[1]);
		glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB,
				   sizeof(local_text) - 1, local_text);
		local(GL_FRAGMENT_PROGRAM_ARB, 0, 2, blue);
		quad();
		check("   two local parameters set, the second drawn (blue)",
		      pixel_is(0, 0, 255));
		glDisable(GL_FRAGMENT_PROGRAM_ARB);
	} else {
		printf("   not there: nothing to try\n");
		failed++;
	}

	printf("%s\n", failed ? "FAILED" : "all passed");
	CGLSetCurrentContext(NULL);
	CGLDestroyContext(ctx);
	CGLDestroyPixelFormat(pix);
	return failed != 0;
}
