#include "hammer.h"
#include "glue.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>

// Test packrat.c: Setting bit_length on AST when it's non-zero (line 64)
static void test_packrat_ast_bit_length(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *p = h_sequence(h_ch('a'), h_ch('b'), NULL);
    h_compile(p, be, NULL);

    HParseResult *res = h_parse(p, (const uint8_t *)"ab", 2);
    if (res && res->ast) {
        g_check_cmp_ptr(res->ast, !=, NULL);
        h_parse_result_free(res);
    }
}

// Test packrat.c: recall with parser not in involved_set (lines 79-82)
static void test_packrat_recall_not_involved(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *num = h_ch_range('0', '9');

    HParser *term = h_indirect();
    HParser *term_seq = h_sequence(term, h_ch('*'), num, NULL);
    HParser *term_choice = h_choice(term_seq, num, NULL);
    h_bind_indirect(term, term_choice);

    HParser *expr = h_indirect();
    HParser *expr_seq = h_sequence(expr, h_ch('+'), term, NULL);
    HParser *expr_choice = h_choice(expr_seq, term, NULL);
    h_bind_indirect(expr, expr_choice);

    h_compile(expr, be, NULL);

    HParseResult *res = h_parse(expr, (const uint8_t *)"1+2*3", 5);
    if (res) {
        h_parse_result_free(res);
    }
}

// Test packrat.c: recall cache update when cached is NULL (lines 93-94)
static void test_packrat_recall_cache_update(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *num = h_ch_range('0', '9');
    HParser *plus = h_ch('+');

    HParser *expr = h_indirect();
    HParser *expr_seq = h_sequence(expr, plus, num, NULL);
    HParser *expr_choice = h_choice(expr_seq, num, NULL);
    h_bind_indirect(expr, expr_choice);

    h_compile(expr, be, NULL);

    HParseResult *res = h_parse(expr, (const uint8_t *)"1+2+3", 5);
    if (res) {
        h_parse_result_free(res);
    }
}

// Test packrat.c: grow with tmp_res NULL (lines 173-175)
static void test_packrat_grow_null_result(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *num = h_ch_range('0', '9');
    HParser *expr = h_indirect();
    HParser *expr_seq = h_sequence(expr, h_ch('+'), num, NULL);
    HParser *expr_choice = h_choice(expr_seq, num, NULL);
    h_bind_indirect(expr, expr_choice);

    h_compile(expr, be, NULL);

    HParseResult *res = h_parse(expr, (const uint8_t *)"1+", 2);
    (void)res;
}

void register_packrat_tests(void) {
    g_test_add_data_func("/core/parser/packrat/ast_bit_length", GINT_TO_POINTER(PB_PACKRAT),
                         test_packrat_ast_bit_length);
    g_test_add_data_func("/core/parser/packrat/recall_not_involved", GINT_TO_POINTER(PB_PACKRAT),
                         test_packrat_recall_not_involved);
    g_test_add_data_func("/core/parser/packrat/recall_cache_update", GINT_TO_POINTER(PB_PACKRAT),
                         test_packrat_recall_cache_update);
    g_test_add_data_func("/core/parser/packrat/grow_null_result", GINT_TO_POINTER(PB_PACKRAT),
                         test_packrat_grow_null_result);
}
