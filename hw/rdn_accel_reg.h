/*
 * Register and packet definitions for the 3D engine and the command
 * processor: Linux's evergreend.h, with the few macros it expects from
 * the rest of the radeon driver.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_ACCEL_REG_H
#define RDN_ACCEL_REG_H

#define RADEON_PACKET_TYPE0	0
#define RADEON_PACKET_TYPE1	1
#define RADEON_PACKET_TYPE2	2
#define RADEON_PACKET_TYPE3	3
#define REG_SET(FIELD, v)	(((v) << FIELD##_SHIFT) & FIELD##_MASK)
#define REG_GET(FIELD, v)	(((v) << FIELD##_SHIFT) & FIELD##_MASK)

#include "linux/evergreend.h"

/* From radeon_ucode.h: microcode sizes in 32-bit words. */
#define EVERGREEN_PFP_UCODE_SIZE	1120
#define EVERGREEN_PM4_UCODE_SIZE	1376

/* From r600d.h: what evergreend.h leaves to it. */
#define CACHE_FLUSH_AND_INV_EVENT	(0x16 << 0)
#define WAIT_3D_IDLE_bit		(1 << 15)
#define WAIT_3D_IDLECLEAN_bit		(1 << 17)
#define EVENT_TYPE(x)			((x) << 0)
#define EVENT_INDEX(x)			((x) << 8)

#define RDN_HDP_MEM_COHERENCY_FLUSH_CNTL	0x5480

/* The scratch registers. */
#define RDN_SCRATCH_REG(n)		(0x8500 + 4 * (n))

#ifndef EBUSY
#define EBUSY 16
#endif
#ifndef ENOMEM
#define ENOMEM 12
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif

#endif /* RDN_ACCEL_REG_H */
