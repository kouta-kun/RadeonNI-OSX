/*
 * EDID interpretation: preferred timing and HDMI detection.
 * Written from the VESA E-EDID and CEA-861 layouts.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include "rdn_mode.h"

#define EDID_DTD_OFFSET		54
#define EDID_BLOCK		128
#define CEA_EXT_TAG		0x02
#define CEA_VENDOR_BLOCK	3

bool rdn_edid_preferred_mode(const uint8_t *edid, struct rdn_mode *mode)
{
	const uint8_t *d = edid + EDID_DTD_OFFSET;
	uint32_t clock = (uint32_t)(d[0] | (d[1] << 8)) * 10;
	uint16_t hact, hblank, vact, vblank, hso, hsw, vso, vsw;

	if (!clock)
		return false;

	hact = (uint16_t)(d[2] | ((d[4] >> 4) << 8));
	hblank = (uint16_t)(d[3] | ((d[4] & 0xf) << 8));
	vact = (uint16_t)(d[5] | ((d[7] >> 4) << 8));
	vblank = (uint16_t)(d[6] | ((d[7] & 0xf) << 8));
	hso = (uint16_t)(d[8] | (((d[11] >> 6) & 3) << 8));
	hsw = (uint16_t)(d[9] | (((d[11] >> 4) & 3) << 8));
	vso = (uint16_t)((d[10] >> 4) | (((d[11] >> 2) & 3) << 4));
	vsw = (uint16_t)((d[10] & 0xf) | ((d[11] & 3) << 4));

	memset(mode, 0, sizeof(*mode));
	mode->clock = clock;
	mode->hdisplay = hact;
	mode->hsync_start = (uint16_t)(hact + hso);
	mode->hsync_end = (uint16_t)(hact + hso + hsw);
	mode->htotal = (uint16_t)(hact + hblank);
	mode->vdisplay = vact;
	mode->vsync_start = (uint16_t)(vact + vso);
	mode->vsync_end = (uint16_t)(vact + vso + vsw);
	mode->vtotal = (uint16_t)(vact + vblank);

	/* Digital separate sync: bit 1 is horizontal, bit 2 vertical polarity. */
	if ((d[17] & 0x18) == 0x18) {
		if (!(d[17] & 0x02))
			mode->flags |= RDN_MODE_NHSYNC;
		if (!(d[17] & 0x04))
			mode->flags |= RDN_MODE_NVSYNC;
	}
	return true;
}

bool rdn_edid_is_hdmi(const uint8_t *edid, int len)
{
	int b, i, end;

	for (b = 1; (b + 1) * EDID_BLOCK <= len; b++) {
		const uint8_t *ext = edid + b * EDID_BLOCK;

		if (ext[0] != CEA_EXT_TAG)
			continue;
		/* Data blocks run from byte 4 up to the detailed timings. */
		end = ext[2];
		if (end > EDID_BLOCK - 1)
			end = EDID_BLOCK - 1;
		for (i = 4; i < end; i += (ext[i] & 0x1f) + 1) {
			int tag = ext[i] >> 5, size = ext[i] & 0x1f;

			/* HDMI Licensing IEEE OUI 00-0C-03, stored LSB first. */
			if (tag == CEA_VENDOR_BLOCK && size >= 3 &&
			    i + 3 < EDID_BLOCK && ext[i + 1] == 0x03 &&
			    ext[i + 2] == 0x0c && ext[i + 3] == 0x00)
				return true;
		}
	}
	return false;
}
