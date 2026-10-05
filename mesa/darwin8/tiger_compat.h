/*
 * What Mac OS X 10.4's C library lacks and current Mesa uses.
 *
 * Reaches Mesa's sources through the wrappers in include/, which shadow
 * the SDK's <time.h>, <stdlib.h>, <string.h> and <stdio.h>, and for C++
 * through -include (libstdc++ skips the wrappers). The functions are in
 * tiger_compat.c, linked into everything as libtigercompat.a. See
 * scripts/build-mesa.sh.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef TIGER_COMPAT_H
#define TIGER_COMPAT_H

#if defined(__APPLE__) && !defined(__ASSEMBLER__)

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* clock_gettime() arrived in 10.12. */
#ifndef CLOCK_REALTIME
typedef int clockid_t;
#define CLOCK_REALTIME			0
#define CLOCK_MONOTONIC			6
#define CLOCK_MONOTONIC_RAW		4
#define CLOCK_PROCESS_CPUTIME_ID	12
#define CLOCK_THREAD_CPUTIME_ID		16
int clock_gettime(clockid_t clock, struct timespec *ts);
int clock_getres(clockid_t clock, struct timespec *ts);
#endif

/* These arrived in 10.6 and 10.7. */
int posix_memalign(void **memptr, size_t alignment, size_t size);
size_t strnlen(const char *s, size_t maxlen);
char *strndup(const char *s, size_t n);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);

#ifdef __cplusplus
}
#endif

#endif /* __APPLE__ */
#endif /* TIGER_COMPAT_H */
