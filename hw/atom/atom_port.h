/*
 * Shim that lets the AtomBIOS interpreter taken from the Linux radeon driver
 * build against the OS layer in rdn_os.h instead of the Linux kernel. It
 * exists to keep the ported files close to upstream.
 *
 * Copyright (c) 2026 the osx-gpu contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef ATOM_PORT_H
#define ATOM_PORT_H

#include <stdbool.h>
#include <string.h>

#include "../rdn_os.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

#ifndef EINVAL
#define EINVAL 22
#endif
#ifndef ENOMEM
#define ENOMEM 12
#endif

/* Tested by atom-types.h to select the bitfield layout in atombios.h. */
#if RDN_BIG_ENDIAN && !defined(__BIG_ENDIAN)
#define __BIG_ENDIAN 4321
#endif

#define get_unaligned_le16(p)	rdn_get_le16((p), 0)
#define get_unaligned_le32(p)	rdn_get_le32((p), 0)

#define cpu_to_le16(v)		rdn_swap_le16(v)
#define cpu_to_le32(v)		rdn_swap_le32(v)
#define le16_to_cpu(v)		rdn_swap_le16(v)
#define le32_to_cpu(v)		rdn_swap_le32(v)

#define atom_err(os, ...)	rdn_log((os), RDN_LOG_ERROR, __VA_ARGS__)
#define atom_info(os, ...)	rdn_log((os), RDN_LOG_INFO, __VA_ARGS__)
#define atom_dbg(os, ...)	rdn_log((os), RDN_LOG_DEBUG, __VA_ARGS__)

#endif /* ATOM_PORT_H */
