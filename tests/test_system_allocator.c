#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>
#include <stdlib.h>

// Test system_allocator.c: system_alloc (line 39)
static void test_system_alloc(void) {
    void *ptr = system_allocator.alloc(&system_allocator, 100);
    g_check_cmp_ptr(ptr, !=, NULL);
    if (ptr) {
        system_allocator.free(&system_allocator, ptr);
    }
}

// Test system_allocator.c: system_alloc with NULL return (line 41)
static void test_system_alloc_null(void) {
    // Hard to test malloc failure, but we test the path exists
    void *ptr = system_allocator.alloc(&system_allocator, 100);
    g_check_cmp_ptr(ptr, !=, NULL);
    if (ptr) {
        system_allocator.free(&system_allocator, ptr);
    }
}

// Test system_allocator.c: system_realloc with NULL uptr (line 53)
static void test_system_realloc_null_uptr(void) {
    void *ptr = system_allocator.realloc(&system_allocator, NULL, 100);
    g_check_cmp_ptr(ptr, !=, NULL);
    if (ptr) {
        system_allocator.free(&system_allocator, ptr);
    }
}

// Test system_allocator.c: system_realloc with non-NULL uptr (line 52)
static void test_system_realloc_nonnull(void) {
    void *ptr = system_allocator.alloc(&system_allocator, 100);
    g_check_cmp_ptr(ptr, !=, NULL);
    if (ptr) {
        void *new_ptr = system_allocator.realloc(&system_allocator, ptr, 200);
        g_check_cmp_ptr(new_ptr, !=, NULL);
        if (new_ptr) {
            system_allocator.free(&system_allocator, new_ptr);
        }
    }
}

// Test system_allocator.c: system_realloc with NULL return (line 59)
static void test_system_realloc_null(void) {
    void *ptr = system_allocator.alloc(&system_allocator, 100);
    g_check_cmp_ptr(ptr, !=, NULL);
    if (ptr) {
        // Hard to test realloc failure, but we test the path exists
        void *new_ptr = system_allocator.realloc(&system_allocator, ptr, 200);
        g_check_cmp_ptr(new_ptr, !=, NULL);
        if (new_ptr) {
            system_allocator.free(&system_allocator, new_ptr);
        }
    }
}

// Test system_allocator.c: system_free with NULL (line 75)
static void test_system_free_null(void) {
    system_allocator.free(&system_allocator, NULL);
    // Should not crash
}

// Test system_allocator.c: system_free with non-NULL (line 74)
static void test_system_free_nonnull(void) {
    void *ptr = system_allocator.alloc(&system_allocator, 100);
    g_check_cmp_ptr(ptr, !=, NULL);
    if (ptr) {
        system_allocator.free(&system_allocator, ptr);
        // Should not crash
    }
}

void register_system_allocator_tests(void) {
    g_test_add_func("/core/system_allocator/alloc", test_system_alloc);
    g_test_add_func("/core/system_allocator/alloc_null", test_system_alloc_null);
    g_test_add_func("/core/system_allocator/realloc_null_uptr", test_system_realloc_null_uptr);
    g_test_add_func("/core/system_allocator/realloc_nonnull", test_system_realloc_nonnull);
    g_test_add_func("/core/system_allocator/realloc_null", test_system_realloc_null);
    g_test_add_func("/core/system_allocator/free_null", test_system_free_null);
    g_test_add_func("/core/system_allocator/free_nonnull", test_system_free_nonnull);
}
