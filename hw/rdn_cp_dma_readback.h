/* Bounded kernel-owned VRAM readback commands. SPDX-License-Identifier: MIT */
#ifndef RDN_CP_DMA_READBACK_H
#define RDN_CP_DMA_READBACK_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RDN_CP_DMA_READBACK_MAX_BYTES (4u * 1024u * 1024u)
#define RDN_CP_DMA_READBACK_MAX_RANGES 16u
#define RDN_CP_DMA_READBACK_MAX_WORDS 121u
#define RDN_CP_DMA_FILL_MAX_BYTES (128u * 1024u * 1024u)
#define RDN_CP_DMA_FILL_MAX_RANGES 18u
#define RDN_CP_DMA_FILL_MAX_WORDS 877u
struct rdn_cp_dma_span {
    uint64_t address;
    uint32_t bytes;
    uint32_t reserved;
};
struct rdn_cp_dma_copy {
    struct rdn_cp_dma_span source;
    struct rdn_cp_dma_span destination;
    uint32_t bytes;
    uint32_t reserved;
};
struct rdn_cp_dma_fill {
    struct rdn_cp_dma_span destination;
    uint32_t value;
    uint32_t reserved;
};
/* Four-byte-aligned owned destinations; none may overlap the complete IB span.
 * Patterns are GPU dwords, so 0x01234567 writes bytes 67,45,23,01.
 * The caller retains every destination and the IB until the CP fence completes.
 */
int rdn_cp_dma_fill_build_list(const struct rdn_cp_dma_fill *fills,
    uint32_t count, const struct rdn_cp_dma_span *command_ib,
    void *output, size_t capacity, uint32_t *word_count);
/* Trusted owned allocations only; this is not a user-address validator.
 * Writes CPU-order GFX IB words; the CP's existing IB swap handles host byte order.
 * Caller waits for UVD before submission and the CP fence before CPU reads/reuse.
 * Output may be unaligned; output/count cannot alias each other or either span.
 * Failure leaves output/count unchanged. No MMIO, submission or allocation occurs.
 */
/* One synchronization/fence covers target and guard copies. Total bytes <=4MiB. */
int rdn_cp_dma_readback_build_list(const struct rdn_cp_dma_copy *copies,
    uint32_t count, void *output, size_t capacity, uint32_t *word_count);
int rdn_cp_dma_readback_build(const struct rdn_cp_dma_span *source,
    const struct rdn_cp_dma_span *destination, uint32_t bytes,
    void *output, size_t capacity, uint32_t *word_count);
/* Trusted strided pixel spans; padding is not copied. */
int rdn_cp_dma_surface_build(uint64_t source, uint32_t source_stride,
    uint64_t destination, uint32_t destination_stride, uint32_t width,
    uint32_t height, void *output, size_t capacity, uint32_t *word_count);
#ifdef __cplusplus
}
#endif
#endif
