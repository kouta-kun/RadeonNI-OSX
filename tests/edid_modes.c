/*
 * Test of the mode list an EDID gives (rdn_edid_modes): reads EDID files,
 * prints the list and checks it: the preferred timing first, no repeats,
 * sane timings, and 640x480, 800x600 and 1024x768 at 60 Hz present when the
 * monitor's range limits allow them. The same output must come from x86
 * and from big-endian PowerPC.
 *
 * Usage: edid_modes edid-file...
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hw/rdn_mode.h"

#define MAX 32

static unsigned hz(const struct rdn_mode *m)
{
	return (unsigned)(((unsigned long long)m->clock * 1000 + (unsigned)m->htotal * m->vtotal / 2) /
			  ((unsigned)m->htotal * m->vtotal));
}

static int has(const struct rdn_mode *l, int n, unsigned w, unsigned h)
{
	int i;

	for (i = 0; i < n; i++)
		if (l[i].hdisplay == w && l[i].vdisplay == h && hz(&l[i]) >= 59 && hz(&l[i]) <= 61)
			return 1;
	return 0;
}

int main(int argc, char **argv)
{
	int a, failures = 0;

	for (a = 1; a < argc; a++) {
		uint8_t edid[512];
		struct rdn_mode list[MAX], pref;
		FILE *f = fopen(argv[a], "rb");
		size_t len;
		int n, i, j;

		if (!f) {
			printf("FAIL: cannot open %s\n", argv[a]);
			return 1;
		}
		len = fread(edid, 1, sizeof(edid), f);
		fclose(f);
		n = rdn_edid_modes(edid, (int)len, list, MAX);
		printf("%s: %d modes\n", argv[a], n);
		for (i = 0; i < n; i++)
			printf("  %2d: %4ux%-4u %3u Hz  %6u kHz%s%s\n", i + 1, list[i].hdisplay,
			       list[i].vdisplay, hz(&list[i]), (unsigned)list[i].clock,
			       (list[i].flags & RDN_MODE_NHSYNC) ? " -h" : " +h",
			       (list[i].flags & RDN_MODE_NVSYNC) ? " -v" : " +v");
		if (n < 1 || !rdn_edid_preferred_mode(edid, &pref) ||
		    memcmp(&pref, &list[0], sizeof(pref))) {
			printf("FAIL: the preferred timing is not first\n");
			failures++;
		}
		for (i = 0; i < n; i++) {
			const struct rdn_mode *m = &list[i];

			if (!(m->hdisplay < m->hsync_start && m->hsync_start < m->hsync_end &&
			      m->hsync_end <= m->htotal && m->vdisplay < m->vsync_start &&
			      m->vsync_start < m->vsync_end && m->vsync_end <= m->vtotal) ||
			    m->clock > 165000) {
				printf("FAIL: mode %d has odd timings\n", i + 1);
				failures++;
			}
			for (j = 0; j < i; j++)
				if (list[j].hdisplay == m->hdisplay && list[j].vdisplay == m->vdisplay &&
				    hz(&list[j]) == hz(m)) {
					printf("FAIL: modes %d and %d repeat\n", j + 1, i + 1);
					failures++;
				}
		}
		if (!has(list, n, 640, 480) || !has(list, n, 800, 600) || !has(list, n, 1024, 768)) {
			printf("FAIL: 640x480, 800x600 or 1024x768 at 60 Hz is missing\n");
			failures++;
		}
	}
	printf(failures ? "FAILED\n" : "PASS\n");
	return failures != 0;
}
