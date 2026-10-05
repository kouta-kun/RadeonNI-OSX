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
/* 10.13. Writing only; *ptr and *size are current after every write. */
FILE *open_memstream(char **ptr, size_t *size);

/* 10.6. Threads have no names on Tiger; accepted and ignored. */
int pthread_setname_np(const char *name);

/* sysconf(_SC_PHYS_PAGES) does not exist; answer it from sysctl. */
#include <unistd.h>
#ifndef _SC_PHYS_PAGES
#define _SC_PHYS_PAGES			0x7001
long tiger_sysconf(int name);
#define sysconf(name)			tiger_sysconf(name)
#endif

#ifdef __cplusplus
}
#endif

/* Names that came later for things Tiger has under an older one. */
#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS			MAP_ANON
#endif
#ifndef MAP_JIT
#define MAP_JIT				0
#endif

#include <fcntl.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC			0
#endif

#include <mach/host_info.h>
#include <mach/vm_statistics.h>
#ifndef HOST_VM_INFO64_COUNT
typedef vm_statistics_data_t vm_statistics64_data_t;
typedef host_info_t host_info64_t;
#define HOST_VM_INFO64_COUNT		HOST_VM_INFO_COUNT
#define host_statistics64		host_statistics
#endif

/*
 * Tiger's qsort_r() is the BSD one (context first). Mesa's build probe
 * takes it for the GNU one, because C lets the mismatched call compile.
 */
#undef HAVE_GNU_QSORT_R
#ifndef HAVE_BSD_QSORT_R
#define HAVE_BSD_QSORT_R 1
#endif

#endif /* __APPLE__ */
#endif /* TIGER_COMPAT_H */
