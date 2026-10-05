/*
 * Test helper that runs inside the Tiger guest: list and switch display
 * modes through Quartz Display Services, the same path System Preferences
 * uses, and put the cursor on a display.
 *
 *   cgmode list
 *   cgmode set <display-index> <width> <height> <bits-per-pixel>
 *   cgmode cursor <display-index>       move the cursor to its centre
 *   cgmode main <display-index>         make it the main display (menu
 *                                       bar, Dock, new windows)
 *
 * Build in the guest: gcc -o cgmode cgmode.c -framework ApplicationServices
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ApplicationServices/ApplicationServices.h>

static long num(CFDictionaryRef d, CFStringRef key)
{
	CFNumberRef n = CFDictionaryGetValue(d, key);
	long v = 0;

	if (n)
		CFNumberGetValue(n, kCFNumberLongType, &v);
	return v;
}

int main(int argc, char **argv)
{
	CGDirectDisplayID displays[8];
	CGDisplayCount count = 0, i;
	CGDisplayErr err;

	if (argc < 2 || CGGetActiveDisplayList(8, displays, &count)) {
		if (!strcmp(argv[1], "main")) {
		CGDisplayConfigRef config;
		long x = CGDisplayPixelsWide(displays[i]);
		CGDisplayCount j;

		/*
		 * The main display is the one at the origin: put this one
		 * there and the others in a row to its right. Kept across
		 * logins.
		 */
		err = CGBeginDisplayConfiguration(&config);
		if (!err)
			err = CGConfigureDisplayOrigin(config, displays[i], 0, 0);
		for (j = 0; !err && j < count; j++) {
			if (j == i)
				continue;
			err = CGConfigureDisplayOrigin(config, displays[j], x, 0);
			x += CGDisplayPixelsWide(displays[j]);
		}
		if (!err)
			err = CGCompleteDisplayConfiguration(config,
							     kCGConfigurePermanently);
		printf("making display %u the main one returned %d\n", (unsigned)i,
		       (int)err);
		return err ? 1 : 0;
	}

	fprintf(stderr, "usage: cgmode list | set N W H BPP | cursor N | main N\n");
		return 2;
	}

	if (!strcmp(argv[1], "list")) {
		for (i = 0; i < count; i++) {
			CFArrayRef modes = CGDisplayAvailableModes(displays[i]);
			CFIndex m, n = modes ? CFArrayGetCount(modes) : 0;

			printf("display %u: %lux%lu, %lu bpp%s, %ld modes\n", (unsigned)i,
			       (unsigned long)CGDisplayPixelsWide(displays[i]),
			       (unsigned long)CGDisplayPixelsHigh(displays[i]),
			       (unsigned long)CGDisplayBitsPerPixel(displays[i]),
			       CGDisplayIsMain(displays[i]) ? " (main)" : "", (long)n);
			if (count > 1 && CGDisplayIsMain(displays[i]))
				continue;
			for (m = 0; m < n; m++) {
				CFDictionaryRef d = CFArrayGetValueAtIndex(modes, m);

				printf("  %ldx%ld %ld bpp\n", num(d, kCGDisplayWidth),
				       num(d, kCGDisplayHeight),
				       num(d, kCGDisplayBitsPerPixel));
			}
		}
		return 0;
	}

	if (argc < 3 || (CGDisplayCount)atoi(argv[2]) >= count) {
		fprintf(stderr, "no such display\n");
		return 1;
	}
	i = atoi(argv[2]);

	if (!strcmp(argv[1], "set") && argc == 6) {
		boolean_t exact = 0;
		CFDictionaryRef mode = CGDisplayBestModeForParameters(displays[i],
			atoi(argv[5]), atoi(argv[3]), atoi(argv[4]), &exact);

		if (!mode || !exact) {
			fprintf(stderr, "no exact match for that mode\n");
			return 1;
		}
		err = CGDisplaySwitchToMode(displays[i], mode);
		printf("switch returned %d; now %lux%lu, %lu bpp\n", (int)err,
		       (unsigned long)CGDisplayPixelsWide(displays[i]),
		       (unsigned long)CGDisplayPixelsHigh(displays[i]),
		       (unsigned long)CGDisplayBitsPerPixel(displays[i]));
		return err ? 1 : 0;
	}

	if (!strcmp(argv[1], "cursor")) {
		CGPoint p;

		p.x = CGDisplayPixelsWide(displays[i]) / 2;
		p.y = CGDisplayPixelsHigh(displays[i]) / 2;
		err = CGDisplayMoveCursorToPoint(displays[i], p);
		printf("cursor move returned %d\n", (int)err);
		return err ? 1 : 0;
	}

	if (!strcmp(argv[1], "main")) {
		CGDisplayConfigRef config;
		long x = CGDisplayPixelsWide(displays[i]);
		CGDisplayCount j;

		/*
		 * The main display is the one at the origin: put this one
		 * there and the others in a row to its right. Kept across
		 * logins.
		 */
		err = CGBeginDisplayConfiguration(&config);
		if (!err)
			err = CGConfigureDisplayOrigin(config, displays[i], 0, 0);
		for (j = 0; !err && j < count; j++) {
			if (j == i)
				continue;
			err = CGConfigureDisplayOrigin(config, displays[j], x, 0);
			x += CGDisplayPixelsWide(displays[j]);
		}
		if (!err)
			err = CGCompleteDisplayConfiguration(config,
							     kCGConfigurePermanently);
		printf("making display %u the main one returned %d\n", (unsigned)i,
		       (int)err);
		return err ? 1 : 0;
	}

	fprintf(stderr, "usage: cgmode list | set N W H BPP | cursor N | main N\n");
	return 2;
}
