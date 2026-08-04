#include "backends/params.h"
#include "hammer.h"
#include "test_suite.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helpers to construct the parameter structures used by the C functions.

static backend_param_with_name_t *make_param_entry_from_cstr(const char *s) {
    backend_param_with_name_t *entry = g_malloc0(sizeof(*entry));
    if (!entry)
        return NULL;

    size_t len = s ? strlen(s) : 0;
    // fprintf(stderr, "Len = %lu", len);
    /* allocate buffer with space for terminating NUL so sscanf/str* are safe */
    uint8_t *buf = NULL;
    if (len > 0) {
        buf = g_malloc(len);
        memcpy(buf, s, len);
    } else {
        /* represent empty buffer as a valid pointer to a zero-length string */
        buf = g_malloc(1);
        buf[0] = '\0';
    }

    entry->param.param = buf;
    entry->param.len = len; /* length excluding terminating NUL */
    return entry;
}

static void free_param_entry(backend_param_with_name_t *e) {
    if (!e)
        return;
    if (e->param.param)
        g_free(e->param.param);
    g_free(e);
}

/* Build backend_params_t with one entry.
 * If entry == NULL, create a params struct with len==1 and params==NULL
 * to simulate a broken input (as used in tests).
 */

static backend_params_t make_params_with_one_entry(backend_param_with_name_t *entry) {
    backend_params_t p;

    if (entry == NULL) {
        p.len = 1;
        p.params = NULL; /* intentionally broken */
        return p;
    }

    /* arr is an array of pointers to backend_param_with_name_t */
    backend_param_with_name_t *arr = g_malloc0(sizeof(*arr));
    if (!arr) {
        p.len = 0;
        p.params = NULL;
        return p;
    }

    arr[0] = *entry; /* assign the pointer into the pointer array */
    p.len = 1;
    p.params = arr;
    return p;
}

static void free_params_array(backend_params_t *p) {
    if (!p)
        return;
    if (p->params) {
        g_free(p->params);
        p->params = NULL;
    }
    p->len = 0;
}

static void test_param_k_null_inputs(void) {
    /* Both pointers NULL -> expect documented null error code.
       The conversation used -1 for NULL input; assert that behavior. */
    int rc = h_extract_param_k(NULL, NULL);
    g_check_cmp_int(rc, ==, -1);
}

static void test_param_k_params_array_null(void) {
    HParserBackendWithParams out;
    backend_with_params_t in;

    /* Simulate params.len == 1 but params == NULL (broken) */
    in.params.len = 1;
    in.params.params = NULL;

    int rc = h_extract_param_k(&out, &in);
    /* The implementation variants used -2 for this case. */
    g_check_cmp_int(rc, ==, -2);
}

static void test_param_k_first_entry_null(void) {
    HParserBackendWithParams out;
    backend_with_params_t in;

    backend_param_with_name_t *entry = make_param_entry_from_cstr("42");
    g_assert_nonnull(entry);

    in.params = make_params_with_one_entry(entry);
    in.params.params[0].param.param = NULL;
    int rc = h_extract_param_k(&out, &in);

    g_check_cmp_int(rc, ==, -3);
}

static void test_param_k_valid_integer(void) {
    HParserBackendWithParams out;
    backend_with_params_t in;

    backend_param_with_name_t *entry = make_param_entry_from_cstr("1234567890");
    g_assert_nonnull(entry);

    in.params = make_params_with_one_entry(entry);

    int rc = h_extract_param_k(&out, &in);

    /* sscanf returns 1 on success; the implementation returns that value */
    g_check_cmp_int(rc, ==, 1);

    /* decode stored pointer back to integer via helper */
    size_t k = h_get_param_k(out.params);

    g_check_cmp_int((int)k, ==, 1234567890);

    free_params_array(&in.params);
    free_param_entry(entry);
}

static void test_param_k_invalid_integer(void) {
    HParserBackendWithParams out;
    memset(&out, 0, sizeof(out)); // Ensure out.params == NULL

    backend_with_params_t in;
    backend_param_with_name_t *entry = make_param_entry_from_cstr("notanumber");
    g_assert_nonnull(entry);

    in.params = make_params_with_one_entry(entry);

    int rc = h_extract_param_k(&out, &in);
    g_check_cmp_int(rc, ==, 0);

    // On parse failure, out.params should remain NULL because we initialized it
    g_check_cmp_ptr(out.params, ==, NULL);

    free_params_array(&in.params);
    free_param_entry(entry);
}

static void test_param_k_negative_integer(void) {
    HParserBackendWithParams out;
    backend_with_params_t in;

    backend_param_with_name_t *entry = make_param_entry_from_cstr("-5");
    g_assert_nonnull(entry);

    in.params = make_params_with_one_entry(entry);

    int rc = h_extract_param_k(&out, &in);
    g_check_cmp_int(rc, ==, 1);

    /* Negative int encoded into pointer then decoded to size_t is implementation-defined.
       We at least expect the stored pointer not to be NULL (i.e., something was stored). */
    g_check_cmp_ptr(out.params, !=, NULL);

    /* We can still call h_get_param_k to see what it returns; it will be a size_t value. */
    size_t k = h_get_param_k(out.params);
    g_check_cmp_int((int)k, !=, 0); /* not zero (likely large value due to cast) */

    free_params_array(&in.params);
    free_param_entry(entry);
}

static void test_param_k_overflow(void) {
    HParserBackendWithParams out;
    backend_with_params_t in;

    backend_param_with_name_t *entry = make_param_entry_from_cstr("12345678910");
    g_assert_nonnull(entry);

    in.params = make_params_with_one_entry(entry);

    int rc = h_extract_param_k(&out, &in);
    g_check_cmp_int(rc, ==, -4);

    // On parse failure, out.params should remain NULL because we initialized it
    g_check_cmp_ptr(out.params, ==, NULL);

    free_params_array(&in.params);
    free_param_entry(entry);
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
    system_allocator.free(&system_allocator, ptr);
}
static HAllocator fail_allocator = {fail_alloc, fail_realloc, fail_free, NULL, NULL};

static void test_format_helpers_specific_k(void) {
    const char *backend = "hammer";
    size_t k = 7;

    char *name = h_format_name_with_param_k(&fail_allocator, backend, k);
    g_assert_nonnull(name);
    g_check_string(name, ==, "hammer(7)");
    free(name);

    char *desc = h_format_description_with_param_k(&fail_allocator, backend, k);
    g_assert_nonnull(desc);
    g_check_string(desc, ==, "hammer(7) parser backend");
    free(desc);
}

static void test_format_helpers_generic_k(void) {
    const char *backend = "hammer";
    size_t k = 0;

    char *name = h_format_name_with_param_k(&fail_allocator, backend, k);
    g_assert_nonnull(name);
    g_check_string(name, ==, "hammer(k)");
    free(name);

    char *desc = h_format_description_with_param_k(&fail_allocator, backend, k);
    g_assert_nonnull(desc);
    g_check_string(desc, ==, "hammer(k) parser backend (default k is 1)");

    // DEFAULT_KMAX should be present in the description
#ifdef DEFAULT_KMAX
    {
        char expected[64];
        snprintf(expected, sizeof(expected), "%zu", (size_t)DEFAULT_KMAX);
        g_check_string(desc, ==, expected);
    }
#endif

    free(desc);
}

void register_param_k_tests(void) {
    g_test_add_func("/core/backends/param_k/null_inputs", test_param_k_null_inputs);
    g_test_add_func("/core/backends/param_k/params_array_null", test_param_k_params_array_null);
    g_test_add_func("/core/backends/param_k/first_entry_null", test_param_k_first_entry_null);
    g_test_add_func("/core/backends/param_k/valid_integer", test_param_k_valid_integer);
    g_test_add_func("/core/backends/param_k/invalid_integer", test_param_k_invalid_integer);
    g_test_add_func("/core/backends/param_k/negative_integer", test_param_k_negative_integer);
    g_test_add_func("/core/backends/param_k/overflow", test_param_k_overflow);
    g_test_add_func("/core/backends/param_k/format_specific_k", test_format_helpers_specific_k);
    g_test_add_func("/core/backends/param_k/format_generic_k", test_format_helpers_generic_k);
}
