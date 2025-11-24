#include "hammer.h"
#include "test_suite.h"

#include <glib.h>

static void test_int64(gconstpointer backend) {
    const HParser *int64_ = h_int64();
    g_check_parse_match(int64_, (HParserBackend)GPOINTER_TO_INT(backend),
                        "\xff\xff\xff\xfe\x00\x00\x00\x00", 8, "s-0x200000000");
    g_check_parse_failed(int64_, (HParserBackend)GPOINTER_TO_INT(backend),
                         "\xff\xff\xff\xfe\x00\x00\x00", 7);
}

static void test_int32(gconstpointer backend) {
    const HParser *int32_ = h_int32();
    g_check_parse_match(int32_, (HParserBackend)GPOINTER_TO_INT(backend), "\xff\xfe\x00\x00", 4,
                        "s-0x20000");
    g_check_parse_failed(int32_, (HParserBackend)GPOINTER_TO_INT(backend), "\xff\xfe\x00", 3);
    g_check_parse_match(int32_, (HParserBackend)GPOINTER_TO_INT(backend), "\x00\x02\x00\x00", 4,
                        "s0x20000");
    g_check_parse_failed(int32_, (HParserBackend)GPOINTER_TO_INT(backend), "\x00\x02\x00", 3);
}

static void test_int16(gconstpointer backend) {
    const HParser *int16_ = h_int16();
    g_check_parse_match(int16_, (HParserBackend)GPOINTER_TO_INT(backend), "\xfe\x00", 2, "s-0x200");
    g_check_parse_failed(int16_, (HParserBackend)GPOINTER_TO_INT(backend), "\xfe", 1);
    g_check_parse_match(int16_, (HParserBackend)GPOINTER_TO_INT(backend), "\x02\x00", 2, "s0x200");
    g_check_parse_failed(int16_, (HParserBackend)GPOINTER_TO_INT(backend), "\x02", 1);
}

static void test_int8(gconstpointer backend) {
    const HParser *int8_ = h_int8();
    g_check_parse_match(int8_, (HParserBackend)GPOINTER_TO_INT(backend), "\x88", 1, "s-0x78");
    g_check_parse_failed(int8_, (HParserBackend)GPOINTER_TO_INT(backend), "", 0);
}

static void test_uint64(gconstpointer backend) {
    const HParser *uint64_ = h_uint64();
    g_check_parse_match(uint64_, (HParserBackend)GPOINTER_TO_INT(backend),
                        "\x00\x00\x00\x02\x00\x00\x00\x00", 8, "u0x200000000");
    g_check_parse_failed(uint64_, (HParserBackend)GPOINTER_TO_INT(backend),
                         "\x00\x00\x00\x02\x00\x00\x00", 7);
}

static void test_uint32(gconstpointer backend) {
    const HParser *uint32_ = h_uint32();
    g_check_parse_match(uint32_, (HParserBackend)GPOINTER_TO_INT(backend), "\x00\x02\x00\x00", 4,
                        "u0x20000");
    g_check_parse_failed(uint32_, (HParserBackend)GPOINTER_TO_INT(backend), "\x00\x02\x00", 3);
}

static void test_uint16(gconstpointer backend) {
    const HParser *uint16_ = h_uint16();
    g_check_parse_match(uint16_, (HParserBackend)GPOINTER_TO_INT(backend), "\x02\x00", 2, "u0x200");
    g_check_parse_failed(uint16_, (HParserBackend)GPOINTER_TO_INT(backend), "\x02", 1);
}

static void test_uint8(gconstpointer backend) {
    const HParser *uint8_ = h_uint8();
    g_check_parse_match(uint8_, (HParserBackend)GPOINTER_TO_INT(backend), "\x78", 1, "u0x78");
    g_check_parse_failed(uint8_, (HParserBackend)GPOINTER_TO_INT(backend), "", 0);
}

static void test_int_range(gconstpointer backend) {
    const HParser *int_range_ = h_int_range(h_uint8(), 3, 10);
    g_check_parse_match(int_range_, (HParserBackend)GPOINTER_TO_INT(backend), "\x05", 1, "u0x5");
    g_check_parse_failed(int_range_, (HParserBackend)GPOINTER_TO_INT(backend), "\xb", 1);
}

void register_integer_parser_tests(void) {
    g_test_add_data_func("/core/parser/packrat/int64", GINT_TO_POINTER(PB_PACKRAT), test_int64);
    g_test_add_data_func("/core/parser/packrat/int32", GINT_TO_POINTER(PB_PACKRAT), test_int32);
    g_test_add_data_func("/core/parser/packrat/int16", GINT_TO_POINTER(PB_PACKRAT), test_int16);
    g_test_add_data_func("/core/parser/packrat/int8", GINT_TO_POINTER(PB_PACKRAT), test_int8);
    g_test_add_data_func("/core/parser/packrat/uint64", GINT_TO_POINTER(PB_PACKRAT), test_uint64);
    g_test_add_data_func("/core/parser/packrat/uint32", GINT_TO_POINTER(PB_PACKRAT), test_uint32);
    g_test_add_data_func("/core/parser/packrat/uint16", GINT_TO_POINTER(PB_PACKRAT), test_uint16);
    g_test_add_data_func("/core/parser/packrat/uint8", GINT_TO_POINTER(PB_PACKRAT), test_uint8);
    g_test_add_data_func("/core/parser/packrat/int_range", GINT_TO_POINTER(PB_PACKRAT),
                         test_int_range);
}


