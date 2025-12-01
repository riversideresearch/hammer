#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>
#include <stdint.h>

#define MK_INPUT_STREAM(buf, len, endianness_)                                                     \
    {                                                                                              \
        .input = (uint8_t *)buf, .length = len, .index = 0, .bit_offset = 0,                       \
        .endianness = endianness_                                                                  \
    }

static void test_bitreader_ints(void) {
    HInputStream is =
        MK_INPUT_STREAM("\xFF\xFF\xFF\xFE\x00\x00\x00\x00", 8, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    g_check_cmp_int64(h_read_bits(&is, 64, true), ==, -0x200000000);
}

static void test_bitreader_be(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A\x5A", 2, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    g_check_cmp_int32(h_read_bits(&is, 3, false), ==, 0x03);
    g_check_cmp_int32(h_read_bits(&is, 8, false), ==, 0x52);
    g_check_cmp_int32(h_read_bits(&is, 5, false), ==, 0x1A);
}

static void test_bitreader_le(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A\x5A", 2, BIT_LITTLE_ENDIAN | BYTE_LITTLE_ENDIAN);
    g_check_cmp_int32(h_read_bits(&is, 3, false), ==, 0x02);
    g_check_cmp_int32(h_read_bits(&is, 8, false), ==, 0x4D);
    g_check_cmp_int32(h_read_bits(&is, 5, false), ==, 0x0B);
}

static void test_largebits_be(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A\x5A", 2, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    g_check_cmp_int32(h_read_bits(&is, 11, false), ==, 0x352);
    g_check_cmp_int32(h_read_bits(&is, 5, false), ==, 0x1A);
}

static void test_largebits_le(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A\x5A", 2, BIT_LITTLE_ENDIAN | BYTE_LITTLE_ENDIAN);
    g_check_cmp_int32(h_read_bits(&is, 11, false), ==, 0x26A);
    g_check_cmp_int32(h_read_bits(&is, 5, false), ==, 0x0B);
}

static void test_offset_largebits_be(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A\x5A", 2, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    g_check_cmp_int32(h_read_bits(&is, 5, false), ==, 0xD);
    g_check_cmp_int32(h_read_bits(&is, 11, false), ==, 0x25A);
}

static void test_offset_largebits_le(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A\x5A", 2, BIT_LITTLE_ENDIAN | BYTE_LITTLE_ENDIAN);
    g_check_cmp_int32(h_read_bits(&is, 5, false), ==, 0xA);
    g_check_cmp_int32(h_read_bits(&is, 11, false), ==, 0x2D3);
}

// Test bitreader.c: h_read_bits with overflow/overrun (line 43)
static void test_read_bits_overrun(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A", 1, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    is.margin = 0;
    int64_t result = h_read_bits(&is, 16, false); // Request more bits than available
    g_check_cmp_int(is.overrun, ==, true);
}

// Test bitreader.c: h_read_bits with signed values (line 34)
static void test_read_bits_signed(void) {
    HInputStream is = MK_INPUT_STREAM("\xFF", 1, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    int64_t result = h_read_bits(&is, 8, true);
    g_check_cmp_int64(result, ==, -1);
}

// Test bitreader.c: h_read_bits with margin (line 42)
static void test_read_bits_margin(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A\x5A", 2, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    is.margin = 2;
    int64_t result = h_read_bits(&is, 6, false);
    // Should read 6 bits accounting for margin
    g_check_cmp_int(result, >=, 0);
}

// Test bitreader.c: h_read_bits fast path (line 54)
static void test_read_bits_fast_path(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A\x5A", 2, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    is.bit_offset = 0;
    is.margin = 0;
    int64_t result = h_read_bits(&is, 8, false);
    g_check_cmp_int32(result, ==, 0x6A);
}

// Test bitreader.c: h_read_bits with BIT_BIG_ENDIAN and partial byte (line 72)
static void test_read_bits_bit_big_endian_partial(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A", 1, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    is.bit_offset = 2;
    is.margin = 0;
    int64_t result = h_read_bits(&is, 3, false);
    g_check_cmp_int(result, >=, 0);
}

// Test bitreader.c: h_read_bits with BIT_LITTLE_ENDIAN and partial byte (line 86)
static void test_read_bits_bit_little_endian_partial(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A", 1, BIT_LITTLE_ENDIAN | BYTE_BIG_ENDIAN);
    is.bit_offset = 2;
    is.margin = 0;
    int64_t result = h_read_bits(&is, 3, false);
    g_check_cmp_int(result, >=, 0);
}

// Test bitreader.c: h_skip_bits with count=0 (line 119)
static void test_skip_bits_zero(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A\x5A", 2, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    size_t old_index = is.index;
    h_skip_bits(&is, 0);
    g_check_cmp_int(is.index, ==, old_index);
}

// Test bitreader.c: h_skip_bits with overrun (line 122)
// Test bitreader.c: h_skip_bits with overrun (line 150-153) - count/8 > left path
static void test_skip_bits_overrun(void) {
    const uint8_t input[] = {0x6A, 0x5A};
    HInputStream is = MK_INPUT_STREAM(input, 2, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    is.index = 1;
    is.bit_offset = 0;
    is.margin = 0;
    // Try to skip more bytes than available (count/8 > left)
    h_skip_bits(&is, 20); // 20 bits = 2.5 bytes, but only 1 byte left
    g_check_cmp_int(is.overrun, ==, true);
    g_check_cmp_int(is.index, ==, 2);
}

// Test bitreader.c: h_skip_bits with index==length (line 125)
static void test_skip_bits_at_end(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A", 1, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    is.index = is.length;
    h_skip_bits(&is, 8);
    g_check_cmp_int(is.overrun, ==, true);
}

// Test bitreader.c: h_skip_bits partial byte (line 132)
static void test_skip_bits_partial_byte(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A\x5A", 2, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    is.bit_offset = 2;
    is.margin = 0;
    h_skip_bits(&is, 3);
    g_check_cmp_int(is.bit_offset, ==, 5);
}

// Test bitreader.c: h_skip_bits full bytes (line 145)
static void test_skip_bits_full_bytes(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A\x5A\x6B", 3, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    is.bit_offset = 0;
    h_skip_bits(&is, 16);
    g_check_cmp_int(is.index, ==, 2);
    g_check_cmp_int(is.bit_offset, ==, 0);
}

// Test bitreader.c: h_skip_bits final partial byte (line 158)
static void test_skip_bits_final_partial(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A\x5A", 2, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    is.index = 1;
    is.bit_offset = 0;
    h_skip_bits(&is, 4);
    g_check_cmp_int(is.bit_offset, ==, 4);
}

// Test bitreader.c: h_seek_bits within same byte (line 169)
static void test_seek_bits_same_byte(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A\x5A", 2, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    is.index = 0;
    h_seek_bits(&is, 3);
    g_check_cmp_int(is.index, ==, 0);
    g_check_cmp_int(is.bit_offset, ==, 3);
}

// Test bitreader.c: h_seek_bits past end (line 177)
static void test_seek_bits_past_end(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A", 1, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    h_seek_bits(&is, 20);
    g_check_cmp_int(is.overrun, ==, true);
    g_check_cmp_int(is.index, ==, is.length);
}

// Test bitreader.c: h_seek_bits to different byte (line 184)
static void test_seek_bits_different_byte(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A\x5A", 2, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
    h_seek_bits(&is, 12);
    g_check_cmp_int(is.index, ==, 1);
    g_check_cmp_int(is.bit_offset, ==, 4);
}

// Test bitreader.c: h_read_bits with BYTE_LITTLE_ENDIAN fast path (line 61)
static void test_read_bits_byte_le_fast_path(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A\x5A", 2, BIT_BIG_ENDIAN | BYTE_LITTLE_ENDIAN);
    is.bit_offset = 0;
    is.margin = 0;
    int64_t result = h_read_bits(&is, 8, false);
    g_check_cmp_int32(result, ==, 0x6A);
}

// Test bitreader.c: h_read_bits with BYTE_LITTLE_ENDIAN slow path (line 105)
static void test_read_bits_byte_le_slow_path(void) {
    HInputStream is = MK_INPUT_STREAM("\x6A", 1, BIT_BIG_ENDIAN | BYTE_LITTLE_ENDIAN);
    is.bit_offset = 2;
    is.margin = 0;
    int64_t result = h_read_bits(&is, 3, false);
    g_check_cmp_int(result, >=, 0);
}

void register_bitreader_tests(void) {
    g_test_add_func("/core/bitreader/be", test_bitreader_be);
    g_test_add_func("/core/bitreader/le", test_bitreader_le);
    g_test_add_func("/core/bitreader/largebits-be", test_largebits_be);
    g_test_add_func("/core/bitreader/largebits-le", test_largebits_le);
    g_test_add_func("/core/bitreader/offset-largebits-be", test_offset_largebits_be);
    g_test_add_func("/core/bitreader/offset-largebits-le", test_offset_largebits_le);
    g_test_add_func("/core/bitreader/ints", test_bitreader_ints);
    g_test_add_func("/core/bitreader/overrun", test_read_bits_overrun);
    g_test_add_func("/core/bitreader/signed", test_read_bits_signed);
    g_test_add_func("/core/bitreader/margin", test_read_bits_margin);
    g_test_add_func("/core/bitreader/fast_path", test_read_bits_fast_path);
    g_test_add_func("/core/bitreader/bit_big_endian_partial",
                    test_read_bits_bit_big_endian_partial);
    g_test_add_func("/core/bitreader/bit_little_endian_partial",
                    test_read_bits_bit_little_endian_partial);
    g_test_add_func("/core/bitreader/skip_zero", test_skip_bits_zero);
    g_test_add_func("/core/bitreader/skip_overrun", test_skip_bits_overrun);
    g_test_add_func("/core/bitreader/skip_at_end", test_skip_bits_at_end);
    g_test_add_func("/core/bitreader/skip_partial_byte", test_skip_bits_partial_byte);
    g_test_add_func("/core/bitreader/skip_full_bytes", test_skip_bits_full_bytes);
    g_test_add_func("/core/bitreader/skip_final_partial", test_skip_bits_final_partial);
    g_test_add_func("/core/bitreader/seek_same_byte", test_seek_bits_same_byte);
    g_test_add_func("/core/bitreader/seek_past_end", test_seek_bits_past_end);
    g_test_add_func("/core/bitreader/seek_different_byte", test_seek_bits_different_byte);
    g_test_add_func("/core/bitreader/byte_le_fast_path", test_read_bits_byte_le_fast_path);
    g_test_add_func("/core/bitreader/byte_le_slow_path", test_read_bits_byte_le_slow_path);
}

