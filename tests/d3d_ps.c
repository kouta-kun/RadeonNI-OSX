/*
 * Test of the Direct3D pixel shader 1.x to ARB translation (gld/rdn_arbtext.c),
 * with the byte code of The Sims 2 tree shadow shader:
 *   ps_1_1 / def c0,1,1,1,0.649999 / tex t0 / mul r0,t0,c0
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../gld/rdn_arbtext.c"

int main(void)
{
	static const unsigned code[] = {
		0xffff0101, 0x00000051, 0xa00f0000, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f266656,
		0x00000042, 0xb00f0000, 0x00000005, 0x800f0000, 0xb0e40000, 0xa0e40000, 0x0000ffff,
	};
	int len = 0;
	char *t = rdn_d3d_ps_to_arb(code, &len);

	if (!t) {
		printf("FAIL: not translated\n");
		return 1;
	}
	printf("%s", t);
	if (len != (int)strlen(t) || !strstr(t, "PARAM c0 = { 1, 1, 1, 0.649999") ||
	    !strstr(t, "TEX t0, fragment.texcoord[0], texture[0], 2D;") || !strstr(t, "MUL r0, t0, c0;") ||
	    !strstr(t, "MOV result.color, r0;\nEND\n")) {
		printf("FAIL\n");
		return 1;
	}
	{
		unsigned bad[] = { 0xffff0200, 0x0000ffff };

		if (rdn_d3d_ps_to_arb(bad, &len)) {
			printf("FAIL: ps_2 taken\n");
			return 1;
		}
	}
	printf("ok\n");
	free(t);
	return 0;
}
