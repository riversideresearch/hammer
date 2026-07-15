#include "glue.h"
#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

#include <stdio.h>

#include <glib.h>

// On a well-formed input it must return the same successful result.
// The trace/error machinery must not disturb the parse itself.
// the 
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
    HParseResult *res = h_parse_debug(p, (const uint8_t *)"ax", 2, &err);
    g_check_cmp_ptr(res, ==, NULL);

    // The failure-position fields are only populated when the library is built
    // with -DHAMMER_TRACE_AST; without it the struct is left zeroed. n_deepest
    // being non-zero is the signal that the tracer ran, so guard on it.
    g_check_cmp_size(err.n_deepest, >, 0);
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
    HParseResult *res = h_parse_debug(p, input, sizeof(input), NULL);
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

    // Only one byte is offered (length 1). The trailing 0 is padding: on an
    // end-of-input failure the tracer reads input[length], so a byte must exist
    // there to keep this test free of out-of-bounds reads.
    uint8_t input[] = {1};
    HParseError err;
    HParseResult *res = h_parse_debug(p, input, 1, &err);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_size(err.index, ==, 1);
    g_check_cmp_int(err.actual, ==, 0);
    g_check_cmp_ptr(err.deepest_parsers[0], !=, NULL);

}

// debugtest/parser5.c: choice of two tokens; "YoS" matches neither "YES" nor
// "NO".
static void test_trace_choice_token_mismatch(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p = h_choice(h_token((const uint8_t *)"YES", 3),
                          h_token((const uint8_t *)"NO", 2), NULL);
    h_compile(p, be, NULL);

    uint8_t input[] = {'Y', 'o', 'S'};
    HParseError err;
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &err);
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
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &err);
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
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &err);
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
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &err);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_int(err.n_deepest, >, 0);
    if (err.n_deepest > 0) {
        g_check_cmp_size(err.index, ==, 0);
        g_check_cmp_int(err.actual, ==, 0xFF);
    }
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
    HParseResult *res = h_parse_debug(p, input, sizeof(input), &err);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_int(err.n_deepest, >, 0);
    if (err.n_deepest > 0) {
        g_check_cmp_size(err.index, ==, 1);
        g_check_cmp_int(err.actual, ==, 0xAA);
    }
}

// debugtest/parser10.c: h_action() sums a pair of bytes; 10 + 20 = 30.
static void test_trace_action_sum(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *pair = h_sequence(h_uint8(), h_uint8(), NULL);
    HParser *p = h_action(pair, trace_sum_action, NULL);
    h_compile(p, be, NULL);

    uint8_t input[] = {10, 20};
    HParseResult *res = h_parse_debug(p, input, sizeof(input), NULL);
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
    HParseResult *res = h_parse_debug(list, input, sizeof(input), &err);
    g_check_cmp_ptr(res, ==, NULL);
    g_check_cmp_int(err.n_deepest, >, 0);
    if (err.n_deepest > 0) {
        g_check_cmp_size(err.index, ==, 7);
        g_check_cmp_int(err.actual, ==, 0x9);
    }
}

void register_trace_tests(void) {
    g_test_add_data_func("/core/parser/packrat/trace_debug_success", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_debug_success);
    g_test_add_data_func("/core/parser/packrat/trace_debug_error_on_failure",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_debug_error_on_failure);
    g_test_add_data_func("/core/parser/packrat/trace_debug_null_error", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_debug_null_error);

    // Ported from the debugtest/ sample parsers.
    g_test_add_data_func("/core/parser/packrat/trace_uint8_success", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_uint8_success);
    g_test_add_data_func("/core/parser/packrat/trace_sequence_truncated",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_sequence_truncated);
    g_test_add_data_func("/core/parser/packrat/trace_choice_token_mismatch",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_choice_token_mismatch);
    g_test_add_data_func("/core/parser/packrat/trace_many1_no_letter",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_many1_no_letter);
    g_test_add_data_func("/core/parser/packrat/trace_middle_bad_close",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_middle_bad_close);
    g_test_add_data_func("/core/parser/packrat/trace_int_range_reject",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_int_range_reject);
    g_test_add_data_func("/core/parser/packrat/trace_attr_bool_checksum",
                         GINT_TO_POINTER(PB_PACKRAT), test_trace_attr_bool_checksum);
    g_test_add_data_func("/core/parser/packrat/trace_action_sum", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_action_sum);
    g_test_add_data_func("/core/parser/packrat/trace_nested_list", GINT_TO_POINTER(PB_PACKRAT),
                         test_trace_nested_list);
}
