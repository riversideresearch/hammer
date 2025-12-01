#include "cfgrammar.h"
#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>

static void test_desugar_ch(void) {
    const HParser *p = h_ch('a');
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHAR);
}

static void test_desugar_token(void) {
    const HParser *p = h_token((const uint8_t *)"abc", 3);
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    // Token desugars to a sequence of characters
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_epsilon(void) {
    const HParser *p = h_epsilon_p();
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    // Epsilon desugars to an empty sequence
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_end(void) {
    const HParser *p = h_end_p();
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_END);
}

static void test_desugar_nothing(void) {
    const HParser *p = h_nothing_p();
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    // Nothing desugars to a choice with no alternatives (empty choice)
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_charset(void) {
    uint8_t options[3] = {'a', 'b', 'c'};
    const HParser *p = h_in(options, 3);
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHARSET);
}

static void test_desugar_charset_not_in(void) {
    uint8_t options[3] = {'a', 'b', 'c'};
    const HParser *p = h_not_in(options, 3);
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHARSET);
}

static void test_desugar_bits(void) {
    const HParser *p = h_bits(8, false);
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    // Bits parser desugars to a choice (not directly to HCF_BITS)
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_bits_signed(void) {
    const HParser *p = h_bits(8, true);
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    // Bits parser desugars to a choice (not directly to HCF_BITS)
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_int_range(void) {
    const HParser *p = h_int_range(h_uint8(), 0, 255);
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    // int_range desugars to a charset (HCF_CHARSET = 2)
    g_check_cmp_int(desugared->type, ==, HCF_CHARSET);
}

static void test_desugar_sequence(void) {
    const HParser *p = h_sequence(h_ch('a'), h_ch('b'), h_ch('c'), NULL);
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_choice(void) {
    const HParser *p = h_choice(h_ch('a'), h_ch('b'), h_ch('c'), NULL);
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_many(void) {
    const HParser *p = h_many(h_ch('a'));
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_many1(void) {
    const HParser *p = h_many1(h_ch('a'));
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_optional(void) {
    const HParser *p = h_optional(h_ch('a'));
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_ignore(void) {
    HParser *p = h_ignore(h_ch('a'));
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_ignoreseq(void) {
    // h_ignoreseq is not in the public API, so test ignoreseq through h_left/h_right/h_middle
    const HParser *p = h_left(h_ch('a'), h_ch('b'));
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_indirect(void) {
    HParser *p = h_indirect();
    h_bind_indirect(p, h_ch('a'));
    // Indirect parser desugars the inner parser
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    // The desugared form should be the same as desugaring h_ch('a') directly
    g_check_cmp_int(desugared->type, ==, HCF_CHAR);
}

// Test desugar for action parser
static HParsedToken *test_action(const HParseResult *p, void *user_data) {
    (void)user_data;
    return (HParsedToken *)p->ast;
}

static void test_desugar_action(void) {
    const HParser *p = h_action(h_ch('a'), test_action, NULL);
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static bool test_predicate(HParseResult *p, void *user_data) {
    (void)user_data;
    return p->ast != NULL;
}

static void test_desugar_attr_bool(void) {
    const HParser *p = h_attr_bool(h_ch('a'), test_predicate, NULL);
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_whitespace(void) {
    const HParser *p = h_whitespace(h_ch('a'));
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_nested_sequence(void) {
    const HParser *p = h_sequence(h_sequence(h_ch('a'), h_ch('b'), NULL), h_ch('c'), NULL);
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_nested_choice(void) {
    const HParser *p = h_choice(h_choice(h_ch('a'), h_ch('b'), NULL), h_ch('c'), NULL);
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_many_with_sep(void) {
    const HParser *p = h_sepBy(h_ch('a'), h_ch(','));
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_many1_with_sep(void) {
    const HParser *p = h_sepBy1(h_ch('a'), h_ch(','));
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, p);
    g_check_cmp_ptr(desugared, !=, NULL);
    g_check_cmp_int(desugared->type, ==, HCF_CHOICE);
}

static void test_desugar_to_cfg(void) {
    const HParser *p = h_sequence(h_ch('a'), h_ch('b'), NULL);
    HCFGrammar *g = h_cfgrammar(&system_allocator, p);
    g_check_cmp_ptr(g, !=, NULL);
    g_check_cmp_ptr(g->start, !=, NULL);
    h_cfgrammar_free(g);
}

static void test_desugar_to_cfg_choice(void) {
    const HParser *p = h_choice(h_ch('a'), h_ch('b'), NULL);
    HCFGrammar *g = h_cfgrammar(&system_allocator, p);
    g_check_cmp_ptr(g, !=, NULL);
    g_check_cmp_ptr(g->start, !=, NULL);
    h_cfgrammar_free(g);
}

static void test_desugar_to_cfg_many(void) {
    const HParser *p = h_many(h_ch('a'));
    HCFGrammar *g = h_cfgrammar(&system_allocator, p);
    g_check_cmp_ptr(g, !=, NULL);
    g_check_cmp_ptr(g->start, !=, NULL);
    h_cfgrammar_free(g);
}

void register_desugar_tests(void) {
    g_test_add_func("/core/desugar/ch", test_desugar_ch);
    g_test_add_func("/core/desugar/token", test_desugar_token);
    g_test_add_func("/core/desugar/epsilon", test_desugar_epsilon);
    g_test_add_func("/core/desugar/end", test_desugar_end);
    g_test_add_func("/core/desugar/nothing", test_desugar_nothing);
    g_test_add_func("/core/desugar/charset", test_desugar_charset);
    g_test_add_func("/core/desugar/charset_not_in", test_desugar_charset_not_in);
    g_test_add_func("/core/desugar/bits", test_desugar_bits);
    g_test_add_func("/core/desugar/bits_signed", test_desugar_bits_signed);
    g_test_add_func("/core/desugar/int_range", test_desugar_int_range);
    g_test_add_func("/core/desugar/sequence", test_desugar_sequence);
    g_test_add_func("/core/desugar/choice", test_desugar_choice);
    g_test_add_func("/core/desugar/many", test_desugar_many);
    g_test_add_func("/core/desugar/many1", test_desugar_many1);
    g_test_add_func("/core/desugar/optional", test_desugar_optional);
    g_test_add_func("/core/desugar/ignore", test_desugar_ignore);
    g_test_add_func("/core/desugar/ignoreseq", test_desugar_ignoreseq);
    g_test_add_func("/core/desugar/indirect", test_desugar_indirect);
    g_test_add_func("/core/desugar/action", test_desugar_action);
    g_test_add_func("/core/desugar/attr_bool", test_desugar_attr_bool);
    g_test_add_func("/core/desugar/whitespace", test_desugar_whitespace);
    g_test_add_func("/core/desugar/nested_sequence", test_desugar_nested_sequence);
    g_test_add_func("/core/desugar/nested_choice", test_desugar_nested_choice);
    g_test_add_func("/core/desugar/many_with_sep", test_desugar_many_with_sep);
    g_test_add_func("/core/desugar/many1_with_sep", test_desugar_many1_with_sep);
    g_test_add_func("/core/desugar/to_cfg", test_desugar_to_cfg);
    g_test_add_func("/core/desugar/to_cfg_choice", test_desugar_to_cfg_choice);
    g_test_add_func("/core/desugar/to_cfg_many", test_desugar_to_cfg_many);
}
