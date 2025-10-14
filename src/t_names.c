#include "hammer.h"
#include "test_suite.h"

#include <glib.h>
#include <string.h>

static void test_tt_backend_description(void) {
    const char *desc = NULL;

    /* Check that we get some */
    desc = h_get_descriptive_text_for_backend(PB_PACKRAT);
    g_check_cmp_ptr(desc, !=, NULL);
}

/* Reference backend names */
static const char *packrat_name = "packrat";

static void test_tt_backend_short_name(void) {
    const char *name = NULL;

    name = h_get_name_for_backend(PB_PACKRAT);
    g_check_maybe_string_eq(name, packrat_name);
}

static void test_tt_query_backend_by_name(void) {
    HParserBackend be;

    be = h_query_backend_by_name(packrat_name);
    g_check_inttype("%d", HParserBackend, be, ==, PB_PACKRAT);
}

static void test_tt_get_backend_with_params_by_name(void) {
    HParserBackendWithParams *be_w_p = NULL;

    /*requests to use default params, or for backends without params*/
    be_w_p = h_get_backend_with_params_by_name(packrat_name);
    g_check_inttype("%d", HParserBackend, be_w_p->backend, ==, PB_PACKRAT);
    g_check_maybe_string_eq(be_w_p->requested_name, packrat_name);
    h_free_backend_with_params(be_w_p);

    /*request for a backend not in the enum of backends included in hammer */
    be_w_p = h_get_backend_with_params_by_name("llvm()");
    g_check_inttype("%d", HParserBackend, be_w_p->backend, ==, PB_INVALID);
    g_check_maybe_string_eq(be_w_p->requested_name, "llvm");
    h_free_backend_with_params(be_w_p);

    be_w_p = h_get_backend_with_params_by_name("packrat(0)");
    g_check_inttype("%d", HParserBackend, be_w_p->backend, ==, PB_PACKRAT);
    g_check_maybe_string_eq(be_w_p->requested_name, packrat_name);
    h_free_backend_with_params(be_w_p);
}

static void test_tt_h_get_descriptive_text_for_backend_with_params(void) {
    HAllocator *mm__ = &system_allocator;
    HParserBackendWithParams *be_with_params = h_get_backend_with_params_by_name("packrat");
    char *descr = h_get_descriptive_text_for_backend_with_params(be_with_params);
    g_check_maybe_string_eq(descr, "Packrat parser with Warth's recursion");
    h_free(descr);
    h_free_backend_with_params(be_with_params);
}

static void test_tt_h_get_name_for_backend_with_params(void) {
    HAllocator *mm__ = &system_allocator;
    HParserBackendWithParams *be_with_params = h_get_backend_with_params_by_name("packrat");
    char *descr = h_get_name_for_backend_with_params(be_with_params);
    g_check_maybe_string_eq(descr, "packrat");
    h_free(descr);
    h_free_backend_with_params(be_with_params);
}

/* test that we can request a backend with params from character
 * and compile a parser using it */
static void test_tt_h_compile_for_backend_with_params(void) {
    HParserBackendWithParams *be_w_p = NULL;

    be_w_p = h_get_backend_with_params_by_name("packrat");
    g_check_inttype("%d", HParserBackend, be_w_p->backend, ==, PB_PACKRAT);

    HParser *p = h_many(h_sequence(h_ch('A'), h_ch('B'), NULL));

    int r = h_compile_for_backend_with_params(p, be_w_p);

    h_free_backend_with_params(be_w_p);
    be_w_p = NULL;

    if (r != 0) {
        g_test_message("Compile failed");
        g_test_fail();
    }
}

void register_names_tests(void) {
    g_test_add_func("/core/names/tt_backend_short_name", test_tt_backend_short_name);
    g_test_add_func("/core/names/tt_backend_description", test_tt_backend_description);
    g_test_add_func("/core/names/tt_query_backend_by_name", test_tt_query_backend_by_name);
    g_test_add_func("/core/names/tt_get_backend_with_params_by_name",
                    test_tt_get_backend_with_params_by_name);
    g_test_add_func("/core/names/tt_test_tt_h_get_descriptive_text_for_backend_with_params",
                    test_tt_h_get_descriptive_text_for_backend_with_params);
    g_test_add_func("/core/names/test_tt_h_get_name_for_backend_with_params",
                    test_tt_h_get_name_for_backend_with_params);
    g_test_add_func("/core/names/tt_h_compile_for_backend_with_params",
                    test_tt_h_compile_for_backend_with_params);
}
