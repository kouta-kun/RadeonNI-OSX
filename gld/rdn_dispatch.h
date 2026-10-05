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

/* Every GL entry point starts with this. */
#define RDN_ENTER(rend) \
	do { \
		if (__builtin_expect((void *)(rend) != rdn_current_rend, 0)) \
			rdn_make_current(rend); \
	} while (0)

#endif /* RDN_DISPATCH_H */
