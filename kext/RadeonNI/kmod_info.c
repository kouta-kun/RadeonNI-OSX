/*
 * Kernel module descriptor. Xcode generates this file for kext targets; the
 * kext is built with a plain Makefile, so it is written out here.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <mach/mach_types.h>

extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);

KMOD_EXPLICIT_DECL(org.osxgpu.driver.RadeonNI, "0.1.0", _start, _stop)

/* An IOKit driver has no module start/stop routines of its own. */
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
