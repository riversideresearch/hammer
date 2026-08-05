/* Copyright (c) 2026 Riverside Research */
#include "hammer.h"
#include "sloballoc.h"
#include "test_suite.h"

#include <glib.h>
#include <stdint.h>
#include <string.h>

#define TEST_SLOB_ALIGNMENT sizeof(long double)

static size_t test_slob_align_up(size_t size) {
    size_t rem = size % TEST_SLOB_ALIGNMENT;
    return rem == 0 ? size : size + (TEST_SLOB_ALIGNMENT - rem);
}

static void check_slob_ptr(void *ptr, uint8_t *mem, size_t mem_size) {
    g_check_cmp_ptr(ptr, !=, NULL);
    if (!ptr)
        return;
    g_check_cmp_uint64((uint64_t)(uintptr_t)ptr, >=, (uint64_t)(uintptr_t)mem);
    g_check_cmp_uint64((uint64_t)(uintptr_t)ptr, <, (uint64_t)(uintptr_t)(mem + mem_size));
    g_check_cmp_uint64((uint64_t)((uintptr_t)ptr % TEST_SLOB_ALIGNMENT), ==, 0);
}

#define check_sloballoc_invariants()                                                               \
    do {                                                                                           \
        int err = slobcheck(slob);                                                                 \
        if (err) {                                                                                 \
            g_test_message("SLOB invariant check failed on line %d, returned %d", __LINE__, err);  \
            g_test_fail();                                                                         \
        }                                                                                          \
    } while (0)

#define check_sloballoc(VAR, SIZE, OFFSET)                                                         \
    do {                                                                                           \
        check_sloballoc_invariants();                                                              \
        VAR = sloballoc(slob, (SIZE));                                                             \
        (void)(OFFSET);                                                                            \
        check_slob_ptr(VAR, mem, N);                                                               \
    } while (0)

#define check_sloballoc_fail(SIZE)                                                                 \
    do {                                                                                           \
        check_sloballoc_invariants();                                                              \
        void *p = sloballoc(slob, (SIZE));                                                         \
        g_check_cmp_ptr(p, ==, NULL);                                                              \
    } while (0)

#define check_slobfree(P)                                                                          \
    do {                                                                                           \
        check_sloballoc_invariants();                                                              \
        slobfree(slob, P);                                                                         \
    } while (0)

#define N 1024

#define SLOBALLOC_FIXTURE                                                                          \
    static uint8_t mem[N] = {0x58};                                                                \
    SLOB *slob = slobinit(mem, N);                                                                 \
    size_t max = N - 4 * sizeof(size_t);                                                           \
    (void)max; /* silence warning */                                                               \
    if (!slob) {                                                                                   \
        g_test_message("SLOB allocator init failed on line %d", __LINE__);                         \
        g_test_fail();                                                                             \
    }

static void test_sloballoc_size(void) {
    SLOBALLOC_FIXTURE
    void *p;

    check_sloballoc(p, max, N - max);
    check_slobfree(p);

    check_sloballoc_fail(N);
    check_sloballoc_fail(max + 1);

    check_sloballoc(p, max, N - max);
    check_slobfree(p);

    check_sloballoc_invariants();
}

static void test_sloballoc_merge(void) {
    SLOBALLOC_FIXTURE
    void *p, *q, *r;

    check_sloballoc(p, 100, N - 100);
    check_slobfree(p);
    check_sloballoc(p, max, N - max);
    check_slobfree(p);

    check_sloballoc(p, 100, N - 100);
    check_sloballoc(q, 100, N - 200 - sizeof(size_t));
    check_slobfree(p);
    check_sloballoc(p, 50, N - 50);
    check_sloballoc(r, 100, N - 300 - 2 * sizeof(size_t));
    check_slobfree(q);
    check_sloballoc(q, 150, N - 200 - sizeof(size_t));
    check_slobfree(p);
    check_slobfree(r);
    check_slobfree(q); // merge left and right

    check_sloballoc_fail(max + 1);
    check_sloballoc(p, max, N - max);
    check_slobfree(p);

    check_sloballoc_invariants();
}

static void test_sloballoc_small(void) {
    SLOBALLOC_FIXTURE
    void *p, *q, *r;

    check_sloballoc(p, 100, N - 100);
    check_sloballoc(q, 1, N - 100 - sizeof(size_t) - sizeof(void *));
    check_sloballoc(r, 100, N - 200 - 2 * sizeof(size_t) - sizeof(void *));
    check_slobfree(q);
    check_sloballoc(q, 1, N - 100 - sizeof(size_t) - sizeof(void *));
    check_slobfree(p);
    check_slobfree(r);

    check_sloballoc_invariants();
}

#define check_h_sloballoc(VAR, SIZE, OFFSET)                                                       \
    do {                                                                                           \
        check_sloballoc_invariants();                                                              \
        VAR = mm->alloc(mm, (SIZE));                                                               \
        (void)(OFFSET);                                                                            \
        check_slob_ptr(VAR, mem, N);                                                               \
    } while (0)

#define check_h_slobfree(P)                                                                        \
    do {                                                                                           \
        check_sloballoc_invariants();                                                              \
        mm->free(mm, P);                                                                           \
    } while (0)

static void test_sloballoc_hammer(void) {
    static uint8_t mem[N] = {0x58};
    HAllocator *mm = h_sloballoc(mem, N);
    int line = __LINE__;
    SLOB *slob = (SLOB *)((unsigned char *)mm + test_slob_align_up(sizeof(HAllocator)));
    void *p, *q, *r;

    if (!mm) {
        g_test_message("h_sloballoc() failed on line %d", line);
        g_test_fail();
    }

    check_h_sloballoc(p, 100, N - 100);
    check_h_sloballoc(q, 1, N - 100 - sizeof(size_t) - sizeof(void *));
    check_h_sloballoc(r, 100, N - 200 - 2 * sizeof(size_t) - sizeof(void *));
    check_h_slobfree(q);
    check_h_sloballoc(q, 1, N - 100 - sizeof(size_t) - sizeof(void *));
    check_h_slobfree(p);
    check_h_slobfree(r);

    check_sloballoc_invariants();
}

#undef N

void register_mm_tests(void) {
    g_test_add_func("/core/mm/sloballoc/size", test_sloballoc_size);
    g_test_add_func("/core/mm/sloballoc/merge", test_sloballoc_merge);
    g_test_add_func("/core/mm/sloballoc/small", test_sloballoc_small);
    g_test_add_func("/core/mm/sloballoc/hammer", test_sloballoc_hammer);
}
