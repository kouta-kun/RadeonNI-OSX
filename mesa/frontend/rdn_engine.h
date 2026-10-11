/*
 * Which game engine a program is, from the files next to its executable.
 *
 * glthread (Mesa's second thread) is worth +25 % to +75 % in id Tech 3 and 4
 * games and nothing in Unreal Engine 2, where it only costs memory (journal,
 * 2026-10-11). The engine is the thing the games of a family share: how they
 * hand the vertices over (buffer objects, or arrays in their own memory).
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_ENGINE_H
#define RDN_ENGINE_H

#include <stdbool.h>
#include <stddef.h>

enum rdn_engine {
   RDN_ENGINE_UNKNOWN,
   RDN_ENGINE_UNREAL2,  /* UT2003, UT2004, ... */
   RDN_ENGINE_IDTECH3,  /* Quake 3 Arena */
   RDN_ENGINE_IDTECH4,  /* Doom 3, Quake 4 */
};

/* Does the file or directory exist? */
typedef bool (*rdn_engine_exists_fn)(const char *path, void *user);

const char *rdn_engine_name(enum rdn_engine engine);

/*
 * What glthread does for the engine: 1 on, 0 off, -1 no opinion (the
 * default decides).
 */
int rdn_engine_glthread(enum rdn_engine engine);

/*
 * The engine of the program whose executable is `exe`. The directories
 * looked in are the executable's own, and for an application bundle
 * (X.app/Contents/MacOS/X) the bundle and the directory it is in.
 */
enum rdn_engine rdn_engine_detect(const char *exe,
                                  rdn_engine_exists_fn exists, void *user);

/* The running program's executable, or false. */
bool rdn_engine_exe_path(char *buf, size_t size);

/* rdn_engine_detect() on the running program, with the file system. */
enum rdn_engine rdn_engine_detect_self(void);

#endif
