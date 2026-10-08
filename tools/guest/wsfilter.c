/*
 * wsfilter: ask the window server to put a Core Image filter on what is
 * behind a window, the way the Dock does for Dashboard's ripple
 * (CGSNewCIFilterByName, CGSAddWindowFilter with 0x3001), and say what the
 * window server reports of itself (CGSServerOperationState: 0xd Quartz 2D
 * Extreme possible, 0xe in use, 0xf Core Image, which is what System
 * Profiler prints as "Core Image: Supported").
 *
 *   wsfilter [filter, default CIColorInvert] [seconds, default 8] [flags]
 *
 * A window of 600x400 at 300,300 is shown for that long, with four opaque
 * bands (red, green, blue, white, top to bottom, 100 rows each; WSF_PLAIN=1
 * leaves it empty and transparent); grab the screen meanwhile
 * (scripts/mac.sh g5 grab). The window server applies the filter to the
 * window's own picture, so with CIColorInvert the bands are cyan, magenta,
 * yellow and black if the window server's filters work. (Dashboard's ripple
 * is put on the Dock's layer in the same way.) The calls are private to
 * CoreGraphics; their arguments are as the Dock passes them.
 *
 * Build on the host:
 *   scripts/darwin.sh powerpc-apple-darwin8-gcc -O2 -o build/wsfilter \
 *       tools/guest/wsfilter.c -framework Carbon
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <Carbon/Carbon.h>

extern int _CGSDefaultConnection(void);
extern int CGSNewCIFilterByName(int cid, CFStringRef name, int *filter);
extern int CGSAddWindowFilter(int cid, int wid, int filter, int flags);
extern int CGSRemoveWindowFilter(int cid, int wid, int filter);
extern int CGSReleaseCIFilter(int cid, int filter);
extern int CGSServerOperationState(int state);
extern int GetNativeWindowFromWindowRef(WindowRef window);

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "CIColorInvert";
	double seconds = argc > 2 ? atof(argv[2]) : 8;
	int flags = argc > 3 ? (int)strtol(argv[3], NULL, 0) : 0x3001;
	Rect bounds = { 300, 300, 700, 900 };	/* top, left, bottom, right */
	WindowRef window = NULL;
	CFStringRef filter_name;
	int cid, wid, filter = 0, err, state;

	setvbuf(stdout, NULL, _IONBF, 0);
	if (CreateNewWindow(kOverlayWindowClass, kWindowNoAttributes, &bounds, &window) ||
	    !window) {
		printf("no window\n");
		return 1;
	}
	ShowWindow(window);
	if (!getenv("WSF_PLAIN")) {
		static const float bands[4][3] = {
			{ 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 1, 1 }
		};
		CGContextRef cg = NULL;
		int i;

		if (!QDBeginCGContext(GetWindowPort(window), &cg) && cg) {
			for (i = 0; i < 4; i++) {
				CGContextSetRGBFillColor(cg, bands[i][0], bands[i][1], bands[i][2], 1);
				CGContextFillRect(cg, CGRectMake(0, i * 100, 600, 100));
			}
			CGContextFlush(cg);
			QDEndCGContext(GetWindowPort(window), &cg);
		}
	}
	cid = _CGSDefaultConnection();
	wid = GetNativeWindowFromWindowRef(window);
	for (state = 0xd; state <= 0xf; state++)
		printf("window server state 0x%x: %d\n", state, CGSServerOperationState(state));
	filter_name = CFStringCreateWithCString(NULL, name, kCFStringEncodingASCII);
	err = CGSNewCIFilterByName(cid, filter_name, &filter);
	printf("CGSNewCIFilterByName(%s): error %d, filter %d\n", name, err, filter);
	if (!err) {
		err = CGSAddWindowFilter(cid, wid, filter, flags);
		printf("CGSAddWindowFilter(window %d, flags 0x%x): error %d\n", wid, flags, err);
	}
	RunCurrentEventLoop(seconds);
	if (filter) {
		CGSRemoveWindowFilter(cid, wid, filter);
		CGSReleaseCIFilter(cid, filter);
	}
	DisposeWindow(window);
	printf("done\n");
	return 0;
}
