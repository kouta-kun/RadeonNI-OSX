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
 * A transparent window of 600x400 at 300,300 is shown for that long; grab
 * the screen meanwhile (~/gl/rdnuc grab). With CIColorInvert the desktop
 * behind it has its colours inverted if the window server's filters work.
 * The calls are private to CoreGraphics; their arguments are as the Dock
 * passes them.
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
