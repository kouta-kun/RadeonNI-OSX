/*
 * The interface between the kext's accelerator user client and user space.
 *
 * A client opens the RadeonNIAccel service (user client type
 * RDN_UC_TYPE), maps memory type RDN_UC_MEMORY_APERTURE to get video
 * memory from offset 0, and calls the methods below. All values are 32-bit
 * and in the CPU's byte order. Offsets are byte offsets into the aperture.
 * Everything a client allocated is freed when it closes.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_USER_H
#define RDN_USER_H

#include <stdint.h>

#define RDN_UC_SERVICE_CLASS		"RadeonNIAccel"
#define RDN_UC_TYPE			0x72646e31	/* 'rdn1' */
#define RDN_UC_MEMORY_APERTURE		0

enum {
	/* struct out: struct rdn_user_info */
	RDN_UC_GET_INFO,
	/* scalars in: size, alignment (power of two); out: offset */
	RDN_UC_ALLOC,
	/* scalars in: offset */
	RDN_UC_FREE,
	/* scalars in: offset, words; out: fence. Words in CPU byte order. */
	RDN_UC_SUBMIT,
	/* scalars in: fence, timeout in ms (0 only asks); out: 1 if reached */
	RDN_UC_FENCE_WAIT,
	/* no arguments: make the CPU see what the GPU wrote */
	RDN_UC_SYNC_FOR_CPU,
	RDN_UC_METHOD_COUNT
};

struct rdn_user_info {
	uint32_t version;		/* RDN_USER_VERSION */
	uint32_t pci_device_id;
	uint32_t vram_gpu_base_hi;	/* GPU address of aperture offset 0 */
	uint32_t vram_gpu_base_lo;
	uint32_t aperture_size;
	uint32_t heap_offset;		/* what RDN_UC_ALLOC hands out from */
	uint32_t heap_size;
	uint32_t tile_config;
	uint32_t backend_map;
	uint32_t max_backends;
	uint32_t max_tile_pipes;
	uint32_t max_pipes;
	uint32_t num_ses;
	/* The screen's surface at fb_offset, or all zero before a mode is set. */
	uint32_t fb_offset;
	uint32_t fb_width;
	uint32_t fb_height;
	uint32_t fb_pitch_pixels;
	uint32_t fb_bits_per_pixel;
};

#define RDN_USER_VERSION		1

#endif /* RDN_USER_H */
