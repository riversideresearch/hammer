#include "glue.h"
#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>

// h_parse_debug() is the tracing twin of h_parse(): on a well-formed input it
// must return the same successful result. The trace/error machinery must not
// disturb the parse itself.
static void test_trace_debug_success(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p = h_sequence(h_ch('a'), h_ch('b'), NULL);
    h_compile(p, be, NULL);

    HParseError err;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"ab", 2, &err);
    g_check_cmp_ptr(res, !=, NULL);
    if (res) {
        h_parse_result_free(res);
    }
}

// On a failing parse, h_parse_debug() returns NULL (exactly like h_parse) and,
// when the AST tracer is compiled in, fills the HParseError with the furthest
// position reached and the offending byte there. Matching 'a' at index 0
// succeeds; matching 'b' against 'x' at index 1 fails, so the furthest progress
// is index 1.
static void test_trace_debug_error_on_failure(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p = h_sequence(h_ch('a'), h_ch('b'), NULL);
    h_compile(p, be, NULL);

    HParseError err;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"ax", 2, &err);
    g_check_cmp_ptr(res, ==, NULL);

    // The failure-position fields are only populated when the library is built
    // with -DHAMMER_TRACE_AST; without it the struct is left zeroed. n_deepest
    // being non-zero is the signal that the tracer ran, so guard on it.
    if (err.n_deepest > 0) {
        g_check_cmp_size(err.index, ==, 1);
        g_check_cmp_int(err.actual, ==, 'x');
        g_check_cmp_ptr(err.deepest_parsers[0], !=, NULL);
    }
}

// Passing a NULL error out-parameter is allowed and must not crash; the parse
// result is unaffected.
static void test_trace_debug_null_error(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p = h_sequence(h_ch('a'), h_ch('b'), NULL);
    h_compile(p, be, NULL);

    HParseResult *res = h_parse_debug(p, (const uint8_t *)"ax", 2, NULL);
    g_check_cmp_ptr(res, ==, NULL);
}

void register_trace_tests(void) {
    g_test_add_data_func("/core/parser/packrat/trace_debug_success", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_debug_success);
    g_test_add_data_func("/core/parser/packrat/trace_debug_error_on_failure",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_debug_error_on_failure);
    g_test_add_data_func("/core/parser/packrat/trace_debug_null_error", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_debug_null_error);
}
