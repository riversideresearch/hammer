#include "glue.h"
#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>

// Helper function for whitespace tests
static HParser *simple_bind_cont(HAllocator *mm__, const HParsedToken *token, void *user_data) {
    (void)mm__;
    (void)user_data;
    (void)token;
    return h_ch('b');
}

// Test whitespace.c: want_suspend case (line 13)
static void test_whitespace_want_suspend(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *ws = h_whitespace(h_ch('a'));

    h_compile(ws, be, NULL);
    HSuspendedParser *s = h_parse_start(ws);
    if (s != NULL) {
        h_parse_chunk(s, (const uint8_t *)" ", 1);
        h_parse_chunk(s, (const uint8_t *)"a", 1);
        HParseResult *res = h_parse_finish(s);
        if (res) {
            h_parse_result_free(res);
        }
    }
}

// Test whitespace.c: ws_isValidRegular (lines 49-52)
static void test_whitespace_isValidRegular(gconstpointer backend) {
    (void)backend;

    HParser *p = h_ch('a');
    HParser *ws = h_whitespace(p);

    bool is_regular = ws->vtable->isValidRegular(ws->env);
    g_check_cmp_int(is_regular, ==, true);

    HParser *bind_p = h_bind(h_ch('a'), simple_bind_cont, NULL);
    HParser *ws2 = h_whitespace(bind_p);
    bool is_regular2 = ws2->vtable->isValidRegular(ws2->env);
    g_check_cmp_int(is_regular2, ==, false);
}

// Test whitespace.c: ws_isValidCF (lines 54-57)
static void test_whitespace_isValidCF(gconstpointer backend) {
    (void)backend;

    HParser *p = h_ch('a');
    HParser *ws = h_whitespace(p);

    bool is_cf = ws->vtable->isValidCF(ws->env);
    g_check_cmp_int(is_cf, ==, true);

    HParser *bind_p = h_bind(h_ch('a'), simple_bind_cont, NULL);
    HParser *ws2 = h_whitespace(bind_p);
    bool is_cf2 = ws2->vtable->isValidCF(ws2->env);
    g_check_cmp_int(is_cf2, ==, false);
}

void register_whitespace_tests(void) {
    g_test_add_data_func("/core/parser/packrat/whitespace_want_suspend",
                         GINT_TO_POINTER(PB_PACKRAT), test_whitespace_want_suspend);
    g_test_add_data_func("/core/parser/packrat/whitespace_isValidRegular",
                         GINT_TO_POINTER(PB_PACKRAT), test_whitespace_isValidRegular);
    g_test_add_data_func("/core/parser/packrat/whitespace_isValidCF", GINT_TO_POINTER(PB_PACKRAT),
                         test_whitespace_isValidCF);
}
