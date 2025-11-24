#include "hammer.h"
#include "glue.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>
#include <stdarg.h>

// Test permutation.c: want_suspend case (line 31)
static void test_permutation_want_suspend(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p1 = h_ch('a');
    HParser *p2 = h_token((const uint8_t *)"bc", 2);
    HParser *perm = h_permutation(p1, p2, NULL);

    h_compile(perm, be, NULL);
    HSuspendedParser *s = h_parse_start(perm);
    if (s != NULL) {
        h_parse_chunk(s, (const uint8_t *)"a", 1);
        h_parse_chunk(s, (const uint8_t *)"bc", 2);
        HParseResult *res = h_parse_finish(s);
        if (res) {
            h_parse_result_free(res);
        }
    }
}

// Test permutation.c: h_permutation__m (lines 119-124)
static void test_permutation_m(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p1 = h_ch('a');
    HParser *p2 = h_ch('b');
    HParser *perm = h_permutation__m(&system_allocator, p1, p2, NULL);

    g_check_parse_match(perm, be, "ab", 2, "(u0x61 u0x62)");
    g_check_parse_match(perm, be, "ba", 2, "(u0x61 u0x62)");
}

// Helper function to test h_permutation__v
static HParser *test_permutation_v_helper(HParser *p1, ...) {
    va_list ap;
    va_start(ap, p1);
    HParser *result = h_permutation__v(p1, ap);
    va_end(ap);
    return result;
}

// Test permutation.c: h_permutation__v (lines 127-129)
static void test_permutation_v(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p1 = h_ch('a');
    HParser *p2 = h_ch('b');
    HParser *perm = test_permutation_v_helper(p1, p2, NULL);

    g_check_parse_match(perm, be, "ab", 2, "(u0x61 u0x62)");
}

// Test permutation.c: h_permutation__a and h_permutation__ma (lines 158-183)
static void test_permutation_a(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p1 = h_ch('a');
    HParser *p2 = h_ch('b');
    HParser *args[] = {p1, p2, NULL};

    HParser *perm_a = h_permutation__a((void **)args);
    g_check_parse_match(perm_a, be, "ab", 2, "(u0x61 u0x62)");

    HParser *perm_ma = h_permutation__ma(&system_allocator, (void **)args);
    g_check_parse_match(perm_ma, be, "ba", 2, "(u0x61 u0x62)");
}

void register_permutation_tests(void) {
    g_test_add_data_func("/core/parser/packrat/permutation_want_suspend", GINT_TO_POINTER(PB_PACKRAT),
                         test_permutation_want_suspend);
    g_test_add_data_func("/core/parser/packrat/permutation_m", GINT_TO_POINTER(PB_PACKRAT),
                         test_permutation_m);
    g_test_add_data_func("/core/parser/packrat/permutation_v", GINT_TO_POINTER(PB_PACKRAT),
                         test_permutation_v);
    g_test_add_data_func("/core/parser/packrat/permutation_a", GINT_TO_POINTER(PB_PACKRAT),
                         test_permutation_a);
}
