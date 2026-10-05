/*
 * What Mac OS X 10.4's C library lacks and current Mesa uses.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include "tiger_compat.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <mach/mach_time.h>

int clock_gettime(clockid_t clock, struct timespec *ts)
{
	switch (clock) {
	case CLOCK_REALTIME: {
		struct timeval tv;

		gettimeofday(&tv, NULL);
		ts->tv_sec = tv.tv_sec;
		ts->tv_nsec = tv.tv_usec * 1000;
		return 0;
	}
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW: {
		static mach_timebase_info_data_t tb;
		uint64_t t = mach_absolute_time(), ns;

		if (!tb.denom)
			mach_timebase_info(&tb);
		/* Split to keep the multiplication inside 64 bits. */
		ns = (t / tb.denom) * tb.numer + (t % tb.denom) * tb.numer / tb.denom;
		ts->tv_sec = ns / 1000000000ull;
		ts->tv_nsec = ns % 1000000000ull;
		return 0;
	}
	case CLOCK_PROCESS_CPUTIME_ID:
	case CLOCK_THREAD_CPUTIME_ID: {
		/* Tiger has no per-thread figure here; the process's will do. */
		struct rusage ru;

		getrusage(RUSAGE_SELF, &ru);
		ts->tv_sec = ru.ru_utime.tv_sec + ru.ru_stime.tv_sec;
		ts->tv_nsec = (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) * 1000;
		if (ts->tv_nsec >= 1000000000) {
			ts->tv_sec++;
			ts->tv_nsec -= 1000000000;
		}
		return 0;
	}
	}
	errno = EINVAL;
	return -1;
}

int clock_getres(clockid_t clock, struct timespec *ts)
{
	(void)clock;
	ts->tv_sec = 0;
	ts->tv_nsec = 1000;
	return 0;
}

/*
 * The block is found again by free() because the pointer handed out is
 * what malloc() returned: Tiger's malloc aligns to 16 bytes, and larger
 * requests go to valloc(), which aligns to a page. More than a page is not
 * supported.
 */
int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	void *p;

	if (alignment < sizeof(void *) || (alignment & (alignment - 1)))
		return EINVAL;
	if (alignment <= 16)
		p = malloc(size ? size : 1);
	else if (alignment <= 4096)
		p = valloc(size ? size : 1);
	else
		return EINVAL;
	if (!p)
		return ENOMEM;
	*memptr = p;
	return 0;
}

size_t strnlen(const char *s, size_t maxlen)
{
	const char *end = memchr(s, 0, maxlen);

	return end ? (size_t)(end - s) : maxlen;
}

char *strndup(const char *s, size_t n)
{
	size_t len = strnlen(s, n);
	char *copy = malloc(len + 1);

	if (copy) {
		memcpy(copy, s, len);
		copy[len] = 0;
	}
	return copy;
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream)
{
	size_t len = 0;
	int c;

	if (!lineptr || !n) {
		errno = EINVAL;
		return -1;
	}
	while ((c = getc(stream)) != EOF) {
		if (len + 2 > *n) {
			size_t size = *n ? *n * 2 : 128;
			char *p = realloc(*lineptr, size);

			if (!p)
				return -1;
			*lineptr = p;
			*n = size;
		}
		(*lineptr)[len++] = (char)c;
		if (c == '\n')
			break;
	}
	if (!len)
		return -1;
	(*lineptr)[len] = 0;
	return (ssize_t)len;
}
