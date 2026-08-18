/* Copyright (c) 2026 Riverside Research */
#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

// h_bit_writer_
HBitWriter *h_bit_writer_new(HAllocator *mm__) {
    HBitWriter *writer = h_new(HBitWriter, 1);
    memset(writer, 0, sizeof(*writer));
    writer->buf = h_alloc(mm__, writer->capacity = 8);
    assert(writer != NULL);
    memset(writer->buf, 0, writer->capacity);
    writer->mm__ = mm__;
    writer->flags = BYTE_BIG_ENDIAN | BIT_BIG_ENDIAN;
    writer->error = 0;

    return writer;
}

/**
 * Ensure there are at least [nbits] bits available at the end of the
 * buffer. If the buffer is expanded, the added bits should be zeroed.
 */
static bool h_bit_writer_reserve(HBitWriter *w, size_t nbits) {
    if (w->error)
        return false;

    size_t nbytes = nbits / 8 + (nbits % 8 != 0) + (w->bit_offset != 0);

    if (nbytes >= SIZE_MAX - w->index) {
        w->error = 1;
        return false;
    }

    size_t required_capacity = w->index + nbytes + 1;
    size_t old_capacity = w->capacity;

    if (required_capacity <= old_capacity)
        return true;

    size_t new_capacity = old_capacity;
    while (new_capacity < required_capacity) {
        if (new_capacity > SIZE_MAX / 2) {
            new_capacity = required_capacity;
            break;
        }
        new_capacity *= 2;
    }

    uint8_t *new_buf = w->mm__->realloc(w->mm__, w->buf, new_capacity);

    if (!new_buf) {
        w->error = 1;
        return false;
    }

    memset(new_buf + old_capacity, 0, new_capacity - old_capacity);
    w->buf = new_buf;
    w->capacity = new_capacity;
    return true;
}

void h_bit_writer_put(HBitWriter *w, uint64_t data, size_t nbits) {
    HAMMER_ASSERT(nbits > 0);

    // expand size...
    if (!h_bit_writer_reserve(w, nbits))
        return;

    while (nbits) {
        size_t count = MIN((size_t)(8 - w->bit_offset), nbits);

        // select the bits to be written from the source
        uint16_t bits;
        if (w->flags & BYTE_BIG_ENDIAN) {
            // read from the top few bits; masking will be done later.
            bits = (uint16_t)(data >> (nbits - count));
        } else {
            // just copy the bottom byte over.
            bits = data & 0xff;
            data >>= count; // remove the bits that have just been used.
        }
        // mask off the unnecessary bits.
        bits &= (uint16_t)((1 << count) - 1);

        // Now, push those bits onto the current byte...
        if (w->flags & BIT_BIG_ENDIAN)
            w->buf[w->index] = (w->buf[w->index] << count) | bits;
        else
            w->buf[w->index] = (uint8_t)((w->buf[w->index] | ((uint16_t)bits << 8)) >> count);

        // update index and bit_offset.
        w->bit_offset += (char)count;
        if (w->bit_offset == 8) {
            w->bit_offset = 0;
            w->index++;
        }
        nbits -= count;
    }
}

const uint8_t *h_bit_writer_get_buffer(HBitWriter *w, size_t *len) {
    HAMMER_ASSERT(w != NULL);
    HAMMER_ASSERT(len != NULL);
    if (w->error) {
        *len = 0;
        return NULL;
    }
    HAMMER_ASSERT(w->bit_offset == 0);
    *len = w->index;
    return w->buf;
}

void h_bit_writer_free(HBitWriter *w) {
    HAllocator *mm__ = w->mm__;
    h_free(w->buf);
    h_free(w);
}
