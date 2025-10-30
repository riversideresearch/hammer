#include "hammer.h"
#include "test_suite.h"

#include <glib.h>

void test_indirect_basic(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *ind = h_indirect();
    h_bind_indirect(ind, h_ch('z'));
    g_check_parse_match(ind, be, "z", 1, "u0x7a");
    g_check_parse_failed(ind, be, "x", 1);
}

void register_indirect_tests(void) {
    g_test_add_data_func("/core/parser/packrat/indirect/basic", GINT_TO_POINTER(PB_PACKRAT),
                         test_indirect_basic);
}
