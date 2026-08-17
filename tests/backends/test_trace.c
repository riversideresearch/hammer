#include "glue.h"
#include "hammer.h"
#include "internal.h"
#include "test_suite.h"
#include "trace.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

// On a well-formed input it must return the same successful result.
// The trace/error machinery must not disturb the parse itself.
// the
static void test_trace_debug_success(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p = h_sequence(h_ch('a'), h_ch('b'), NULL);
    h_compile(p, be, NULL);

    HParseDiagnostic *diagnostic;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"ab", 2, &diagnostic, true);
    g_check_cmp_ptr(res, !=, NULL);
    if (res) {
        h_parse_result_free(res);
    }
    const HParseError *err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_size(err->n_deepest, ==, 0);
    g_check_cmp_size(err->index, ==, 0);
    h_parse_diagnostic_free(diagnostic);
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

    HParseDiagnostic *diagnostic;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"ax", 2, &diagnostic, true);
    g_check_cmp_ptr(res, ==, NULL);

    // The failure-position fields are only populated when the library is built
    // with -DHAMMER_TRACE_AST; without it the struct is left zeroed. n_deepest
    // being non-zero is the signal that the tracer ran, so guard on it.
    const HParseError *err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_size(err->n_deepest, >, 0);
    if (err->n_deepest > 0) {
        g_check_cmp_size(err->index, ==, 1);
        g_check_cmp_int(err->actual, ==, 'x');
        g_check_cmp_int(err->has_actual, ==, true);
        g_check_cmp_int(err->kind, ==, H_PARSE_ERROR_PRIMITIVE_MISMATCH);
        g_check_cmp_ptr(err->deepest_parsers[0], !=, NULL);
    }
    h_parse_diagnostic_free(diagnostic);
}

// Passing a NULL error out-parameter is allowed and must not crash; the parse
// result is unaffected.
static void test_trace_debug_null_error(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p = h_sequence(h_ch('a'), h_ch('b'), NULL);
    h_compile(p, be, NULL);

    HParseResult *res = h_parse_debug(p, (const uint8_t *)"ax", 2, NULL, true);
    g_check_cmp_ptr(res, ==, NULL);
}

static void test_trace_cf_unexpected_eof(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_sequence(h_ch('A'), h_ch('B'), h_ch('C'), NULL);
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"AB", 2, &diagnostic, true);
    const HParseError *err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_size(err->index, ==, 2);
    g_check_cmp_size(err->end_index, ==, 2);
    g_check_cmp_int(err->has_actual, ==, false);
    g_check_cmp_int(err->kind, ==, H_PARSE_ERROR_UNEXPECTED_EOF);
    g_check_cmp_size(err->n_deepest, >, 0);
    h_parse_diagnostic_free(diagnostic);
}

/* A zero-length input may use a NULL buffer-> Diagnostics must classify the
 * failure as EOF and must never attempt to fetch input[0]. */
static void test_trace_empty_input_eof(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_ch('A');
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(p, NULL, 0, &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_UNEXPECTED_EOF);
    g_check_cmp_size(error->index, ==, 0);
    g_check_cmp_size(error->end_index, ==, 0);
    g_check_cmp_int(error->has_actual, ==, false);

    HParseExpectation expected;
    g_check_cmp_size(h_parse_diagnostic_expected_count(diagnostic), ==, 1);
    g_check_cmp_int(h_parse_diagnostic_expected(diagnostic, 0, &expected), ==, true);
    g_check_cmp_int(expected.kind, ==, H_PARSE_EXPECT_BYTE_RANGE);
    g_check_cmp_int(expected.lower, ==, 'A');
    g_check_cmp_int(expected.upper, ==, 'A');
    h_parse_diagnostic_free(diagnostic);
}

typedef struct TraceThreadCase_ {
    const HParser *parser;
    uint8_t input[2];
    uint8_t actual;
    bool passed;
} TraceThreadCase;

static gpointer trace_concurrent_worker(gpointer user_data) {
    TraceThreadCase *test = user_data;
    test->passed = true;
    for (size_t i = 0; i < 200; i++) {
        HParseDiagnostic *diagnostic;
        HParseResult *result =
            h_parse_debug(test->parser, test->input, sizeof(test->input), &diagnostic, false);
        const HParseError *error = h_parse_diagnostic_error(diagnostic);
        if (result || error->kind != H_PARSE_ERROR_PRIMITIVE_MISMATCH || error->index != 1 ||
            !error->has_actual || error->actual != test->actual || !error->parser ||
            strcmp(error->parser, "h_ch") != 0) {
            test->passed = false;
        }
        if (result)
            h_parse_result_free(result);
        h_parse_diagnostic_free(diagnostic);
        if (!test->passed)
            break;
    }
    return NULL;
}

static void test_trace_concurrent_parse_isolation(void) {
    HParser *left = h_sequence(h_ch('A'), h_ch('B'), NULL);
    HParser *right = h_sequence(h_ch('X'), h_ch('Y'), NULL);
    g_check_cmp_int(h_compile(left, PB_PACKRAT, NULL), ==, 0);
    g_check_cmp_int(h_compile(right, PB_PACKRAT, NULL), ==, 0);

    TraceThreadCase left_case = {left, {'A', '!'}, '!', false};
    TraceThreadCase right_case = {right, {'X', '?'}, '?', false};
    GThread *left_thread = g_thread_new("trace-left", trace_concurrent_worker, &left_case);
    GThread *right_thread = g_thread_new("trace-right", trace_concurrent_worker, &right_case);
    g_thread_join(left_thread);
    g_thread_join(right_thread);

    g_check_cmp_int(left_case.passed, ==, true);
    g_check_cmp_int(right_case.passed, ==, true);
}

static void test_trace_formatter_with_input(void) {
    HParser *parser = h_sequence(h_ch('A'), h_ch('B'), h_ch('C'), NULL);
    const uint8_t input[] = {'A', 'B', 'X'};
    g_check_cmp_int(h_compile(parser, PB_PACKRAT, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    size_t execution_length = 0;
    const char *execution_trace = h_parse_diagnostic_execution_trace(diagnostic, &execution_length);
    g_check_cmp_ptr(execution_trace, !=, NULL);
    g_check_cmp_size(execution_length, >, 0);
    if (execution_trace) {
        g_check_cmp_ptr(strstr(execution_trace, "h_packrat_parse: begin"), !=, NULL);
        g_check_cmp_ptr(strstr(execution_trace, "-> h_sequence"), !=, NULL);
        g_check_cmp_ptr(strstr(execution_trace, "h_packrat_parse: end (FAILURE)"), !=, NULL);
    }

    FILE *stream = tmpfile();
    g_check_cmp_ptr(stream, !=, NULL);
    if (stream) {
        char rendered[2048] = {0};
        h_parse_diagnostic_fprint_with_input(stream, diagnostic, input, sizeof(input));
        rewind(stream);
        size_t length = fread(rendered, 1, sizeof(rendered) - 1, stream);
        rendered[length] = '\0';
        g_check_cmp_ptr(strstr(rendered, "unexpected byte 'X'"), !=, NULL);
        g_check_cmp_ptr(strstr(rendered, "input trail:"), !=, NULL);
        g_check_cmp_ptr(strstr(rendered, "input context (3 bytes)"), !=, NULL);
        g_check_cmp_ptr(strstr(rendered, "41 42 58"), !=, NULL);
        fclose(stream);
    }
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_flag_prints_condensed_report(void) {
    if (g_test_subprocess()) {
        HParser *parser = h_sequence(h_ch('A'), h_ch('B'), h_ch('C'), NULL);
        const uint8_t input[] = {'A', 'B', 'X'};
        g_assert_cmpint(h_compile(parser, PB_PACKRAT, NULL), ==, 0);

        HParseDiagnostic *diagnostic = NULL;
        HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, true);
        g_assert_null(result);
        g_assert_nonnull(diagnostic);
        size_t trace_length = 0;
        g_assert_nonnull(h_parse_diagnostic_execution_trace(diagnostic, &trace_length));
        g_assert_cmpuint(trace_length, >, 0);
        h_parse_diagnostic_free(diagnostic);
        return;
    }

    g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_passed();
    g_test_trap_assert_stderr("*error: unexpected byte 'X'*input trail:*input context (3 bytes)*");
}

static void test_trace_cf_range_failure(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_int_range(h_ch('A'), 'B', 'Z');
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"A", 1, &diagnostic, true);
    g_check_cmp_ptr(res, ==, NULL);
    const HParseError *err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_size(err->index, ==, 0);
    g_check_cmp_size(err->end_index, ==, 1);
    g_check_cmp_int(err->actual, ==, 'A');
    g_check_cmp_int(err->has_actual, ==, true);
    g_check_cmp_int(err->kind, ==, H_PARSE_ERROR_RANGE);
    g_check_string(err->parser, ==, "h_int_range");
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_float_range_failure(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_float_range(h_float32(), 1.0, 2.0);
    const uint8_t input[] = {0x40, 0x20, 0x00, 0x00}; /* 2.5 */
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;
    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_RANGE);
    g_check_cmp_size(error->index, ==, 0);
    g_check_cmp_size(error->end_index, ==, 4);
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_float_range_token_type_mismatch(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_float_range(h_ch('A'), 0.0, 1.0);
    const uint8_t input[] = {'A'};
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;
    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_RANGE);
    g_check_cmp_size(error->index, ==, 0);
    g_check_cmp_size(error->end_index, ==, 1);

    FILE *stream = tmpfile();
    g_check_cmp_ptr(stream, !=, NULL);
    if (stream) {
        char rendered[256] = {0};
        h_parse_diagnostic_fprint(stream, diagnostic);
        rewind(stream);
        size_t rendered_len = fread(rendered, 1, sizeof(rendered) - 1, stream);
        rendered[rendered_len] = '\0';
        g_check_cmp_ptr(strstr(rendered, "mismatched token type"), !=, NULL);
        fclose(stream);
    }
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_choice_trie(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *left = H_CONTEXT(h_ch('A'), "left");
    HParser *right = H_CONTEXT(h_ch('B'), "right");
    HParser *parser = H_CONTEXT(h_choice(left, right, NULL), "letter");
    const uint8_t input[] = {'X'};
    g_check_cmp_int(h_compile(parser, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_string(error->parser, ==, "letter");
    g_check_cmp_size(error->index, ==, 0);
    g_check_cmp_size(diagnostic->choice_node_count, ==, 1);
    g_check_cmp_size(diagnostic->choice_alternative_count, ==, 2);
    g_check_cmp_size(diagnostic->choice_root, ==, 0);
    g_check_cmp_size(diagnostic->choice_nodes[0].alternative_count, ==, 2);
    g_check_cmp_int(diagnostic->expected_bytes['A'], ==, true);
    g_check_cmp_int(diagnostic->expected_bytes['B'], ==, true);

    FILE *stream = tmpfile();
    g_check_cmp_ptr(stream, !=, NULL);
    if (stream) {
        char rendered[1024] = {0};
        h_parse_diagnostic_fprint(stream, diagnostic);
        rewind(stream);
        size_t rendered_len = fread(rendered, 1, sizeof(rendered) - 1, stream);
        rendered[rendered_len] = '\0';
        g_check_cmp_ptr(strstr(rendered, "no alternative matched at index 0"), !=, NULL);
        g_check_cmp_ptr(strstr(rendered, "alternatives:"), !=, NULL);
        g_check_cmp_ptr(strstr(rendered, "1. [left] unexpected byte 'X' (0x58 = 88) at index 0; "
                                         "expected 'A'"),
                        !=, NULL);
        g_check_cmp_ptr(strstr(rendered, "2. [right] unexpected byte 'X' (0x58 = 88) at index 0; "
                                         "expected 'B'"),
                        !=, NULL);
        fclose(stream);
    }
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_choice_leaf_range_detail_packrat(void) {
    HParser *small = H_CONTEXT(h_int_range(h_uint8(), 10, 20), "small");
    HParser *large = H_CONTEXT(h_int_range(h_uint8(), 30, 40), "large");
    HParser *parser = H_CONTEXT(h_choice(small, large, NULL), "number");
    const uint8_t input[] = {25};

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
    g_assert_null(result);
    g_assert_nonnull(diagnostic);

    FILE *stream = tmpfile();
    g_assert_nonnull(stream);
    char rendered[2048] = {0};
    h_parse_diagnostic_fprint(stream, diagnostic);
    rewind(stream);
    size_t rendered_len = fread(rendered, 1, sizeof(rendered) - 1, stream);
    rendered[rendered_len] = '\0';
    g_assert_nonnull(strstr(rendered, "no alternative matched at index 0"));
    g_assert_null(strstr(rendered, "at index 0 at index 0"));
    g_assert_nonnull(strstr(rendered,
                            "1. [small] unexpected int 25 at index 0; expected value between "
                            "10 and 20"));
    g_assert_nonnull(strstr(rendered,
                            "2. [large] unexpected int 25 at index 0; expected value between "
                            "30 and 40"));
    fclose(stream);
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_choice_leaf_short_input_packrat(void) {
    HParser *wide = H_CONTEXT(h_bits(12, false), "wide");
    HParser *wider = H_CONTEXT(h_bits(16, false), "wider");
    HParser *parser = H_CONTEXT(h_choice(wide, wider, NULL), "bit-field");
    const uint8_t input[] = {0xff};

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
    g_assert_null(result);
    g_assert_nonnull(diagnostic);

    FILE *stream = tmpfile();
    g_assert_nonnull(stream);
    char rendered[2048] = {0};
    h_parse_diagnostic_fprint(stream, diagnostic);
    rewind(stream);
    size_t rendered_len = fread(rendered, 1, sizeof(rendered) - 1, stream);
    rendered[rendered_len] = '\0';
    g_assert_nonnull(strstr(rendered, "1. [wide] ran out of bits to parse at index 0"));
    g_assert_nonnull(strstr(rendered, "2. [wider] ran out of bits to parse at index 0"));
    fclose(stream);
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_choice_trie_furthest_packrat(void) {
    HParser *near = H_CONTEXT(h_sequence(h_ch('A'), h_ch('B'), h_ch('X'), NULL), "near");
    HParser *shorter = H_CONTEXT(h_sequence(h_ch('A'), h_ch('Y'), NULL), "shorter");
    HParser *parser = H_CONTEXT(h_choice(near, shorter, NULL), "branch");
    const uint8_t input[] = {'A', 'B', '?'};

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_string(error->parser, ==, "branch");
    g_check_cmp_size(error->index, ==, 2);
    g_check_cmp_size(diagnostic->choice_alternative_count, ==, 2);
    g_check_cmp_int(diagnostic->expected_bytes['X'], ==, true);
    g_check_cmp_int(diagnostic->expected_bytes['Y'], ==, false);
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_choice_trie_nested(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *inner =
        H_CONTEXT(h_choice(H_CONTEXT(h_ch('A'), "A"), H_CONTEXT(h_ch('B'), "B"), NULL), "inner");
    HParser *outer = H_CONTEXT(h_choice(inner, H_CONTEXT(h_ch('C'), "C"), NULL), "outer");
    const uint8_t input[] = {'X'};
    g_check_cmp_int(h_compile(outer, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(outer, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    g_check_cmp_size(diagnostic->choice_node_count, ==, 2);
    g_check_cmp_size(diagnostic->choice_root, ==, 1);
    size_t first = diagnostic->choice_nodes[diagnostic->choice_root].first_alternative;
    g_check_cmp_size(first, <, diagnostic->choice_alternative_count);
    if (first < diagnostic->choice_alternative_count)
        g_check_cmp_size(diagnostic->choice_alternatives[first].child_node, ==, 0);

    FILE *stream = tmpfile();
    g_check_cmp_ptr(stream, !=, NULL);
    if (stream) {
        char rendered[2048] = {0};
        h_parse_diagnostic_fprint(stream, diagnostic);
        rewind(stream);
        size_t rendered_len = fread(rendered, 1, sizeof(rendered) - 1, stream);
        rendered[rendered_len] = '\0';
        g_check_cmp_ptr(strstr(rendered, "1. [inner] no alternative matched at index 0"), !=, NULL);
        g_check_cmp_ptr(strstr(rendered, "1. [A] unexpected byte 'X'"), !=, NULL);
        g_check_cmp_ptr(strstr(rendered, "2. [B] unexpected byte 'X'"), !=, NULL);
        fclose(stream);
    }
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_choice_trie_nonzero(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *choice = H_CONTEXT(
        h_choice(H_CONTEXT(h_ch('A'), "left"), H_CONTEXT(h_ch('B'), "right"), NULL), "letter");
    HParser *parser = h_sequence(h_ch('Z'), choice, NULL);
    const uint8_t input[] = {'Z', 'X'};
    g_check_cmp_int(h_compile(parser, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_string(error->parser, ==, "letter");
    g_check_cmp_size(error->index, ==, 1);
    g_check_cmp_size(diagnostic->choice_node_count, ==, 1);
    g_check_cmp_size(diagnostic->choice_alternative_count, ==, 2);
    g_check_cmp_int(diagnostic->expected_bytes['A'], ==, true);
    g_check_cmp_int(diagnostic->expected_bytes['B'], ==, true);
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_choice_trie_reused_parser_packrat(void) {
    HParser *shared = H_CONTEXT(h_ch('A'), "shared");
    HParser *parser =
        H_CONTEXT(h_choice(shared, h_sequence(h_ch('Z'), shared, NULL), NULL), "reuse");
    const uint8_t input[] = {'Z', 'X'};

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_size(error->index, ==, 1);
    size_t first = diagnostic->choice_nodes[diagnostic->choice_root].first_alternative;
    size_t second = diagnostic->choice_alternatives[first].next;
    g_check_cmp_size(second, <, diagnostic->choice_alternative_count);
    if (second < diagnostic->choice_alternative_count)
        g_check_cmp_size(diagnostic->choice_alternatives[second].error.index, ==, 1);
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_choice_success_discards_failures_packrat(void) {
    HParser *parser = h_choice(h_ch('A'), h_ch('B'), NULL);
    const uint8_t input[] = {'B'};
    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(result, !=, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (diagnostic) {
        const HParseError *error = h_parse_diagnostic_error(diagnostic);
        g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_NONE);
        g_check_cmp_size(diagnostic->choice_node_count, ==, 0);
        size_t trace_length = 0;
        g_check_cmp_ptr(h_parse_diagnostic_execution_trace(diagnostic, &trace_length), !=, NULL);
        g_check_cmp_size(trace_length, >, 0);
        h_parse_diagnostic_free(diagnostic);
    }
    h_parse_result_free(result);
}

static void test_trace_dispatch_failure(void) {
    OpcodeMap entries[] = {{1, h_ch('A')}, {2, h_ch('B')}};
    HParser *parser = h_dispatch(h_uint8(), entries, NULL);
    const uint8_t input[] = {3};
    g_check_cmp_int(h_compile(parser, PB_PACKRAT, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_DISPATCH);
    g_check_cmp_size(error->index, ==, 0);
    g_check_cmp_size(error->end_index, ==, 1);
    g_check_string(error->parser, ==, "h_dispatch");
    g_check_cmp_int(diagnostic->dispatch_failure.has_opcode, ==, true);
    g_check_cmp_size(diagnostic->dispatch_failure.opcode, ==, 3);
    g_check_cmp_size(diagnostic->dispatch_failure.expected_count, ==, 2);

    FILE *stream = tmpfile();
    g_check_cmp_ptr(stream, !=, NULL);
    if (stream) {
        char rendered[256] = {0};
        h_parse_diagnostic_fprint(stream, diagnostic);
        rewind(stream);
        size_t rendered_len = fread(rendered, 1, sizeof(rendered) - 1, stream);
        rendered[rendered_len] = '\0';
        g_check_cmp_ptr(strstr(rendered, "no dispatch case for opcode 3"), !=, NULL);
        g_check_cmp_ptr(strstr(rendered, "expected opcode 1, 2"), !=, NULL);
        fclose(stream);
    }
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_dispatch_invalid_opcode(void) {
    OpcodeMap entries[] = {{1, h_ch('A')}};
    HParser *discriminator = h_sequence(h_ch('A'), NULL);
    HParser *parser = h_dispatch(discriminator, entries, NULL);
    const uint8_t input[] = {'A'};
    g_check_cmp_int(h_compile(parser, PB_PACKRAT, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_DISPATCH);
    g_check_cmp_int(diagnostic->dispatch_failure.has_opcode, ==, false);

    FILE *stream = tmpfile();
    g_check_cmp_ptr(stream, !=, NULL);
    if (stream) {
        char rendered[256] = {0};
        h_parse_diagnostic_fprint(stream, diagnostic);
        rewind(stream);
        size_t rendered_len = fread(rendered, 1, sizeof(rendered) - 1, stream);
        rendered[rendered_len] = '\0';
        g_check_cmp_ptr(strstr(rendered, "dispatch discriminator produced an invalid opcode"), !=,
                        NULL);
        fclose(stream);
    }
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_dispatch_preserves_body_failure(void) {
    OpcodeMap entries[] = {{1, h_ch('A')}};
    HParser *parser = h_dispatch(h_uint8(), entries, NULL);
    const uint8_t input[] = {1, 'X'};
    g_check_cmp_int(h_compile(parser, PB_PACKRAT, NULL), ==, 0);

    HParseDiagnostic *diagnostic;
    HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_PRIMITIVE_MISMATCH);
    g_check_cmp_size(error->index, ==, 1);
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_glr_ambiguous_failure(void) {
    HParser *value = h_indirect();
    h_bind_indirect(value, h_choice(h_sequence(value, value, NULL), h_ch('a'), NULL));
    HParser *p = h_sequence(value, h_end_p(), NULL);
    g_check_cmp_int(h_compile(p, PB_GLR, NULL), ==, 0);

    HParseDiagnostic *diagnostic;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"aab", 3, &diagnostic, true);
    g_check_cmp_ptr(res, ==, NULL);
    const HParseError *err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_size(err->index, ==, 2);
    g_check_cmp_int(err->actual, ==, 'b');
    g_check_cmp_int(err->has_actual, ==, true);
    g_check_cmp_int(err->kind, ==, H_PARSE_ERROR_PRIMITIVE_MISMATCH);
    g_check_cmp_size(err->n_deepest, >, 0);
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_cf_verbose_dump(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_sequence(h_ch('a'), h_ch('b'), NULL);
    g_assert_cmpint(h_compile(p, be, NULL), ==, 0);
    HParseDiagnostic *diagnostic = NULL;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"ab", 2, &diagnostic, false);
    g_assert_nonnull(res);
    g_assert_nonnull(diagnostic);
    size_t trace_length = 0;
    const char *execution_trace = h_parse_diagnostic_execution_trace(diagnostic, &trace_length);
    g_assert_nonnull(execution_trace);
    g_assert_cmpuint(trace_length, >, 0);
    if (be == PB_LL) {
        g_assert_nonnull(strstr(execution_trace, "predict"));
        g_assert_nonnull(strstr(execution_trace, "terminal"));
        g_assert_nonnull(strstr(execution_trace, "reduce"));
    } else {
        g_assert_nonnull(strstr(execution_trace, "SHIFT"));
        g_assert_nonnull(strstr(execution_trace, "REDUCE"));
    }
    h_parse_diagnostic_free(diagnostic);
    h_parse_result_free(res);
}

static void test_trace_structured_expectations(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_choice(h_ch_range('a', 'c'), h_ch('x'), NULL);
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"z", 1, &diagnostic, false);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_ptr(error, !=, NULL);
    g_check_cmp_size(error->index, ==, 0);
    g_check_cmp_int(error->actual, ==, 'z');
    g_check_cmp_size(h_parse_diagnostic_expected_count(diagnostic), ==, 2);

    HParseExpectation expected;
    g_check_cmp_int(h_parse_diagnostic_expected(diagnostic, 0, &expected), ==, true);
    g_check_cmp_int(expected.kind, ==, H_PARSE_EXPECT_BYTE_RANGE);
    g_check_cmp_int(expected.lower, ==, 'a');
    g_check_cmp_int(expected.upper, ==, 'c');
    g_check_cmp_int(h_parse_diagnostic_expected(diagnostic, 1, &expected), ==, true);
    g_check_cmp_int(expected.kind, ==, H_PARSE_EXPECT_BYTE_RANGE);
    g_check_cmp_int(expected.lower, ==, 'x');
    g_check_cmp_int(expected.upper, ==, 'x');
    g_check_cmp_int(h_parse_diagnostic_expected(diagnostic, 2, &expected), ==, false);
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_structured_expected_eof(void) {
    HParser *p = h_sequence(h_ch('A'), h_end_p(), NULL);
    g_check_cmp_int(h_compile(p, PB_REGULAR, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"AB", 2, &diagnostic, false);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    HParseExpectation expected;
    g_check_cmp_size(h_parse_diagnostic_expected_count(diagnostic), ==, 1);
    g_check_cmp_int(h_parse_diagnostic_expected(diagnostic, 0, &expected), ==, true);
    g_check_cmp_int(expected.kind, ==, H_PARSE_EXPECT_END_OF_INPUT);
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_custom_label_and_message(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    char label[] = "udp.protocol";
    char message[] = "invalid UDP protocol";
    char file_name[] = "udp.c";
    char function_name[] = "make_udp_parser";
    HSourceLocation source = {file_name, function_name, 42, 7};
    HParser *prefix = h_ch('A');
    HParser *protocol = h_ch(0x11);
    HParser *sequence = h_sequence(prefix, protocol, NULL);

    g_check_cmp_int(h_parser_set_label(sequence, label), ==, true);
    g_check_cmp_int(h_parser_set_error_message(sequence, message), ==, true);
    HParser *p = h_with_context(sequence, NULL, &source);
    g_check_cmp_ptr(p, !=, NULL);
    g_check_cmp_ptr(p, !=, sequence);
    label[0] = 'X';
    message[0] = 'X';
    file_name[0] = 'X';
    function_name[0] = 'X';
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    const uint8_t input[] = {'A', 0x12};
    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(p, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    h_parser_free(p);
    if (diagnostic) {
        const HParseError *error = h_parse_diagnostic_error(diagnostic);
        g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_PRIMITIVE_MISMATCH);
        g_check_string(error->parser, ==, "udp.protocol");
        g_check_string(error->message, ==, "invalid UDP protocol");
        g_check_cmp_ptr(error->source, !=, NULL);
        if (error->source) {
            g_check_string(error->source->file_name, ==, "udp.c");
            g_check_string(error->source->function_name, ==, "make_udp_parser");
            g_check_cmp_size(error->source->line, ==, 42);
            g_check_cmp_size(error->source->column, ==, 7);
        }
        g_check_cmp_size(error->index, ==, 1);
        g_check_cmp_size(h_parse_diagnostic_expected_count(diagnostic), ==, 1);
        h_parse_diagnostic_free(diagnostic);
    }
    h_parser_free(sequence);
    h_parser_free(prefix);
    h_parser_free(protocol);
}

static void test_with_context_owns_metadata(void) {
    char label[] = "udp.protocol";
    char file_name[] = "udp.c";
    char function_name[] = "make_udp_parser";
    HSourceLocation source = {file_name, function_name, 42, 7};
    HParser *child = h_ch(0x11);

    HParser *parser = h_with_context(child, label, &source);
    g_check_cmp_ptr(parser, !=, NULL);
    g_check_cmp_ptr(parser, !=, child);
    label[0] = 'X';
    file_name[0] = 'X';
    function_name[0] = 'X';

    g_check_string(parser->diagnostic_label, ==, "udp.protocol");
    g_check_cmp_ptr(parser->diagnostic_source, !=, &source);
    g_check_string(parser->diagnostic_source->file_name, ==, "udp.c");
    g_check_string(parser->diagnostic_source->function_name, ==, "make_udp_parser");
    g_check_cmp_size(parser->diagnostic_source->line, ==, 42);
    g_check_cmp_size(parser->diagnostic_source->column, ==, 7);
    g_check_cmp_ptr(child->diagnostic_label, ==, NULL);
    g_check_cmp_ptr(child->diagnostic_source, ==, NULL);

    size_t macro_line = __LINE__ + 1;
    HParser *outer = H_CONTEXT(parser, "udp.protocol.byte");
    g_check_cmp_ptr(outer, !=, NULL);
    g_check_cmp_ptr(outer, !=, parser);
    g_check_string(outer->diagnostic_label, ==, "udp.protocol.byte");
    g_check_string(outer->diagnostic_source->file_name, ==, __FILE__);
    g_check_string(outer->diagnostic_source->function_name, ==, "test_with_context_owns_metadata");
    g_check_cmp_size(outer->diagnostic_source->line, ==, macro_line);
    g_check_cmp_size(outer->diagnostic_source->column, ==, 0);

    h_parser_free(outer);
    h_parser_free(parser);
    h_parser_free(child);
}

static void test_trace_occurrence_provenance(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *shared = h_ch('A');
    HSourceLocation first_source = {"grammar->ddl", "make_grammar", 10, 3};
    HSourceLocation second_source = {"grammar->ddl", "make_grammar", 20, 7};
    HParser *first = h_with_context(shared, "first.A", &first_source);
    HParser *second = h_with_context(shared, "second.A", &second_source);
    HParser *parser = h_sequence(first, second, NULL);

    g_check_cmp_ptr(first, !=, shared);
    g_check_cmp_ptr(second, !=, shared);
    g_check_cmp_ptr(first, !=, second);
    g_check_cmp_ptr(shared->diagnostic_source, ==, NULL);
    g_check_cmp_int(h_compile(parser, be, NULL), ==, 0);

    HParseResult *success = h_parse(parser, (const uint8_t *)"AA", 2);
    g_check_cmp_ptr(success, !=, NULL);
    if (success)
        h_parse_result_free(success);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(parser, (const uint8_t *)"AB", 2, &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (diagnostic) {
        const HParseError *error = h_parse_diagnostic_error(diagnostic);
        g_check_string(error->parser, ==, "second.A");
        g_check_cmp_size(error->index, ==, 1);
        g_check_cmp_ptr(error->source, !=, NULL);
        if (error->source) {
            g_check_string(error->source->file_name, ==, "grammar->ddl");
            g_check_cmp_size(error->source->line, ==, 20);
            g_check_cmp_size(error->source->column, ==, 7);
        }
        h_parse_diagnostic_free(diagnostic);
    }

    h_parser_free(parser);
    h_parser_free(first);
    h_parser_free(second);
    h_parser_free(shared);
}

static void test_trace_context_preserves_error_kind(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HSourceLocation source = {"grammar->ddl", "make_grammar", 30, 2};

    HParser *nothing = h_nothing_p();
    HParser *nothing_at = h_with_context(nothing, "required.variant", &source);
    g_check_cmp_int(h_compile(nothing_at, be, NULL), ==, 0);
    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result =
        h_parse_debug(nothing_at, (const uint8_t *)"A", 1, &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (diagnostic) {
        const HParseError *error = h_parse_diagnostic_error(diagnostic);
        g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_EXPLICIT_FAILURE);
        g_check_string(error->parser, ==, "required.variant");
        h_parse_diagnostic_free(diagnostic);
    }
    h_parser_free(nothing_at);
    h_parser_free(nothing);

    HParser *byte = h_ch('A');
    HParser *range = h_float_range(byte, 0.0, 1.0);
    HParser *range_at = h_with_context(range, "typed.float", &source);
    diagnostic = NULL;
    g_check_cmp_int(h_compile(range_at, be, NULL), ==, 0);
    result = h_parse_debug(range_at, (const uint8_t *)"A", 1, &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (diagnostic) {
        const HParseError *error = h_parse_diagnostic_error(diagnostic);
        g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_RANGE);
        g_check_string(error->parser, ==, "typed.float");
        h_parse_diagnostic_free(diagnostic);
    }
    h_parser_free(range_at);
    h_parser_free(range);
    h_parser_free(byte);
}

static void test_trace_custom_failure(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_nothing_p();
    h_parser_set_error_message(p, "unsupported packet variant");
    g_check_cmp_ptr(p, !=, NULL);
    if (!p)
        return;
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(p, (const uint8_t *)"A", 1, &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (diagnostic) {
        const HParseError *error = h_parse_diagnostic_error(diagnostic);
        g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_EXPLICIT_FAILURE);
        g_check_string(error->message, ==, "unsupported packet variant");
        g_check_cmp_size(error->index, ==, 0);
        h_parse_diagnostic_free(diagnostic);
    }
    h_parser_free(p);
}

static void test_trace_nothing_failure(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_nothing_p();
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"A", 1, &diagnostic, false);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_EXPLICIT_FAILURE);
    g_check_cmp_size(error->index, ==, 0);
    g_check_cmp_size(error->end_index, ==, 0);
    g_check_cmp_int(error->has_actual, ==, false);
    g_check_cmp_size(h_parse_diagnostic_expected_count(diagnostic), ==, 0);
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_sequence_ending_in_nothing(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_sequence(h_token((const uint8_t *)"AB", 2), h_nothing_p(), NULL);
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"AB", 2, &diagnostic, false);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_EXPLICIT_FAILURE);
    g_check_cmp_size(error->index, ==, 2);
    g_check_cmp_size(error->end_index, ==, 2);
    g_check_cmp_int(error->has_actual, ==, false);
    g_check_cmp_size(h_parse_diagnostic_expected_count(diagnostic), ==, 0);
    h_parse_diagnostic_free(diagnostic);
}

/* An empty-language branch must not hide a useful expectation from another
 * branch at the same input position. */
static void test_trace_nothing_choice_priority(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_choice(h_ch('A'), NULL);
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"B", 1, &diagnostic, false);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_PRIMITIVE_MISMATCH);
    g_check_cmp_size(h_parse_diagnostic_expected_count(diagnostic), ==, 1);
    HParseExpectation expected;
    g_check_cmp_int(h_parse_diagnostic_expected(diagnostic, 0, &expected), ==, true);
    g_check_cmp_int(expected.kind, ==, H_PARSE_EXPECT_BYTE_RANGE);
    g_check_cmp_int(expected.lower, ==, 'A');
    g_check_cmp_int(expected.upper, ==, 'A');
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_token_mismatch_position(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_token((const uint8_t *)"ABCE", 4);
    const uint8_t mismatch[] = {'A', 'B', 'C', 'D', 0xff};
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *res = h_parse_debug(p, mismatch, sizeof(mismatch), &diagnostic, false);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_PRIMITIVE_MISMATCH);
    g_check_cmp_size(error->index, ==, 3);
    g_check_cmp_size(error->end_index, ==, 4);
    g_check_cmp_int(error->actual, ==, 'D');

    HParseExpectation expected;
    g_check_cmp_size(h_parse_diagnostic_expected_count(diagnostic), ==, 1);
    g_check_cmp_int(h_parse_diagnostic_expected(diagnostic, 0, &expected), ==, true);
    g_check_cmp_int(expected.kind, ==, H_PARSE_EXPECT_BYTE_RANGE);
    g_check_cmp_int(expected.lower, ==, 'E');
    g_check_cmp_int(expected.upper, ==, 'E');
    h_parse_diagnostic_free(diagnostic);

    diagnostic = NULL;
    res = h_parse_debug(p, (const uint8_t *)"ABC", 3, &diagnostic, false);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;
    error = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_UNEXPECTED_EOF);
    g_check_cmp_size(error->index, ==, 3);
    g_check_cmp_size(error->end_index, ==, 3);
    g_check_cmp_int(error->has_actual, ==, false);
    g_check_cmp_size(h_parse_diagnostic_expected_count(diagnostic), ==, 1);
    g_check_cmp_int(h_parse_diagnostic_expected(diagnostic, 0, &expected), ==, true);
    g_check_cmp_int(expected.lower, ==, 'E');
    g_check_cmp_int(expected.upper, ==, 'E');
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_relational_failure_starts(void) {
    const uint8_t input[] = {'Z', 'A', 'B'};
    struct {
        HParser *parser;
        HParseErrorKind kind;
    } cases[] = {
        {h_xor(h_token((const uint8_t *)"AB", 2), h_ch('A')), H_PARSE_ERROR_XOR},
        {h_difference(h_ch('A'), h_token((const uint8_t *)"AB", 2)), H_PARSE_ERROR_DIFFERENCE},
        {h_butnot(h_ch('A'), h_ch('A')), H_PARSE_ERROR_BUTNOT},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        HParser *parser = h_sequence(h_ch('Z'), cases[i].parser, NULL);
        g_check_cmp_int(h_compile(parser, PB_PACKRAT, NULL), ==, 0);

        HParseDiagnostic *diagnostic;
        HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
        g_check_cmp_ptr(result, ==, NULL);
        const HParseError *error = h_parse_diagnostic_error(diagnostic);
        g_check_cmp_int(error->kind, ==, cases[i].kind);
        g_check_cmp_size(error->index, ==, 1);
        g_check_cmp_int(error->actual, ==, 'A');
        g_check_cmp_int(error->has_actual, ==, true);
        h_parse_diagnostic_free(diagnostic);
    }
}

// --- Cases ported from the debugtest/ sample parsers ---------------------
// Each of the parserN.c programs is turned into a pass/fail assertion here,
// exercised through h_parse_debug(). Some inputs parse cleanly; most are
// crafted to fail, which is exactly what makes them useful trace fixtures.

// Predicate from debugtest/parser9.c (kept byte-for-byte, quirky precedence and
// all, so the outcome matches the original program).
static bool trace_validate_checksum(HParseResult *p, void *user_data) {
    (void)user_data;
    const HParsedToken *seq = p->ast;
    uint8_t data = h_seq_index(seq, 0)->token_data.uint;
    uint8_t checksum = h_seq_index(seq, 1)->token_data.uint;
    return checksum == ((data + 1) ^ 0xFF);
}

// Action from debugtest/parser10.c: sum the two bytes of the pair->
static HParsedToken *trace_sum_action(const HParseResult *p, void *user_data) {
    (void)user_data;
    uint64_t a = h_seq_index(p->ast, 0)->token_data.uint;
    uint64_t b = h_seq_index(p->ast, 1)->token_data.uint;
    return H_MAKE_UINT(a + b);
}

// first byte and succeeds even with trailing input; 0xFF -> 255.
static void test_trace_uint8_success(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p = h_uint8();
    h_compile(p, be, NULL);

    uint8_t input[] = {0xFF, 0xFF, 0xFF, 0xFF};
    HParseResult *res = h_parse_debug(p, input, sizeof(input), NULL, true);
    g_check_cmp_ptr(res, !=, NULL);
    if (res) {
        g_check_cmp_uint64(res->ast->token_data.uint, ==, 255);
        h_parse_result_free(res);
    }
}

// debugtest/parser4.c: a two-byte sequence handed only one byte fails when the
// second h_uint8() hits end-of-input.
static void test_trace_sequence_truncated(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p = h_sequence(h_uint8(), h_uint8(), NULL);
    h_compile(p, be, NULL);

    uint8_t input[] = {1};
    HParseDiagnostic *diagnostic;
    HParseResult *res = h_parse_debug(p, input, 1, &diagnostic, true);
    g_check_cmp_ptr(res, ==, NULL);
    const HParseError *err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_size(err->index, ==, 1);
    g_check_cmp_int(err->has_actual, ==, false);
    g_check_cmp_int(err->kind, ==, H_PARSE_ERROR_UNEXPECTED_EOF);
    g_check_cmp_ptr(err->deepest_parsers[0], !=, NULL);
}

// debugtest/parser5.c: choice of two tokens; "YoS" matches neither "YES" nor
// "NO".
static void test_trace_choice_token_mismatch(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p =
        h_choice(h_token((const uint8_t *)"YES", 3), h_token((const uint8_t *)"NO", 2), NULL);
    h_compile(p, be, NULL);

    uint8_t input[] = {'Y', 'o', 'S'};
    HParseDiagnostic *diagnostic;
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &diagnostic, true);
    const HParseError *err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_size(err->index, ==, 1);
    g_check_cmp_int(err->actual, ==, 'o');
    g_check_cmp_ptr(err->deepest_parsers[0], !=, NULL);
}

// debugtest/parser6.c: h_many1() needs at least one uppercase letter, but the
// input starts with '!'.
static void test_trace_many1_no_letter(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p = h_many1(h_ch_range('A', 'Z'));
    h_compile(p, be, NULL);

    uint8_t input[] = {'!', 'E', 'L', 'L', 'O', '!'};
    HParseDiagnostic *diagnostic;
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &diagnostic, true);
    g_check_cmp_ptr(res, ==, NULL);
    const HParseError *err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_size(err->index, ==, 0);
    g_check_cmp_int(err->actual, ==, '!');
    g_check_cmp_ptr(err->deepest_parsers[0], !=, NULL);
}

// debugtest/parser7.c: h_middle() around a word; the closing ')' is actually a
// '{' at index 6, so the parse fails there.
static void test_trace_middle_bad_close(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *word = h_many1(h_ch_range('A', 'Z'));
    HParser *p = h_middle(h_ch('('), word, h_ch(')'));
    h_compile(p, be, NULL);

    uint8_t input[] = {'(', 'H', 'E', 'L', 'L', 'O', '{'};
    HParseDiagnostic *diagnostic;
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &diagnostic, true);
    g_check_cmp_ptr(res, ==, NULL);
    const HParseError *err = h_parse_diagnostic_error(diagnostic);
    // Furthest progress is index 6, the '{' where ')' was expected. These
    // fields are only populated when the tracer is compiled in (n_deepest > 0).
    // No free is needed here: the parser names are borrowed from the tracer's
    // permanent cache, not owned by the struct.
    g_check_cmp_int(err->n_deepest, >, 0);
    if (err->n_deepest > 0) {
        g_check_cmp_size(err->index, ==, 6);
        g_check_cmp_int(err->actual, ==, '{');
    }
}

// debugtest/parser8.c: h_int_range() rejects a value outside [1,3]; 0xFF (255)
// is out of range.
static void test_trace_int_range_reject(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p = h_int_range(h_uint8(), 1, 3);
    h_compile(p, be, NULL);

    uint8_t input[] = {0xFF};
    HParseDiagnostic *diagnostic;
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &diagnostic, true);
    g_check_cmp_ptr(res, ==, NULL);
    const HParseError *err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(err->n_deepest, >, 0);
    if (err->n_deepest > 0) {
        g_check_cmp_size(err->index, ==, 0);
        g_check_cmp_int(err->actual, ==, 0xFF);
        g_check_cmp_int(err->kind, ==, H_PARSE_ERROR_RANGE);
        g_check_string(err->parser, ==, "h_int_range");
    }
    h_parse_diagnostic_free(diagnostic);
}

// debugtest/parser9.c: h_attr_bool() checksum predicate; 0x55/0xAA does not
// satisfy checksum == ((data + 1) ^ 0xFF), so the parse is rejected.
static void test_trace_attr_bool_checksum(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *raw = h_sequence(h_uint8(), h_uint8(), NULL);
    HParser *p = h_attr_bool(raw, trace_validate_checksum, NULL);
    h_compile(p, be, NULL);

    uint8_t input[] = {0x55, 0xAA};
    HParseDiagnostic *diagnostic;
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &diagnostic, true);
    g_check_cmp_ptr(res, ==, NULL);
    const HParseError *err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(err->n_deepest, >, 0);
    if (err->n_deepest > 0) {
        g_check_cmp_size(err->index, ==, 0);
        g_check_cmp_size(err->end_index, ==, 2);
        g_check_cmp_int(err->actual, ==, 0x55);
        g_check_cmp_int(err->has_actual, ==, true);
        g_check_cmp_int(err->kind, ==, H_PARSE_ERROR_SEMANTIC_PREDICATE);
        g_check_string(err->parser, ==, "h_attr_bool");
    }
    h_parse_diagnostic_free(diagnostic);
}

static bool trace_reject_value(HParseResult *p, void *user_data) {
    (void)p;
    (void)user_data;
    return false;
}

/* A semantic predicate runs after its child has consumed input. The reported
 * location is the start of the rejected value, while end_index remains the
 * exclusive end of that value. */
static void test_trace_attr_bool_nonzero_offset(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *rejected =
        h_attr_bool(h_sequence(h_uint8(), h_uint8(), NULL), trace_reject_value, NULL);
    HParser *p = h_sequence(h_token((const uint8_t *)"ABCD", 4), rejected, NULL);
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    const uint8_t input[] = {'A', 'B', 'C', 'D', 0x11, 0x12};
    HParseDiagnostic *diagnostic;
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &diagnostic, false);
    const HParseError *err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_size(err->index, ==, 4);
    g_check_cmp_size(err->end_index, ==, 6);
    g_check_cmp_int(err->actual, ==, 0x11);
    g_check_cmp_int(err->has_actual, ==, true);
    g_check_cmp_int(err->kind, ==, H_PARSE_ERROR_SEMANTIC_PREDICATE);
    h_parse_diagnostic_free(diagnostic);
}

static HParser *trace_nested_parser;
static HParseDiagnostic *trace_nested_error;

static bool trace_nested_success_then_reject(HParseResult *p, void *user_data) {
    (void)p;
    (void)user_data;
    uint8_t nested[] = {7};
    HParseResult *result = h_parse(trace_nested_parser, nested, sizeof(nested));
    g_assert_nonnull(result);
    h_parse_result_free(result);
    return false;
}

static bool trace_nested_debug_failure_then_reject(HParseResult *p, void *user_data) {
    (void)p;
    (void)user_data;
    uint8_t nested[] = {7};
    HParseResult *result = h_parse_debug(trace_nested_parser, nested, 0, &trace_nested_error, true);
    g_assert_null(result);
    return false;
}

static void test_trace_nested_parse_isolation(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    trace_nested_parser = h_sequence(h_uint8(), h_end_p(), NULL);
    HParser *outer =
        h_attr_bool(h_sequence(h_uint8(), h_uint8(), NULL), trace_nested_success_then_reject, NULL);
    h_compile(trace_nested_parser, be, NULL);
    h_compile(outer, be, NULL);

    uint8_t input[] = {1, 2};
    HParseDiagnostic *diagnostic;
    HParseResult *result = h_parse_debug(outer, input, sizeof(input), &diagnostic, true);
    const HParseError *err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_int(err->kind, ==, H_PARSE_ERROR_SEMANTIC_PREDICATE);
    g_check_cmp_size(err->index, ==, 0);
    g_check_cmp_size(err->end_index, ==, 2);
    g_check_string(err->parser, ==, "h_attr_bool");
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_nested_debug_restores_outer(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    memset(&trace_nested_error, 0, sizeof(trace_nested_error));
    trace_nested_parser = h_uint8();
    HParser *outer = h_attr_bool(h_sequence(h_uint8(), h_uint8(), NULL),
                                 trace_nested_debug_failure_then_reject, NULL);
    h_compile(trace_nested_parser, be, NULL);
    h_compile(outer, be, NULL);

    uint8_t input[] = {1, 2};
    HParseDiagnostic *diagnostic;
    HParseResult *result = h_parse_debug(outer, input, sizeof(input), &diagnostic, true);
    const HParseError *error = h_parse_diagnostic_error(trace_nested_error);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_UNEXPECTED_EOF);
    g_check_cmp_size(error->index, ==, 0);
    const HParseError *err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(err->kind, ==, H_PARSE_ERROR_SEMANTIC_PREDICATE);
    g_check_cmp_size(err->index, ==, 0);
    g_check_cmp_size(err->end_index, ==, 2);
    h_parse_diagnostic_free(trace_nested_error);
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_repeated_parses_and_nul_byte(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *parser = h_ch('x');
    h_compile(parser, be, NULL);

    uint8_t bad[] = {0};
    HParseDiagnostic *diagnostic;
    HParseResult *result = h_parse_debug(parser, bad, sizeof(bad), &diagnostic, true);
    g_check_cmp_ptr(result, ==, NULL);
    const HParseError *err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(err->has_actual, ==, true);
    g_check_cmp_int(err->actual, ==, 0);
    g_check_cmp_int(err->kind, ==, H_PARSE_ERROR_PRIMITIVE_MISMATCH);
    h_parse_diagnostic_free(diagnostic);

    result = h_parse_debug(parser, (const uint8_t *)"x", 1, &diagnostic, true);
    err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_ptr(result, !=, NULL);
    g_check_cmp_int(err->kind, ==, H_PARSE_ERROR_NONE);
    g_check_cmp_size(err->n_deepest, ==, 0);
    h_parse_result_free(result);

    result = h_parse_debug(parser, bad, sizeof(bad), &diagnostic, true);
    err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_int(err->has_actual, ==, true);
    g_check_cmp_int(err->actual, ==, 0);
    h_parse_diagnostic_free(diagnostic);
}

// debugtest/parser10.c: h_action() sums a pair of bytes; 10 + 20 = 30.
static void test_trace_action_sum(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *pair = h_sequence(h_uint8(), h_uint8(), NULL);
    HParser *p = h_action(pair, trace_sum_action, NULL);
    h_compile(p, be, NULL);

    uint8_t input[] = {10, 20};
    HParseResult *res = h_parse_debug(p, input, sizeof(input), NULL, true);
    g_check_cmp_ptr(res, !=, NULL);
    if (res) {
        g_check_cmp_uint64(res->ast->token_data.uint, ==, 30);
        h_parse_result_free(res);
    }
}

// debugtest/parser->c: nested list grammar-> The innermost value 9 is outside the
// number range [0,8] and is not a '[', so the whole structure fails to parse.
static void test_trace_nested_list(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *value = h_indirect();
    HParser *number = h_int_range(h_uint8(), 0, 8);
    HParser *comma = h_ignore(h_ch(','));
    HParser *list = h_middle(h_ch('['), h_sepBy1(value, comma), h_ch(']'));
    h_bind_indirect(value, h_choice(number, list, NULL));
    h_compile(list, be, NULL);

    uint8_t input[] = {'[', 1, ',', '[', 2, ',', '[', 9, ']', ']', ']'};
    HParseDiagnostic *diagnostic;
    HParseResult *res = h_parse_debug(list, input, sizeof(input), &diagnostic, true);
    g_check_cmp_ptr(res, ==, NULL);
    const HParseError *err = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(err->n_deepest, >, 0);
    if (err->n_deepest > 0) {
        g_check_cmp_size(err->index, ==, 7);
        g_check_cmp_int(err->actual, ==, 0x9);
    }
}

/* Nested H_CONTEXT occurrences must survive backend compilation as a path,
 * not collapse into whichever wrapper happened to compile last. */
static void test_trace_nested_input_trail(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    uint8_t input[] = {'A', 'B', 'C', 'D', 0};
    HParser *a = H_CONTEXT(h_ch('A'), NULL);
    HParser *b = H_CONTEXT(h_ch('B'), NULL);
    HParser *c = H_CONTEXT(h_ch('C'), NULL);
    HParser *d = H_CONTEXT(h_ch('D'), NULL);
    HParser *e = H_CONTEXT(h_ch('E'), NULL);
    HParser *sequence = H_CONTEXT(h_sequence(a, b, c, d, e, NULL), NULL);
    HParser *parser = H_CONTEXT(h_attr_bool(sequence, trace_reject_value, NULL), NULL);

    g_check_cmp_int(h_compile(parser, be, NULL), ==, 0);
    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    g_check_cmp_size(diagnostic->input_frame_count, ==, 3);
    if (diagnostic->input_frame_count == 3) {
        const HTraceFrame *outer = &diagnostic->input_frames[0];
        const HTraceFrame *middle = &diagnostic->input_frames[1];
        const HTraceFrame *inner = &diagnostic->input_frames[2];
        g_check_cmp_ptr(outer->parser, ==, parser);
        g_check_cmp_size(outer->start, ==, 0);
        g_check_cmp_size(outer->reached, ==, 5);
        g_check_cmp_ptr(middle->parser, ==, sequence);
        g_check_cmp_size(middle->start, ==, 0);
        g_check_cmp_size(middle->reached, ==, 5);
        g_check_cmp_ptr(inner->parser, ==, e);
        g_check_cmp_size(inner->start, ==, 4);
        g_check_cmp_size(inner->reached, ==, 5);
    }
    h_parse_diagnostic_free(diagnostic);
}

static size_t trace_find_frame_after(const HParseDiagnostic *diagnostic, const HParser *parser,
                                     size_t start, size_t after) {
    for (size_t i = after; i < diagnostic->input_frame_count; i++)
        if (diagnostic->input_frames[i].parser == parser &&
            diagnostic->input_frames[i].start == start)
            return i;
    return SIZE_MAX;
}

/* An unannotated grammar still exposes the causal parser nesting. */
static void test_trace_automatic_input_trail(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *a = h_ch('A');
    HParser *b = h_ch('B');
    HParser *c = h_ch('C');
    HParser *d = h_ch('D');
    HParser *head = h_sequence(a, b, NULL);
    HParser *tail = h_sequence(c, d, NULL);
    HParser *parser = h_sequence(head, tail, NULL);
    uint8_t input[] = {'A', 'B', 'C', 'X'};

    g_check_cmp_int(h_compile(parser, be, NULL), ==, 0);
    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    size_t root_frame = trace_find_frame_after(diagnostic, parser, 0, 0);
    size_t tail_frame = root_frame == SIZE_MAX
                            ? SIZE_MAX
                            : trace_find_frame_after(diagnostic, tail, 2, root_frame + 1);
    size_t leaf_frame = tail_frame == SIZE_MAX
                            ? SIZE_MAX
                            : trace_find_frame_after(diagnostic, d, 3, tail_frame + 1);
    g_check_cmp_size(root_frame, !=, SIZE_MAX);
    g_check_cmp_size(tail_frame, !=, SIZE_MAX);
    g_check_cmp_size(leaf_frame, !=, SIZE_MAX);
    h_parse_diagnostic_free(diagnostic);
}

/* Reusing a parser must select the occurrence on the failing path, not its
 * first appearance in the grammar or input. */
static void test_trace_automatic_reused_input_trail(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *c = h_ch('C');
    HParser *d = h_ch('D');
    HParser *shared = h_sequence(c, d, NULL);
    HParser *parser = h_sequence(shared, shared, NULL);
    uint8_t input[] = {'C', 'D', 'C', 'X'};

    g_check_cmp_int(h_compile(parser, be, NULL), ==, 0);
    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    size_t root_frame = trace_find_frame_after(diagnostic, parser, 0, 0);
    size_t shared_frame = root_frame == SIZE_MAX
                              ? SIZE_MAX
                              : trace_find_frame_after(diagnostic, shared, 2, root_frame + 1);
    size_t leaf_frame = shared_frame == SIZE_MAX
                            ? SIZE_MAX
                            : trace_find_frame_after(diagnostic, d, 3, shared_frame + 1);
    g_check_cmp_size(root_frame, !=, SIZE_MAX);
    g_check_cmp_size(shared_frame, !=, SIZE_MAX);
    g_check_cmp_size(leaf_frame, !=, SIZE_MAX);
    h_parse_diagnostic_free(diagnostic);
}

/* A context wrapper replaces only its direct child occurrence; unwrapped
 * ancestors and descendants remain in the automatic trail. */
static void test_trace_mixed_context_input_trail(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *a = h_ch('A');
    HParser *b = h_ch('B');
    HParser *c = h_ch('C');
    HParser *tail_child = h_sequence(b, c, NULL);
    HParser *tail = H_CONTEXT(tail_child, "wrapped_tail");
    HParser *parser = h_sequence(a, tail, NULL);
    uint8_t input[] = {'A', 'B', 'X'};

    g_check_cmp_int(h_compile(parser, be, NULL), ==, 0);
    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (!diagnostic)
        return;

    size_t root_frame = trace_find_frame_after(diagnostic, parser, 0, 0);
    size_t wrapper_frame = root_frame == SIZE_MAX
                               ? SIZE_MAX
                               : trace_find_frame_after(diagnostic, tail, 1, root_frame + 1);
    size_t leaf_frame = wrapper_frame == SIZE_MAX
                            ? SIZE_MAX
                            : trace_find_frame_after(diagnostic, c, 2, wrapper_frame + 1);
    g_check_cmp_size(root_frame, !=, SIZE_MAX);
    g_check_cmp_size(wrapper_frame, !=, SIZE_MAX);
    g_check_cmp_size(leaf_frame, !=, SIZE_MAX);
    h_parse_diagnostic_free(diagnostic);
}

static void trace_check_owned_failure(HParser *parser, const uint8_t *input, size_t length,
                                      HParseErrorKind kind, const char *message, size_t start,
                                      size_t end) {
    g_check_cmp_int(h_compile(parser, PB_PACKRAT, NULL), ==, 0);
    HParseDiagnostic *diagnostic;
    HParseResult *result = h_parse_debug(parser, input, length, &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_cmp_int(error->kind, ==, kind);
    g_check_string(error->message, ==, message);
    g_check_cmp_size(error->index, ==, start);
    g_check_cmp_size(error->end_index, ==, end);
    h_parse_diagnostic_free(diagnostic);
}

static HParser *trace_null_continuation(HAllocator *allocator, const HParsedToken *token,
                                        void *environment) {
    (void)allocator;
    (void)token;
    (void)environment;
    return NULL;
}

static void test_trace_parser_owned_failures(void) {
    const uint8_t empty[] = {0};
    const uint8_t one[] = {'A'};
    const uint8_t two[] = {'A', 'B'};

    trace_check_owned_failure(h_many(h_epsilon_p()), empty, 0, H_PARSE_ERROR_HIGHER_ORDER,
                              "repeated parser matched empty input", 0, 0);
    trace_check_owned_failure(h_length_value(h_bytes(1), h_ch('A')), one, 1,
                              H_PARSE_ERROR_HIGHER_ORDER,
                              "length parser must return an unsigned integer", 0, 1);
    trace_check_owned_failure(h_many1_cap(h_ch('A'), 0), empty, 0, H_PARSE_ERROR_HIGHER_ORDER,
                              "minimum repetition cannot be satisfied with a zero cap", 0, 0);
    trace_check_owned_failure(h_not(h_token(two, sizeof(two))), two, sizeof(two),
                              H_PARSE_ERROR_HIGHER_ORDER,
                              "negative lookahead failed because its parser matched", 0, 2);
    trace_check_owned_failure(h_seek(-1, SEEK_SET), one, sizeof(one), H_PARSE_ERROR_HIGHER_ORDER,
                              "seek target is before the start of input", 0, 0);
    trace_check_owned_failure(h_bind(h_ch('A'), trace_null_continuation, NULL), one, sizeof(one),
                              H_PARSE_ERROR_HIGHER_ORDER,
                              "bind continuation did not produce a parser", 1, 1);
    trace_check_owned_failure(h_indirect(), empty, 0, H_PARSE_ERROR_HIGHER_ORDER,
                              "indirect parser has not been bound", 0, 0);
    trace_check_owned_failure(h_permutation(h_ch('A'), h_ch('B'), NULL), (const uint8_t *)"AX", 2,
                              H_PARSE_ERROR_HIGHER_ORDER, "no permutation ordering matched", 1, 2);
    trace_check_owned_failure(h_get_value("missing"), empty, 0, H_PARSE_ERROR_NO_VALUE,
                              "no value is stored under that name", 0, 0);

    HParser *put_twice =
        h_sequence(h_put_value(h_ch('A'), "saved"), h_put_value(h_ch('B'), "saved"), NULL);
    trace_check_owned_failure(put_twice, two, sizeof(two), H_PARSE_ERROR_REUSED_NAME,
                              "a value is already stored under that name", 1, 1);
}

static void test_trace_not_in_failure(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    const uint8_t forbidden[] = {'A'};
    HParser *parser = h_not_in(forbidden, sizeof(forbidden));
    g_check_cmp_int(h_compile(parser, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic;
    HParseResult *result = h_parse_debug(parser, forbidden, sizeof(forbidden), &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    const HParseError *error = h_parse_diagnostic_error(diagnostic);
    g_check_string(error->message, ==, "input byte is forbidden by this charset");
    g_check_cmp_size(error->index, ==, 0);
    h_parse_diagnostic_free(diagnostic);
}

static void test_trace_dispatch_rejects_negative_opcode(void) {
    OpcodeMap entries[] = {{1, h_ch('A')}};
    HParser *parser = h_dispatch(h_int8(), entries, NULL);
    const uint8_t input[] = {0xff};
    g_check_cmp_int(h_compile(parser, PB_PACKRAT, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *result = h_parse_debug(parser, input, sizeof(input), &diagnostic, false);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_ptr(diagnostic, !=, NULL);
    if (diagnostic) {
        const HParseError *error = h_parse_diagnostic_error(diagnostic);
        g_check_cmp_int(error->kind, ==, H_PARSE_ERROR_DISPATCH);
        g_check_cmp_int(diagnostic->dispatch_failure.has_opcode, ==, false);
        h_parse_diagnostic_free(diagnostic);
    }
}

void register_trace_tests(void) {
    g_test_add_func("/core/parser/trace_with_context_owns_metadata",
                    test_with_context_owns_metadata);
    g_test_add_func("/core/parser/trace_concurrent_parse_isolation",
                    test_trace_concurrent_parse_isolation);
    g_test_add_func("/core/parser/trace_formatter_with_input", test_trace_formatter_with_input);
    g_test_add_func("/core/parser/trace_flag_prints_condensed_report",
                    test_trace_flag_prints_condensed_report);
    g_test_add_func("/core/parser/packrat/trace_parser_owned_failures",
                    test_trace_parser_owned_failures);
    g_test_add_func("/core/parser/packrat/trace_dispatch_rejects_negative_opcode",
                    test_trace_dispatch_rejects_negative_opcode);

#define ADD_NOT_IN_FAILURE_TEST(name, backend)                                                     \
    g_test_add_data_func("/core/parser/" name "/trace_not_in_failure", GINT_TO_POINTER(backend),   \
                         test_trace_not_in_failure)
    ADD_NOT_IN_FAILURE_TEST("regex", PB_REGULAR);
    ADD_NOT_IN_FAILURE_TEST("packrat", PB_PACKRAT);
    ADD_NOT_IN_FAILURE_TEST("ll", PB_LL);
    ADD_NOT_IN_FAILURE_TEST("lalr", PB_LALR);
    ADD_NOT_IN_FAILURE_TEST("glr", PB_GLR);
#undef ADD_NOT_IN_FAILURE_TEST

#define ADD_EMPTY_INPUT_TEST(name, backend)                                                        \
    g_test_add_data_func("/core/parser/" name "/trace_empty_input_eof", GINT_TO_POINTER(backend),  \
                         test_trace_empty_input_eof)
    ADD_EMPTY_INPUT_TEST("regex", PB_REGULAR);
    ADD_EMPTY_INPUT_TEST("packrat", PB_PACKRAT);
    ADD_EMPTY_INPUT_TEST("ll", PB_LL);
    ADD_EMPTY_INPUT_TEST("lalr", PB_LALR);
    ADD_EMPTY_INPUT_TEST("glr", PB_GLR);
#undef ADD_EMPTY_INPUT_TEST

    g_test_add_data_func("/core/parser/regex/trace_debug_success", GINT_TO_POINTER(PB_REGULAR),
                         test_trace_debug_success);
    g_test_add_data_func("/core/parser/regex/trace_debug_error_on_failure",
                         GINT_TO_POINTER(PB_REGULAR), test_trace_debug_error_on_failure);
    g_test_add_data_func("/core/parser/regex/trace_debug_null_error", GINT_TO_POINTER(PB_REGULAR),
                         test_trace_debug_null_error);
    g_test_add_data_func("/core/parser/regex/trace_range_failure", GINT_TO_POINTER(PB_REGULAR),
                         test_trace_cf_range_failure);
    g_test_add_data_func("/core/parser/regex/trace_float_range_failure",
                         GINT_TO_POINTER(PB_REGULAR), test_trace_float_range_failure);
    g_test_add_data_func("/core/parser/regex/trace_float_range_token_type_mismatch",
                         GINT_TO_POINTER(PB_REGULAR), test_trace_float_range_token_type_mismatch);
    g_test_add_data_func("/core/parser/regex/trace_structured_expectations",
                         GINT_TO_POINTER(PB_REGULAR), test_trace_structured_expectations);
    g_test_add_data_func("/core/parser/regex/trace_choice_trie", GINT_TO_POINTER(PB_REGULAR),
                         test_trace_choice_trie);
    g_test_add_data_func("/core/parser/regex/trace_choice_trie_nested", GINT_TO_POINTER(PB_REGULAR),
                         test_trace_choice_trie_nested);
    g_test_add_data_func("/core/parser/regex/trace_choice_trie_nonzero",
                         GINT_TO_POINTER(PB_REGULAR), test_trace_choice_trie_nonzero);
    g_test_add_func("/core/parser/regex/trace_structured_expected_eof",
                    test_trace_structured_expected_eof);
    g_test_add_data_func("/core/parser/regex/trace_attr_bool_checksum", GINT_TO_POINTER(PB_REGULAR),
                         test_trace_attr_bool_checksum);
    g_test_add_data_func("/core/parser/regex/trace_attr_bool_nonzero_offset",
                         GINT_TO_POINTER(PB_REGULAR), test_trace_attr_bool_nonzero_offset);
    g_test_add_data_func("/core/parser/regex/trace_nothing_failure", GINT_TO_POINTER(PB_REGULAR),
                         test_trace_nothing_failure);
    g_test_add_data_func("/core/parser/regex/trace_sequence_ending_in_nothing",
                         GINT_TO_POINTER(PB_REGULAR), test_trace_sequence_ending_in_nothing);
    g_test_add_data_func("/core/parser/regex/trace_nothing_choice_priority",
                         GINT_TO_POINTER(PB_REGULAR), test_trace_nothing_choice_priority);
    g_test_add_data_func("/core/parser/regex/trace_token_mismatch_position",
                         GINT_TO_POINTER(PB_REGULAR), test_trace_token_mismatch_position);

    g_test_add_data_func("/core/parser/packrat/trace_debug_success", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_debug_success);
    g_test_add_data_func("/core/parser/packrat/trace_debug_error_on_failure",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_debug_error_on_failure);
    g_test_add_data_func("/core/parser/packrat/trace_debug_null_error", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_debug_null_error);
    g_test_add_data_func("/core/parser/packrat/trace_range_failure", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_cf_range_failure);
    g_test_add_data_func("/core/parser/packrat/trace_float_range_failure",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_float_range_failure);
    g_test_add_data_func("/core/parser/packrat/trace_float_range_token_type_mismatch",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_float_range_token_type_mismatch);
    g_test_add_data_func("/core/parser/packrat/trace_structured_expectations",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_structured_expectations);
    g_test_add_data_func("/core/parser/packrat/trace_nothing_failure", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_nothing_failure);
    g_test_add_data_func("/core/parser/packrat/trace_sequence_ending_in_nothing",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_sequence_ending_in_nothing);
    g_test_add_data_func("/core/parser/packrat/trace_nothing_choice_priority",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_nothing_choice_priority);
    g_test_add_data_func("/core/parser/packrat/trace_token_mismatch_position",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_token_mismatch_position);
    g_test_add_func("/core/parser/packrat/trace_relational_failure_starts",
                    test_trace_relational_failure_starts);
    g_test_add_data_func("/core/parser/packrat/trace_choice_trie", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_choice_trie);
    g_test_add_func("/core/parser/packrat/trace_choice_leaf_range_detail",
                    test_trace_choice_leaf_range_detail_packrat);
    g_test_add_func("/core/parser/packrat/trace_choice_leaf_short_input",
                    test_trace_choice_leaf_short_input_packrat);
    g_test_add_func("/core/parser/packrat/trace_choice_trie_furthest",
                    test_trace_choice_trie_furthest_packrat);
    g_test_add_data_func("/core/parser/packrat/trace_choice_trie_nested",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_choice_trie_nested);
    g_test_add_data_func("/core/parser/packrat/trace_choice_trie_nonzero",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_choice_trie_nonzero);
    g_test_add_func("/core/parser/packrat/trace_choice_trie_reused",
                    test_trace_choice_trie_reused_parser_packrat);
    g_test_add_func("/core/parser/packrat/trace_choice_trie_success",
                    test_trace_choice_success_discards_failures_packrat);
    g_test_add_func("/core/parser/packrat/trace_dispatch_failure", test_trace_dispatch_failure);
    g_test_add_func("/core/parser/packrat/trace_dispatch_invalid_opcode",
                    test_trace_dispatch_invalid_opcode);
    g_test_add_func("/core/parser/packrat/trace_dispatch_preserves_body_failure",
                    test_trace_dispatch_preserves_body_failure);

    g_test_add_data_func("/core/parser/ll/trace_debug_success", GINT_TO_POINTER(PB_LL),
                         test_trace_debug_success);
    g_test_add_data_func("/core/parser/ll/trace_debug_error_on_failure", GINT_TO_POINTER(PB_LL),
                         test_trace_debug_error_on_failure);
    g_test_add_data_func("/core/parser/ll/trace_debug_null_error", GINT_TO_POINTER(PB_LL),
                         test_trace_debug_null_error);
    g_test_add_data_func("/core/parser/ll/trace_unexpected_eof", GINT_TO_POINTER(PB_LL),
                         test_trace_cf_unexpected_eof);
    g_test_add_data_func("/core/parser/ll/trace_range_failure", GINT_TO_POINTER(PB_LL),
                         test_trace_cf_range_failure);
    g_test_add_data_func("/core/parser/ll/trace_float_range_failure", GINT_TO_POINTER(PB_LL),
                         test_trace_float_range_failure);
    g_test_add_data_func("/core/parser/ll/trace_float_range_token_type_mismatch",
                         GINT_TO_POINTER(PB_LL), test_trace_float_range_token_type_mismatch);
    g_test_add_data_func("/core/parser/ll/trace_attr_bool_nonzero_offset", GINT_TO_POINTER(PB_LL),
                         test_trace_attr_bool_nonzero_offset);
    g_test_add_data_func("/core/parser/ll/trace_nothing_failure", GINT_TO_POINTER(PB_LL),
                         test_trace_nothing_failure);
    g_test_add_data_func("/core/parser/ll/trace_sequence_ending_in_nothing", GINT_TO_POINTER(PB_LL),
                         test_trace_sequence_ending_in_nothing);
    g_test_add_data_func("/core/parser/ll/trace_nothing_choice_priority", GINT_TO_POINTER(PB_LL),
                         test_trace_nothing_choice_priority);
    g_test_add_data_func("/core/parser/ll/trace_token_mismatch_position", GINT_TO_POINTER(PB_LL),
                         test_trace_token_mismatch_position);
    g_test_add_data_func("/core/parser/ll/trace_verbose_dump", GINT_TO_POINTER(PB_LL),
                         test_trace_cf_verbose_dump);

    g_test_add_data_func("/core/parser/lalr/trace_debug_success", GINT_TO_POINTER(PB_LALR),
                         test_trace_debug_success);
    g_test_add_data_func("/core/parser/lalr/trace_debug_error_on_failure", GINT_TO_POINTER(PB_LALR),
                         test_trace_debug_error_on_failure);
    g_test_add_data_func("/core/parser/lalr/trace_debug_null_error", GINT_TO_POINTER(PB_LALR),
                         test_trace_debug_null_error);
    g_test_add_data_func("/core/parser/lalr/trace_unexpected_eof", GINT_TO_POINTER(PB_LALR),
                         test_trace_cf_unexpected_eof);
    g_test_add_data_func("/core/parser/lalr/trace_range_failure", GINT_TO_POINTER(PB_LALR),
                         test_trace_cf_range_failure);
    g_test_add_data_func("/core/parser/lalr/trace_float_range_failure", GINT_TO_POINTER(PB_LALR),
                         test_trace_float_range_failure);
    g_test_add_data_func("/core/parser/lalr/trace_float_range_token_type_mismatch",
                         GINT_TO_POINTER(PB_LALR), test_trace_float_range_token_type_mismatch);
    g_test_add_data_func("/core/parser/lalr/trace_attr_bool_checksum", GINT_TO_POINTER(PB_LALR),
                         test_trace_attr_bool_checksum);
    g_test_add_data_func("/core/parser/lalr/trace_attr_bool_nonzero_offset",
                         GINT_TO_POINTER(PB_LALR), test_trace_attr_bool_nonzero_offset);
    g_test_add_data_func("/core/parser/lalr/trace_nothing_failure", GINT_TO_POINTER(PB_LALR),
                         test_trace_nothing_failure);
    g_test_add_data_func("/core/parser/lalr/trace_sequence_ending_in_nothing",
                         GINT_TO_POINTER(PB_LALR), test_trace_sequence_ending_in_nothing);
    g_test_add_data_func("/core/parser/lalr/trace_nothing_choice_priority",
                         GINT_TO_POINTER(PB_LALR), test_trace_nothing_choice_priority);
    g_test_add_data_func("/core/parser/lalr/trace_token_mismatch_position",
                         GINT_TO_POINTER(PB_LALR), test_trace_token_mismatch_position);
    g_test_add_data_func("/core/parser/lalr/trace_verbose_dump", GINT_TO_POINTER(PB_LALR),
                         test_trace_cf_verbose_dump);

    g_test_add_data_func("/core/parser/glr/trace_debug_success", GINT_TO_POINTER(PB_GLR),
                         test_trace_debug_success);
    g_test_add_data_func("/core/parser/glr/trace_debug_error_on_failure", GINT_TO_POINTER(PB_GLR),
                         test_trace_debug_error_on_failure);
    g_test_add_data_func("/core/parser/glr/trace_debug_null_error", GINT_TO_POINTER(PB_GLR),
                         test_trace_debug_null_error);
    g_test_add_data_func("/core/parser/glr/trace_unexpected_eof", GINT_TO_POINTER(PB_GLR),
                         test_trace_cf_unexpected_eof);
    g_test_add_data_func("/core/parser/glr/trace_range_failure", GINT_TO_POINTER(PB_GLR),
                         test_trace_cf_range_failure);
    g_test_add_data_func("/core/parser/glr/trace_float_range_failure", GINT_TO_POINTER(PB_GLR),
                         test_trace_float_range_failure);
    g_test_add_data_func("/core/parser/glr/trace_float_range_token_type_mismatch",
                         GINT_TO_POINTER(PB_GLR), test_trace_float_range_token_type_mismatch);
    g_test_add_data_func("/core/parser/glr/trace_attr_bool_checksum", GINT_TO_POINTER(PB_GLR),
                         test_trace_attr_bool_checksum);
    g_test_add_data_func("/core/parser/glr/trace_attr_bool_nonzero_offset", GINT_TO_POINTER(PB_GLR),
                         test_trace_attr_bool_nonzero_offset);
    g_test_add_data_func("/core/parser/glr/trace_nothing_failure", GINT_TO_POINTER(PB_GLR),
                         test_trace_nothing_failure);
    g_test_add_data_func("/core/parser/glr/trace_sequence_ending_in_nothing",
                         GINT_TO_POINTER(PB_GLR), test_trace_sequence_ending_in_nothing);
    g_test_add_data_func("/core/parser/glr/trace_nothing_choice_priority", GINT_TO_POINTER(PB_GLR),
                         test_trace_nothing_choice_priority);
    g_test_add_data_func("/core/parser/glr/trace_token_mismatch_position", GINT_TO_POINTER(PB_GLR),
                         test_trace_token_mismatch_position);
    g_test_add_func("/core/parser/glr/trace_ambiguous_failure", test_trace_glr_ambiguous_failure);
    g_test_add_data_func("/core/parser/glr/trace_verbose_dump", GINT_TO_POINTER(PB_GLR),
                         test_trace_cf_verbose_dump);

    g_test_add_data_func("/core/parser/ll/trace_structured_expectations", GINT_TO_POINTER(PB_LL),
                         test_trace_structured_expectations);
    g_test_add_data_func("/core/parser/ll/trace_choice_trie", GINT_TO_POINTER(PB_LL),
                         test_trace_choice_trie);
    g_test_add_data_func("/core/parser/ll/trace_choice_trie_nested", GINT_TO_POINTER(PB_LL),
                         test_trace_choice_trie_nested);
    g_test_add_data_func("/core/parser/ll/trace_choice_trie_nonzero", GINT_TO_POINTER(PB_LL),
                         test_trace_choice_trie_nonzero);
    g_test_add_data_func("/core/parser/lalr/trace_structured_expectations",
                         GINT_TO_POINTER(PB_LALR), test_trace_structured_expectations);
    g_test_add_data_func("/core/parser/lalr/trace_choice_trie", GINT_TO_POINTER(PB_LALR),
                         test_trace_choice_trie);
    g_test_add_data_func("/core/parser/lalr/trace_choice_trie_nested", GINT_TO_POINTER(PB_LALR),
                         test_trace_choice_trie_nested);
    g_test_add_data_func("/core/parser/lalr/trace_choice_trie_nonzero", GINT_TO_POINTER(PB_LALR),
                         test_trace_choice_trie_nonzero);
    g_test_add_data_func("/core/parser/glr/trace_structured_expectations", GINT_TO_POINTER(PB_GLR),
                         test_trace_structured_expectations);
    g_test_add_data_func("/core/parser/glr/trace_choice_trie", GINT_TO_POINTER(PB_GLR),
                         test_trace_choice_trie);
    g_test_add_data_func("/core/parser/glr/trace_choice_trie_nested", GINT_TO_POINTER(PB_GLR),
                         test_trace_choice_trie_nested);
    g_test_add_data_func("/core/parser/glr/trace_choice_trie_nonzero", GINT_TO_POINTER(PB_GLR),
                         test_trace_choice_trie_nonzero);

    g_test_add_data_func("/core/parser/regex/trace_custom_label_and_message",
                         GINT_TO_POINTER(PB_REGULAR), test_trace_custom_label_and_message);
    g_test_add_data_func("/core/parser/regex/trace_occurrence_provenance",
                         GINT_TO_POINTER(PB_REGULAR), test_trace_occurrence_provenance);
    g_test_add_data_func("/core/parser/regex/trace_context_preserves_error_kind",
                         GINT_TO_POINTER(PB_REGULAR), test_trace_context_preserves_error_kind);
    g_test_add_data_func("/core/parser/packrat/trace_custom_label_and_message",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_custom_label_and_message);
    g_test_add_data_func("/core/parser/packrat/trace_occurrence_provenance",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_occurrence_provenance);
    g_test_add_data_func("/core/parser/packrat/trace_context_preserves_error_kind",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_context_preserves_error_kind);
    g_test_add_data_func("/core/parser/ll/trace_custom_label_and_message", GINT_TO_POINTER(PB_LL),
                         test_trace_custom_label_and_message);
    g_test_add_data_func("/core/parser/ll/trace_occurrence_provenance", GINT_TO_POINTER(PB_LL),
                         test_trace_occurrence_provenance);
    g_test_add_data_func("/core/parser/ll/trace_context_preserves_error_kind",
                         GINT_TO_POINTER(PB_LL), test_trace_context_preserves_error_kind);
    g_test_add_data_func("/core/parser/lalr/trace_custom_label_and_message",
                         GINT_TO_POINTER(PB_LALR), test_trace_custom_label_and_message);
    g_test_add_data_func("/core/parser/lalr/trace_occurrence_provenance", GINT_TO_POINTER(PB_LALR),
                         test_trace_occurrence_provenance);
    g_test_add_data_func("/core/parser/lalr/trace_context_preserves_error_kind",
                         GINT_TO_POINTER(PB_LALR), test_trace_context_preserves_error_kind);
    g_test_add_data_func("/core/parser/glr/trace_custom_label_and_message", GINT_TO_POINTER(PB_GLR),
                         test_trace_custom_label_and_message);
    g_test_add_data_func("/core/parser/glr/trace_occurrence_provenance", GINT_TO_POINTER(PB_GLR),
                         test_trace_occurrence_provenance);
    g_test_add_data_func("/core/parser/glr/trace_context_preserves_error_kind",
                         GINT_TO_POINTER(PB_GLR), test_trace_context_preserves_error_kind);

    g_test_add_data_func("/core/parser/regex/trace_nested_input_trail", GINT_TO_POINTER(PB_REGULAR),
                         test_trace_nested_input_trail);
    g_test_add_data_func("/core/parser/packrat/trace_nested_input_trail",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_nested_input_trail);
    g_test_add_data_func("/core/parser/ll/trace_nested_input_trail", GINT_TO_POINTER(PB_LL),
                         test_trace_nested_input_trail);
    g_test_add_data_func("/core/parser/lalr/trace_nested_input_trail", GINT_TO_POINTER(PB_LALR),
                         test_trace_nested_input_trail);
    g_test_add_data_func("/core/parser/glr/trace_nested_input_trail", GINT_TO_POINTER(PB_GLR),
                         test_trace_nested_input_trail);

#define ADD_AUTOMATIC_TRAIL_TESTS(name, backend)                                                   \
    g_test_add_data_func("/core/parser/" name "/trace_automatic_input_trail",                      \
                         GINT_TO_POINTER(backend), test_trace_automatic_input_trail);              \
    g_test_add_data_func("/core/parser/" name "/trace_automatic_reused_input_trail",               \
                         GINT_TO_POINTER(backend), test_trace_automatic_reused_input_trail);       \
    g_test_add_data_func("/core/parser/" name "/trace_mixed_context_input_trail",                  \
                         GINT_TO_POINTER(backend), test_trace_mixed_context_input_trail)
    ADD_AUTOMATIC_TRAIL_TESTS("regex", PB_REGULAR);
    ADD_AUTOMATIC_TRAIL_TESTS("packrat", PB_PACKRAT);
    ADD_AUTOMATIC_TRAIL_TESTS("ll", PB_LL);
    ADD_AUTOMATIC_TRAIL_TESTS("lalr", PB_LALR);
    ADD_AUTOMATIC_TRAIL_TESTS("glr", PB_GLR);
#undef ADD_AUTOMATIC_TRAIL_TESTS

    g_test_add_data_func("/core/parser/regex/trace_custom_failure", GINT_TO_POINTER(PB_REGULAR),
                         test_trace_custom_failure);
    g_test_add_data_func("/core/parser/packrat/trace_custom_failure", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_custom_failure);
    g_test_add_data_func("/core/parser/ll/trace_custom_failure", GINT_TO_POINTER(PB_LL),
                         test_trace_custom_failure);
    g_test_add_data_func("/core/parser/lalr/trace_custom_failure", GINT_TO_POINTER(PB_LALR),
                         test_trace_custom_failure);
    g_test_add_data_func("/core/parser/glr/trace_custom_failure", GINT_TO_POINTER(PB_GLR),
                         test_trace_custom_failure);

    // Ported from the debugtest/ sample parsers.
    g_test_add_data_func("/core/parser/packrat/trace_uint8_success", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_uint8_success);
    g_test_add_data_func("/core/parser/packrat/trace_sequence_truncated",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_sequence_truncated);
    g_test_add_data_func("/core/parser/packrat/trace_choice_token_mismatch",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_choice_token_mismatch);
    g_test_add_data_func("/core/parser/packrat/trace_many1_no_letter", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_many1_no_letter);
    g_test_add_data_func("/core/parser/packrat/trace_middle_bad_close", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_middle_bad_close);
    g_test_add_data_func("/core/parser/packrat/trace_int_range_reject", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_int_range_reject);
    g_test_add_data_func("/core/parser/packrat/trace_attr_bool_checksum",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_attr_bool_checksum);
    g_test_add_data_func("/core/parser/packrat/trace_attr_bool_nonzero_offset",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_attr_bool_nonzero_offset);
    g_test_add_data_func("/core/parser/packrat/trace_nested_parse_isolation",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_nested_parse_isolation);
    g_test_add_data_func("/core/parser/packrat/trace_nested_debug_restores_outer",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_nested_debug_restores_outer);
    g_test_add_data_func("/core/parser/packrat/trace_repeated_parses_and_nul_byte",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_repeated_parses_and_nul_byte);
    g_test_add_data_func("/core/parser/packrat/trace_action_sum", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_action_sum);
    g_test_add_data_func("/core/parser/packrat/trace_nested_list", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_nested_list);
}
