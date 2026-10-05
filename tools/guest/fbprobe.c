/*
 * Test helper for the Tiger guest: ask every IOFramebuffer to re-probe its
 * displays, which is what the "Detect Displays" button does.
 *
 * Build: gcc -o fbprobe fbprobe.c -framework IOKit -framework CoreFoundation
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsLib.h>

int main(void)
{
	io_iterator_t it;
	io_service_t fb;
	io_name_t name;

	if (IOServiceGetMatchingServices(kIOMasterPortDefault,
			IOServiceMatching("IOFramebuffer"), &it))
		return 1;
	while ((fb = IOIteratorNext(it))) {
		IORegistryEntryGetName(fb, name);
		printf("%s: request probe returned 0x%x\n", name,
		       IOServiceRequestProbe(fb, kIOFBUserRequestProbe));
		IOObjectRelease(fb);
	}
	IOObjectRelease(it);
	return 0;
}
