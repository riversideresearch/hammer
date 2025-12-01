#include "glue.h"
#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>

// Test missing.c: h_missing_parse
static void test_missing_parse(gconstpointer backend) {
    (void)backend;

    HParser *p = h_ch('a');
    int compile_result = h_compile(p, PB_INVALID, NULL);
    g_check_cmp_int(compile_result, ==, -1);

    // When compile fails, backend_vtable is not set, so we need to set it manually
    // to test the missing backend's parse function
    extern HParserBackendVTable h__missing_backend_vtable;
    p->backend_vtable = &h__missing_backend_vtable;
    p->backend = PB_INVALID;

    HParseResult *res = h_parse(p, (const uint8_t *)"a", 1);
    g_check_cmp_ptr(res, ==, NULL);
}

void register_missing_tests(void) {
    g_test_add_data_func("/core/parser/packrat/missing_parse", GINT_TO_POINTER(PB_PACKRAT),
                         test_missing_parse);
}
