#include <glib.h>
#include <string.h>
#include "test_suite.h"
#include "hammer.h"

static void test_tt_backend_description(void) {
  const char *desc = NULL;

  /* Check that we get some */
  desc = h_get_descriptive_text_for_backend(PB_PACKRAT);
  g_check_cmp_ptr(desc, !=, NULL);
  desc = h_get_descriptive_text_for_backend(PB_REGULAR);
  g_check_cmp_ptr(desc, !=, NULL);
  desc = h_get_descriptive_text_for_backend(PB_LLk);
  g_check_cmp_ptr(desc, !=, NULL);
  desc = h_get_descriptive_text_for_backend(PB_LALR);
  g_check_cmp_ptr(desc, !=, NULL);
  desc = h_get_descriptive_text_for_backend(PB_GLR);
  g_check_cmp_ptr(desc, !=, NULL);
}

/* Reference backend names */
static const char * packrat_name = "packrat";
static const char * regular_name = "regex";
static const char * llk_name = "llk";
static const char * lalr_name = "lalr";
static const char * glr_name = "glr";

static void test_tt_backend_short_name(void) {
  const char *name = NULL;

  name = h_get_name_for_backend(PB_PACKRAT);
  g_check_maybe_string_eq(name, packrat_name);
  name = h_get_name_for_backend(PB_REGULAR);
  g_check_maybe_string_eq(name, regular_name);
  name = h_get_name_for_backend(PB_LLk);
  g_check_maybe_string_eq(name, llk_name);
  name = h_get_name_for_backend(PB_LALR);
  g_check_maybe_string_eq(name, lalr_name);
  name = h_get_name_for_backend(PB_GLR);
  g_check_maybe_string_eq(name, glr_name);
}

static void test_tt_query_backend_by_name(void) {
	HParserBackend be;

	be = h_query_backend_by_name(packrat_name);
	g_check_inttype("%d", HParserBackend, be, ==, PB_PACKRAT);
	be = h_query_backend_by_name(regular_name);
	g_check_inttype("%d", HParserBackend, be, ==, PB_REGULAR);

}

static void test_tt_get_backend_with_params_by_name(void) {
	HParserBackendWithParams * be_w_p = NULL;

	be_w_p = h_get_backend_with_params_by_name(glr_name);
	g_check_inttype("%d", HParserBackend, be_w_p->backend, ==, PB_GLR);
	g_check_maybe_string_eq(be_w_p->name, glr_name);

    be_w_p = h_get_backend_with_params_by_name("glr(1)");

    g_check_inttype("%d", HParserBackend, be_w_p->backend, ==, PB_GLR);

    g_check_maybe_string_eq(be_w_p->name, glr_name);

    g_check_cmp_size((uintptr_t)be_w_p->params, ==, 1);


}

/* test that we can request a backend with params from character
 * and compile a parser using it */
static void test_tt_h_compile_for_named_backend(void) {
	HParserBackendWithParams * be_w_p = NULL;

	be_w_p = h_get_backend_with_params_by_name(llk_name);
	g_check_inttype("%d", HParserBackend, be_w_p->backend, ==, PB_LLk);

	be_w_p->mm__ = &system_allocator;

	HParser *p = h_many(h_sequence(h_ch('A'), h_ch('B'), NULL));

	int r = h_compile_for_named_backend(p, be_w_p);
	if (r != 0) {
		g_test_message("Compile failed");
	    g_test_fail();
	}

  /* TODO further test for success? */

}

void register_names_tests(void) {
  g_test_add_func("/core/names/tt_backend_short_name", test_tt_backend_short_name);
  g_test_add_func("/core/names/tt_backend_description", test_tt_backend_description);
  g_test_add_func("/core/names/tt_query_backend_by_name", test_tt_query_backend_by_name);
  g_test_add_func("/core/names/tt_get_backend_with_params_by_name", test_tt_get_backend_with_params_by_name);
  g_test_add_func("/core/names/tt_h_compile_for_named_backend", test_tt_h_compile_for_named_backend);
 }
