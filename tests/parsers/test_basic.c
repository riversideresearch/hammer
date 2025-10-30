#include "hammer.h"
#include "test_suite.h"

#include <glib.h>
#include <string.h>

static void test_token(gconstpointer backend) {
    const HParser *token_ = h_token((const uint8_t *)"95\xa2", 3);
    g_check_parse_match(token_, (HParserBackend)GPOINTER_TO_INT(backend), "95\xa2", 3,
                        "<39.35.a2>");
    g_check_parse_failed(token_, (HParserBackend)GPOINTER_TO_INT(backend), "95", 2);
}

static void test_ch(gconstpointer backend) {
    const HParser *ch_ = h_ch(0xa2);
    g_check_parse_match(ch_, (HParserBackend)GPOINTER_TO_INT(backend), "\xa2", 1, "u0xa2");
    g_check_parse_failed(ch_, (HParserBackend)GPOINTER_TO_INT(backend), "\xa3", 1);
}

static void test_ch_range(gconstpointer backend) {
    const HParser *range_ = h_ch_range('a', 'c');
    g_check_parse_match(range_, (HParserBackend)GPOINTER_TO_INT(backend), "b", 1, "u0x62");
    g_check_parse_failed(range_, (HParserBackend)GPOINTER_TO_INT(backend), "d", 1);
}

static void test_bits0(gconstpointer backend) {
    const HParser *bits0_;
    bits0_ = h_bits(0, false);
    g_check_parse_match(bits0_, (HParserBackend)GPOINTER_TO_INT(backend), "", 0, "u0");
    bits0_ = h_bits(0, true);
    g_check_parse_match(bits0_, (HParserBackend)GPOINTER_TO_INT(backend), "", 0, "s0");
    bits0_ = h_sequence(h_bits(0, false), h_ch('a'), NULL);
    g_check_parse_match(bits0_, (HParserBackend)GPOINTER_TO_INT(backend), "a", 1, "(u0 u0x61)");
    bits0_ = h_sequence(h_bits(0, true), h_ch('a'), NULL);
    g_check_parse_match(bits0_, (HParserBackend)GPOINTER_TO_INT(backend), "a", 1, "(s0 u0x61)");
}

static void test_bits(gconstpointer backend) {
    const HParser *bits_;
    bits_ = h_bits(3, false);
    g_check_parse_match(bits_, (HParserBackend)GPOINTER_TO_INT(backend), "\0", 1, "u0");
    g_check_parse_match(bits_, (HParserBackend)GPOINTER_TO_INT(backend), "\xff", 1, "u0x7");
    g_check_parse_failed(bits_, (HParserBackend)GPOINTER_TO_INT(backend), "", 0);
    bits_ = h_bits(3, true);
    g_check_parse_match(bits_, (HParserBackend)GPOINTER_TO_INT(backend), "\0", 1, "s0");
    g_check_parse_match(bits_, (HParserBackend)GPOINTER_TO_INT(backend), "\xff", 1, "s-0x1");
    g_check_parse_failed(bits_, (HParserBackend)GPOINTER_TO_INT(backend), "", 0);
    bits_ = h_bits(9, false);
    g_check_parse_match(bits_, (HParserBackend)GPOINTER_TO_INT(backend), "\0\0", 2, "u0");
    g_check_parse_match(bits_, (HParserBackend)GPOINTER_TO_INT(backend), "\xff\xff", 2, "u0x1ff");
    g_check_parse_failed(bits_, (HParserBackend)GPOINTER_TO_INT(backend), "a", 1);
    bits_ = h_bits(9, true);
    g_check_parse_match(bits_, (HParserBackend)GPOINTER_TO_INT(backend), "\0\0", 2, "s0");
    g_check_parse_match(bits_, (HParserBackend)GPOINTER_TO_INT(backend), "\xff\xff", 2, "s-0x1");
    g_check_parse_failed(bits_, (HParserBackend)GPOINTER_TO_INT(backend), "a", 1);
}

static void test_bytes(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    const HParser *p;
    p = h_bytes(0);
    g_check_parse_match(p, be, "", 0, "<>");
    g_check_parse_match(p, be, "abc", 3, "<>");
    p = h_bytes(1);
    g_check_parse_failed(p, be, "", 0);
    g_check_parse_match(p, be, " ", 1, "<20>");
    g_check_parse_match(p, be, "abc", 3, "<61>");
    p = h_bytes(5);
    g_check_parse_failed(p, be, "", 0);
    g_check_parse_failed(p, be, "1", 1);
    g_check_parse_failed(p, be, "12", 2);
    g_check_parse_failed(p, be, "123", 3);
    g_check_parse_failed(p, be, "1234", 4);
    g_check_parse_match(p, be, "12345", 5, "<31.32.33.34.35>");
    g_check_parse_match(p, be, "12345abc", 8, "<31.32.33.34.35>");
}

void register_basic_parser_tests(void) {
    g_test_add_data_func("/core/parser/packrat/token", GINT_TO_POINTER(PB_PACKRAT), test_token);
    g_test_add_data_func("/core/parser/packrat/ch", GINT_TO_POINTER(PB_PACKRAT), test_ch);
    g_test_add_data_func("/core/parser/packrat/ch_range", GINT_TO_POINTER(PB_PACKRAT),
                         test_ch_range);
    g_test_add_data_func("/core/parser/packrat/bits0", GINT_TO_POINTER(PB_PACKRAT), test_bits0);
    g_test_add_data_func("/core/parser/packrat/bits", GINT_TO_POINTER(PB_PACKRAT), test_bits);
    g_test_add_data_func("/core/parser/packrat/bytes", GINT_TO_POINTER(PB_PACKRAT), test_bytes);
}


