#include "hammer.h"
#include "test_suite.h"

#include <glib.h>
#include <stdarg.h>
#include <stdlib.h>

typedef struct {
    size_t live_allocations;
} TrackingAllocator;

static void *tracking_alloc(HAllocator *allocator, size_t size) {
    TrackingAllocator *tracking = allocator->env;
    void *ptr = malloc(size);

    if (ptr)
        ++tracking->live_allocations;
    return ptr;
}

static void *tracking_realloc(HAllocator *allocator, void *ptr, size_t size) {
    TrackingAllocator *tracking = allocator->env;
    void *result = realloc(ptr, size);

    if (!ptr && result)
        ++tracking->live_allocations;
    else if (ptr && !result && size == 0)
        --tracking->live_allocations;
    return result;
}

static void tracking_free(HAllocator *allocator, void *ptr) {
    TrackingAllocator *tracking = allocator->env;

    if (ptr) {
        free(ptr);
        --tracking->live_allocations;
    }
}

static HAllocator install_tracking_allocator(TrackingAllocator *tracking) {
    HAllocator saved = system_allocator;

    system_allocator = (HAllocator){
        .alloc = tracking_alloc,
        .realloc = tracking_realloc,
        .free = tracking_free,
        .vt = NULL,
        .env = tracking,
    };
    return saved;
}

static HParser *sequence_from_v(HParser *p, ...) {
    va_list ap;
    va_start(ap, p);
    HParser *result = h_sequence__v(p, ap);
    va_end(ap);
    return result;
}

static HParser *drop_from_v(HParser *p, ...) {
    va_list ap;
    va_start(ap, p);
    HParser *result = h_drop_from___v(p, ap);
    va_end(ap);
    return result;
}

static HParser *make_sequence_variant(unsigned variant, HParser *p1, HParser *p2) {
    void *args[] = {p1, p2, NULL};

    switch (variant) {
    case 0:
        return h_sequence(p1, p2, NULL);
    case 1:
        return h_sequence__m(&system_allocator, p1, p2, NULL);
    case 2:
        return sequence_from_v(p1, p2, NULL);
    case 3:
        return h_sequence__a(args);
    default:
        return h_sequence__ma(&system_allocator, args);
    }
}

static HParser *make_drop_from_variant(unsigned variant, HParser *sequence) {
    int indices[] = {1, -1};
    void *args[] = {sequence, indices};

    switch (variant) {
    case 0:
        return h_drop_from_(sequence, 1, -1);
    case 1:
        return h_drop_from___m(&system_allocator, sequence, 1, -1);
    case 2:
        return drop_from_v(sequence, 1, -1);
    case 3:
        return h_drop_from___a(args);
    default:
        return h_drop_from___ma(&system_allocator, args);
    }
}

static HParsedToken *identity_action(const HParseResult *result, void *user_data) {
    (void)user_data;
    return (HParsedToken *)result->ast;
}

static bool accept_predicate(HParseResult *result, void *user_data) {
    (void)result;
    (void)user_data;
    return true;
}

static void parse_after_freeing_compiled_child(HParser *parent, HParser *child,
                                               HParserBackend backend, const uint8_t *input,
                                               size_t length) {
    g_assert_cmpint(h_compile(parent, backend, NULL), ==, 0);
    h_parser_free(child);

    HParseResult *result = h_parse(parent, input, length);
    g_assert_nonnull(result);
    h_parse_result_free(result);
}

static void test_sequence_variants_free_root(void) {
    TrackingAllocator tracking = {0};
    HAllocator saved = install_tracking_allocator(&tracking);

    for (unsigned variant = 0; variant < 5; ++variant) {
        HParser *p1 = h_ch('a');
        HParser *p2 = h_ch('b');
        size_t before_root = tracking.live_allocations;
        HParser *sequence = make_sequence_variant(variant, p1, p2);

        g_assert_nonnull(sequence);
        h_parser_free(sequence);
        g_assert_cmpuint(tracking.live_allocations, ==, before_root);

        h_parser_free(p1);
        h_parser_free(p2);
        g_assert_cmpuint(tracking.live_allocations, ==, 0);
    }

    system_allocator = saved;
}

static void test_drop_from_variants_free_root_and_wrappers(void) {
    TrackingAllocator tracking = {0};
    HAllocator saved = install_tracking_allocator(&tracking);

    for (unsigned variant = 0; variant < 5; ++variant) {
        HParser *p1 = h_ch('a');
        HParser *p2 = h_ch('b');
        HParser *p3 = h_ch('c');
        HParser *sequence = h_sequence(p1, p2, p3, NULL);
        size_t before_rewrite = tracking.live_allocations;
        HParser *rewrite = make_drop_from_variant(variant, sequence);

        g_assert_nonnull(rewrite);
        h_parser_free(rewrite);
        g_assert_cmpuint(tracking.live_allocations, ==, before_rewrite);

        h_parser_free(sequence);
        h_parser_free(p1);
        h_parser_free(p2);
        h_parser_free(p3);
        g_assert_cmpuint(tracking.live_allocations, ==, 0);
    }

    system_allocator = saved;
}

static void test_lalr_desugaring_context_is_freed(void) {
    TrackingAllocator tracking = {0};
    HAllocator saved = install_tracking_allocator(&tracking);
    HParser *a = h_ch('a');
    HParser *b = h_ch('b');
    HParser *c = h_ch('c');
    HParser *d = h_ch('d');
    HParser *choice = h_choice(b, c, NULL);
    HParser *sequence = h_sequence(a, choice, d, NULL);
    //HParseResult *res = h_parse(sequence, NULL, 0);
    g_assert_cmpint(h_compile(sequence, PB_LALR, NULL), ==, 0);
    //g_assert_nonnull(sequence->desugar_ctx);

    h_parser_free(sequence);
    h_parser_free(choice);
    h_parser_free(a);
    h_parser_free(b);
    h_parser_free(c);
    h_parser_free(d);
    g_assert_cmpuint(tracking.live_allocations, ==, 0);

    system_allocator = saved;
}

static void test_lalr_desugaring_context_survives_child_frees(void) {
    TrackingAllocator tracking = {0};
    HAllocator saved = install_tracking_allocator(&tracking);
    HParser *a = h_ch('a');
    HParser *b = h_ch('b');
    HParser *c = h_ch('c');
    HParser *d = h_ch('d');
    HParser *choice = h_choice(b, c, NULL);
    HParser *sequence = h_sequence(a, choice, d, NULL);

    g_assert_cmpint(h_compile(sequence, PB_LALR, NULL), ==, 0);

    h_parser_free(a);
    h_parser_free(b);
    h_parser_free(c);
    h_parser_free(d);
    h_parser_free(choice);
    h_parser_free(sequence);
    g_assert_cmpuint(tracking.live_allocations, ==, 0);

    system_allocator = saved;
}

static void test_lalr_conflict_frees_table(void) {
    TrackingAllocator tracking = {0};
    HAllocator saved = install_tracking_allocator(&tracking);
    HParser *d = h_ch('d');
    HParser *expr = h_indirect();
    HParser *plus = h_ch('+');
    HParser *sum = h_sequence(expr, plus, expr, NULL);
    HParser *choice = h_choice(sum, d, NULL);

    h_bind_indirect(expr, choice);
    g_assert_cmpint(h_compile(expr, PB_LALR, NULL), ==, -2);

    h_parser_free(expr);
    h_parser_free(choice);
    h_parser_free(sum);
    h_parser_free(plus);
    h_parser_free(d);
    g_assert_cmpuint(tracking.live_allocations, ==, 0);

    system_allocator = saved;
}

static void test_lalr_parent_retains_independently_desugared_child(void) {
    TrackingAllocator tracking = {0};
    HAllocator saved = install_tracking_allocator(&tracking);
    HParser *child = h_ch('a');
    HParser *tail = h_ch('b');
    HParser *parent = h_sequence(child, tail, NULL);

    g_assert_cmpint(h_compile(child, PB_LALR, NULL), ==, 0);
    g_assert_cmpint(h_compile(parent, PB_LALR, NULL), ==, 0);

    h_parser_free(child);

    HParseResult *result = h_parse(parent, (const uint8_t *)"ab", 2);
    g_assert_nonnull(result);
    h_parse_result_free(result);

    h_parser_free(parent);
    h_parser_free(tail);
    g_assert_cmpuint(tracking.live_allocations, ==, 0);

    system_allocator = saved;
}

static void test_contextfree_parent_survives_freed_child_env(void) {
    TrackingAllocator tracking = {0};
    HAllocator saved = install_tracking_allocator(&tracking);

    {
        HParser *child = h_ch_range('a', 'c');
        HParser *tail = h_ch('x');
        HParser *parent = h_sequence(child, tail, NULL);
        const uint8_t input[] = {'b', 'x'};

        g_assert_cmpint(h_compile(child, PB_LALR, NULL), ==, 0);
        parse_after_freeing_compiled_child(parent, child, PB_LALR, input, sizeof(input));
        h_parser_free(parent);
        h_parser_free(tail);
        g_assert_cmpuint(tracking.live_allocations, ==, 0);
    }

    {
        HParser *child = h_float32();
        HParser *tail = h_ch('x');
        HParser *parent = h_sequence(child, tail, NULL);
        const uint8_t input[] = {0, 0, 0, 0, 'x'};

        parse_after_freeing_compiled_child(parent, child, PB_LALR, input, sizeof(input));
        h_parser_free(parent);
        h_parser_free(tail);
        g_assert_cmpuint(tracking.live_allocations, ==, 0);
    }

    system_allocator = saved;
}

static void test_regex_parent_survives_freed_child_env(void) {
    TrackingAllocator tracking = {0};
    HAllocator saved = install_tracking_allocator(&tracking);

    {
        HParser *child = h_uint8();
        HParser *tail = h_ch('x');
        HParser *parent = h_sequence(child, tail, NULL);
        const uint8_t input[] = {'a', 'x'};

        parse_after_freeing_compiled_child(parent, child, PB_REGULAR, input, sizeof(input));
        h_parser_free(parent);
        h_parser_free(tail);
        g_assert_cmpuint(tracking.live_allocations, ==, 0);
    }

    {
        HParser *atom = h_ch('a');
        HParser *child = h_action(atom, identity_action, NULL);
        HParser *tail = h_ch('x');
        HParser *parent = h_sequence(child, tail, NULL);
        const uint8_t input[] = {'a', 'x'};

        parse_after_freeing_compiled_child(parent, child, PB_REGULAR, input, sizeof(input));
        h_parser_free(parent);
        h_parser_free(tail);
        h_parser_free(atom);
        g_assert_cmpuint(tracking.live_allocations, ==, 0);
    }

    {
        HParser *atom = h_ch('a');
        HParser *child = h_attr_bool(atom, accept_predicate, NULL);
        HParser *tail = h_ch('x');
        HParser *parent = h_sequence(child, tail, NULL);
        const uint8_t input[] = {'a', 'x'};

        parse_after_freeing_compiled_child(parent, child, PB_REGULAR, input, sizeof(input));
        h_parser_free(parent);
        h_parser_free(tail);
        h_parser_free(atom);
        g_assert_cmpuint(tracking.live_allocations, ==, 0);
    }

    {
        HParser *left = h_ch('a');
        HParser *right = h_ch('b');
        HParser *child = h_left(left, right);
        HParser *tail = h_ch('x');
        HParser *parent = h_sequence(child, tail, NULL);
        const uint8_t input[] = {'a', 'b', 'x'};

        parse_after_freeing_compiled_child(parent, child, PB_REGULAR, input, sizeof(input));
        h_parser_free(parent);
        h_parser_free(tail);
        h_parser_free(left);
        h_parser_free(right);
        g_assert_cmpuint(tracking.live_allocations, ==, 0);
    }

    {
        HParser *integer = h_uint8();
        HParser *child = h_int_range(integer, 1, 9);
        HParser *tail = h_ch('x');
        HParser *parent = h_sequence(child, tail, NULL);
        const uint8_t input[] = {5, 'x'};

        parse_after_freeing_compiled_child(parent, child, PB_REGULAR, input, sizeof(input));
        h_parser_free(parent);
        h_parser_free(tail);
        h_parser_free(integer);
        g_assert_cmpuint(tracking.live_allocations, ==, 0);
    }

    system_allocator = saved;
}

void register_parser_free_tests(void) {
    g_test_add_func("/core/parser/free/sequence_variants", test_sequence_variants_free_root);
    g_test_add_func("/core/parser/free/drop_from_variants",
                    test_drop_from_variants_free_root_and_wrappers);
    g_test_add_func("/core/parser/free/lalr_desugaring_context",
                    test_lalr_desugaring_context_is_freed);
    g_test_add_func("/core/parser/free/lalr_desugaring_context_child_first",
                    test_lalr_desugaring_context_survives_child_frees);
    g_test_add_func("/core/parser/free/lalr_conflict_table",
                    test_lalr_conflict_frees_table);
    g_test_add_func("/core/parser/free/lalr_independently_desugared_child",
                    test_lalr_parent_retains_independently_desugared_child);
    g_test_add_func("/core/parser/free/contextfree_child_env",
                    test_contextfree_parent_survives_freed_child_env);
    g_test_add_func("/core/parser/free/regex_child_env", test_regex_parent_survives_freed_child_env);
}
