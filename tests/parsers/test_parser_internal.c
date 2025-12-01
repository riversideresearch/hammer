#include "../../src/parsers/parser_internal.h"
#include "glue.h"
#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>

// Test parser_internal.h: token_length with NULL (line 29)
static void test_token_length_null(gconstpointer backend) {
    (void)backend;
    size_t len = token_length(NULL);
    g_check_cmp_int(len, ==, 0);
}

void register_parser_internal_tests(void) {
    g_test_add_data_func("/core/parser/packrat/token_length_null", GINT_TO_POINTER(PB_PACKRAT),
                         test_token_length_null);
}
