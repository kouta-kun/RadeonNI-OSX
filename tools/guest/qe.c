/*
 * qe: does the window server use OpenGL acceleration (Quartz Extreme) on
 * each display?
 *
 * Build in the guest:
 *   gcc -Wall -o qe qe.c -framework ApplicationServices
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <ApplicationServices/ApplicationServices.h>

int main(void)
{
	CGDirectDisplayID displays[8];
	CGDisplayCount n = 0, i;

	CGGetActiveDisplayList(8, displays, &n);
	for (i = 0; i < n; i++) {
		CGRect r = CGDisplayBounds(displays[i]);

		printf("display %u (%.0fx%.0f at %.0f,%.0f): Quartz Extreme %s\n",
		       (unsigned)i, r.size.width, r.size.height, r.origin.x, r.origin.y,
		       CGDisplayUsesOpenGLAcceleration(displays[i]) ? "in use" : "not in use");
	}
	return 0;
}
