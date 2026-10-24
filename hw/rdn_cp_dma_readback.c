/* Evergreen CP_DMA address copy, matching Mesa r600_cp_dma_copy_buffer.
 * SPDX-License-Identifier: MIT
 */
#include "rdn_cp_dma_readback.h"
#ifdef KERNEL
#include <libkern/libkern.h>
#else
#include <string.h>
#endif
#ifndef EINVAL
#define EINVAL 22
#endif

#define GPU_LIMIT (1ull << 40)
#define COPY_CHUNK (1024u * 1024u)

static int gpu_span(const struct rdn_cp_dma_span *span)
{
    return !span->reserved && span->bytes && span->address < GPU_LIMIT &&
           span->bytes <= GPU_LIMIT - span->address;
}

static int overlaps(uint64_t a, uint64_t an, uint64_t b, uint64_t bn)
{
    return a < b + bn && b < a + an;
}

static int cpu_span(const void *pointer, size_t bytes)
{
    return pointer && bytes <= UINTPTR_MAX - (uintptr_t)pointer;
}

static int output_valid(void *output, size_t output_bytes, uint32_t *count,
                        const void *input, size_t input_bytes)
{
    return cpu_span(output, output_bytes) && cpu_span(count, sizeof(*count)) &&
        !((uintptr_t)count & 3) && cpu_span(input, input_bytes) &&
        !overlaps((uintptr_t)output, output_bytes, (uintptr_t)count, sizeof(*count)) &&
        !overlaps((uintptr_t)output, output_bytes, (uintptr_t)input, input_bytes) &&
        !overlaps((uintptr_t)count, sizeof(*count), (uintptr_t)input, input_bytes);
}

int rdn_cp_dma_readback_build_list(const struct rdn_cp_dma_copy *copies,
    uint32_t count, void *output, size_t capacity, uint32_t *word_count)
{
    uint32_t words[RDN_CP_DMA_READBACK_MAX_WORDS];
    uint32_t i, j, total = 0, packets = 0, n = 0;
    size_t input_bytes, output_bytes;
    if (!count || count > RDN_CP_DMA_READBACK_MAX_RANGES)
        return -EINVAL;
    input_bytes = count * sizeof(*copies);
    if (!cpu_span(copies, input_bytes))
        return -EINVAL;
    for (i = 0; i < count; i++) {
        const struct rdn_cp_dma_copy *copy = copies + i;
        if (copy->reserved || !gpu_span(&copy->source) || !gpu_span(&copy->destination) ||
            !copy->bytes || copy->bytes > RDN_CP_DMA_READBACK_MAX_BYTES - total ||
            copy->bytes > copy->source.bytes || copy->bytes > copy->destination.bytes)
            return -EINVAL;
        total += copy->bytes;
        packets += (copy->bytes + COPY_CHUNK - 1) / COPY_CHUNK;
    }
    for (i = 0; i < count; i++)
        for (j = 0; j < count; j++) {
            if (overlaps(copies[i].source.address, copies[i].source.bytes,
                         copies[j].destination.address, copies[j].destination.bytes) ||
                (i != j && overlaps(copies[i].destination.address, copies[i].destination.bytes,
                                   copies[j].destination.address, copies[j].destination.bytes)))
                return -EINVAL;
        }
    output_bytes = (7u + 6u * packets) * sizeof(uint32_t);
    if (packets > (RDN_CP_DMA_READBACK_MAX_WORDS - 7u) / 6u || capacity < output_bytes ||
        !output_valid(output, output_bytes, word_count, copies, input_bytes))
        return -EINVAL;
    words[n++] = 0xc0034300u; words[n++] = 0x1980003cu;
    words[n++] = 0xffffffffu; words[n++] = 0; words[n++] = 10;
    for (i = 0; i < count; i++) {
        uint64_t src = copies[i].source.address, dst = copies[i].destination.address;
        uint32_t left = copies[i].bytes;
        while (left) {
            uint32_t part = left > COPY_CHUNK ? COPY_CHUNK : left;
            words[n++] = 0xc0044100u;
            words[n++] = (uint32_t)src;
            words[n++] = (i + 1 == count && left == part ? 0x80000000u : 0) |
                         (uint32_t)(src >> 32);
            words[n++] = (uint32_t)dst;
            words[n++] = (uint32_t)(dst >> 32);
            words[n++] = part; /* No source/destination byte swapping. */
            src += part; dst += part; left -= part;
        }
    }
    words[n++] = 0xc0004200u; words[n++] = 0;
    memcpy(output, words, output_bytes);
    *word_count = n;
    return 0;
}

int rdn_cp_dma_readback_build(const struct rdn_cp_dma_span *source,
    const struct rdn_cp_dma_span *destination, uint32_t bytes,
    void *output, size_t capacity, uint32_t *word_count)
{
    struct rdn_cp_dma_copy copy;
    size_t output_bytes;
    if (!bytes || bytes > RDN_CP_DMA_READBACK_MAX_BYTES ||
        !cpu_span(source, sizeof(*source)) || !cpu_span(destination, sizeof(*destination)))
        return -EINVAL;
    output_bytes = (7u + 6u * ((bytes + COPY_CHUNK - 1) / COPY_CHUNK)) * sizeof(uint32_t);
    if (!output_valid(output, output_bytes, word_count, source, sizeof(*source)) ||
        !output_valid(output, output_bytes, word_count, destination, sizeof(*destination)))
        return -EINVAL;
    copy.source = *source; copy.destination = *destination;
    copy.bytes = bytes; copy.reserved = 0;
    return rdn_cp_dma_readback_build_list(&copy, 1, output, capacity, word_count);
}

static void emit_word(void *output, uint32_t *cursor, uint32_t value)
{
    memcpy((uint8_t *)output + 4u * (*cursor)++, &value, sizeof(value));
}

int rdn_cp_dma_fill_build_list(const struct rdn_cp_dma_fill *fills,
    uint32_t count, const struct rdn_cp_dma_span *command_ib,
    void *output, size_t capacity, uint32_t *word_count)
{
    uint32_t i, j, total = 0, packets = 0, n = 0;
    size_t input_bytes, output_bytes;
    if (!count || count > RDN_CP_DMA_FILL_MAX_RANGES ||
        !cpu_span(command_ib, sizeof(*command_ib)))
        return -EINVAL;
    input_bytes = count * sizeof(*fills);
    if (!cpu_span(fills, input_bytes) || !gpu_span(command_ib) ||
        (command_ib->address & 63) || command_ib->bytes > 4096)
        return -EINVAL;
    for (i = 0; i < count; i++) {
        const struct rdn_cp_dma_span *span = &fills[i].destination;
        if (fills[i].reserved || !gpu_span(span) ||
            ((span->address | span->bytes) & 3) ||
            span->bytes > RDN_CP_DMA_FILL_MAX_BYTES - total ||
            overlaps(span->address, span->bytes, command_ib->address, command_ib->bytes))
            return -EINVAL;
        total += span->bytes;
        packets += (span->bytes + COPY_CHUNK - 1) / COPY_CHUNK;
    }
    for (i = 0; i < count; i++)
        for (j = i + 1; j < count; j++)
            if (overlaps(fills[i].destination.address, fills[i].destination.bytes,
                         fills[j].destination.address, fills[j].destination.bytes))
                return -EINVAL;
    output_bytes = (7u + 6u * packets) * sizeof(uint32_t);
    if (packets > (RDN_CP_DMA_FILL_MAX_WORDS - 7u) / 6u ||
        output_bytes > command_ib->bytes || capacity < output_bytes ||
        !output_valid(output, output_bytes, word_count, fills, input_bytes) ||
        !output_valid(output, output_bytes, word_count, command_ib, sizeof(*command_ib)))
        return -EINVAL;
    emit_word(output, &n, 0xc0034300u); emit_word(output, &n, 0x1980003cu);
    emit_word(output, &n, 0xffffffffu); emit_word(output, &n, 0); emit_word(output, &n, 10);
    for (i = 0; i < count; i++) {
        uint64_t address = fills[i].destination.address;
        uint32_t left = fills[i].destination.bytes;
        while (left) {
            uint32_t part = left > COPY_CHUNK ? COPY_CHUNK : left;
            emit_word(output, &n, 0xc0044100u);
            emit_word(output, &n, fills[i].value);
            emit_word(output, &n, 0x40000000u |
                (i + 1 == count && part == left ? 0x80000000u : 0));
            emit_word(output, &n, (uint32_t)address);
            emit_word(output, &n, (uint32_t)(address >> 32));
            emit_word(output, &n, part);
            address += part; left -= part;
        }
    }
    emit_word(output, &n, 0xc0004200u); emit_word(output, &n, 0);
    *word_count = n;
    return 0;
}

int rdn_cp_dma_surface_build(uint64_t source, uint32_t source_stride,
    uint64_t destination, uint32_t destination_stride, uint32_t width,
    uint32_t height, void *output, size_t capacity, uint32_t *word_count)
{
    uint32_t *words = output, n = 0, y, count, bytes;
    uint64_t source_bytes, destination_bytes;
    if (!width || !height || width > 16384 || height > 16384 ||
        source_stride < width * 4 || destination_stride < width * 4 ||
        ((source | destination | source_stride | destination_stride) & 3)) return -EINVAL;
    source_bytes = (uint64_t)source_stride * (height - 1) + width * 4;
    destination_bytes = (uint64_t)destination_stride * (height - 1) + width * 4;
    if (source >= GPU_LIMIT || destination >= GPU_LIMIT ||
        source_bytes > GPU_LIMIT - source || destination_bytes > GPU_LIMIT - destination ||
        overlaps(source, source_bytes, destination, destination_bytes)) return -EINVAL;
    count = source_stride == width * 4 && destination_stride == width * 4 &&
        (uint64_t)width * height * 4 <= COPY_CHUNK ? 1 : height;
    if (!cpu_span(output, (size_t)(count * 6 + 12) * 4) ||
        capacity < (size_t)(count * 6 + 12) * 4 || !cpu_span(word_count, sizeof(*word_count)) ||
        ((uintptr_t)output & 3) || ((uintptr_t)word_count & 3) ||
        overlaps((uintptr_t)output, (count * 6 + 12) * 4,
                 (uintptr_t)word_count, sizeof(*word_count))) return -EINVAL;
    words[n++] = 0xc0034300u; words[n++] = 0x1980003cu;
    words[n++] = 0xffffffffu; words[n++] = 0; words[n++] = 10;
    bytes = count == 1 ? width * height * 4 : width * 4;
    for (y = 0; y < count; ++y) {
        words[n++] = 0xc0044100u; words[n++] = (uint32_t)source;
        words[n++] = (y + 1 == count ? 0x80000000u : 0) | (uint32_t)(source >> 32);
        words[n++] = (uint32_t)destination;
        words[n++] = (uint32_t)(destination >> 32); words[n++] = bytes;
        source += source_stride; destination += destination_stride;
    }
    words[n++] = 0xc0034300u; words[n++] = 0x1980003cu;
    words[n++] = 0xffffffffu; words[n++] = 0; words[n++] = 10;
    words[n++] = 0xc0004200u; words[n++] = 0;
    *word_count = n;
    return 0;
}
