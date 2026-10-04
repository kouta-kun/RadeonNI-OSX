/*
 * Glue between the OS layer and the AtomBIOS interpreter: register access
 * callbacks and interpreter setup.
 *
 * Copyright (c) 2026 the osx-gpu contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_ATOM_H
#define RDN_ATOM_H

#include "rdn_os.h"
#include "atom/atom.h"

/* Size of the I/O BAR: registers below this are reachable directly. */
#define RDN_IO_BAR_SIZE		0x100

/* Index/data pair, at the start of both the I/O BAR and the register BAR. */
#define RDN_MM_INDEX		0x0000
#define RDN_MM_DATA		0x0004

struct rdn_atom {
	struct card_info card;
	struct atom_context *ctx;
};

/*
 * Parse the VBIOS image and set up the interpreter. The image must stay
 * valid until rdn_atom_fini(). Returns 0 or a negative errno value.
 */
int rdn_atom_init(struct rdn_atom *atom, struct rdn_os *os, void *bios);
void rdn_atom_fini(struct rdn_atom *atom);

#endif /* RDN_ATOM_H */
