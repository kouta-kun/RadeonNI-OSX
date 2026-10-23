/*
 * Test aid, not part of the driver: post mouse and keyboard events to the
 * logged-in user's session, so that a program (a game) can be driven from ssh.
 *   rdninput move X Y        move the pointer (screen coordinates, top left 0,0)
 *   rdninput click X Y       move, left button down and up
 *   rdninput drag X1 Y1 X2 Y2
 *   rdninput key CODE        a virtual key code (down, up); 53 escape, 36 return, 49 space
 *   rdninput scroll N        wheel, positive up
 * Built on the Mac: gcc -o rdninput rdninput.c -framework ApplicationServices
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void mouse(int x, int y, int down)
{
	CGPoint p = CGPointMake(x, y);

	CGPostMouseEvent(p, 1, 1, down);
}

int main(int argc, char **argv)
{
	if (argc >= 4 && !strcmp(argv[1], "move")) {
		CGWarpMouseCursorPosition(CGPointMake(atoi(argv[2]), atoi(argv[3])));
		mouse(atoi(argv[2]), atoi(argv[3]), 0);
	} else if (argc >= 4 && !strcmp(argv[1], "click")) {
		int x = atoi(argv[2]), y = atoi(argv[3]);

		mouse(x, y, 0);
		usleep(150000);
		mouse(x, y, 1);
		usleep(80000);
		mouse(x, y, 0);
	} else if (argc >= 6 && !strcmp(argv[1], "drag")) {
		int x1 = atoi(argv[2]), y1 = atoi(argv[3]), x2 = atoi(argv[4]), y2 = atoi(argv[5]), i;

		mouse(x1, y1, 0);
		usleep(100000);
		mouse(x1, y1, 1);
		for (i = 1; i <= 10; i++) {
			mouse(x1 + (x2 - x1) * i / 10, y1 + (y2 - y1) * i / 10, 1);
			usleep(40000);
		}
		mouse(x2, y2, 0);
	} else if (argc >= 3 && !strcmp(argv[1], "key")) {
		CGKeyCode k = (CGKeyCode)atoi(argv[2]);

		CGPostKeyboardEvent((CGCharCode)0, k, 1);
		usleep(60000);
		CGPostKeyboardEvent((CGCharCode)0, k, 0);
	} else if (argc >= 3 && !strcmp(argv[1], "scroll")) {
		CGPostScrollWheelEvent(1, atoi(argv[2]));
	} else {
		fprintf(stderr, "usage: rdninput move|click X Y | drag X1 Y1 X2 Y2 | key CODE | scroll N\n");
		return 1;
	}
	return 0;
}
