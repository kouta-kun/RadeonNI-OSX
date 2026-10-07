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
	/*
	 * scalar in: surface ID (what the window server named the surface
	 * with IOAccelSetSurfaceID); struct out: struct rdn_user_region, its
	 * shape on the screen. Fails if there is no such surface.
	 */
	RDN_UC_SURFACE_REGION,
	/*
	 * scalars in: surface ID, aperture offset, bytes per row, width,
	 * height. Where the surface's picture is kept: a linear buffer of
	 * 32-bit pixels, top row first, which the kext shows to whoever
	 * locks the surface for reading (the window server). Width 0 takes
	 * the buffer away again.
	 */
	RDN_UC_SURFACE_BUFFER,
	/* struct out: struct rdn_user_surfaces, every surface with a buffer */
	RDN_UC_SURFACE_LIST,
	/*
	 * scalar out: the ID of the surface that is locked for reading at
	 * this moment, or 0. The window server makes a texture for a
	 * surface between locking and unlocking it.
	 */
	RDN_UC_SURFACE_LOCKED,
	/*
	 * scalars in: state (RDN_UC_POWER_*), what to change (RDN_PM_* of
	 * hw/rdn_pm.h; ignored for a query); out: engine clock and memory
	 * clock now, both in units of 10 kHz as the VBIOS reads them back,
	 * and the GPU's temperature in thousandths of a degree Celsius plus
	 * RDN_UC_TEMPERATURE_BIAS. Changing the state waits for the GPU to
	 * be idle and fails if it does not become so.
	 */
	RDN_UC_POWER,
	/*
	 * scalar in: register offset (a multiple of 4 inside the register
	 * BAR); out: its value. Reads only, for diagnosis.
	 */
	RDN_UC_REG_READ,
	/*
	 * scalars in: size, alignment (power of two); out: offset. Like
	 * RDN_UC_ALLOC, but from the video memory beyond the aperture, which
	 * the CPU cannot reach: for what only the GPU reads and writes. The
	 * offset counts from the same origin and is at least aperture_size.
	 * Freed with RDN_UC_FREE.
	 */
	RDN_UC_ALLOC_HIDDEN,
	/* scalars out: offset and size of the region RDN_UC_ALLOC_HIDDEN uses */
	RDN_UC_HIDDEN_INFO,
	/*
	 * scalars out: 1 if the GART is on, the GPU address its range starts
	 * at (low 32 bits; it is below 4 GB) and the range's size in pages of
	 * 4 KB.
	 */
	RDN_UC_GART_INFO,
	/*
	 * scalars in: address and size in bytes of memory of the calling
	 * program, both multiples of 4 KB; out: where the GPU finds it, as a
	 * byte offset into the GART's range. The pages are wired and stay so
	 * until RDN_UC_GART_UNBIND or the client's end. The program reads
	 * and writes the memory as any other; what the GPU reads there is
	 * what the bytes are (the same as video memory seen through the
	 * aperture).
	 */
	RDN_UC_GART_BIND,
	/* scalar in: the offset RDN_UC_GART_BIND gave. The GPU must be done with it. */
	RDN_UC_GART_UNBIND,
	/*
	 * scalars in: x, y, width, height, colour; out: fence. The GPU fills
	 * that rectangle of the screen, cut to the screen's size. The colour
	 * is a pixel as the caller would store it in the screen's memory with
	 * one 32-bit store. Queued behind what the GPU still has to do and
	 * not waited for: before touching the screen through the aperture,
	 * wait for the fence (RDN_UC_FENCE_WAIT). A rectangle with nothing
	 * left of it draws nothing and gives a fence all the same. Fails,
	 * having drawn nothing, when the screen does not have 32 bits a
	 * pixel or the GPU does not keep up; a kext from before this method
	 * fails the call too. The caller then draws by itself.
	 */
	RDN_UC_SCREEN_FILL,
	/*
	 * scalars in: source x, source y, destination x, destination y, size
	 * (the width in its low 16 bits, the height in its high 16: a method
	 * has room for six values in all); out: fence. The GPU copies that
	 * rectangle of the screen to another place on it; the two may
	 * overlap. Cut so that both lie on the screen. Otherwise as
	 * RDN_UC_SCREEN_FILL.
	 */
	RDN_UC_SCREEN_COPY,
	RDN_UC_METHOD_COUNT
};

#define RDN_UC_POWER_QUERY		0
#define RDN_UC_POWER_PERFORMANCE	1	/* the PowerPlay table's fastest */
#define RDN_UC_POWER_BOOT		2	/* what ASIC_Init leaves */
#define RDN_UC_TEMPERATURE_BIAS		1000000

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

/* A surface's shape: screen coordinates, y from the top. */
#define RDN_USER_REGION_RECTS		256

struct rdn_user_region {
	/* Rectangles in the shape; only the first RDN_USER_REGION_RECTS are here. */
	uint32_t count;
	/* x, y, width, height of the box around them all. */
	int32_t bounds[4];
	int16_t rects[RDN_USER_REGION_RECTS][4];
};

/* The surfaces whose owners have registered a buffer. */
#define RDN_USER_SURFACES		32

struct rdn_user_surfaces {
	uint32_t count;
	struct {
		uint32_t id;
		uint32_t offset, row_bytes, width, height;
	} surface[RDN_USER_SURFACES];
};

#define RDN_USER_VERSION		1

#endif /* RDN_USER_H */
