#include "glue.h"
#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

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

    HParseError err;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"ab", 2, &err, true);
    g_check_cmp_ptr(res, !=, NULL);
    if (res) {
        h_parse_result_free(res);
    }
    g_check_cmp_size(err.n_deepest, ==, 0);
    g_check_cmp_size(err.index, ==, 0);
    h_parse_error_free(&err);
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
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"ax", 2, &err, true);
    g_check_cmp_ptr(res, ==, NULL);

    // The failure-position fields are only populated when the library is built
    // with -DHAMMER_TRACE_AST; without it the struct is left zeroed. n_deepest
    // being non-zero is the signal that the tracer ran, so guard on it.
    g_check_cmp_size(err.n_deepest, >, 0);
    if (err.n_deepest > 0) {
        g_check_cmp_size(err.index, ==, 1);
        g_check_cmp_int(err.actual, ==, 'x');
        g_check_cmp_int(err.has_actual, ==, true);
        g_check_cmp_int(err.kind, ==, H_PARSE_ERROR_PRIMITIVE_MISMATCH);
        g_check_cmp_ptr(err.deepest_parsers[0], !=, NULL);
    }
    h_parse_error_free(&err);
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

    HParseError err;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"AB", 2, &err, true);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_size(err.index, ==, 2);
    g_check_cmp_size(err.end_index, ==, 2);
    g_check_cmp_int(err.has_actual, ==, false);
    g_check_cmp_int(err.kind, ==, H_PARSE_ERROR_UNEXPECTED_EOF);
    g_check_cmp_size(err.n_deepest, >, 0);
    h_parse_error_free(&err);
}

static void test_trace_cf_range_failure(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_int_range(h_ch('A'), 'B', 'Z');
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    HParseError err;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"A", 1, &err, true);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_size(err.index, ==, 0);
    g_check_cmp_size(err.end_index, ==, 1);
    g_check_cmp_int(err.actual, ==, 'A');
    g_check_cmp_int(err.has_actual, ==, true);
    g_check_cmp_int(err.kind, ==, H_PARSE_ERROR_RANGE);
    g_check_string(err.parser, ==, "parse_int_range");
    h_parse_error_free(&err);
}

static void test_trace_glr_ambiguous_failure(void) {
    HParser *value = h_indirect();
    h_bind_indirect(value, h_choice(h_sequence(value, value, NULL), h_ch('a'), NULL));
    HParser *p = h_sequence(value, h_end_p(), NULL);
    g_check_cmp_int(h_compile(p, PB_GLR, NULL), ==, 0);

    HParseError err;
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"aab", 3, &err, true);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_size(err.index, ==, 2);
    g_check_cmp_int(err.actual, ==, 'b');
    g_check_cmp_int(err.has_actual, ==, true);
    g_check_cmp_int(err.kind, ==, H_PARSE_ERROR_PRIMITIVE_MISMATCH);
    g_check_cmp_size(err.n_deepest, >, 0);
    h_parse_error_free(&err);
}

static void test_trace_cf_verbose_dump(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    if (g_test_subprocess()) {
        HParser *p = h_sequence(h_ch('a'), h_ch('b'), NULL);
        g_assert_cmpint(h_compile(p, be, NULL), ==, 0);
        HParseResult *res = h_parse_debug(p, (const uint8_t *)"ab", 2, NULL, true);
        g_assert_nonnull(res);
        h_parse_result_free(res);
        return;
    }

    g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_passed();
    if (be == PB_LL)
        g_test_trap_assert_stderr("*predict*terminal*reduce*");
    else
        g_test_trap_assert_stderr("*SHIFT*REDUCE*");
}

static void test_trace_structured_expectations(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_choice(h_ch_range('a', 'c'), h_ch('x'), NULL);
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *res = h_parse_debug_ex(p, (const uint8_t *)"z", 1, &diagnostic, false);
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
    HParseResult *res = h_parse_debug_ex(p, (const uint8_t *)"AB", 2, &diagnostic, false);
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

static void test_trace_nothing_failure(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *p = h_nothing_p();
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *res = h_parse_debug_ex(p, (const uint8_t *)"A", 1, &diagnostic, false);
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
    HParseResult *res = h_parse_debug_ex(p, (const uint8_t *)"AB", 2, &diagnostic, false);
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
    HParser *p = h_choice(h_nothing_p(), h_ch('A'), NULL);
    g_check_cmp_int(h_compile(p, be, NULL), ==, 0);

    HParseDiagnostic *diagnostic = NULL;
    HParseResult *res = h_parse_debug_ex(p, (const uint8_t *)"B", 1, &diagnostic, false);
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

static void test_trace_relational_failure_starts(void) {
    const uint8_t input[] = {'Z', 'A', 'B'};
    struct {
        HParser *parser;
        HParseErrorKind kind;
    } cases[] = {
        {h_xor(h_token((const uint8_t *)"AB", 2), h_ch('A')), H_PARSE_ERROR_XOR},
        {h_difference(h_ch('A'), h_token((const uint8_t *)"AB", 2)),
         H_PARSE_ERROR_DIFFERENCE},
        {h_butnot(h_ch('A'), h_ch('A')), H_PARSE_ERROR_BUTNOT},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        HParser *parser = h_sequence(h_ch('Z'), cases[i].parser, NULL);
        g_check_cmp_int(h_compile(parser, PB_PACKRAT, NULL), ==, 0);

        HParseError error;
        HParseResult *result = h_parse_debug(parser, input, sizeof(input), &error, false);
        g_check_cmp_ptr(result, ==, NULL);
        g_check_cmp_int(error.kind, ==, cases[i].kind);
        g_check_cmp_size(error.index, ==, 1);
        g_check_cmp_int(error.actual, ==, 'A');
        g_check_cmp_int(error.has_actual, ==, true);
        h_parse_error_free(&error);
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

// Action from debugtest/parser10.c: sum the two bytes of the pair.
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
    HParseError err;
    HParseResult *res = h_parse_debug(p, input, 1, &err, true);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_size(err.index, ==, 1);
    g_check_cmp_int(err.has_actual, ==, false);
    g_check_cmp_int(err.kind, ==, H_PARSE_ERROR_UNEXPECTED_EOF);
    g_check_cmp_ptr(err.deepest_parsers[0], !=, NULL);
}

// debugtest/parser5.c: choice of two tokens; "YoS" matches neither "YES" nor
// "NO".
static void test_trace_choice_token_mismatch(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p =
        h_choice(h_token((const uint8_t *)"YES", 3), h_token((const uint8_t *)"NO", 2), NULL);
    h_compile(p, be, NULL);

    uint8_t input[] = {'Y', 'o', 'S'};
    HParseError err;
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &err, true);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_size(err.index, ==, 0);
    g_check_cmp_int(err.actual, ==, 'Y');
    g_check_cmp_ptr(err.deepest_parsers[0], !=, NULL);
}

// debugtest/parser6.c: h_many1() needs at least one uppercase letter, but the
// input starts with '!'.
static void test_trace_many1_no_letter(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p = h_many1(h_ch_range('A', 'Z'));
    h_compile(p, be, NULL);

    uint8_t input[] = {'!', 'E', 'L', 'L', 'O', '!'};
    HParseError err;
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &err, true);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_size(err.index, ==, 0);
    g_check_cmp_int(err.actual, ==, '!');
    g_check_cmp_ptr(err.deepest_parsers[0], !=, NULL);
}

// debugtest/parser7.c: h_middle() around a word; the closing ')' is actually a
// '{' at index 6, so the parse fails there.
static void test_trace_middle_bad_close(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *word = h_many1(h_ch_range('A', 'Z'));
    HParser *p = h_middle(h_ch('('), word, h_ch(')'));
    h_compile(p, be, NULL);

    uint8_t input[] = {'(', 'H', 'E', 'L', 'L', 'O', '{'};
    HParseError err;
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &err, true);
    g_check_cmp_ptr(res, ==, NULL);

    // Furthest progress is index 6, the '{' where ')' was expected. These
    // fields are only populated when the tracer is compiled in (n_deepest > 0).
    // No free is needed here: the parser names are borrowed from the tracer's
    // permanent cache, not owned by the struct.
    g_check_cmp_int(err.n_deepest, >, 0);
    if (err.n_deepest > 0) {
        g_check_cmp_size(err.index, ==, 6);
        g_check_cmp_int(err.actual, ==, '{');
    }
}

// debugtest/parser8.c: h_int_range() rejects a value outside [1,3]; 0xFF (255)
// is out of range.
static void test_trace_int_range_reject(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p = h_int_range(h_uint8(), 1, 3);
    h_compile(p, be, NULL);

    uint8_t input[] = {0xFF};
    HParseError err;
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &err, true);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_int(err.n_deepest, >, 0);
    if (err.n_deepest > 0) {
        g_check_cmp_size(err.index, ==, 0);
        g_check_cmp_int(err.actual, ==, 0xFF);
        g_check_cmp_int(err.kind, ==, H_PARSE_ERROR_RANGE);
        g_check_string(err.parser, ==, "parse_int_range");
    }
    h_parse_error_free(&err);
}

// debugtest/parser9.c: h_attr_bool() checksum predicate; 0x55/0xAA does not
// satisfy checksum == ((data + 1) ^ 0xFF), so the parse is rejected.
static void test_trace_attr_bool_checksum(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *raw = h_sequence(h_uint8(), h_uint8(), NULL);
    HParser *p = h_attr_bool(raw, trace_validate_checksum, NULL);
    h_compile(p, be, NULL);

    uint8_t input[] = {0x55, 0xAA};
    HParseError err;
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &err, true);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_int(err.n_deepest, >, 0);
    if (err.n_deepest > 0) {
        g_check_cmp_size(err.index, ==, 0);
        g_check_cmp_size(err.end_index, ==, 2);
        g_check_cmp_int(err.actual, ==, 0x55);
        g_check_cmp_int(err.has_actual, ==, true);
        g_check_cmp_int(err.kind, ==, H_PARSE_ERROR_SEMANTIC_PREDICATE);
        g_check_string(err.parser, ==, "parse_attr_bool");
    }
    h_parse_error_free(&err);
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
    HParseError err;
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &err, false);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_size(err.index, ==, 4);
    g_check_cmp_size(err.end_index, ==, 6);
    g_check_cmp_int(err.actual, ==, 0x11);
    g_check_cmp_int(err.has_actual, ==, true);
    g_check_cmp_int(err.kind, ==, H_PARSE_ERROR_SEMANTIC_PREDICATE);
    h_parse_error_free(&err);
}

static HParser *trace_nested_parser;
static HParseError trace_nested_error;

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
    HParseError err;
    HParseResult *result = h_parse_debug(outer, input, sizeof(input), &err, true);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_int(err.kind, ==, H_PARSE_ERROR_SEMANTIC_PREDICATE);
    g_check_cmp_size(err.index, ==, 0);
    g_check_cmp_size(err.end_index, ==, 2);
    g_check_string(err.parser, ==, "parse_attr_bool");
    h_parse_error_free(&err);
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
    HParseError err;
    HParseResult *result = h_parse_debug(outer, input, sizeof(input), &err, true);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_int(trace_nested_error.kind, ==, H_PARSE_ERROR_UNEXPECTED_EOF);
    g_check_cmp_size(trace_nested_error.index, ==, 0);
    g_check_cmp_int(err.kind, ==, H_PARSE_ERROR_SEMANTIC_PREDICATE);
    g_check_cmp_size(err.index, ==, 0);
    g_check_cmp_size(err.end_index, ==, 2);
    h_parse_error_free(&trace_nested_error);
    h_parse_error_free(&err);
}

static void test_trace_repeated_parses_and_nul_byte(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *parser = h_ch('x');
    h_compile(parser, be, NULL);

    uint8_t bad[] = {0};
    HParseError err;
    HParseResult *result = h_parse_debug(parser, bad, sizeof(bad), &err, true);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_int(err.has_actual, ==, true);
    g_check_cmp_int(err.actual, ==, 0);
    g_check_cmp_int(err.kind, ==, H_PARSE_ERROR_PRIMITIVE_MISMATCH);
    h_parse_error_free(&err);

    result = h_parse_debug(parser, (const uint8_t *)"x", 1, &err, true);
    g_check_cmp_ptr(result, !=, NULL);
    g_check_cmp_int(err.kind, ==, H_PARSE_ERROR_NONE);
    g_check_cmp_size(err.n_deepest, ==, 0);
    h_parse_result_free(result);

    result = h_parse_debug(parser, bad, sizeof(bad), &err, true);
    g_check_cmp_ptr(result, ==, NULL);
    g_check_cmp_int(err.has_actual, ==, true);
    g_check_cmp_int(err.actual, ==, 0);
    h_parse_error_free(&err);
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

// debugtest/parser.c: nested list grammar. The innermost value 9 is outside the
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
    HParseError err;
    HParseResult *res = h_parse_debug(list, input, sizeof(input), &err, true);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_int(err.n_deepest, >, 0);
    if (err.n_deepest > 0) {
        g_check_cmp_size(err.index, ==, 7);
        g_check_cmp_int(err.actual, ==, 0x9);
    }
}

void register_trace_tests(void) {
    g_test_add_data_func("/core/parser/regex/trace_debug_success", GINT_TO_POINTER(PB_REGULAR),
                         test_trace_debug_success);
    g_test_add_data_func("/core/parser/regex/trace_debug_error_on_failure",
                         GINT_TO_POINTER(PB_REGULAR), test_trace_debug_error_on_failure);
    g_test_add_data_func("/core/parser/regex/trace_debug_null_error", GINT_TO_POINTER(PB_REGULAR),
                         test_trace_debug_null_error);
    g_test_add_data_func("/core/parser/regex/trace_structured_expectations",
                         GINT_TO_POINTER(PB_REGULAR), test_trace_structured_expectations);
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

    g_test_add_data_func("/core/parser/packrat/trace_debug_success", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_debug_success);
    g_test_add_data_func("/core/parser/packrat/trace_debug_error_on_failure",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_debug_error_on_failure);
    g_test_add_data_func("/core/parser/packrat/trace_debug_null_error", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_debug_null_error);
    g_test_add_data_func("/core/parser/packrat/trace_structured_expectations",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_structured_expectations);
    g_test_add_data_func("/core/parser/packrat/trace_nothing_failure", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_nothing_failure);
    g_test_add_data_func("/core/parser/packrat/trace_sequence_ending_in_nothing",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_sequence_ending_in_nothing);
    g_test_add_data_func("/core/parser/packrat/trace_nothing_choice_priority",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_nothing_choice_priority);
    g_test_add_func("/core/parser/packrat/trace_relational_failure_starts",
                    test_trace_relational_failure_starts);

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
    g_test_add_data_func("/core/parser/ll/trace_attr_bool_nonzero_offset", GINT_TO_POINTER(PB_LL),
                         test_trace_attr_bool_nonzero_offset);
    g_test_add_data_func("/core/parser/ll/trace_nothing_failure", GINT_TO_POINTER(PB_LL),
                         test_trace_nothing_failure);
    g_test_add_data_func("/core/parser/ll/trace_sequence_ending_in_nothing", GINT_TO_POINTER(PB_LL),
                         test_trace_sequence_ending_in_nothing);
    g_test_add_data_func("/core/parser/ll/trace_nothing_choice_priority", GINT_TO_POINTER(PB_LL),
                         test_trace_nothing_choice_priority);
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
    g_test_add_func("/core/parser/glr/trace_ambiguous_failure", test_trace_glr_ambiguous_failure);
    g_test_add_data_func("/core/parser/glr/trace_verbose_dump", GINT_TO_POINTER(PB_GLR),
                         test_trace_cf_verbose_dump);

    g_test_add_data_func("/core/parser/ll/trace_structured_expectations", GINT_TO_POINTER(PB_LL),
                         test_trace_structured_expectations);
    g_test_add_data_func("/core/parser/lalr/trace_structured_expectations",
                         GINT_TO_POINTER(PB_LALR), test_trace_structured_expectations);
    g_test_add_data_func("/core/parser/glr/trace_structured_expectations", GINT_TO_POINTER(PB_GLR),
                         test_trace_structured_expectations);

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
