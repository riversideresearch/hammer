#include "hammer.h"
#include "test_suite.h"

#include <glib.h>

void test_ignoreseq_left(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_left(h_ch('a'), h_ch('b'));
    g_check_parse_match(p, be, "ab", 2, "u0x61");
    g_check_parse_failed(p, be, "xb", 2);
}

void test_ignoreseq_right(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_right(h_ch('a'), h_ch('b'));
    g_check_parse_match(p, be, "ab", 2, "u0x62");
    g_check_parse_failed(p, be, "ax", 2);
}

void test_ignoreseq_middle(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_middle(h_ch('('), h_ch('x'), h_ch(')'));
    g_check_parse_match(p, be, "(x)", 3, "u0x78");
    g_check_parse_failed(p, be, "(y]", 3);
}

void register_ignoreseq_tests(void) {
    g_test_add_data_func("/core/parser/packrat/ignoreseq/left", GINT_TO_POINTER(PB_PACKRAT),
                         test_ignoreseq_left);
    g_test_add_data_func("/core/parser/packrat/ignoreseq/right", GINT_TO_POINTER(PB_PACKRAT),
                         test_ignoreseq_right);
    g_test_add_data_func("/core/parser/packrat/ignoreseq/middle", GINT_TO_POINTER(PB_PACKRAT),
                         test_ignoreseq_middle);
}
