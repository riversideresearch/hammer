#include "glue.h"
#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>
#include <stdarg.h>

// Helper function for sequence tests
static HParser *simple_bind_cont(HAllocator *mm__, const HParsedToken *token, void *user_data) {
    (void)mm__;
    (void)user_data;
    (void)token;
    return h_ch('b');
}

// Test sequence.c: sequence_isValidRegular (lines 33-39)
static void test_sequence_isValidRegular(gconstpointer backend) {
    (void)backend;
    HParser *p1 = h_ch('a');
    HParser *p2 = h_ch('b');
    HParser *seq = h_sequence(p1, p2, NULL);

    bool is_regular = seq->vtable->isValidRegular(seq->env);
    g_check_cmp_int(is_regular, ==, true);

    HParser *bind_p = h_bind(h_ch('a'), simple_bind_cont, NULL);
    HParser *seq2 = h_sequence(p1, bind_p, NULL);
    bool is_regular2 = seq2->vtable->isValidRegular(seq2->env);
    g_check_cmp_int(is_regular2, ==, false);
}

// Test sequence.c: sequence_isValidCF false case (line 46)
static void test_sequence_isValidCF_false(gconstpointer backend) {
    (void)backend;
    HParser *bind_p = h_bind(h_ch('a'), simple_bind_cont, NULL);
    HParser *seq = h_sequence(bind_p, h_ch('b'), NULL);

    bool is_cf = seq->vtable->isValidCF(seq->env);
    g_check_cmp_int(is_cf, ==, false);
}

// Test sequence.c: reshape_sequence (lines 51-71)
static void test_reshape_sequence(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *seq = h_sequence(h_ch('a'), h_ignore(h_ch('b')), h_ch('c'), NULL);
    h_compile(seq, be, NULL);
    HParseResult *res = h_parse(seq, (const uint8_t *)"abc", 3);
    if (res) {
        g_check_cmp_ptr(res->ast, !=, NULL);
        h_parse_result_free(res);
    }

    HParser *seq2 = h_sequence(h_ch('a'), h_ignore(h_ch('b')), h_ch('c'), NULL);
    HCFChoice *desugared2 = h_desugar(&system_allocator, NULL, seq2);
    if (desugared2 && desugared2->reshape) {
        HArena *arena = h_new_arena(&system_allocator, 0);
        HParsedToken *seq_token = h_make_seq(arena);
        HParsedToken *elem1 = h_make_uint(arena, 97);
        h_carray_append(seq_token->seq, elem1);
        h_carray_append(seq_token->seq, NULL);
        HParsedToken *elem3 = h_make_uint(arena, 99);
        h_carray_append(seq_token->seq, elem3);
        HParseResult mock_result = {.arena = arena, .ast = seq_token, .bit_length = 24};
        HParsedToken *reshaped = desugared2->reshape(&mock_result, NULL);
        g_check_cmp_ptr(reshaped, !=, NULL);
        h_delete_arena(arena);
    }
}

// Test sequence.c: h_sequence__m (lines 103-108)
static void test_sequence_m(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p1 = h_ch('a');
    HParser *p2 = h_ch('b');
    HParser *seq = h_sequence__m(&system_allocator, p1, p2, NULL);

    g_check_parse_match(seq, be, "ab", 2, "(u0x61 u0x62)");
}

// Helper function to test h_sequence__v
static HParser *test_sequence_v_helper(HParser *p1, ...) {
    va_list ap;
    va_start(ap, p1);
    HParser *result = h_sequence__v(p1, ap);
    va_end(ap);
    return result;
}

// Test sequence.c: h_sequence__v (line 111)
static void test_sequence_v(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p1 = h_ch('a');
    HParser *p2 = h_ch('b');
    HParser *seq = test_sequence_v_helper(p1, p2, NULL);

    g_check_parse_match(seq, be, "ab", 2, "(u0x61 u0x62)");
}

// Test sequence.c: h_sequence__a (line 146)
static void test_sequence_a(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p1 = h_ch('a');
    HParser *p2 = h_ch('b');
    HParser *args[] = {p1, p2, NULL};

    HParser *seq = h_sequence__a((void **)args);
    g_check_parse_match(seq, be, "ab", 2, "(u0x61 u0x62)");
}

// Test sequence.c: h_drop_from___m (lines 182-190)
static void test_drop_from_m(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *seq = h_sequence(h_ch('a'), h_ch('b'), h_ch('c'), NULL);
    HParser *dropped = h_drop_from___m(&system_allocator, seq, 1, -1);

    g_check_parse_match(dropped, be, "abc", 3, "(u0x61 u0x63)");

    HParser *dropped_null = h_drop_from___m(&system_allocator, NULL, 0, -1);
    g_check_cmp_ptr(dropped_null, ==, NULL);
}

// Helper function to test h_drop_from___v
static HParser *test_drop_from_v_helper(HParser *p, ...) {
    va_list ap;
    va_start(ap, p);
    HParser *result = h_drop_from___v(p, ap);
    va_end(ap);
    return result;
}

// Test sequence.c: h_drop_from___v (lines 193-195)
static void test_drop_from_v(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *seq = h_sequence(h_ch('a'), h_ch('b'), h_ch('c'), NULL);
    HParser *dropped = test_drop_from_v_helper(seq, 1, -1);

    g_check_parse_match(dropped, be, "abc", 3, "(u0x61 u0x63)");
}

// Helper function to test h_drop_from___mv with NULL
static HParser *test_drop_from_mv_null_helper(HAllocator *mm__, HParser *p, ...) {
    va_list ap;
    va_start(ap, p);
    HParser *result = h_drop_from___mv(mm__, p, ap);
    va_end(ap);
    return result;
}

// Test sequence.c: h_drop_from___mv with NULL (line 206)
static void test_drop_from_mv_null(gconstpointer backend) {
    (void)backend;

    HParser *dropped = test_drop_from_mv_null_helper(&system_allocator, NULL, -1);

    g_check_cmp_ptr(dropped, ==, NULL);
}

// Test sequence.c: h_drop_from___a (line 233)
static void test_drop_from_a(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *seq = h_sequence(h_ch('a'), h_ch('b'), h_ch('c'), NULL);
    h_compile(seq, be, NULL);
    int indices[] = {0, 1, 2, -1};
    void *args[] = {seq, indices};

    HParser *dropped = h_drop_from___a((void **)args);
    g_check_cmp_ptr(dropped, !=, NULL);
}

// Test sequence.c: h_drop_from___ma (lines 235-255)
static void test_drop_from_ma(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *seq = h_sequence(h_ch('a'), h_ch('b'), h_ch('c'), NULL);
    h_compile(seq, be, NULL);
    int indices[] = {2, -1};
    void *args[] = {seq, indices};

    HParser *dropped = h_drop_from___ma(&system_allocator, (void **)args);
    h_compile(dropped, be, NULL);
    g_check_parse_match(dropped, be, "abc", 3, "(u0x61 u0x62)");
}

void register_sequence_tests(void) {
    g_test_add_data_func("/core/parser/packrat/sequence_isValidRegular",
                         GINT_TO_POINTER(PB_PACKRAT), test_sequence_isValidRegular);
    g_test_add_data_func("/core/parser/packrat/sequence_isValidCF_false",
                         GINT_TO_POINTER(PB_PACKRAT), test_sequence_isValidCF_false);
    g_test_add_data_func("/core/parser/packrat/reshape_sequence", GINT_TO_POINTER(PB_PACKRAT),
                         test_reshape_sequence);
    g_test_add_data_func("/core/parser/packrat/sequence_m", GINT_TO_POINTER(PB_PACKRAT),
                         test_sequence_m);
    g_test_add_data_func("/core/parser/packrat/sequence_v", GINT_TO_POINTER(PB_PACKRAT),
                         test_sequence_v);
    g_test_add_data_func("/core/parser/packrat/sequence_a", GINT_TO_POINTER(PB_PACKRAT),
                         test_sequence_a);
    g_test_add_data_func("/core/parser/packrat/drop_from_m", GINT_TO_POINTER(PB_PACKRAT),
                         test_drop_from_m);
    g_test_add_data_func("/core/parser/packrat/drop_from_v", GINT_TO_POINTER(PB_PACKRAT),
                         test_drop_from_v);
    g_test_add_data_func("/core/parser/packrat/drop_from_mv_null", GINT_TO_POINTER(PB_PACKRAT),
                         test_drop_from_mv_null);
    g_test_add_data_func("/core/parser/packrat/drop_from_a", GINT_TO_POINTER(PB_PACKRAT),
                         test_drop_from_a);
    g_test_add_data_func("/core/parser/packrat/drop_from_ma", GINT_TO_POINTER(PB_PACKRAT),
                         test_drop_from_ma);
}
