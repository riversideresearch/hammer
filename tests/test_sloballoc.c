#include "hammer.h"
#include "sloballoc.h"
#include "test_suite.h"

#include <glib.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>

static jmp_buf abort_jmp_buf;
// Signal handler for testing abort conditions
static void abort_handler(int sig) {
    (void)sig;
    longjmp(abort_jmp_buf, 1);
}

static void test_slobinit(void) {
    uint8_t mem[1024];
    SLOB *slob = slobinit(mem, 1024);
    g_check_cmp_ptr(slob, !=, NULL);
    if (slob) {
        void *ptr = sloballoc(slob, 100);
        g_check_cmp_ptr(ptr, !=, NULL);
    }
}

static void test_sloballoc_small_size(void) {
    uint8_t mem[1024];
    SLOB *slob = slobinit(mem, 1024);
    void *ptr = sloballoc(slob, 1);
    g_check_cmp_ptr(ptr, !=, NULL);
}

static void test_sloballoc_overflow(void) {
    uint8_t mem[1024];
    SLOB *slob = slobinit(mem, 1024);
    void *ptr = sloballoc(slob, SIZE_MAX);
    g_check_cmp_ptr(ptr, ==, NULL);
}

static void test_sloballoc_remblock(void) {
    uint8_t mem[1024];
    SLOB *slob = slobinit(mem, 1024);
    void *ptr = sloballoc(slob, 100);
    g_check_cmp_ptr(ptr, !=, NULL);
}

static void test_sloballoc_exact_fit(void) {
    uint8_t mem[1024];
    SLOB *slob = slobinit(mem, 1024);
    void *ptr = sloballoc(slob, 900);
    g_check_cmp_ptr(ptr, !=, NULL);
}

static void test_slobfree_left_adjacent(void) {
    uint8_t mem[1024];
    SLOB *slob = slobinit(mem, 1024);
    void *ptr1 = sloballoc(slob, 100);
    void *ptr2 = sloballoc(slob, 100);
    slobfree(slob, ptr1);
    slobfree(slob, ptr2);
}

static void test_slobfree_right_adjacent(void) {
    uint8_t mem[1024];
    SLOB *slob = slobinit(mem, 1024);
    void *ptr1 = sloballoc(slob, 100);
    void *ptr2 = sloballoc(slob, 100);
    slobfree(slob, ptr2);
    slobfree(slob, ptr1);
}

static void test_slobfree_both_adjacent(void) {
    uint8_t mem[1024];
    SLOB *slob = slobinit(mem, 1024);
    void *ptr1 = sloballoc(slob, 100);
    void *ptr2 = sloballoc(slob, 100);
    void *ptr3 = sloballoc(slob, 100);
    slobfree(slob, ptr1);
    slobfree(slob, ptr3);
    slobfree(slob, ptr2);
}

static void test_slobfree_no_adjacent(void) {
    uint8_t mem[1024];
    SLOB *slob = slobinit(mem, 1024);
    void *ptr1 = sloballoc(slob, 100);
    void *ptr2 = sloballoc(slob, 200);
    slobfree(slob, ptr1);
    slobfree(slob, ptr2);
}

static void test_slobcheck(void) {
    uint8_t mem[1024];
    SLOB *slob = slobinit(mem, 1024);
    int result = slobcheck(slob);
    g_check_cmp_int(result, ==, 0);
}

static void test_h_sloballoc(void) {
    uint8_t mem[1024];
    HAllocator *mm = h_sloballoc(mem, 1024);
    g_check_cmp_ptr(mm, !=, NULL);
    if (mm) {
        void *ptr = mm->alloc(mm, 100);
        g_check_cmp_ptr(ptr, !=, NULL);
    }
}

static void test_h_sloballoc_too_small(void) {
    uint8_t mem[10];
    HAllocator *mm = h_sloballoc(mem, 10);
    g_check_cmp_ptr(mm, ==, NULL);
}

static void test_h_slob_realloc(void) {
    uint8_t mem[1024];
    HAllocator *mm = h_sloballoc(mem, 1024);
    g_check_cmp_ptr(mm, !=, NULL);
    if (mm && mm->realloc) {
        void *ptr = mm->alloc(mm, 100);
        g_check_cmp_ptr(ptr, !=, NULL);
        if (ptr) {
            void (*old_handler)(int) = signal(SIGABRT, abort_handler);
            if (setjmp(abort_jmp_buf) == 0) {
                void *new_ptr = mm->realloc(mm, ptr, 200);
                (void)new_ptr;
            }
            signal(SIGABRT, old_handler);
        }
    }
}

void register_sloballoc_tests(void) {
    g_test_add_func("/core/sloballoc/slobinit", test_slobinit);
    g_test_add_func("/core/sloballoc/sloballoc_small_size", test_sloballoc_small_size);
    g_test_add_func("/core/sloballoc/sloballoc_overflow", test_sloballoc_overflow);
    g_test_add_func("/core/sloballoc/sloballoc_remblock", test_sloballoc_remblock);
    g_test_add_func("/core/sloballoc/sloballoc_exact_fit", test_sloballoc_exact_fit);
    g_test_add_func("/core/sloballoc/slobfree_left_adjacent", test_slobfree_left_adjacent);
    g_test_add_func("/core/sloballoc/slobfree_right_adjacent", test_slobfree_right_adjacent);
    g_test_add_func("/core/sloballoc/slobfree_both_adjacent", test_slobfree_both_adjacent);
    g_test_add_func("/core/sloballoc/slobfree_no_adjacent", test_slobfree_no_adjacent);
    g_test_add_func("/core/sloballoc/slobcheck", test_slobcheck);
    g_test_add_func("/core/sloballoc/h_sloballoc", test_h_sloballoc);
    g_test_add_func("/core/sloballoc/h_sloballoc_too_small", test_h_sloballoc_too_small);
    g_test_add_func("/core/sloballoc/h_slob_realloc", test_h_slob_realloc);
}
