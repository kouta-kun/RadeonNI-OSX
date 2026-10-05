/*
 * <stdbool.h> for the kernel build: with -nostdinc the compiler's own copy
 * is not on the include path, and Kernel.framework does not ship one.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_COMPAT_STDBOOL_H
#define RDN_COMPAT_STDBOOL_H

#ifndef __cplusplus
#define bool	_Bool
#define true	1
#define false	0
#endif
#define __bool_true_false_are_defined	1

#endif
