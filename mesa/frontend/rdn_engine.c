/*
 * Which game engine a program is; see rdn_engine.h.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include "rdn_engine.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

/*
 * The files that give an engine away, relative to a directory of the
 * program. All of them must be there.
 */
static const struct {
   enum rdn_engine engine;
   const char *a, *b;
} markers[] = {
   /* UT2004's own bundle holds System/Engine.u and System/XInterface.u. UT99
    * (Unreal Engine 1) has Engine.u too, so the second file is asked for:
    * only Unreal Engine 2 was measured. */
   { RDN_ENGINE_UNREAL2, "System/Engine.u", "System/XInterface.u" },
   { RDN_ENGINE_IDTECH3, "baseq3", NULL },
   { RDN_ENGINE_IDTECH4, "q4base", NULL },
   { RDN_ENGINE_IDTECH4, "base/pak000.pk4", NULL },  /* Doom 3 */
   { RDN_ENGINE_IDTECH4, "demo/demo00.pk4", NULL },  /* Doom 3 demo */
};

const char *
rdn_engine_name(enum rdn_engine engine)
{
   switch (engine) {
   case RDN_ENGINE_UNREAL2: return "Unreal Engine 2";
   case RDN_ENGINE_IDTECH3: return "id Tech 3";
   case RDN_ENGINE_IDTECH4: return "id Tech 4";
   default: return "unknown";
   }
}

int
rdn_engine_glthread(enum rdn_engine engine)
{
   switch (engine) {
   case RDN_ENGINE_UNREAL2: return 0;
   case RDN_ENGINE_IDTECH3:
   case RDN_ENGINE_IDTECH4: return 1;
   default: return -1;
   }
}

static bool
has(const char *dir, const char *name, rdn_engine_exists_fn exists, void *user)
{
   char path[PATH_MAX];

   if (!name)
      return true;
   if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path))
      return false;
   return exists(path, user);
}

static enum rdn_engine
detect_in(const char *dir, rdn_engine_exists_fn exists, void *user)
{
   unsigned i;

   for (i = 0; i < sizeof(markers) / sizeof(markers[0]); i++)
      if (has(dir, markers[i].a, exists, user) &&
          has(dir, markers[i].b, exists, user))
         return markers[i].engine;
   return RDN_ENGINE_UNKNOWN;
}

/* Cut the last path component off. */
static void
parent_of(char *path)
{
   char *slash = strrchr(path, '/');

   if (slash && slash != path)
      *slash = 0;
   else if (slash)
      slash[1] = 0;
}

enum rdn_engine
rdn_engine_detect(const char *exe, rdn_engine_exists_fn exists, void *user)
{
   static const char tail[] = ".app/Contents/MacOS";
   char dir[PATH_MAX];
   size_t len;
   enum rdn_engine engine;

   if (!exe || strlen(exe) >= sizeof(dir))
      return RDN_ENGINE_UNKNOWN;
   strcpy(dir, exe);
   parent_of(dir);          /* the executable's directory */

   engine = detect_in(dir, exists, user);
   if (engine != RDN_ENGINE_UNKNOWN)
      return engine;

   len = strlen(dir);
   if (len > sizeof(tail) - 1 &&
       !strcmp(dir + len - (sizeof(tail) - 1), tail)) {
      dir[len - (sizeof("/Contents/MacOS") - 1)] = 0;  /* X.app */
      engine = detect_in(dir, exists, user);
      if (engine != RDN_ENGINE_UNKNOWN)
         return engine;
      parent_of(dir);        /* where X.app is */
      engine = detect_in(dir, exists, user);
   }
   return engine;
}

enum rdn_engine
rdn_engine_detect_dir(const char *dir, rdn_engine_exists_fn exists, void *user)
{
   char path[PATH_MAX];
   enum rdn_engine engine;
   int level;

   if (!dir || !dir[0] || strlen(dir) >= sizeof(path))
      return RDN_ENGINE_UNKNOWN;
   strcpy(path, dir);
   for (level = 0; level < 3; level++) {
      engine = detect_in(path, exists, user);
      if (engine != RDN_ENGINE_UNKNOWN)
         return engine;
      if (!strcmp(path, "/"))
         break;
      parent_of(path);
   }
   return RDN_ENGINE_UNKNOWN;
}

bool
rdn_engine_exe_path(char *buf, size_t size)
{
   char raw[PATH_MAX], full[PATH_MAX];

#ifdef __APPLE__
   uint32_t n = (uint32_t)sizeof(raw);

   /* The path as the program was started with: "./Name" is possible. */
   if (_NSGetExecutablePath(raw, &n) != 0)
      return false;
#else
   ssize_t n = readlink("/proc/self/exe", raw, sizeof(raw) - 1);

   if (n <= 0)
      return false;
   raw[n] = 0;
#endif
   if (!realpath(raw, full))
      return false;
   if (strlen(full) >= size)
      return false;
   strcpy(buf, full);
   return true;
}

static bool
file_exists(const char *path, void *user)
{
   (void)user;
   return access(path, F_OK) == 0;
}

enum rdn_engine
rdn_engine_detect_self(void)
{
   char path[PATH_MAX];
   enum rdn_engine engine = RDN_ENGINE_UNKNOWN;

   if (rdn_engine_exe_path(path, sizeof(path)))
      engine = rdn_engine_detect(path, file_exists, NULL);
   /* A program started by a relative path that then changed its directory
    * (UT2004 does) has no executable path to be found. */
   if (engine == RDN_ENGINE_UNKNOWN && getcwd(path, sizeof(path)))
      engine = rdn_engine_detect_dir(path, file_exists, NULL);
   return engine;
}
