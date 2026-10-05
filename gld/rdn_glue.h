/*
 * What the parts of the driver bundle share. Plain pointers, so that the
 * part that talks to Mesa does not need Apple's OpenGL headers and the part
 * that talks to Apple's does not need Mesa's.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_GLUE_H
#define RDN_GLUE_H

/* The generated glue (gen_dispatch.py). */
/* Look Mesa's functions up by OpenGL name; returns how many exist. */
unsigned rdn_dispatch_resolve(void *(*lookup)(const char *name),
			      void (*missing)(const char *name));
/*
 * Put our entry points into a GLIFunctionDispatch that has `entries`
 * entries (the running system's may be shorter than the SDK header's);
 * returns how many were replaced.
 */
unsigned rdn_dispatch_install(void *table, unsigned entries);
extern const unsigned rdn_dispatch_entries;

/* rdn_mesa.c: the engine context whose Mesa context is current. */
extern void *rdn_current_rend;
void rdn_make_current(void *rend);

/* rdn_mesa.c: what the bundle tells it about, as the gld* calls go by. */
void rdn_mesa_context_created(void *gld_ctx);
void rdn_mesa_context_destroyed(void *gld_ctx);
void rdn_mesa_attach(void *gld_ctx, long type, const void *drawable);
/* True if the context's GL entry points are now Mesa's. */
int rdn_mesa_dispatch(void *gld_ctx, void *engine_table);
/* Finish the frame and put it into the drawable's buffer. */
void rdn_mesa_present(void *gld_ctx);

/* RadeonNIGLDriver.c: one line into the bundle's log, if it has one. */
void rdn_log(const char *fmt, ...);

#endif /* RDN_GLUE_H */
