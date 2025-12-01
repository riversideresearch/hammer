#include "glue.h"
#include "hammer.h"
#include "test_suite.h"

#include <glib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>

static void test_tt_user(void) {
    g_check_cmp_int32(TT_USER, >, TT_NONE);
    g_check_cmp_int32(TT_USER, >, TT_BYTES);
    g_check_cmp_int32(TT_USER, >, TT_SINT);
    g_check_cmp_int32(TT_USER, >, TT_UINT);
    g_check_cmp_int32(TT_USER, >, TT_SEQUENCE);
    g_check_cmp_int32(TT_USER, >, TT_ERR);
}

static void test_tt_registry(void) {
    int id = h_allocate_token_type("com.upstandinghackers.test.token_type");
    g_check_cmp_int32(id, >=, TT_USER);
    int id2 = h_allocate_token_type("com.upstandinghackers.test.token_type_2");
    g_check_cmp_int32(id2, !=, id);
    g_check_cmp_int32(id2, >=, TT_USER);
    g_check_cmp_int32(id, ==, h_get_token_type_number("com.upstandinghackers.test.token_type"));
    g_check_cmp_int32(id2, ==, h_get_token_type_number("com.upstandinghackers.test.token_type_2"));
    g_check_string("com.upstandinghackers.test.token_type", ==, h_get_token_type_name(id));
    g_check_string("com.upstandinghackers.test.token_type_2", ==, h_get_token_type_name(id2));
    if (h_get_token_type_name(0) != NULL) {
        g_test_message("Unknown token type should not return a name");
        g_test_fail();
    }
    g_check_cmp_int32(h_get_token_type_number("com.upstandinghackers.test.unkown_token_type"), ==,
                      0);
}

// Helper function for out-of-memory test allocator
static void *fail_alloc(HAllocator *mm__, size_t size) {
    if (size - 0xdead <= 0x30) // allow for overhead of arena link structure
        return NULL;
    return system_allocator.alloc(&system_allocator, size);
}

// Helper function for out-of-memory test realloc
static void *fail_realloc(HAllocator *mm__, void *ptr, size_t size) {
    return system_allocator.realloc(&system_allocator, ptr, size);
}

// Helper function for out-of-memory test free
static void fail_free(HAllocator *mm__, void *ptr) {
    return system_allocator.free(&system_allocator, ptr);
}
static HAllocator fail_allocator = {fail_alloc, fail_realloc, fail_free};

// Helper function for out-of-memory test action
static HParsedToken *act_oom(const HParseResult *r, void *user) {
    void *buf = h_arena_malloc(r->arena, 0xdead);
    assert(buf != NULL);
    return NULL; // succeed with null token
}

static void test_oom(void) {
    HParser *p = h_action(h_ch('x'), act_oom, NULL);
    // this should always fail, but never crash

    // sanity-check: parses should succeed with the normal allocator...
    g_check_parse_ok(p, PB_PACKRAT, "x", 1);
    g_check_parse_chunks_ok(p, PB_PACKRAT, "", 0, "x", 1);

    // ...and fail gracefully with the broken one
    HAllocator *mm__ = &fail_allocator;
    g_check_parse_failed__m(mm__, p, PB_PACKRAT, "x", 1);
    g_check_parse_chunks_failed__m(mm__, p, PB_PACKRAT, "", 0, "x", 1);
}

static void test_glue_act_index(void) {
    HParser *p = h_sequence(h_ch('a'), h_ch('b'), h_ch('c'), NULL);

    // Use H_ACT_APPLY to create wrapper actions
    H_ACT_APPLY(act_index_0, h_act_index, 0);
    H_ACT_APPLY(act_index_1, h_act_index, 1);
    H_ACT_APPLY(act_index_2, h_act_index, 2);
    H_ACT_APPLY(act_index_neg, h_act_index, -1);
    H_ACT_APPLY(act_index_large, h_act_index, 10);

    HParser *act0 = h_action(p, act_index_0, NULL);
    HParser *act1 = h_action(p, act_index_1, NULL);
    HParser *act2 = h_action(p, act_index_2, NULL);
    HParser *act_neg = h_action(p, act_index_neg, NULL);
    HParser *act_large = h_action(p, act_index_large, NULL);

    g_check_parse_match(act0, PB_PACKRAT, "abc", 3, "u0x61");
    g_check_parse_match(act1, PB_PACKRAT, "abc", 3, "u0x62");
    g_check_parse_match(act2, PB_PACKRAT, "abc", 3, "u0x63");
    // When h_act_index returns NULL, check if parse succeeds with NULL ast
    HParseResult *res_neg = h_parse(act_neg, (const uint8_t *)"abc", 3);
    if (res_neg != NULL && res_neg->ast == NULL) {
        h_parse_result_free(res_neg);
    } else if (res_neg == NULL) {
        // Parse failed, which is also acceptable
    } else {
        h_parse_result_free(res_neg);
        g_test_fail();
    }

    HParseResult *res_large = h_parse(act_large, (const uint8_t *)"abc", 3);
    if (res_large != NULL && res_large->ast == NULL) {
        h_parse_result_free(res_large);
    } else if (res_large == NULL) {
        // Parse failed, which is also acceptable
    } else {
        h_parse_result_free(res_large);
        g_test_fail();
    }

    // Test with non-sequence token - h_act_index returns NULL for non-sequences
    HParser *single = h_action(h_ch('x'), act_index_0, NULL);
    HParseResult *res_single = h_parse(single, (const uint8_t *)"x", 1);
    if (res_single != NULL && res_single->ast == NULL) {
        h_parse_result_free(res_single);
    } else if (res_single == NULL) {
        // Parse failed, which is also acceptable
    } else {
        h_parse_result_free(res_single);
        g_test_fail();
    }
}

static void test_glue_act_first_second_last(void) {
    HParser *p = h_sequence(h_ch('a'), h_ch('b'), h_ch('c'), NULL);
    HParser *act_first = h_action(p, h_act_first, NULL);
    HParser *act_second = h_action(p, h_act_second, NULL);
    HParser *act_last = h_action(p, h_act_last, NULL);

    g_check_parse_match(act_first, PB_PACKRAT, "abc", 3, "u0x61");
    g_check_parse_match(act_second, PB_PACKRAT, "abc", 3, "u0x62");
    g_check_parse_match(act_last, PB_PACKRAT, "abc", 3, "u0x63");

    // Test with two-element sequence
    HParser *p2 = h_sequence(h_ch('x'), h_ch('y'), NULL);
    HParser *act_last2 = h_action(p2, h_act_last, NULL);
    g_check_parse_match(act_last2, PB_PACKRAT, "xy", 2, "u0x79");
}

static void test_glue_act_ignore(void) {
    HParser *p = h_action(h_sequence(h_ch('a'), h_ch('b'), NULL), h_act_ignore, NULL);
    // h_act_ignore returns NULL, so parse succeeds but with NULL ast
    HParseResult *res = h_parse(p, (const uint8_t *)"ab", 2);
    g_check_cmp_ptr(res, !=, NULL);
    g_check_cmp_ptr(res->ast, ==, NULL);
    h_parse_result_free(res);
}

HParsedToken *make_user_action(const HParseResult *p, void *user) {
    int *type_ptr = (int *)user;
    return h_make(p->arena, (HTokenType)*type_ptr, (void *)0x1234);
}

HParsedToken *make_double_action(const HParseResult *p, void *user) {
    return h_make_double(p->arena, 3.14159);
}

HParsedToken *make_float_action(const HParseResult *p, void *user) {
    return h_make_float(p->arena, 2.718f);
}

HParsedToken *make_double_action2(const HParseResult *p, void *user) {
    return h_make_double(p->arena, 3.14);
}

HParsedToken *make_float_action2(const HParseResult *p, void *user) {
    return h_make_float(p->arena, 2.5f);
}

static void test_glue_make_functions(void) {
    // Test h_make with user token type
    int token_type = h_allocate_token_type("com.test.mytoken");
    HParser *p = h_action(h_ch('x'), make_user_action, &token_type);
    HParseResult *res = h_parse(p, (const uint8_t *)"x", 1);
    g_check_cmp_int(res->ast->token_type, ==, token_type);
    h_parse_result_free(res);

    // Test h_make_double
    HParser *p_double = h_action(h_epsilon_p(), make_double_action, NULL);
    res = h_parse(p_double, (const uint8_t *)"", 0);
    g_check_cmp_int(res->ast->token_type, ==, TT_DOUBLE);
    g_check_cmpdouble(res->ast->dbl, ==, 3.14159);
    h_parse_result_free(res);

    // Test h_make_float
    HParser *p_float = h_action(h_epsilon_p(), make_float_action, NULL);
    res = h_parse(p_float, (const uint8_t *)"", 0);
    g_check_cmp_int(res->ast->token_type, ==, TT_FLOAT);
    g_check_cmpfloat(res->ast->flt, ==, 2.718f);
    h_parse_result_free(res);
}

// Helper function for flatten test
static HParsedToken *flatten_action(const HParseResult *p, void *user) {
    return (HParsedToken *)h_seq_flatten(p->arena, p->ast);
}

static void test_glue_seq_flatten(void) {
    // Create nested sequences manually
    HParser *p = h_sequence(h_ch('a'), h_sequence(h_ch('b'), h_ch('c'), NULL), h_ch('d'), NULL);
    HParser *p_flatten = h_action(p, flatten_action, NULL);
    g_check_parse_match(p_flatten, PB_PACKRAT, "abcd", 4, "(u0x61 u0x62 u0x63 u0x64)");

    // Test with non-sequence (should become singleton)
    HParser *p_single = h_ch('x');
    HParser *p_flatten_single = h_action(p_single, flatten_action, NULL);
    g_check_parse_match(p_flatten_single, PB_PACKRAT, "x", 1, "(u0x78)");
}

HParsedToken *snoc_action(const HParseResult *p, void *user) {
    HParsedToken *seq = (HParsedToken *)p->ast;
    HParsedToken *tok = h_make_uint(p->arena, 42);
    h_seq_snoc(seq, tok);
    return seq;
}

HParsedToken *append_action(const HParseResult *p, void *user) {
    HParsedToken *seq1 = (HParsedToken *)p->ast;
    // Parse second sequence
    HParseResult *res2 = h_parse((HParser *)user, (const uint8_t *)"cd", 2);
    HParsedToken *seq2 = (HParsedToken *)res2->ast;
    h_seq_append(seq1, seq2);
    h_parse_result_free(res2);
    return seq1;
}

static void test_glue_seq_append_snoc(void) {
    // Test h_seq_snoc
    HParser *p = h_sequence(h_ch('a'), NULL);
    HParser *p_snoc = h_action(p, snoc_action, NULL);
    g_check_parse_match(p_snoc, PB_PACKRAT, "a", 1, "(u0x61 u0x2a)");

    // Test h_seq_append
    HParser *p1 = h_sequence(h_ch('a'), h_ch('b'), NULL);
    HParser *p2 = h_sequence(h_ch('c'), h_ch('d'), NULL);
    HParser *p_append = h_action(p1, append_action, p2);
    g_check_parse_match(p_append, PB_PACKRAT, "ab", 2, "(u0x61 u0x62 u0x63 u0x64)");
}

HParsedToken *test_vpath_helper(const HParsedToken *p, ...) {
    va_list va;
    va_start(va, p);
    int i0 = va_arg(va, int);
    HParsedToken *result = h_seq_index_vpath(p, i0, va);
    va_end(va);
    return result;
}

static void test_glue_seq_index_vpath(void) {
    // Create nested sequence: ((a b) (c d))
    HParser *inner1 = h_sequence(h_ch('a'), h_ch('b'), NULL);
    HParser *inner2 = h_sequence(h_ch('c'), h_ch('d'), NULL);
    HParser *outer = h_sequence(inner1, inner2, NULL);

    HParseResult *res = h_parse(outer, (const uint8_t *)"abcd", 4);

    // Test h_seq_index_path (which uses va_list internally)
    HParsedToken *tok0 = h_seq_index_path(res->ast, 0, -1);
    g_check_cmp_int(tok0->token_type, ==, TT_SEQUENCE);

    HParsedToken *tok01 = h_seq_index_path(res->ast, 0, 1, -1);
    g_check_cmp_int(tok01->token_type, ==, TT_UINT);
    g_check_cmp_int64(tok01->uint, ==, 0x62); // 'b'

    // Test h_seq_index_vpath directly
    HParsedToken *tok_vpath = test_vpath_helper(res->ast, 0, 1, -1);
    g_check_cmp_int(tok_vpath->token_type, ==, TT_UINT);
    g_check_cmp_int64(tok_vpath->uint, ==, 0x62);

    h_parse_result_free(res);
}

static void test_pprint_basic(void) {
    // Test h_pprint with various token types
    HParser *p_bytes = h_bytes(3);
    HParseResult *res = h_parse(p_bytes, (const uint8_t *)"abc", 3);

    // Test h_pprint with bytes
    FILE *tmp = tmpfile();
    h_pprint(tmp, res->ast, 0, 2);
    fseek(tmp, 0, SEEK_SET);
    char buf[256];
    if (fgets(buf, sizeof(buf), tmp) == NULL) {
        // Handle error case
        buf[0] = '\0';
    }
    fclose(tmp);

    // Test h_pprintln
    tmp = tmpfile();
    h_pprintln(tmp, res->ast);
    fclose(tmp);

    h_parse_result_free(res);

    // Test with sequence
    HParser *p_seq = h_sequence(h_ch('a'), h_ch('b'), NULL);
    res = h_parse(p_seq, (const uint8_t *)"ab", 2);
    tmp = tmpfile();
    h_pprint(tmp, res->ast, 0, 2);
    fclose(tmp);
    h_parse_result_free(res);

    // Test with integers
    HParser *p_int = h_int64();
    res = h_parse(p_int, (const uint8_t *)"\x00\x00\x00\x00\x00\x00\x00\x42", 8);
    tmp = tmpfile();
    h_pprint(tmp, res->ast, 0, 2);
    fclose(tmp);
    h_parse_result_free(res);

    // Test with null token
    tmp = tmpfile();
    h_pprint(tmp, NULL, 0, 2);
    fclose(tmp);
}

static void test_pprint_write_result_unamb(void) {
    // Test h_write_result_unamb with various token types
    HParser *p_bytes = h_bytes(2);
    HParseResult *res = h_parse(p_bytes, (const uint8_t *)"ab", 2);
    char *unamb = h_write_result_unamb(res->ast);
    g_check_string(unamb, ==, "<61.62>");
    (&system_allocator)->free(&system_allocator, unamb);
    h_parse_result_free(res);

    // Test with sequence
    HParser *p_seq = h_sequence(h_ch('x'), h_ch('y'), NULL);
    res = h_parse(p_seq, (const uint8_t *)"xy", 2);
    unamb = h_write_result_unamb(res->ast);
    g_check_string(unamb, ==, "(u0x78 u0x79)");
    (&system_allocator)->free(&system_allocator, unamb);
    h_parse_result_free(res);

    // Test with integers
    HParser *p_uint = h_uint64();
    res = h_parse(p_uint, (const uint8_t *)"\x00\x00\x00\x00\x00\x00\x00\x2a", 8);
    unamb = h_write_result_unamb(res->ast);
    g_check_string(unamb, ==, "u0x2a");
    (&system_allocator)->free(&system_allocator, unamb);
    h_parse_result_free(res);

    // Test with signed integers (negative) - -2 in little-endian
    HParser *p_sint = h_int64();
    res = h_parse(p_sint, (const uint8_t *)"\xff\xff\xff\xff\xff\xff\xff\xfe", 8);
    unamb = h_write_result_unamb(res->ast);
    g_check_string(unamb, ==, "s-0x2");
    (&system_allocator)->free(&system_allocator, unamb);
    h_parse_result_free(res);

    // Test with doubles
    HParser *p_double = h_action(h_epsilon_p(), make_double_action2, NULL);
    res = h_parse(p_double, (const uint8_t *)"", 0);
    unamb = h_write_result_unamb(res->ast);
    // Just check it doesn't crash and produces some output
    g_check_cmp_int(strlen(unamb), >, 0);
    (&system_allocator)->free(&system_allocator, unamb);
    h_parse_result_free(res);

    // Test with floats
    HParser *p_float = h_action(h_epsilon_p(), make_float_action2, NULL);
    res = h_parse(p_float, (const uint8_t *)"", 0);
    unamb = h_write_result_unamb(res->ast);
    g_check_cmp_int(strlen(unamb), >, 0);
    (&system_allocator)->free(&system_allocator, unamb);
    h_parse_result_free(res);

    // Test with NULL
    unamb = h_write_result_unamb(NULL);
    g_check_string(unamb, ==, "NULL");
    (&system_allocator)->free(&system_allocator, unamb);
}

void register_misc_tests(void) {
    g_test_add_func("/core/misc/tt_user", test_tt_user);
    g_test_add_func("/core/misc/tt_registry", test_tt_registry);
    g_test_add_func("/core/misc/oom", test_oom);
    g_test_add_func("/core/misc/glue_act_index", test_glue_act_index);
    g_test_add_func("/core/misc/glue_act_first_second_last", test_glue_act_first_second_last);
    g_test_add_func("/core/misc/glue_act_ignore", test_glue_act_ignore);
    g_test_add_func("/core/misc/glue_make_functions", test_glue_make_functions);
    g_test_add_func("/core/misc/glue_seq_flatten", test_glue_seq_flatten);
    g_test_add_func("/core/misc/glue_seq_append_snoc", test_glue_seq_append_snoc);
    g_test_add_func("/core/misc/glue_seq_index_vpath", test_glue_seq_index_vpath);
    g_test_add_func("/core/misc/pprint_basic", test_pprint_basic);
    g_test_add_func("/core/misc/pprint_write_result_unamb", test_pprint_write_result_unamb);
}
