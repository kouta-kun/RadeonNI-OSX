/*
 * curmove: move the mouse pointer smoothly around a display, for looking
 * at the cursor on a monitor that has no mouse of its own.
 *
 *   curmove [display-index [seconds]]
 *
 * The pointer follows a slow figure over most of the display (default:
 * display 1 for 3600 seconds), 60 steps a second, as real mouse-moved
 * events. Stop it early with killall curmove.
 *
 * Build in the guest:
 *   gcc -Wall -o curmove curmove.c -framework ApplicationServices -lm
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ApplicationServices/ApplicationServices.h>

int main(int argc, char **argv)
{
	CGDirectDisplayID displays[8];
	CGDisplayCount count = 0;
	unsigned index = argc > 1 ? (unsigned)atoi(argv[1]) : 1;
	long seconds = argc > 2 ? atol(argv[2]) : 3600;
	CGRect r;
	long step;

	if (CGGetActiveDisplayList(8, displays, &count) || index >= count) {
		fprintf(stderr, "no display %u (%u active)\n", index, (unsigned)count);
		return 1;
	}
	r = CGDisplayBounds(displays[index]);
	printf("moving the pointer on display %u (%.0fx%.0f at %.0f,%.0f) for %ld s\n",
	       index, r.size.width, r.size.height, r.origin.x, r.origin.y, seconds);
	fflush(stdout);

	for (step = 0; step < seconds * 60; step++) {
		double t = step / 60.0;
		CGPoint p;

		/* Two periods that do not divide: the path covers the area. */
		p.x = r.origin.x + r.size.width * (0.5 + 0.45 * sin(t * 2 * M_PI / 11.0));
		p.y = r.origin.y + r.size.height * (0.5 + 0.45 * sin(t * 2 * M_PI / 7.0));
		CGPostMouseEvent(p, true, 1, false);
		usleep(16667);
	}
	return 0;
}
