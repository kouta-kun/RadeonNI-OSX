/*
 * useractivity: tell Mac OS X the user is active (UpdateSystemActivity,
 * CoreServices), which wakes displays that went to sleep and restarts the
 * idle timers. The documented way to do from ssh what a key press does.
 * It writes nothing to IODisplayWrangler (Tiger panics on that, journal).
 *
 * Build (on the Mac): gcc -o useractivity useractivity.c \
 *     -framework CoreServices
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <CoreServices/CoreServices.h>
#include <stdio.h>

int main(void)
{
	OSErr err = UpdateSystemActivity(UsrActivity);

	printf("UpdateSystemActivity(UsrActivity): %d\n", (int)err);
	return err != 0;
}
