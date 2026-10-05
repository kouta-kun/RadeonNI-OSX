/*
 * drag: press the mouse button at one point, move to another, release.
 *
 *   drag x0 y0 x1 y1 [steps [milliseconds]]
 *
 * In desktop coordinates, as mouse events posted from inside the guest
 * (CGPostMouseEvent): for dragging a window when there is no usable mouse.
 * Default: 20 steps, 40 ms apart.
 *
 * Build in the guest:
 *   gcc -Wall -o drag drag.c -framework ApplicationServices
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ApplicationServices/ApplicationServices.h>

int main(int argc, char **argv)
{
	int steps = argc > 5 ? atoi(argv[5]) : 20;
	int ms = argc > 6 ? atoi(argv[6]) : 40;
	CGPoint a, b, p;
	int i;

	if (argc < 5 || steps < 1) {
		fprintf(stderr, "usage: drag x0 y0 x1 y1 [steps [milliseconds]]\n");
		return 2;
	}
	a.x = atoi(argv[1]);
	a.y = atoi(argv[2]);
	b.x = atoi(argv[3]);
	b.y = atoi(argv[4]);
	CGPostMouseEvent(a, true, 1, false);
	usleep(200000);
	CGPostMouseEvent(a, true, 1, true);
	usleep(200000);
	for (i = 1; i <= steps; i++) {
		p.x = a.x + (b.x - a.x) * i / steps;
		p.y = a.y + (b.y - a.y) * i / steps;
		CGPostMouseEvent(p, true, 1, true);
		usleep(ms * 1000);
	}
	usleep(200000);
	CGPostMouseEvent(b, true, 1, false);
	return 0;
}
