#include "hammer.h"
#include "test_suite.h"

#include <glib.h>

void test_charset_in(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    const uint8_t opts[] = {'a', 'b', 'c'};
    HParser *p = h_in(opts, sizeof(opts));
    g_check_parse_match(p, be, "b", 1, "u0x62");
    g_check_parse_failed(p, be, "x", 1);
}

void test_charset_not_in(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    const uint8_t opts[] = {'a', 'b', 'c'};
    HParser *p = h_not_in(opts, sizeof(opts));
    g_check_parse_match(p, be, "x", 1, "u0x78");
    g_check_parse_failed(p, be, "a", 1);
}

void register_charset_tests(void) {
    g_test_add_data_func("/core/parser/packrat/charset/in", GINT_TO_POINTER(PB_PACKRAT),
                         test_charset_in);
    g_test_add_data_func("/core/parser/packrat/charset/not_in", GINT_TO_POINTER(PB_PACKRAT),
                         test_charset_not_in);
}
