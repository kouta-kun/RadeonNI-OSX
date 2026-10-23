/*
 * Between the driver bundle and the generated dispatch glue
 * (gen_dispatch.py).
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_DISPATCH_H
#define RDN_DISPATCH_H

#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/gliContext.h>
#include <OpenGL/gliDispatch.h>

#include "rdn_glue.h"

/*
 * Every GL entry point starts with this. On the thread that draws, the
 * stack pointer and the context are compared to what rdn_fast_rend holds,
 * which costs no thread-local lookup; the others (and a context that is
 * not the one noted) are compared to rdn_current_rend.
 */
#if defined(__ppc__)
#define RDN_ENTER(ctx_) \
	do { \
		uintptr_t rdn_sp_; \
		__asm__("mr %0,r1" : "=r"(rdn_sp_)); \
		if (__builtin_expect(rdn_sp_ - rdn_fast_rend.low < rdn_fast_rend.len && \
				     (void *)(ctx_) == rdn_fast_rend.rend, 1)) \
			break; \
		if (__builtin_expect((void *)(ctx_) != rdn_current_rend, 0)) { \
			rdn_dispatch_flush_pending(); \
			rdn_make_current(ctx_); \
		} \
	} while (0)
#else
#define RDN_ENTER(ctx_) \
	do { \
		if (__builtin_expect((void *)(ctx_) != rdn_current_rend, 0)) { \
			rdn_dispatch_flush_pending(); \
			rdn_make_current(ctx_); \
		} \
	} while (0)
#endif

#endif /* RDN_DISPATCH_H */
