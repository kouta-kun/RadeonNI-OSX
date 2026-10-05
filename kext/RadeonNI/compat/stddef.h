/*
 * <stddef.h> for the kernel build: Kernel.framework does not ship one.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_COMPAT_STDDEF_H
#define RDN_COMPAT_STDDEF_H

#include <sys/types.h>

#ifndef NULL
#ifdef __cplusplus
#define NULL	0
#else
#define NULL	((void *)0)
#endif
#endif

#ifndef offsetof
#define offsetof(type, member)	__builtin_offsetof(type, member)
#endif

#endif
