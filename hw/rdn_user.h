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
#define RDN_UC_TYPE			0x72646e31	/* upstream 'rdn1' */
#define RDN_UC_TYPE_V2			0x72646e32	/* unified 'rdn2' */
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
	 * the buffer away again. Offset UINT32_MAX with the other fields zero
	 * releases the registration, including an unattached reservation.
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
	/*
	 * UVD (hw/rdn_uvd.h). Needs the personality's FW_UVD (the firmware as
	 * in linux-firmware's SUMO_uvd.bin); without it every UVD method
	 * returns kIOReturnNotReady and nothing touches the block.
	 *
	 * RDN_UC_UVD_INIT: scalar in: start-up variant flags (RDN_UVD_F_*, 0
	 * is Linux's sequence); out: UVD_STATUS afterwards. Sets the PLL,
	 * loads the firmware, boots the VCPU and tests the ring. Once the
	 * block is up a second call only reports; a card that does not answer
	 * returns an error, logs the registers, and may be tried again.
	 */
	RDN_UC_UVD_INIT,
	/*
	 * scalars in: offset (in the aperture, from RDN_UC_ALLOC) of an
	 * indirect buffer of UVD commands and its length in words, host byte
	 * order; out: fence. Raises the UVD clocks.
	 */
	RDN_UC_UVD_SUBMIT,
	/* scalars in: fence, timeout in ms; out: 1 if reached. Lowers the clocks once nothing is pending. */
	RDN_UC_UVD_FENCE_WAIT,
	/* scalars out: 1 if the engine is up, its VCPU region's offset, the ring's offset */
	RDN_UC_UVD_INFO,
	/*
	 * Start the block's firmware again now, whatever is running: every session of every program is gone. Out: the new
	 * epoch (below). A program whose decoder stopped answering uses it; its own sessions have to be made again.
	 */
	RDN_UC_UVD_RESET,
	/*
	 * Out: the epoch of the firmware, a number that changes each time it is started again (by the kext when a new decoder
	 * starts with no other decoder running, or by RDN_UC_UVD_RESET). Sessions made in an older epoch are gone.
	 */
	RDN_UC_UVD_EPOCH,
	/*
	 * Debugging: the command buffers programs submit (RDN_UC_SUBMIT) are listed and scanned for packets that write registers outside the 3D
	 * engine's ranges or memory the video decoder owns. Scalar in: 0 off, 1 on (the lists are cleared), 2 put the lists into the registry
	 * property "IBLog" of the accelerator (`ioreg -c RadeonNIAccel -l -w0`), 3 note a moment (an entry with the kext's time). Out: the kext's
	 * time in milliseconds.
	 */
	RDN_UC_IBLOG,
	/*
	 * Debugging the memory controller's busy flags (docs/UVD.md): scalar in: 1 stop the display controllers' reads of video memory,
	 * 2 let them read again, 3 flush the host data path, 4 reset the decoder's sub-blocks (the decoder needs a restart after it),
	 * 5 reset the decoder's whole block with SRBM_SOFT_RESET (same), 20 to 83 set how the block is restarted (20 + RDN_UVD_RESET_* bits: 1 sub-blocks, 2 no RLC, 4 no stall, 8 no clock idling, 16 no rewrite, 32 no register programming). Out: SRBM_STATUS (0xE50) 20 ms after the operation.
	 */
	RDN_UC_DEBUG,
	/*
	 * The decoder in a 256 MB segment of the video memory beyond the aperture (docs/UVD.md). RDN_UVD_F_SEGMENT in
	 * RDN_UC_UVD_INIT's flags asks for it (alone it means the default start, in a segment); the kext keeps the aperture
	 * when no segment has room, and the mode it started in is the mode for every program.
	 *
	 * RDN_UC_UVD_SEGMENT: scalars out: 1 if the block runs in a segment (0: in the aperture, as ever; 2: split mode, the
	 * block in the aperture and the decoder's own buffers in a segment), the segment's first byte (an offset in video
	 * memory) and its size.
	 */
	RDN_UC_UVD_SEGMENT,
	/* scalars in: size, alignment; out: offset. Like RDN_UC_ALLOC_HIDDEN, inside the decoder's segment. Freed with RDN_UC_FREE. */
	RDN_UC_ALLOC_UVD,
	/*
	 * scalars in: offset in video memory, address in the caller's memory, byte count. Copy into (PUT) or out of (GET) the
	 * decoder's segment through the register window (a few MB a second): the bytes must lie inside the segment and outside
	 * the decoder's own blocks (firmware, ring, indirect buffers).
	 */
	RDN_UC_VRAM_PUT,
	RDN_UC_VRAM_GET,
	/*
	 * scalars in: address in the caller's memory of UVD command words (host order), their number (at most 1024);
	 * out: fence. As RDN_UC_UVD_SUBMIT, with the indirect buffer given by value (the kext keeps it in its own slot of the segment).
	 */
	RDN_UC_UVD_SUBMIT_WORDS,
	RDN_UC_V1_METHOD_COUNT,
	/* scalars in: address low/high words, byte length; out: GART offset. */
	RDN_UC_GART_BIND64 = RDN_UC_V1_METHOD_COUNT,
	/* Surface backing metadata, restricted to its owning task. */
	RDN_UC_SURFACE_BACKING,
	/* scalar in: surface ID; out: metadata, with id 0 when absent. */
	RDN_UC_SURFACE_QUERY,
	/* Diagnostic only: does not advertise a hardware video decoder. */
	RDN_UC_UVD_DIAGNOSTIC_INFO,
	/* Admin only, EnableUVDDiagnostics personality opt-in; reserved input 0. */
	RDN_UC_UVD_DIAGNOSTIC_INIT,
	/* Admin-only fixed fixture; reserved input0, versioned status output. */
	RDN_UC_UVD_FIXTURE_RUN,
	/* Admin-only immutable CPU snapshot: generation, chunk index ->4096 bytes. */
	RDN_UC_UVD_FIXTURE_READ,
	/* Metadata only; owner or administrator (WindowServer). */
	RDN_UC_SURFACE_GPU_INFO,
	/* Scalars: ID, owned destination offset/stride, width, height. */
	RDN_UC_SURFACE_GPU_COPY,
	RDN_UC_METHOD_COUNT
};

#define RDN_UVD_FIXTURE_VERSION 1u
#define RDN_UVD_FIXTURE_ID 1u
#define RDN_UVD_FIXTURE_CHUNK_BYTES 4096u
#define RDN_UVD_FIXTURE_IDLE 0u
#define RDN_UVD_FIXTURE_RUNNING 1u
#define RDN_UVD_FIXTURE_COMPLETE 2u
#define RDN_UVD_FIXTURE_FAILED 3u
/* Complete CPU snapshot available, including diagnostic output from a failed check. */
#define RDN_UVD_FIXTURE_OUTPUT_READY 1u
#define RDN_UVD_FIXTURE_GUARDS_OK 2u
#define RDN_UVD_FIXTURE_PACKED_MESSAGE 1u
enum {
	RDN_UVD_FIXTURE_PREPARE,
	RDN_UVD_FIXTURE_CREATE_WAIT,
	RDN_UVD_FIXTURE_DECODE_WAIT,
	RDN_UVD_FIXTURE_SNAPSHOT,
	RDN_UVD_FIXTURE_DESTROY_WAIT,
	RDN_UVD_FIXTURE_FINISHED
};

struct rdn_user_uvd_fixture {
	uint32_t version, fixture_id, state, stage;
	int32_t error;
	uint32_t generation, width, height, target_pitch, target_bytes;
	uint32_t snapshot_bytes, feedback_offset, feedback_bytes;
	uint32_t create_fence, decode_fence, destroy_fence, message_variant, flags;
	int32_t idle_error;
	uint32_t reserved[5];
};

#define RDN_UVD_DIAGNOSTIC_VERSION 1
#define RDN_UVD_DIAGNOSTIC_ALLOWED 1
#define RDN_UVD_DIAGNOSTIC_FIRMWARE 2
#define RDN_UVD_DIAGNOSTIC_RLC_FIRMWARE 4
#define RDN_UVD_DIAGNOSTIC_OFF 0
#define RDN_UVD_DIAGNOSTIC_RING_READY 1
#define RDN_UVD_DIAGNOSTIC_FAULTED 2

struct rdn_user_uvd_diagnostic {
	uint32_t version;
	uint32_t flags;
	uint32_t state;
	uint32_t last_status;
	int32_t last_error;
	uint32_t reserved_bytes;
	uint32_t reserved[2];
};

#define RDN_UC_POWER_QUERY		0
#define RDN_UC_POWER_PERFORMANCE	1	/* the PowerPlay table's fastest */
#define RDN_UC_POWER_BOOT		2	/* what ASIC_Init leaves */
#define RDN_UC_TEMPERATURE_BIAS		1000000

/* RDN_UC_UVD_INIT's flags (the same bits as RDN_UVD_F_* in hw/rdn_uvd.h): the decoder in a segment, back in the aperture. */
#define RDN_UC_UVD_F_SEGMENT		(1u << 18)
#define RDN_UC_UVD_F_APERTURE		(1u << 19)
#define RDN_UC_UVD_F_SPLIT		(1u << 31)

struct rdn_user_backing {
	uint32_t address_lo, address_hi, row_bytes, width, height;
};

struct rdn_user_surface_info {
	uint32_t id, offset, row_bytes, width, height;
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
