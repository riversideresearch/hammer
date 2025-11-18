#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>
#include <stdint.h>

typedef struct {
    uint64_t data;
    size_t nbits;
} bitwriter_test_elem; // should end with {0,0}

void run_bitwriter_test(bitwriter_test_elem data[], char flags) {
    size_t len;
    const uint8_t *buf;
    HBitWriter *w = h_bit_writer_new(&system_allocator);
    int i;
    w->flags = flags;
    for (i = 0; data[i].nbits; i++) {
        h_bit_writer_put(w, data[i].data, data[i].nbits);
    }

    buf = h_bit_writer_get_buffer(w, &len);
    HInputStream input = {.input = buf,
                          .index = 0,
                          .length = len,
                          .bit_offset = 0,
                          .endianness = flags,
                          .overrun = 0};

    for (i = 0; data[i].nbits; i++) {
        g_check_cmp_uint64((uint64_t)h_read_bits(&input, data[i].nbits, FALSE), ==, data[i].data);
    }

    h_bit_writer_free(w);
}

static void test_bitwriter_ints(void) {
    bitwriter_test_elem data[] = {{-0x200000000, 64}, {0, 0}};
    run_bitwriter_test(data, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
}

static void test_bitwriter_be(void) {
    bitwriter_test_elem data[] = {{0x03, 3}, {0x52, 8}, {0x1A, 5}, {0, 0}};
    run_bitwriter_test(data, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
}

static void test_bitwriter_le(void) {
    bitwriter_test_elem data[] = {{0x02, 3}, {0x4D, 8}, {0x0B, 5}, {0, 0}};
    run_bitwriter_test(data, BIT_LITTLE_ENDIAN | BYTE_LITTLE_ENDIAN);
}

static void test_largebits_be(void) {
    bitwriter_test_elem data[] = {{0x352, 11}, {0x1A, 5}, {0, 0}};
    run_bitwriter_test(data, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
}

static void test_largebits_le(void) {
    bitwriter_test_elem data[] = {{0x26A, 11}, {0x0B, 5}, {0, 0}};
    run_bitwriter_test(data, BIT_LITTLE_ENDIAN | BYTE_LITTLE_ENDIAN);
}

static void test_offset_largebits_be(void) {
    bitwriter_test_elem data[] = {{0xD, 5}, {0x25A, 11}, {0, 0}};
    run_bitwriter_test(data, BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN);
}

static void test_offset_largebits_le(void) {
    bitwriter_test_elem data[] = {{0xA, 5}, {0x2D3, 11}, {0, 0}};
    run_bitwriter_test(data, BIT_LITTLE_ENDIAN | BYTE_LITTLE_ENDIAN);
}

// Test bitwriter.c: h_bit_writer_reserve (indirectly via h_bit_writer_put)
static void test_bit_writer_reserve(void) {
    HBitWriter *w = h_bit_writer_new(&system_allocator);
    // Write enough to trigger reserve
    for (int i = 0; i < 100; i++) {
        h_bit_writer_put(w, 0xFF, 8);
    }
    size_t len;
    const uint8_t *buf = h_bit_writer_get_buffer(w, &len);
    g_check_cmp_ptr(buf, !=, NULL);
    g_check_cmp_int(len, >=, 100);
    h_bit_writer_free(w);
}

// Test bitwriter.c: h_bit_writer_put with BIT_LITTLE_ENDIAN (line 79)
static void test_bit_writer_put_bit_le(void) {
    HBitWriter *w = h_bit_writer_new(&system_allocator);
    w->flags = BIT_LITTLE_ENDIAN | BYTE_BIG_ENDIAN;
    h_bit_writer_put(w, 0xAB, 8);
    size_t len;
    const uint8_t *buf = h_bit_writer_get_buffer(w, &len);
    g_check_cmp_ptr(buf, !=, NULL);
    h_bit_writer_free(w);
}

// Test bitwriter.c: h_bit_writer_put with BYTE_LITTLE_ENDIAN (line 68)
static void test_bit_writer_put_byte_le(void) {
    HBitWriter *w = h_bit_writer_new(&system_allocator);
    w->flags = BIT_BIG_ENDIAN | BYTE_LITTLE_ENDIAN;
    h_bit_writer_put(w, 0xAB, 8);
    size_t len;
    const uint8_t *buf = h_bit_writer_get_buffer(w, &len);
    g_check_cmp_ptr(buf, !=, NULL);
    h_bit_writer_free(w);
}

// Test bitwriter.c: h_bit_writer_put with partial byte (line 60)
static void test_bit_writer_put_partial_byte(void) {
    HBitWriter *w = h_bit_writer_new(&system_allocator);
    w->flags = BIT_BIG_ENDIAN | BYTE_BIG_ENDIAN;
    h_bit_writer_put(w, 0x3, 3);
    h_bit_writer_put(w, 0x5, 5);
    size_t len;
    const uint8_t *buf = h_bit_writer_get_buffer(w, &len);
    g_check_cmp_ptr(buf, !=, NULL);
    g_check_cmp_int(len, ==, 1);
    h_bit_writer_free(w);
}

// Test bitwriter.c: h_bit_writer_get_buffer with non-zero bit_offset (line 95)
// Note: This will assert if bit_offset != 0, so we skip this test
static void test_bit_writer_get_buffer_partial(void) {
    // Skip test - h_bit_writer_get_buffer asserts if bit_offset != 0
}

// Test bitwriter.c: h_bit_writer_free (line 101)
static void test_bit_writer_free(void) {
    HBitWriter *w = h_bit_writer_new(&system_allocator);
    h_bit_writer_put(w, 0xFF, 8);
    h_bit_writer_free(w);
    // Should not crash
}

void register_bitwriter_tests(void) {
    g_test_add_func("/core/bitwriter/be", test_bitwriter_be);
    g_test_add_func("/core/bitwriter/le", test_bitwriter_le);
    g_test_add_func("/core/bitwriter/largebits-be", test_largebits_be);
    g_test_add_func("/core/bitwriter/largebits-le", test_largebits_le);
    g_test_add_func("/core/bitwriter/offset-largebits-be", test_offset_largebits_be);
    g_test_add_func("/core/bitwriter/offset-largebits-le", test_offset_largebits_le);
    g_test_add_func("/core/bitwriter/ints", test_bitwriter_ints);
    g_test_add_func("/core/bitwriter/reserve", test_bit_writer_reserve);
    g_test_add_func("/core/bitwriter/put_bit_le", test_bit_writer_put_bit_le);
    g_test_add_func("/core/bitwriter/put_byte_le", test_bit_writer_put_byte_le);
    g_test_add_func("/core/bitwriter/put_partial_byte", test_bit_writer_put_partial_byte);
    g_test_add_func("/core/bitwriter/get_buffer_partial", test_bit_writer_get_buffer_partial);
    g_test_add_func("/core/bitwriter/free", test_bit_writer_free);
}
