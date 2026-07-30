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

void register_parser_free_tests(void) {
    g_test_add_func("/core/parser/free/sequence_variants", test_sequence_variants_free_root);
    g_test_add_func("/core/parser/free/drop_from_variants",
                    test_drop_from_variants_free_root_and_wrappers);
}
