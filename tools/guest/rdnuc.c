/*
 * rdnuc: talk to the kext's accelerator user client from Tiger.
 *
 *   rdnuc info             print what the accelerator reports
 *   rdnuc probe            read the pixels the drawing self-test colours
 *   rdnuc grab file.ppm    save the screen as the card holds it
 *   rdnuc peek off [n]     print n words of video memory at aperture offset off
 *   rdnuc alloc            allocate, write, read back and free video memory
 *   rdnuc power [performance|boot [what]]
 *                          print the card's clocks and temperature, after
 *                          switching power state if one is named; what is
 *                          a mask: 1 voltage, 2 engine clock, 4 memory
 *                          clock (default 7)
 *
 * Build in the guest, next to a copy of hw/rdn_user.h:
 *   gcc -Wall -o rdnuc rdnuc.c -framework IOKit -framework CoreFoundation
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>

#include "rdn_user.h"

static io_connect_t conn;
static struct rdn_user_info info;
static volatile uint32_t *aperture;

static int open_accel(void)
{
	io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault,
		IOServiceMatching(RDN_UC_SERVICE_CLASS));
	IOByteCount size = sizeof(info);
	vm_address_t addr = 0;
	vm_size_t len = 0;
	kern_return_t kr;

	if (!service) {
		fprintf(stderr, "no %s service; is the kext loaded with RDN_ACCEL=1?\n",
			RDN_UC_SERVICE_CLASS);
		return -1;
	}
	kr = IOServiceOpen(service, mach_task_self(), RDN_UC_TYPE, &conn);
	IOObjectRelease(service);
	if (kr) {
		fprintf(stderr, "IOServiceOpen: 0x%x\n", kr);
		return -1;
	}
	kr = IOConnectMethodScalarIStructureO(conn, RDN_UC_GET_INFO, 0, &size, &info);
	if (kr || info.version != RDN_USER_VERSION) {
		fprintf(stderr, "get info: 0x%x, version %u\n", kr, (unsigned)info.version);
		return -1;
	}
	kr = IOConnectMapMemory(conn, RDN_UC_MEMORY_APERTURE, mach_task_self(),
				&addr, &len, kIOMapAnywhere);
	if (kr) {
		fprintf(stderr, "map aperture: 0x%x\n", kr);
		return -1;
	}
	aperture = (volatile uint32_t *)addr;
	printf("aperture mapped at %p, %lu MB\n", (void *)addr,
	       (unsigned long)(len >> 20));
	return 0;
}

/* The screen holds native 32-bit pixels. */
static uint32_t screen_pixel(uint32_t x, uint32_t y)
{
	return aperture[info.fb_offset / 4 + y * info.fb_pitch_pixels + x] & 0xffffff;
}

int main(int argc, char **argv)
{
	const char *cmd = argc > 1 ? argv[1] : "info";

	if (open_accel())
		return 1;
	IOConnectMethodScalarIScalarO(conn, RDN_UC_SYNC_FOR_CPU, 0, 0);

	if (!strcmp(cmd, "info")) {
		printf("device %04x, video memory at GPU 0x%x%08x, aperture %u MB\n",
		       (unsigned)info.pci_device_id, (unsigned)info.vram_gpu_base_hi,
		       (unsigned)info.vram_gpu_base_lo, (unsigned)(info.aperture_size >> 20));
		printf("heap %u MB at %u MB; tile_config 0x%x, backend_map 0x%x\n",
		       (unsigned)(info.heap_size >> 20), (unsigned)(info.heap_offset >> 20),
		       (unsigned)info.tile_config, (unsigned)info.backend_map);
		printf("screen %ux%u, pitch %u, %u bpp, at offset %u\n",
		       (unsigned)info.fb_width, (unsigned)info.fb_height,
		       (unsigned)info.fb_pitch_pixels, (unsigned)info.fb_bits_per_pixel,
		       (unsigned)info.fb_offset);
	} else if (!strcmp(cmd, "probe")) {
		static const struct { uint32_t x, y; const char *what; } probe[] = {
			{ 128, 128, "square, red ff0000" }, { 256, 128, "square, green 00ff00" },
			{ 128, 256, "square, blue 0000ff" }, { 256, 256, "square, white ffffff" },
			{ 760, 128, "triangle, red" }, { 776, 128, "triangle, green" },
			{ 700, 300, "triangle, blue" }, { 840, 300, "triangle, white" },
		};
		unsigned i;

		if (!info.fb_width)
			return 1;
		for (i = 0; i < sizeof(probe) / sizeof(probe[0]); i++)
			printf("pixel (%u,%u) = %06x  (%s)\n", (unsigned)probe[i].x,
			       (unsigned)probe[i].y,
			       (unsigned)screen_pixel(probe[i].x, probe[i].y), probe[i].what);
	} else if (!strcmp(cmd, "grab") && argc > 2) {
		FILE *f = fopen(argv[2], "wb");
		uint32_t x, y;

		if (!f || !info.fb_width)
			return 1;
		fprintf(f, "P6\n%u %u\n255\n", (unsigned)info.fb_width,
			(unsigned)info.fb_height);
		for (y = 0; y < info.fb_height; y++)
			for (x = 0; x < info.fb_width; x++) {
				uint32_t v = screen_pixel(x, y);

				fputc((v >> 16) & 0xff, f);
				fputc((v >> 8) & 0xff, f);
				fputc(v & 0xff, f);
			}
		fclose(f);
		printf("saved %ux%u\n", (unsigned)info.fb_width, (unsigned)info.fb_height);
	} else if (!strcmp(cmd, "power")) {
		int state = RDN_UC_POWER_QUERY, what = 7;
		int sclk = 0, mclk = 0, temp = 0;
		kern_return_t kr;

		if (argc > 2 && !strcmp(argv[2], "performance"))
			state = RDN_UC_POWER_PERFORMANCE;
		else if (argc > 2 && !strcmp(argv[2], "boot"))
			state = RDN_UC_POWER_BOOT;
		else if (argc > 2)
			return 1;
		if (argc > 3)
			what = atoi(argv[3]);
		kr = IOConnectMethodScalarIScalarO(conn, RDN_UC_POWER, 2, 3,
						   state, what, &sclk, &mclk, &temp);
		temp -= RDN_UC_TEMPERATURE_BIAS;
		printf("%s: engine %d.%02d MHz, memory %d.%02d MHz, %d.%d C\n",
		       kr ? "FAILED" : "ok", sclk / 100, sclk % 100,
		       mclk / 100, mclk % 100, temp / 1000, (temp % 1000) / 100);
		return kr ? 1 : 0;
	} else if (!strcmp(cmd, "peek") && argc > 2) {
		/* Words of video memory at an aperture offset, as the CPU reads them. */
		unsigned long off = strtoul(argv[2], NULL, 0);
		int i, n = argc > 3 ? atoi(argv[3]) : 8;

		for (i = 0; i < n; i++)
			printf("%08x%s", (unsigned)aperture[off / 4 + i],
			       i % 8 == 7 || i == n - 1 ? "\n" : " ");
	} else if (!strcmp(cmd, "alloc")) {
		int off[3], i, bad = 0;
		kern_return_t kr;

		for (i = 0; i < 3; i++) {
			kr = IOConnectMethodScalarIScalarO(conn, RDN_UC_ALLOC, 2, 1,
							   (1 << 20) * (i + 1), 4096, &off[i]);
			printf("alloc %d MB: 0x%x, offset 0x%x\n", i + 1, kr, off[i]);
			if (kr)
				return 1;
			aperture[off[i] / 4] = 0x12345678 + i;
			aperture[off[i] / 4 + 1000] = ~(0x12345678 + i);
		}
		for (i = 0; i < 3; i++) {
			if (aperture[off[i] / 4] != 0x12345678 + i ||
			    aperture[off[i] / 4 + 1000] != ~(0x12345678 + i))
				bad++;
			kr = IOConnectMethodScalarIScalarO(conn, RDN_UC_FREE, 1, 0, off[i]);
			if (kr)
				bad++;
		}
		printf("%s\n", bad ? "FAIL" : "PASS: written, read back and freed");
	} else {
		fprintf(stderr, "unknown command\n");
		return 2;
	}
	IOServiceClose(conn);
	return 0;
}
