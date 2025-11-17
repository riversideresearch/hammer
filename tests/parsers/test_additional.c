#include "hammer.h"
#include "test_suite.h"

#include <glib.h>
#include <string.h>

// Test butnot edge cases
static void test_butnot_edge_cases(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    
    // Test when p1 matches but p2 also matches with same length - should fail
    const HParser *p1 = h_butnot(h_token((const uint8_t *)"abc", 3), h_token((const uint8_t *)"abc", 3));
    g_check_parse_failed(p1, be, "abc", 3);
    
    // Test when p1 matches but p2 doesn't match (different string) - should succeed
    const HParser *p2 = h_butnot(h_token((const uint8_t *)"ab", 2), h_token((const uint8_t *)"xy", 2));
    g_check_parse_match(p2, be, "ab", 2, "<61.62>");
    
    // Test when p1 fails - should fail
    const HParser *p3 = h_butnot(h_ch('x'), h_ch('y'));
    g_check_parse_failed(p3, be, "a", 1);
}

// Test difference edge cases
static void test_difference_edge_cases(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    
    // Test when p1 matches but p2 doesn't match (different string) - should succeed
    const HParser *p1 = h_difference(h_token((const uint8_t *)"ab", 2), h_token((const uint8_t *)"xy", 2));
    g_check_parse_match(p1, be, "ab", 2, "<61.62>");
    
    // Test when p1 fails - should fail
    const HParser *p2 = h_difference(h_ch('x'), h_ch('y'));
    g_check_parse_failed(p2, be, "a", 1);
}

// Test bytes parser edge cases
static void test_bytes_edge_cases(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    
    // Test zero-length bytes
    const HParser *p0 = h_bytes(0);
    g_check_parse_match(p0, be, "", 0, "<>");
    g_check_parse_match(p0, be, "x", 1, "<>");
    
    // Test single byte
    const HParser *p1 = h_bytes(1);
    g_check_parse_failed(p1, be, "", 0);
    g_check_parse_match(p1, be, "a", 1, "<61>");
    g_check_parse_match(p1, be, "ab", 2, "<61>");
    
    // Test multiple bytes
    const HParser *p3 = h_bytes(3);
    g_check_parse_failed(p3, be, "", 0);
    g_check_parse_failed(p3, be, "a", 1);
    g_check_parse_failed(p3, be, "ab", 2);
    g_check_parse_match(p3, be, "abc", 3, "<61.62.63>");
    g_check_parse_match(p3, be, "abcd", 4, "<61.62.63>");
}

// Test epsilon parser
static void test_epsilon(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    
    const HParser *p = h_epsilon_p();
    // Epsilon always succeeds with NULL ast
    HParseResult *res = h_parse(p, (const uint8_t *)"", 0);
    g_check_cmp_ptr(res, !=, NULL);
    g_check_cmp_ptr(res->ast, ==, NULL);
    h_parse_result_free(res);
    
    res = h_parse(p, (const uint8_t *)"abc", 3);
    g_check_cmp_ptr(res, !=, NULL);
    g_check_cmp_ptr(res->ast, ==, NULL);
    h_parse_result_free(res);
}

// Test end parser edge cases
static void test_end_edge_cases(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    
    // End should succeed at end of input
    const HParser *p = h_end_p();
    HParseResult *res = h_parse(p, (const uint8_t *)"", 0);
    g_check_cmp_ptr(res, !=, NULL);
    g_check_cmp_ptr(res->ast, ==, NULL);
    h_parse_result_free(res);
    
    // End should fail when not at end
    res = h_parse(p, (const uint8_t *)"a", 1);
    g_check_cmp_ptr(res, ==, NULL);
    
    // End after sequence
    const HParser *p2 = h_sequence(h_ch('a'), h_end_p(), NULL);
    g_check_parse_match(p2, be, "a", 1, "(u0x61)");
    g_check_parse_failed(p2, be, "ab", 2);
}

// Test ch parser edge cases
static void test_ch_edge_cases(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    
    // Test various characters
    const HParser *p0 = h_ch(0);
    g_check_parse_match(p0, be, "\0", 1, "u0");
    
    const HParser *p255 = h_ch(255);
    g_check_parse_match(p255, be, "\xff", 1, "u0xff");
    
    const HParser *p_space = h_ch(' ');
    g_check_parse_match(p_space, be, " ", 1, "u0x20");
    g_check_parse_failed(p_space, be, "a", 1);
}

// Test token parser edge cases
static void test_token_edge_cases(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    
    // Test empty token
    const HParser *p0 = h_token((const uint8_t *)"", 0);
    g_check_parse_match(p0, be, "", 0, "<>");
    g_check_parse_match(p0, be, "a", 1, "<>");
    
    // Test single byte token
    const HParser *p1 = h_token((const uint8_t *)"a", 1);
    g_check_parse_match(p1, be, "a", 1, "<61>");
    g_check_parse_failed(p1, be, "b", 1);
    g_check_parse_failed(p1, be, "", 0);
    
    // Test multi-byte token with partial match
    const HParser *p3 = h_token((const uint8_t *)"abc", 3);
    g_check_parse_match(p3, be, "abc", 3, "<61.62.63>");
    g_check_parse_failed(p3, be, "ab", 2);
    g_check_parse_failed(p3, be, "abx", 3);
}

// Test permutation edge cases (two-element only, three-element already tested)
static void test_permutation_two_elements(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    
    // Test two-element permutation
    const HParser *p2 = h_permutation(h_ch('a'), h_ch('b'), NULL);
    g_check_parse_match(p2, be, "ab", 2, "(u0x61 u0x62)");
    g_check_parse_match(p2, be, "ba", 2, "(u0x61 u0x62)");
    g_check_parse_failed(p2, be, "aa", 2);
    g_check_parse_failed(p2, be, "a", 1);
}

// Test bind parser (additional edge cases)
static HParser *bind_continuation_fail(HAllocator *mm__, const HParsedToken *token, void *user_data) {
    (void)mm__;
    (void)user_data;
    // Return NULL to test failure path
    (void)token;
    return NULL;
}

static void test_bind_edge_cases(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    
    // Test bind with continuation that returns NULL
    const HParser *p = h_bind(h_ch('a'), bind_continuation_fail, NULL);
    g_check_parse_failed(p, be, "a", 1);
}

// Test attr_bool parser
static bool attr_predicate(HParseResult *p, void *user_data) {
    (void)user_data;
    if (p->ast && p->ast->token_type == TT_UINT) {
        return p->ast->uint == 'a';
    }
    return false;
}

static bool always_false(HParseResult *p, void *user_data) {
    (void)p;
    (void)user_data;
    return false;
}

static void test_attr_bool(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    
    const HParser *p = h_attr_bool(h_ch('a'), attr_predicate, NULL);
    g_check_parse_match(p, be, "a", 1, "u0x61");
    
    const HParser *p2 = h_attr_bool(h_ch('b'), attr_predicate, NULL);
    g_check_parse_failed(p2, be, "b", 1);
    
    // Test with predicate that always fails
    const HParser *p3 = h_attr_bool(h_ch('a'), always_false, NULL);
    g_check_parse_failed(p3, be, "a", 1);
}

// Test value parser (put/get/free)
static void test_value_put_get_free(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    
    // Test put_value
    HParser *put_p = h_put_value(h_ch('a'), "test_key");
    g_check_parse_match(put_p, be, "a", 1, "u0x61");
    
    // Test get_value (should retrieve stored value)
    HParser *get_p = h_get_value("test_key");
    // Note: get_value requires the value to be stored first, so we need to parse put first
    HParser *seq_p = h_sequence(put_p, get_p, NULL);
    HParseResult *res = h_parse(seq_p, (const uint8_t *)"a", 1);
    // The result should contain both the stored value and the retrieved value
    g_check_cmp_ptr(res, !=, NULL);
    if (res) {
        h_parse_result_free(res);
    }
    
    // Test free_value
    HParser *free_p = h_free_value("test_key");
    // Free should retrieve and remove the value
    HParser *seq2_p = h_sequence(put_p, free_p, NULL);
    res = h_parse(seq2_p, (const uint8_t *)"a", 1);
    g_check_cmp_ptr(res, !=, NULL);
    if (res) {
        h_parse_result_free(res);
    }
}

void register_additional_parser_tests(void) {
    g_test_add_data_func("/core/parser/packrat/butnot_edge_cases", GINT_TO_POINTER(PB_PACKRAT),
                         test_butnot_edge_cases);
    g_test_add_data_func("/core/parser/packrat/difference_edge_cases", GINT_TO_POINTER(PB_PACKRAT),
                         test_difference_edge_cases);
    g_test_add_data_func("/core/parser/packrat/bytes_edge_cases", GINT_TO_POINTER(PB_PACKRAT),
                         test_bytes_edge_cases);
    g_test_add_data_func("/core/parser/packrat/epsilon", GINT_TO_POINTER(PB_PACKRAT), test_epsilon);
    g_test_add_data_func("/core/parser/packrat/end_edge_cases", GINT_TO_POINTER(PB_PACKRAT),
                         test_end_edge_cases);
    g_test_add_data_func("/core/parser/packrat/ch_edge_cases", GINT_TO_POINTER(PB_PACKRAT),
                         test_ch_edge_cases);
    g_test_add_data_func("/core/parser/packrat/token_edge_cases", GINT_TO_POINTER(PB_PACKRAT),
                         test_token_edge_cases);
    g_test_add_data_func("/core/parser/packrat/permutation_two_elements", GINT_TO_POINTER(PB_PACKRAT),
                         test_permutation_two_elements);
    g_test_add_data_func("/core/parser/packrat/bind_edge_cases", GINT_TO_POINTER(PB_PACKRAT), test_bind_edge_cases);
    g_test_add_data_func("/core/parser/packrat/attr_bool_edge_cases", GINT_TO_POINTER(PB_PACKRAT),
                         test_attr_bool);
    g_test_add_data_func("/core/parser/packrat/value_put_get_free", GINT_TO_POINTER(PB_PACKRAT),
                         test_value_put_get_free);
}
