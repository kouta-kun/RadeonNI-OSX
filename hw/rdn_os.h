/*
 * OS layer for the Radeon hardware library.
 *
 * Everything under hw/ reaches the outside world only through this
 * structure, so the same code runs in a Linux userspace tool, in a test
 * harness with no hardware, and in a Mac OS X kernel extension.
 *
 * The library is not thread-safe: the caller serialises all entry.
 *
 * Copyright (c) 2026 the osx-gpu contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_OS_H
#define RDN_OS_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__BIG_ENDIAN__) || \
	(defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define RDN_BIG_ENDIAN 1
#else
#define RDN_BIG_ENDIAN 0
#endif

enum rdn_log_level {
	RDN_LOG_ERROR,
	RDN_LOG_INFO,
	RDN_LOG_DEBUG
};

/*
 * Register values cross this interface in CPU byte order. The card's
 * registers are little-endian; the implementation does the swap on a
 * big-endian host. Offsets are byte offsets into the respective BAR.
 */
struct rdn_os {
	void *cookie;

	/* Register BAR (BAR2), 32-bit accesses. */
	uint32_t (*mmio_read32)(void *cookie, uint32_t offset);
	void (*mmio_write32)(void *cookie, uint32_t offset, uint32_t value);

	/*
	 * I/O BAR (BAR4), 32-bit accesses. Both may be NULL when the platform
	 * has no usable I/O space; the library then uses MMIO instead.
	 */
	uint32_t (*io_read32)(void *cookie, uint32_t offset);
	void (*io_write32)(void *cookie, uint32_t offset, uint32_t value);

	/* PCI configuration space of the VGA function. */
	uint32_t (*cfg_read32)(void *cookie, uint32_t offset);
	void (*cfg_write32)(void *cookie, uint32_t offset, uint32_t value);

	/* Busy-wait or sleep for at least this many microseconds. */
	void (*delay_us)(void *cookie, uint32_t usec);

	/* Monotonic milliseconds, for timeouts. */
	uint64_t (*time_ms)(void *cookie);

	/* Zeroed allocation; NULL on failure. */
	void *(*alloc)(void *cookie, size_t size);
	void (*free)(void *cookie, void *ptr);

	/* One complete message, no trailing newline required. */
	void (*log)(void *cookie, enum rdn_log_level level, const char *fmt,
		    va_list ap);
};

static inline void rdn_log(struct rdn_os *os, enum rdn_log_level level,
			   const char *fmt, ...)
#if defined(__GNUC__)
	__attribute__((format(printf, 3, 4)))
#endif
	;

static inline void rdn_log(struct rdn_os *os, enum rdn_log_level level,
			   const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	os->log(os->cookie, level, fmt, ap);
	va_end(ap);
}

/*
 * Explicit little-endian accessors for byte buffers (VBIOS tables). These
 * make no assumption about alignment or host byte order.
 */
static inline uint8_t rdn_get_u8(const void *base, uint32_t off)
{
	return ((const uint8_t *)base)[off];
}

static inline uint16_t rdn_get_le16(const void *base, uint32_t off)
{
	const uint8_t *p = (const uint8_t *)base + off;

	return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t rdn_get_le32(const void *base, uint32_t off)
{
	const uint8_t *p = (const uint8_t *)base + off;

	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Byte-swap a CPU-order value to or from little-endian storage. */
static inline uint16_t rdn_swap_le16(uint16_t v)
{
#if RDN_BIG_ENDIAN
	return (uint16_t)((v << 8) | (v >> 8));
#else
	return v;
#endif
}

static inline uint32_t rdn_swap_le32(uint32_t v)
{
#if RDN_BIG_ENDIAN
	return (v << 24) | ((v & 0xff00) << 8) | ((v >> 8) & 0xff00) | (v >> 24);
#else
	return v;
#endif
}

#endif /* RDN_OS_H */
