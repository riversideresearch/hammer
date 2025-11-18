#include "hammer.h"
#include "glue.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>

// Test xor.c: want_suspend cases (lines 14, 20)
static void test_xor_want_suspend(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p1 = h_token((const uint8_t *)"ab", 2);
    HParser *p2 = h_token((const uint8_t *)"cd", 2);
    HParser *xor_p = h_xor(p1, p2);

    h_compile(xor_p, be, NULL);
    HSuspendedParser *s = h_parse_start(xor_p);
    if (s != NULL) {
        h_parse_chunk(s, (const uint8_t *)"a", 1);
        h_parse_chunk(s, (const uint8_t *)"b", 1);
        HParseResult *res = h_parse_finish(s);
        if (res) {
            h_parse_result_free(res);
        }
    }

    HParser *xor_p2 = h_xor(p1, p2);
    h_compile(xor_p2, be, NULL);
    HSuspendedParser *s2 = h_parse_start(xor_p2);
    if (s2 != NULL) {
        h_parse_chunk(s2, (const uint8_t *)"c", 1);
        h_parse_chunk(s2, (const uint8_t *)"d", 1);
        HParseResult *res2 = h_parse_finish(s2);
        if (res2) {
            h_parse_result_free(res2);
        }
    }
}

void register_xor_tests(void) {
    g_test_add_data_func("/core/parser/packrat/xor_want_suspend", GINT_TO_POINTER(PB_PACKRAT),
                         test_xor_want_suspend);
}
