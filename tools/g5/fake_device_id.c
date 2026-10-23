/*
 * Test aid, not part of the driver: load it into one program with
 *   DYLD_INSERT_LIBRARIES=/path/fake_device_id.dylib RDN_FAKE_DEVICE_ID=4150 ./program
 * and that program sees the I/O Registry's "device-id" as 0x4150 instead of
 * the card's. RDN_FAKE_REGISTRY_LOG=file logs every registry property asked
 * for. Nothing in the registry is changed. Built on the Mac:
 *   gcc -arch ppc -dynamiclib -o fake_device_id.dylib fake_device_id.c -framework IOKit -framework CoreFoundation
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdlib.h>

static CFTypeRef fake_create(io_registry_entry_t entry, CFStringRef key, CFAllocatorRef alloc, IOOptionBits options)
{
	CFTypeRef v = IORegistryEntryCreateCFProperty(entry, key, alloc, options);
	const char *fake = getenv("RDN_FAKE_DEVICE_ID"), *path = getenv("RDN_FAKE_REGISTRY_LOG");
	char name[128] = "?";

	if (key)
		CFStringGetCString(key, name, sizeof(name), kCFStringEncodingUTF8);
	if (fake && key && CFStringCompare(key, CFSTR("device-id"), 0) == kCFCompareEqualTo) {
		unsigned long id = strtoul(fake, NULL, 16);
		unsigned char b[4];

		b[0] = id & 255; b[1] = (id >> 8) & 255; b[2] = (id >> 16) & 255; b[3] = (id >> 24) & 255;
		if (v)
			CFRelease(v);
		v = CFDataCreate(alloc, b, 4);
	}
	if (path) {
		FILE *f = fopen(path, "a");

		if (f) {
			fprintf(f, "key %s -> %s%s\n", name, v ? "value" : "none",
				fake && key && CFStringCompare(key, CFSTR("device-id"), 0) == kCFCompareEqualTo ? " (device-id faked)" : "");
			fclose(f);
		}
	}
	return v;
}

typedef struct { const void *replacement; const void *replacee; } interpose_t;
__attribute__((used)) static const interpose_t interposers[] __attribute__((section("__DATA,__interpose"))) = {
	{ (const void *)fake_create, (const void *)IORegistryEntryCreateCFProperty },
};
