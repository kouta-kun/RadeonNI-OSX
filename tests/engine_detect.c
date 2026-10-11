/*
 * Test of the engine detection that decides glthread (mesa/frontend/
 * rdn_engine.c): the games on the G5 as they lie on its disk, with a made-up
 * file system.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "../mesa/frontend/rdn_engine.c"

static const char *files[] = {
	"/Applications/Unreal Tournament 2004.app/System/Engine.u",
	"/Applications/Unreal Tournament 2004.app/System/XInterface.u",
	"/Applications/UT99.app/System/Engine.u",
	"/Users/tiger/Desktop/Quake 3/baseq3",
	"/Users/tiger/Desktop/Quake 3/Quake3.app/Contents/MacOS/Quake3",
	"/Users/tiger/Desktop/Quake 4/q4base",
	"/Users/tiger/Desktop/Doom 3 Demo/demo/demo00.pk4",
	"/Users/tiger/Desktop/Doom 3/base/pak000.pk4",
	"/opt/game/bin/baseq3",
	NULL,
};

static bool fake_exists(const char *path, void *user)
{
	int i;

	(void)user;
	for (i = 0; files[i]; i++)
		if (!strcmp(files[i], path))
			return true;
	return false;
}

static int failures;

static void check(const char *exe, enum rdn_engine want, int glthread)
{
	enum rdn_engine got = rdn_engine_detect(exe, fake_exists, NULL);
	int gl = rdn_engine_glthread(got);

	if (got != want || gl != glthread) {
		printf("FAIL %s: %s, glthread %d (wanted %s, %d)\n", exe,
		       rdn_engine_name(got), gl, rdn_engine_name(want), glthread);
		failures++;
	} else {
		printf("ok   %s: %s, glthread %d\n", exe, rdn_engine_name(got), gl);
	}
}

int main(void)
{
	check("/Applications/Unreal Tournament 2004.app/Contents/MacOS/Unreal Tournament 2004",
	      RDN_ENGINE_UNREAL2, 0);
	/* Unreal Engine 1 has Engine.u too; it was not measured. */
	check("/Applications/UT99.app/Contents/MacOS/UT",
	      RDN_ENGINE_UNKNOWN, -1);
	check("/Users/tiger/Desktop/Quake 3/Quake3.app/Contents/MacOS/Quake3",
	      RDN_ENGINE_IDTECH3, 1);
	check("/Users/tiger/Desktop/Quake 4/Quake 4.app/Contents/MacOS/Quake 4",
	      RDN_ENGINE_IDTECH4, 1);
	check("/Users/tiger/Desktop/Doom 3 Demo/Doom 3 Demo.app/Contents/MacOS/Doom 3 Demo",
	      RDN_ENGINE_IDTECH4, 1);
	check("/Users/tiger/Desktop/Doom 3/Doom 3.app/Contents/MacOS/Doom 3",
	      RDN_ENGINE_IDTECH4, 1);
	/* Not in a bundle: the executable's own directory. */
	check("/opt/game/bin/ioq3", RDN_ENGINE_IDTECH3, 1);
	check("/System/Library/CoreServices/WindowServer", RDN_ENGINE_UNKNOWN, -1);
	check("/Applications/Chess.app/Contents/MacOS/Chess", RDN_ENGINE_UNKNOWN, -1);
	check("", RDN_ENGINE_UNKNOWN, -1);

	if (failures) {
		printf("FAIL engine_detect: %d\n", failures);
		return 1;
	}
	printf("PASS engine_detect\n");
	return 0;
}
