/*
 * gamma: the main display's gamma table as Quartz has it.
 *
 *   gamma            print 16 of its entries (red green blue, 0 to 1)
 *   gamma restore    put back the table of the display's ColorSync profile
 *   gamma linear     set a straight ramp
 *
 * A program that dies while it has changed the table (a game killed in
 * the middle of a fade) can leave it behind.
 *
 * Build in the guest:
 *   gcc -Wall -o gamma gamma.c -framework ApplicationServices
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <ApplicationServices/ApplicationServices.h>

int main(int argc, char **argv)
{
	CGDirectDisplayID d = CGMainDisplayID();
	CGGammaValue r[256], g[256], b[256];
	CGTableCount n = 0, i;

	if (argc > 1 && !strcmp(argv[1], "restore")) {
		CGDisplayRestoreColorSyncSettings();
		printf("restored\n");
	} else if (argc > 1 && !strcmp(argv[1], "linear")) {
		printf("linear: %d\n", (int)CGSetDisplayTransferByFormula(d,
			0, 1, 1, 0, 1, 1, 0, 1, 1));
	}
	if (CGGetDisplayTransferByTable(d, 256, r, g, b, &n)) {
		printf("no table\n");
		return 1;
	}
	printf("%u entries\n", (unsigned)n);
	for (i = 0; i < n; i += n / 16 ? n / 16 : 1)
		printf("%3u: %.3f %.3f %.3f\n", (unsigned)i, r[i], g[i], b[i]);
	if (n)
		printf("%3u: %.3f %.3f %.3f\n", (unsigned)n - 1, r[n - 1], g[n - 1], b[n - 1]);
	return 0;
}
