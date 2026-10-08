/*
 * ciprobe: what Core Image and CGL pbuffers ask of an OpenGL driver.
 *
 *   ciprobe pbuffer [renderer-id]
 *       No Core Image. One context draws four coloured quarters into a
 *       pbuffer; a second context that shares with it, on an off-screen
 *       drawable, takes the pbuffer as a texture (CGLTexImagePBuffer) and
 *       draws it. Once with a 2D texture, once with a rectangle texture.
 *       Saves ciprobe-pbuffer-2d.ppm and ciprobe-pbuffer-rect.ppm and
 *       prints PASS or FAIL for each.
 *   ciprobe gl [renderer-id] [filter]
 *       A Core Image context on a CGL context with a pbuffer drawable
 *       draws a generated picture through a filter (CIGaussianBlur unless
 *       one is named). Saves ciprobe-gl.ppm; prints the GL strings, the
 *       time of the first render and of the rest, and a checksum.
 *   ciprobe soft [filter]
 *       The same picture through Core Image's software renderer into a
 *       bitmap. Saves ciprobe-soft.ppm.
 *   ciprobe list
 *       The names of Core Image's built-in filters, one a line.
 *   ciprobe compare a.ppm b.ppm
 *       Greatest and mean difference per channel, and how many pixels
 *       differ by more than 8 in some channel.
 *
 * Our renderer is 0x00021a00, Apple's software renderer 0x00020400.
 * Environment: CIPROBE_N renders (default 10), CIPROBE_OUT the file to
 * save, CIPROBE_MASK=0 leaves the display mask out of the pixel format,
 * CIPROBE_PBUFFER=1 makes `gl` ask for a pixel format that can do
 * pbuffers as well.
 *
 * Every CGL call is announced before it is made ("step: ..."), unbuffered:
 * with RDN_GLD_LOG=/dev/stdout the driver bundle's log lines come out
 * between the steps that caused them.
 *
 * Build on Tiger:
 *   gcc -isysroot /Developer/SDKs/MacOSX10.4u.sdk -Wall -o ciprobe \
 *       ciprobe.m -framework Cocoa -framework QuartzCore -framework OpenGL
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define WIDTH	512
#define HEIGHT	384

static double now(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
}

static const char *cgl_name(CGLError err)
{
	switch (err) {
	case kCGLNoError:		return "kCGLNoError";
	case kCGLBadAttribute:		return "kCGLBadAttribute";
	case kCGLBadProperty:		return "kCGLBadProperty";
	case kCGLBadPixelFormat:	return "kCGLBadPixelFormat";
	case kCGLBadRendererInfo:	return "kCGLBadRendererInfo";
	case kCGLBadContext:		return "kCGLBadContext";
	case kCGLBadDrawable:		return "kCGLBadDrawable";
	case kCGLBadDisplay:		return "kCGLBadDisplay";
	case kCGLBadState:		return "kCGLBadState";
	case kCGLBadValue:		return "kCGLBadValue";
	case kCGLBadMatch:		return "kCGLBadMatch";
	case kCGLBadEnumeration:	return "kCGLBadEnumeration";
	case kCGLBadOffScreen:		return "kCGLBadOffScreen";
	case kCGLBadFullScreen:		return "kCGLBadFullScreen";
	case kCGLBadWindow:		return "kCGLBadWindow";
	case kCGLBadAddress:		return "kCGLBadAddress";
	case kCGLBadCodeModule:		return "kCGLBadCodeModule";
	case kCGLBadAlloc:		return "kCGLBadAlloc";
	case kCGLBadConnection:		return "kCGLBadConnection";
	}
	return "an error CGLTypes.h does not name";
}

static CGLError said(CGLError err)
{
	if (err)
		printf("  -> %s (%d): %s\n", cgl_name(err), (int)err, CGLErrorString(err));
	else
		printf("  -> ok\n");
	return err;
}

/* Announce a CGL call, make it, say how it went; the value is its error. */
#define CGL(call)	(printf("step: %s\n", #call), said(call))
#define STEP(text)	printf("step: %s\n", text)

static void gl_errors(const char *after)
{
	GLenum err;
	int n = 0;

	while ((err = glGetError()) != GL_NO_ERROR && n++ < 8)
		printf("  GL error 0x%04x after %s\n", (unsigned)err, after);
}

/* `rgb` holds the top row first. */
static int save_ppm(const char *path, const unsigned char *rgb, int w, int h)
{
	FILE *f = fopen(path, "wb");

	if (!f) {
		perror(path);
		return 0;
	}
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	fwrite(rgb, 3, (size_t)w * h, f);
	fclose(f);
	printf("saved %s (%dx%d)\n", path, w, h);
	return 1;
}

static unsigned char *load_ppm(const char *path, int *w, int *h)
{
	FILE *f = fopen(path, "rb");
	unsigned char *rgb;
	int max = 0;

	if (!f) {
		perror(path);
		return NULL;
	}
	if (fscanf(f, "P6 %d %d %d", w, h, &max) != 3 || max != 255 ||
	    *w < 1 || *h < 1 || *w > 16384 || *h > 16384) {
		fprintf(stderr, "%s: not a PPM (P6, 255) this program reads\n", path);
		fclose(f);
		return NULL;
	}
	fgetc(f);
	rgb = malloc((size_t)*w * *h * 3);
	if (!rgb || fread(rgb, 3, (size_t)*w * *h, f) != (size_t)*w * *h) {
		fprintf(stderr, "%s: too short\n", path);
		fclose(f);
		free(rgb);
		return NULL;
	}
	fclose(f);
	return rgb;
}

static unsigned checksum(const unsigned char *p, size_t n)
{
	unsigned sum = 2166136261u;

	while (n--)
		sum = (sum ^ *p++) * 16777619u;
	return sum;
}

static const char *out_name(const char *otherwise)
{
	return getenv("CIPROBE_OUT") ? getenv("CIPROBE_OUT") : otherwise;
}

static int renders(void)
{
	int n = getenv("CIPROBE_N") ? atoi(getenv("CIPROBE_N")) : 10;

	return n < 1 ? 1 : n;
}

/* Rows as glReadPixels gives them (RGBA, bottom first) to RGB, top first. */
static unsigned char *flipped_rgb(const unsigned char *rgba, int w, int h)
{
	unsigned char *rgb = malloc((size_t)w * h * 3);
	int x, y;

	for (y = 0; rgb && y < h; y++)
		for (x = 0; x < w; x++)
			memcpy(rgb + ((size_t)y * w + x) * 3,
			       rgba + ((size_t)(h - 1 - y) * w + x) * 4, 3);
	return rgb;
}

static CGLPixelFormatObj choose(const CGLPixelFormatAttribute *kinds, long renderer)
{
	CGLPixelFormatAttribute attrs[16];
	CGLPixelFormatObj pix = NULL;
	long npix = 0, value = 0;
	int n = 0;

	while (*kinds)
		attrs[n++] = *kinds++;
	attrs[n++] = kCGLPFAColorSize;
	attrs[n++] = 32;
	if (renderer) {
		attrs[n++] = kCGLPFARendererID;
		attrs[n++] = renderer;
	}
	/*
	 * Core Image counts the video memory of the renderers of the context's
	 * display mask; an off-screen format without one has none, and every
	 * region is then too big to render ("ROI is not tilable"). Programs
	 * with windows always have a mask. CIPROBE_MASK=0: none.
	 */
	if (!getenv("CIPROBE_MASK") || atoi(getenv("CIPROBE_MASK"))) {
		attrs[n++] = kCGLPFADisplayMask;
		attrs[n++] = CGDisplayIDToOpenGLDisplayMask(CGMainDisplayID());
	}
	attrs[n] = 0;
	if (CGL(CGLChoosePixelFormat(attrs, &pix, &npix)) || !pix) {
		printf("  no pixel format\n");
		return NULL;
	}
	CGLDescribePixelFormat(pix, 0, kCGLPFARendererID, &value);
	printf("  %ld formats, the first on renderer 0x%08lx", npix, value);
	CGLDescribePixelFormat(pix, 0, kCGLPFAAccelerated, &value);
	printf(", accelerated %ld", value);
	CGLDescribePixelFormat(pix, 0, kCGLPFAOffScreen, &value);
	printf(", off-screen %ld", value);
	CGLDescribePixelFormat(pix, 0, kCGLPFAPBuffer, &value);
	printf(", pbuffer %ld", value);
	CGLDescribePixelFormat(pix, 0, kCGLPFADisplayMask, &value);
	printf(", display mask 0x%lx\n", value);
	return pix;
}

static void gl_strings(void)
{
	const char *ext = (const char *)glGetString(GL_EXTENSIONS);

	printf("GL_VENDOR:   %s\n", glGetString(GL_VENDOR));
	printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
	printf("GL_VERSION:  %s\n", glGetString(GL_VERSION));
	printf("fragment programs %s, rectangle textures %s\n",
	       ext && strstr(ext, "GL_ARB_fragment_program") ? "yes" : "no",
	       ext && strstr(ext, "_texture_rectangle") ? "yes" : "no");
}

/*
 * The pbuffer test.
 */

/* The four quarters: bottom left, bottom right, top left, top right. */
static const unsigned char quarter[4][3] = {
	{ 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 255 }
};

/* Compare 8x8 points away from the quarters' edges; returns how many differ. */
static int check_quarters(const unsigned char *rgba, int w, int h, const char *what)
{
	int gx, gy, c, bad = 0;

	for (gy = 0; gy < 8; gy++)
		for (gx = 0; gx < 8; gx++) {
			int x = w * (2 * gx + 1) / 16, y = h * (2 * gy + 1) / 16;
			const unsigned char *p = rgba + ((size_t)y * w + x) * 4;
			const unsigned char *q = quarter[(gy >= 4) * 2 + (gx >= 4)];
			int off = 0;

			for (c = 0; c < 3; c++)
				if (abs((int)p[c] - (int)q[c]) > 8)
					off = 1;
			if (off && bad++ < 4)
				printf("  %s: at %d,%d read %u %u %u, expected %u %u %u\n",
				       what, x, y, p[0], p[1], p[2], q[0], q[1], q[2]);
		}
	printf("  %s: %d of 64 points wrong\n", what, bad);
	return bad;
}

static int pbuffer_test(long renderer, GLenum target, int w, int h, const char *file)
{
	static const CGLPixelFormatAttribute both[] = { kCGLPFAPBuffer, kCGLPFAOffScreen, 0 };
	static const CGLPixelFormatAttribute pb_only[] = { kCGLPFAPBuffer, 0 };
	static const CGLPixelFormatAttribute off_only[] = { kCGLPFAOffScreen, 0 };
	CGLPixelFormatObj pix_a, pix_b;
	CGLContextObj a = NULL, b = NULL;
	CGLPBufferObj pbuffer = NULL;
	unsigned char *screen = calloc((size_t)w * h, 4), *rgba = calloc((size_t)w * h, 4);
	unsigned char *rgb;
	float s = target == GL_TEXTURE_2D ? 1.0f : (float)w;
	float t = target == GL_TEXTURE_2D ? 1.0f : (float)h;
	long virtual_screen = 0;
	GLuint texture = 0;
	int i, ok = 0;

	printf("\npbuffer %dx%d as a %s texture\n", w, h,
	       target == GL_TEXTURE_2D ? "2D" : "rectangle");
	STEP("one pixel format for pbuffer and off-screen");
	pix_a = pix_b = choose(both, renderer);
	if (!pix_a) {
		STEP("a pixel format for the pbuffer, another for off-screen");
		pix_a = choose(pb_only, renderer);
		pix_b = choose(off_only, renderer);
	}
	if (!pix_a || !pix_b || !screen || !rgba)
		goto out;

	if (CGL(CGLCreateContext(pix_a, NULL, &a)) ||
	    CGL(CGLCreatePBuffer(w, h, target, GL_RGBA, 0, &pbuffer)) ||
	    CGL(CGLGetVirtualScreen(a, &virtual_screen)) ||
	    CGL(CGLSetPBuffer(a, pbuffer, 0, 0, virtual_screen)) ||
	    CGL(CGLSetCurrentContext(a)))
		goto out;
	gl_strings();

	STEP("draw the four quarters into the pbuffer");
	glViewport(0, 0, w, h);
	glEnable(GL_SCISSOR_TEST);
	for (i = 0; i < 4; i++) {
		glScissor((i & 1) * (w / 2), (i >> 1) * (h / 2), w / 2, h / 2);
		glClearColor(quarter[i][0] / 255.0f, quarter[i][1] / 255.0f,
			     quarter[i][2] / 255.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	glDisable(GL_SCISSOR_TEST);
	glFinish();
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	gl_errors("drawing into the pbuffer");
	check_quarters(rgba, w, h, "read back from the pbuffer");

	if (CGL(CGLCreateContext(pix_b, a, &b)) ||
	    CGL(CGLSetOffScreen(b, w, h, w * 4, screen)) ||
	    CGL(CGLSetCurrentContext(b)))
		goto out;
	glViewport(0, 0, w, h);
	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glGenTextures(1, &texture);
	glBindTexture(target, texture);
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	gl_errors("making the texture");
	if (CGL(CGLTexImagePBuffer(b, pbuffer, GL_FRONT)))
		goto out;
	gl_errors("CGLTexImagePBuffer");

	STEP("draw the texture over the whole off-screen drawable");
	glEnable(target);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(-1, -1);
	glTexCoord2f(s, 0); glVertex2f(1, -1);
	glTexCoord2f(s, t); glVertex2f(1, 1);
	glTexCoord2f(0, t); glVertex2f(-1, 1);
	glEnd();
	glFinish();
	memset(rgba, 0, (size_t)w * h * 4);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	gl_errors("drawing the texture");
	ok = check_quarters(rgba, w, h, "the pbuffer as a texture") == 0;
	rgb = flipped_rgb(rgba, w, h);
	if (rgb)
		save_ppm(file, rgb, w, h);
	free(rgb);
out:
	CGL(CGLSetCurrentContext(NULL));
	if (b)
		CGL(CGLDestroyContext(b));
	if (a)
		CGL(CGLDestroyContext(a));
	if (pbuffer)
		CGL(CGLDestroyPBuffer(pbuffer));
	if (pix_b && pix_b != pix_a)
		CGLDestroyPixelFormat(pix_b);
	if (pix_a)
		CGLDestroyPixelFormat(pix_a);
	free(screen);
	free(rgba);
	printf("%s: pbuffer as a %s texture\n", ok ? "PASS" : "FAIL",
	       target == GL_TEXTURE_2D ? "2D" : "rectangle");
	return ok;
}

/*
 * Core Image.
 */

/*
 * The input: red grows along a row, green from row to row, blue is a
 * checker, and the corner the data starts in is white, to show which way
 * up the picture ends. Bytes A, R, G, B.
 */
static CIImage *picture(CGColorSpaceRef space)
{
	NSMutableData *data = [NSMutableData dataWithLength:WIDTH * HEIGHT * 4];
	unsigned char *p = [data mutableBytes];
	int x, y;

	for (y = 0; y < HEIGHT; y++)
		for (x = 0; x < WIDTH; x++, p += 4) {
			p[0] = 255;
			p[1] = (unsigned char)(x * 255 / (WIDTH - 1));
			p[2] = (unsigned char)(y * 255 / (HEIGHT - 1));
			p[3] = ((x / 32 + y / 32) & 1) ? 255 : 64;
			if (x < 48 && y < 48)
				p[1] = p[2] = p[3] = 255;
		}
	return [CIImage imageWithBitmapData:data bytesPerRow:WIDTH * 4
				       size:CGSizeMake(WIDTH, HEIGHT)
				     format:kCIFormatARGB8 colorSpace:space];
}

/* The picture through the filter: its defaults, every image input ours. */
static CIImage *filtered(NSString *name, CIImage *input)
{
	CIFilter *filter = [CIFilter filterWithName:name];
	NSDictionary *attributes;
	NSArray *keys;
	CIImage *output;
	unsigned i;

	if (!filter) {
		fprintf(stderr, "Core Image has no filter named %s\n", [name UTF8String]);
		return nil;
	}
	[filter setDefaults];
	attributes = [filter attributes];
	keys = [filter inputKeys];
	for (i = 0; i < [keys count]; i++) {
		NSString *key = [keys objectAtIndex:i];
		NSString *class = [[attributes objectForKey:key] objectForKey:kCIAttributeClass];

		if ([class isEqualToString:@"CIImage"])
			[filter setValue:input forKey:key];
	}
	if ([name isEqualToString:@"CIGaussianBlur"])
		[filter setValue:[NSNumber numberWithFloat:6.0f] forKey:@"inputRadius"];
	output = [filter valueForKey:@"outputImage"];
	if (!output)
		fprintf(stderr, "%s gave no output image\n", [name UTF8String]);
	return output;
}

static void timing(const double *seconds, int n)
{
	double rest = 0;
	int i;

	for (i = 1; i < n; i++)
		rest += seconds[i];
	printf("first render %.1f ms", seconds[0] * 1e3);
	if (n > 1)
		printf(", %d more %.1f ms each", n - 1, rest * 1e3 / (n - 1));
	printf("\n");
}

static int mode_gl(long renderer, NSString *name)
{
	static const CGLPixelFormatAttribute off[] = { kCGLPFAOffScreen, 0 };
	static const CGLPixelFormatAttribute off_pb[] = { kCGLPFAOffScreen, kCGLPFAPBuffer, 0 };
	CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
	CGRect whole = CGRectMake(0, 0, WIDTH, HEIGHT);
	CGLPixelFormatObj pix;
	CGLContextObj ctx = NULL;
	unsigned char *screen = calloc(WIDTH * HEIGHT, 4), *rgba = calloc(WIDTH * HEIGHT, 4);
	unsigned char *rgb;
	double seconds[256];
	CIContext *ci;
	CIImage *output;
	int i, n = renders();

	if (n > 256)
		n = 256;
	/*
	 * The program's context draws into a pbuffer and its format is no
	 * off-screen one: only the others have a display mask, and Core Image
	 * counts the video memory of a context's display only (with none it
	 * finds every region too big: "ROI is not tilable").
	 * CIPROBE_DRAW=offscreen: an off-screen format and drawable, the way
	 * this program first did it.
	 */
	int into_pbuffer = !getenv("CIPROBE_DRAW") || strcmp(getenv("CIPROBE_DRAW"), "offscreen");
	static const CGLPixelFormatAttribute none[] = { 0 };
	CGLPBufferObj pbuffer = NULL;
	long virtual_screen = 0;

	pix = choose(into_pbuffer ? none : getenv("CIPROBE_PBUFFER") ? off_pb : off, renderer);
	if (!pix || !screen || !rgba)
		return 1;
	if (CGL(CGLCreateContext(pix, NULL, &ctx)))
		return 1;
	if (into_pbuffer) {
		if (CGL(CGLCreatePBuffer(WIDTH, HEIGHT, GL_TEXTURE_RECTANGLE_EXT, GL_RGBA, 0, &pbuffer)) ||
		    CGL(CGLGetVirtualScreen(ctx, &virtual_screen)) ||
		    CGL(CGLSetPBuffer(ctx, pbuffer, 0, 0, virtual_screen)))
			return 1;
	} else if (CGL(CGLSetOffScreen(ctx, WIDTH, HEIGHT, WIDTH * 4, screen))) {
		return 1;
	}
	if (CGL(CGLSetCurrentContext(ctx)))
		return 1;
	gl_strings();
	/* One unit is one pixel, as Core Image wants its context. */
	glViewport(0, 0, WIDTH, HEIGHT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, WIDTH, 0, HEIGHT, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	STEP("[CIContext contextWithCGLContext:pixelFormat:options:]");
	ci = [CIContext contextWithCGLContext:ctx pixelFormat:pix
			options:[NSDictionary dictionaryWithObject:(id)space
						forKey:kCIContextOutputColorSpace]];
	output = filtered(name, picture(space));
	if (!ci || !output) {
		fprintf(stderr, "no Core Image context or no picture\n");
		return 1;
	}
	for (i = 0; i < n; i++) {
		double start = now();

		if (i < 2)
			STEP(i ? "second render" : "first render");
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		[ci drawImage:output atPoint:CGPointZero fromRect:whole];
		glFinish();
		seconds[i] = now() - start;
	}
	STEP("read the picture back");
	CGLSetCurrentContext(ctx);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	gl_errors("Core Image's drawing");
	timing(seconds, n);
	rgb = flipped_rgb(rgba, WIDTH, HEIGHT);
	if (!rgb)
		return 1;
	printf("checksum %08x\n", checksum(rgb, WIDTH * HEIGHT * 3));
	save_ppm(out_name("ciprobe-gl.ppm"), rgb, WIDTH, HEIGHT);

	STEP("done; the contexts go");
	[ci clearCaches];
	CGL(CGLSetCurrentContext(NULL));
	CGL(CGLDestroyContext(ctx));
	if (pbuffer)
		CGL(CGLDestroyPBuffer(pbuffer));
	CGLDestroyPixelFormat(pix);
	CGColorSpaceRelease(space);
	return 0;
}

static int mode_soft(NSString *name)
{
	CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
	CGRect whole = CGRectMake(0, 0, WIDTH, HEIGHT);
	unsigned char *argb = calloc(WIDTH * HEIGHT, 4), *rgb = malloc(WIDTH * HEIGHT * 3);
	CGContextRef cg;
	double seconds[256];
	CIContext *ci;
	CIImage *output;
	int i, n = renders();

	if (n > 256)
		n = 256;
	if (!argb || !rgb)
		return 1;
	/* Bytes A, R, G, B; the top row first. */
	cg = CGBitmapContextCreate(argb, WIDTH, HEIGHT, 8, WIDTH * 4, space,
				   kCGImageAlphaPremultipliedFirst);
	if (!cg) {
		fprintf(stderr, "no bitmap context\n");
		return 1;
	}
	ci = [CIContext contextWithCGContext:cg
			options:[NSDictionary dictionaryWithObject:[NSNumber numberWithBool:YES]
						forKey:kCIContextUseSoftwareRenderer]];
	output = filtered(name, picture(space));
	if (!ci || !output) {
		fprintf(stderr, "no Core Image context or no picture\n");
		return 1;
	}
	for (i = 0; i < n; i++) {
		double start = now();

		CGContextSetRGBFillColor(cg, 0, 0, 0, 1);
		CGContextFillRect(cg, whole);
		[ci drawImage:output atPoint:CGPointZero fromRect:whole];
		CGContextFlush(cg);
		seconds[i] = now() - start;
	}
	timing(seconds, n);
	for (i = 0; i < WIDTH * HEIGHT; i++)
		memcpy(rgb + i * 3, argb + i * 4 + 1, 3);
	printf("checksum %08x\n", checksum(rgb, WIDTH * HEIGHT * 3));
	save_ppm(out_name("ciprobe-soft.ppm"), rgb, WIDTH, HEIGHT);
	CGContextRelease(cg);
	CGColorSpaceRelease(space);
	return 0;
}

static int mode_compare(const char *path_a, const char *path_b)
{
	int wa, ha, wb, hb, c, d, greatest[3] = { 0, 0, 0 };
	unsigned char *a = load_ppm(path_a, &wa, &ha), *b = load_ppm(path_b, &wb, &hb);
	double sum[3] = { 0, 0, 0 };
	long i, pixels, far = 0;

	if (!a || !b)
		return 2;
	if (wa != wb || ha != hb) {
		fprintf(stderr, "%dx%d against %dx%d\n", wa, ha, wb, hb);
		return 2;
	}
	pixels = (long)wa * ha;
	for (i = 0; i < pixels; i++) {
		int off = 0;

		for (c = 0; c < 3; c++) {
			d = abs((int)a[i * 3 + c] - (int)b[i * 3 + c]);
			sum[c] += d;
			if (d > greatest[c])
				greatest[c] = d;
			if (d > 8)
				off = 1;
		}
		far += off;
	}
	printf("greatest difference: red %d, green %d, blue %d\n",
	       greatest[0], greatest[1], greatest[2]);
	printf("mean difference:     red %.3f, green %.3f, blue %.3f\n",
	       sum[0] / pixels, sum[1] / pixels, sum[2] / pixels);
	printf("%ld of %ld pixels differ by more than 8\n", far, pixels);
	return 0;
}

int main(int argc, char **argv)
{
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	const char *mode = argc > 1 ? argv[1] : "";
	long renderer = 0;
	int arg = 2, status;

	setvbuf(stdout, NULL, _IONBF, 0);
	if (!strcmp(mode, "compare") && argc == 4)
		return mode_compare(argv[2], argv[3]);
	/* The same form as the driver bundle's log lines begin with. */
	printf("ciprobe [%d:%x] %s\n", (int)getpid(),
	       (unsigned)pthread_mach_thread_np(pthread_self()), mode);
	if (strcmp(mode, "soft") && argc > arg && argv[arg][0] >= '0' && argv[arg][0] <= '9')
		renderer = strtol(argv[arg++], NULL, 0);
	if (!strcmp(mode, "list")) {
		NSArray *names = [CIFilter filterNamesInCategory:kCICategoryBuiltIn];
		unsigned i;

		for (i = 0; i < [names count]; i++)
			printf("%s\n", [[names objectAtIndex:i] UTF8String]);
		return 0;
	}
	if (!strcmp(mode, "pbuffer")) {
		int ok = pbuffer_test(renderer, GL_TEXTURE_2D, 256, 256,
				      "ciprobe-pbuffer-2d.ppm");

		ok &= pbuffer_test(renderer, GL_TEXTURE_RECTANGLE_EXT, 320, 200,
				   "ciprobe-pbuffer-rect.ppm");
		status = !ok;
	} else if (!strcmp(mode, "gl") || !strcmp(mode, "soft")) {
		NSString *name = argc > arg ? [NSString stringWithUTF8String:argv[arg]]
					    : @"CIGaussianBlur";

		status = !strcmp(mode, "gl") ? mode_gl(renderer, name) : mode_soft(name);
	} else {
		fprintf(stderr, "usage: ciprobe pbuffer [renderer-id]\n"
				"       ciprobe gl [renderer-id] [filter]\n"
				"       ciprobe soft [filter]\n"
				"       ciprobe compare a.ppm b.ppm\n");
		status = 2;
	}
	[pool release];
	return status;
}
