#include "glue.h"
#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>

HParsedToken *act_param_name(const HParseResult *p, void *user_data);
static void test_hammer_backend_available_invalid(void) {
    int result = h_is_backend_available((HParserBackend)999);
    g_check_cmp_int(result, ==, 0);
}

static void test_hammer_backend_available_missing(void) {

    int result = h_is_backend_available(PB_INVALID);
    g_check_cmp_int(result, ==, 0);
}

static void test_hammer_backend_available_valid(void) {
    int result = h_is_backend_available(PB_PACKRAT);
    g_check_cmp_int(result, ==, 1);
}

static void test_hammer_copy_backend_with_params(void) {
    HAllocator *mm__ = &system_allocator;
    HParserBackendWithParams *be = h_new(HParserBackendWithParams, 1);
    be->mm__ = mm__;
    be->requested_name = NULL;
    be->backend = PB_PACKRAT;
    be->backend_vtable = h_get_default_backend_vtable();
    be->params = NULL;

    HParserBackendWithParams *copy = h_copy_backend_with_params(be);
    g_check_cmp_ptr(copy, !=, NULL);
    if (copy) {
        g_check_cmp_int(copy->backend, ==, PB_PACKRAT);
        h_free_backend_with_params(copy);
    }

    h_free_backend_with_params(be);
}

static void test_hammer_copy_backend_with_params_null_mm(void) {
    HAllocator *mm__ = &system_allocator;
    HParserBackendWithParams *be = h_new(HParserBackendWithParams, 1);
    be->mm__ = mm__;
    be->requested_name = NULL;
    be->backend = PB_PACKRAT;
    be->backend_vtable = h_get_default_backend_vtable();
    be->params = NULL;

    HParserBackendWithParams *copy = h_copy_backend_with_params__m(NULL, be);
    g_check_cmp_ptr(copy, !=, NULL);
    if (copy) {
        g_check_cmp_ptr(copy->mm__, ==, be->mm__);
        h_free_backend_with_params(copy);
    }

    h_free_backend_with_params(be);
}

static void test_hammer_copy_backend_with_params_null_be(void) {
    HParserBackendWithParams *copy = h_copy_backend_with_params__m(&system_allocator, NULL);
    g_check_cmp_ptr(copy, ==, NULL);
}

static void test_hammer_copy_backend_with_params_null_mm_both(void) {
    HAllocator *mm__ = &system_allocator;

    HParserBackendWithParams *be = h_new(HParserBackendWithParams, 1);
    be->mm__ = NULL;
    be->requested_name = NULL;
    be->backend = PB_PACKRAT;
    be->backend_vtable = h_get_default_backend_vtable();
    be->params = NULL;

    HParserBackendWithParams *copy = h_copy_backend_with_params__m(NULL, be);
    g_check_cmp_ptr(copy, ==, NULL);

    h_free(be);
}

static void test_hammer_copy_backend_with_params_invalid_backend(void) {
    HAllocator *mm__ = &system_allocator;
    HParserBackendWithParams *be = h_new(HParserBackendWithParams, 1);
    be->mm__ = mm__;
    be->requested_name = NULL;
    be->backend = (HParserBackend)999;
    be->backend_vtable = NULL;
    be->params = NULL;

    HParserBackendWithParams *copy = h_copy_backend_with_params__m(mm__, be);
    g_check_cmp_ptr(copy, ==, NULL);

    h_free_backend_with_params(be);
}

static void test_hammer_copy_backend_with_params_null_params(void) {
    HAllocator *mm__ = &system_allocator;
    HParserBackendWithParams *be = h_new(HParserBackendWithParams, 1);
    be->mm__ = mm__;
    be->requested_name = NULL;
    be->backend = PB_PACKRAT;
    be->backend_vtable = h_get_default_backend_vtable();
    be->params = NULL;

    HParserBackendWithParams *copy = h_copy_backend_with_params__m(&system_allocator, be);
    g_check_cmp_ptr(copy, !=, NULL);
    if (copy) {
        g_check_cmp_ptr(copy->params, ==, NULL);
        h_free_backend_with_params(copy);
    }

    h_free_backend_with_params(be);
}

static void test_hammer_copy_numeric_param(void) {
    void *out = NULL;
    void *in = (void *)(uintptr_t)42;

    int result = h_copy_numeric_param(&system_allocator, &out, in);
    g_check_cmp_int(result, ==, 0);
    g_check_cmp_ptr(out, ==, in);

    result = h_copy_numeric_param(&system_allocator, NULL, in);
    g_check_cmp_int(result, ==, -1);
}

static void test_hammer_free_backend_with_params_null(void) { h_free_backend_with_params(NULL); }
static void test_hammer_free_backend_with_params_null_mm(void) {
    HAllocator *mm__ = &system_allocator;
    HParserBackendWithParams *be = h_new(HParserBackendWithParams, 1);
    be->mm__ = NULL;
    be->requested_name = NULL;
    be->backend = PB_PACKRAT;
    be->backend_vtable = h_get_default_backend_vtable();
    be->params = NULL;

    h_free_backend_with_params(be);
}

static void test_hammer_free_backend_with_params_unavailable_backend(void) {
    HAllocator *mm__ = &system_allocator;
    HParserBackendWithParams *be = h_new(HParserBackendWithParams, 1);
    be->mm__ = mm__;
    be->requested_name = NULL;
    be->backend = (HParserBackend)999;
    be->backend_vtable = NULL;
    be->params = NULL;

    h_free_backend_with_params(be);
}

static void test_hammer_free_backend_with_params_null_vtable(void) {
    HAllocator *mm__ = &system_allocator;
    HParserBackendWithParams *be = h_new(HParserBackendWithParams, 1);
    be->mm__ = mm__;
    be->requested_name = NULL;
    be->backend = PB_PACKRAT;
    be->backend_vtable = NULL;
    be->params = NULL;

    h_free_backend_with_params(be);
}

static void test_hammer_free_backend_with_params_null_free_params(void) {
    HAllocator *mm__ = &system_allocator;
    HParserBackendWithParams *be = h_new(HParserBackendWithParams, 1);
    be->mm__ = mm__;
    be->requested_name = NULL;
    be->backend = PB_PACKRAT;
    be->backend_vtable = h_get_default_backend_vtable();

    be->params = NULL;

    h_free_backend_with_params(be);
}

static void test_hammer_free_backend_with_params_with_name(void) {
    HAllocator *mm__ = &system_allocator;
    HParserBackendWithParams *be = h_new(HParserBackendWithParams, 1);
    be->mm__ = mm__;
    be->requested_name = h_new(char, 10);
    strcpy(be->requested_name, "test");
    be->backend = PB_PACKRAT;
    be->backend_vtable = h_get_default_backend_vtable();
    be->params = NULL;

    h_free_backend_with_params(be);
}

static void test_hammer_get_string_for_backend_invalid(void) {
    const char *name = h_get_name_for_backend(PB_INVALID);
    g_check_cmp_ptr(name, ==, NULL);

    const char *desc = h_get_descriptive_text_for_backend(PB_INVALID);
    g_assert_cmpstr(desc, ==, "invalid backend");
}

static void test_hammer_get_string_for_backend_out_of_range(void) {
    const char *name = h_get_name_for_backend((HParserBackend)999);
    g_check_cmp_ptr(name, ==, NULL);

    const char *desc = h_get_descriptive_text_for_backend((HParserBackend)999);
    g_assert_cmpstr(desc, ==, "bad backend number");
}

static void test_hammer_get_string_for_backend_valid(void) {
    const char *name = h_get_name_for_backend(PB_PACKRAT);
    g_check_cmp_ptr(name, !=, NULL);

    const char *desc = h_get_descriptive_text_for_backend(PB_PACKRAT);
    g_check_cmp_ptr(desc, !=, NULL);
}

static void test_hammer_get_string_for_backend_with_params_null_mm(void) {
    HAllocator *mm__ = &system_allocator;
    HParserBackendWithParams *be = h_new(HParserBackendWithParams, 1);
    be->mm__ = mm__;
    be->requested_name = NULL;
    be->backend = PB_PACKRAT;
    be->backend_vtable = h_get_default_backend_vtable();
    be->params = NULL;

    char *name = h_get_name_for_backend_with_params__m(NULL, be);
    g_check_cmp_ptr(name, ==, NULL);

    h_free_backend_with_params(be);
}

static void test_hammer_get_string_for_backend_with_params_null_be(void) {
    char *name = h_get_name_for_backend_with_params__m(&system_allocator, NULL);
    g_check_cmp_ptr(name, ==, NULL);
}

static void test_hammer_act_param_name(void) {

    HParser *parser = h_sequence(h_uint8(), h_uint8(), h_uint8(), NULL);
    parser = h_action(parser, act_param_name, NULL);
    h_compile(parser, PB_PACKRAT, NULL);

    const uint8_t input[] = {'a', 'b', 'c'};
    HParseResult *result = h_parse(parser, input, 3);
    g_check_cmp_ptr(result, !=, NULL);
    if (result) {
        g_check_cmp_ptr(result->ast, !=, NULL);
        if (result->ast) {
            g_check_cmp_int(result->ast->token_type, ==, TT_backend_param_name_t);
        }
        h_parse_result_free(result);
    }
}

static void test_hammer_parse_result_free_m(void) {
    HParser *parser = h_ch('a');
    h_compile(parser, PB_PACKRAT, NULL);
    const uint8_t input[] = {'a'};
    HParseResult *result = h_parse(parser, input, 1);
    g_check_cmp_ptr(result, !=, NULL);
    if (result) {
        h_parse_result_free__m(&system_allocator, result);
    }
}

static void test_hammer_parse_result_free_m_null(void) {
    h_parse_result_free__m(&system_allocator, NULL);
}

void register_hammer_tests(void) {
    g_test_add_func("/core/hammer/backend_available_invalid",
                    test_hammer_backend_available_invalid);
    g_test_add_func("/core/hammer/backend_available_missing",
                    test_hammer_backend_available_missing);
    g_test_add_func("/core/hammer/backend_available_valid", test_hammer_backend_available_valid);
    g_test_add_func("/core/hammer/copy_backend_with_params", test_hammer_copy_backend_with_params);
    g_test_add_func("/core/hammer/copy_backend_with_params_null_mm",
                    test_hammer_copy_backend_with_params_null_mm);
    g_test_add_func("/core/hammer/copy_backend_with_params_null_be",
                    test_hammer_copy_backend_with_params_null_be);
    g_test_add_func("/core/hammer/copy_backend_with_params_null_mm_both",
                    test_hammer_copy_backend_with_params_null_mm_both);
    g_test_add_func("/core/hammer/copy_backend_with_params_invalid_backend",
                    test_hammer_copy_backend_with_params_invalid_backend);
    g_test_add_func("/core/hammer/copy_backend_with_params_null_params",
                    test_hammer_copy_backend_with_params_null_params);
    g_test_add_func("/core/hammer/copy_numeric_param", test_hammer_copy_numeric_param);
    g_test_add_func("/core/hammer/free_backend_with_params_null",
                    test_hammer_free_backend_with_params_null);
    g_test_add_func("/core/hammer/free_backend_with_params_null_mm",
                    test_hammer_free_backend_with_params_null_mm);
    g_test_add_func("/core/hammer/free_backend_with_params_unavailable_backend",
                    test_hammer_free_backend_with_params_unavailable_backend);
    g_test_add_func("/core/hammer/free_backend_with_params_null_vtable",
                    test_hammer_free_backend_with_params_null_vtable);
    g_test_add_func("/core/hammer/free_backend_with_params_null_free_params",
                    test_hammer_free_backend_with_params_null_free_params);
    g_test_add_func("/core/hammer/free_backend_with_params_with_name",
                    test_hammer_free_backend_with_params_with_name);
    g_test_add_func("/core/hammer/get_string_for_backend_invalid",
                    test_hammer_get_string_for_backend_invalid);
    g_test_add_func("/core/hammer/get_string_for_backend_out_of_range",
                    test_hammer_get_string_for_backend_out_of_range);
    g_test_add_func("/core/hammer/get_string_for_backend_valid",
                    test_hammer_get_string_for_backend_valid);
    g_test_add_func("/core/hammer/get_string_for_backend_with_params_null_mm",
                    test_hammer_get_string_for_backend_with_params_null_mm);
    g_test_add_func("/core/hammer/get_string_for_backend_with_params_null_be",
                    test_hammer_get_string_for_backend_with_params_null_be);
    g_test_add_func("/core/hammer/act_param_name", test_hammer_act_param_name);
    g_test_add_func("/core/hammer/parse_result_free_m", test_hammer_parse_result_free_m);
    g_test_add_func("/core/hammer/parse_result_free_m_null", test_hammer_parse_result_free_m_null);
}
