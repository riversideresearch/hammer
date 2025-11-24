#include "hammer.h"
#include "glue.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>

// Test glue.c: h_act_index with NULL p (line 12)
static void test_glue_act_index_null_p(void) {
    HParsedToken *result = h_act_index(0, NULL, NULL);
    g_check_cmp_ptr(result, ==, NULL);
}

// Test glue.c: h_act_index with non-sequence token (line 16)
static void test_glue_act_index_non_sequence(void) {
    HParser *p = h_ch('a');
    h_compile(p, PB_PACKRAT, NULL);
    HParseResult *res = h_parse(p, (const uint8_t *)"a", 1);
    if (res) {
        HParsedToken *result = h_act_index(0, res, NULL);
        g_check_cmp_ptr(result, ==, NULL);
        h_parse_result_free(res);
    }
}

// Test glue.c: h_act_index with NULL token (line 16)
static void test_glue_act_index_null_token(void) {
    HParseResult res;
    res.ast = NULL;
    HParsedToken *result = h_act_index(0, &res, NULL);
    g_check_cmp_ptr(result, ==, NULL);
}

// Test glue.c: h_act_index with negative index (line 22)
static void test_glue_act_index_negative(void) {
    HParser *p = h_sequence(h_ch('a'), h_ch('b'), NULL);
    h_compile(p, PB_PACKRAT, NULL);
    HParseResult *res = h_parse(p, (const uint8_t *)"ab", 2);
    if (res) {
        HParsedToken *result = h_act_index(-1, res, NULL);
        g_check_cmp_ptr(result, ==, NULL);
        h_parse_result_free(res);
    }
}

// Test glue.c: h_act_index with index too large (line 22)
static void test_glue_act_index_too_large(void) {
    HParser *p = h_sequence(h_ch('a'), h_ch('b'), NULL);
    h_compile(p, PB_PACKRAT, NULL);
    HParseResult *res = h_parse(p, (const uint8_t *)"ab", 2);
    if (res) {
        HParsedToken *result = h_act_index(10, res, NULL);
        g_check_cmp_ptr(result, ==, NULL);
        h_parse_result_free(res);
    }
}

// Test glue.c: h_act_index with valid index (line 25)
static void test_glue_act_index_valid(void) {
    HParser *p = h_sequence(h_ch('a'), h_ch('b'), NULL);
    h_compile(p, PB_PACKRAT, NULL);
    HParseResult *res = h_parse(p, (const uint8_t *)"ab", 2);
    if (res) {
        HParsedToken *result = h_act_index(0, res, NULL);
        g_check_cmp_ptr(result, !=, NULL);
        h_parse_result_free(res);
    }
}

// Test glue.c: h_make_bytes (lines 98-102)
static void test_glue_make_bytes(void) {
    HParser *p = h_ch('a');
    h_compile(p, PB_PACKRAT, NULL);
    HParseResult *res = h_parse(p, (const uint8_t *)"a", 1);
    if (res && res->arena) {
        const uint8_t data[] = {0x01, 0x02, 0x03};
        HParsedToken *bytes = h_make_bytes(res->arena, data, 3);
        g_check_cmp_ptr(bytes, !=, NULL);
        if (bytes) {
            g_check_cmp_int(bytes->token_type, ==, TT_BYTES);
            g_check_cmp_int(bytes->bytes.len, ==, 3);
            g_check_cmp_ptr(bytes->bytes.token, ==, data);
        }
        h_parse_result_free(res);
    }
}

// Test glue.c: h_make_sint (lines 105-108)
static void test_glue_make_sint(void) {
    HParser *p = h_ch('a');
    h_compile(p, PB_PACKRAT, NULL);
    HParseResult *res = h_parse(p, (const uint8_t *)"a", 1);
    if (res && res->arena) {
        HParsedToken *sint = h_make_sint(res->arena, -42);
        g_check_cmp_ptr(sint, !=, NULL);
        if (sint) {
            g_check_cmp_int(sint->token_type, ==, TT_SINT);
            g_check_cmp_int64(sint->sint, ==, -42);
        }
        h_parse_result_free(res);
    }
}

// Test glue.c: h_make_sint with large values
static void test_glue_make_sint_large(void) {
    HParser *p = h_ch('a');
    h_compile(p, PB_PACKRAT, NULL);
    HParseResult *res = h_parse(p, (const uint8_t *)"a", 1);
    if (res && res->arena) {
        HParsedToken *sint = h_make_sint(res->arena, INT64_MIN);
        g_check_cmp_ptr(sint, !=, NULL);
        if (sint) {
            g_check_cmp_int64(sint->sint, ==, INT64_MIN);
        }
        h_parse_result_free(res);
    }
}

void register_glue_tests(void) {
    g_test_add_func("/core/glue/act_index_null_p", test_glue_act_index_null_p);
    g_test_add_func("/core/glue/act_index_non_sequence", test_glue_act_index_non_sequence);
    g_test_add_func("/core/glue/act_index_null_token", test_glue_act_index_null_token);
    g_test_add_func("/core/glue/act_index_negative", test_glue_act_index_negative);
    g_test_add_func("/core/glue/act_index_too_large", test_glue_act_index_too_large);
    g_test_add_func("/core/glue/act_index_valid", test_glue_act_index_valid);
    g_test_add_func("/core/glue/make_bytes", test_glue_make_bytes);
    g_test_add_func("/core/glue/make_sint", test_glue_make_sint);
    g_test_add_func("/core/glue/make_sint_large", test_glue_make_sint_large);
}
