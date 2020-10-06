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

/*TODO create proper test(s) with test name matching what is being tested.
 * As is this is just what I am using to drive development
 * and check I am not making silly mistakes */
static void test_tt_query_backend_by_name(void) {
	HParserBackend be;

	be = h_query_backend_by_name(packrat_name);
	g_check_inttype("%d", HParserBackend, be, ==, PB_PACKRAT);
	be = h_query_backend_by_name(regular_name);
	g_check_inttype("%d", HParserBackend, be, ==, PB_REGULAR);


	HParserBackendWithParams * be_w_p = NULL;

	be_w_p = h_get_backend_with_params_by_name(glr_name);
	g_check_inttype("%d", HParserBackend, be_w_p->backend, ==, PB_GLR);
	g_check_maybe_string_eq(be_w_p->name, glr_name);

    be_w_p = h_get_backend_with_params_by_name("glr(1)");
    printf("%d be enum val \n", be_w_p->backend);
    g_check_inttype("%d", HParserBackend, be_w_p->backend, ==, PB_GLR);
    printf("%s be name \n", be_w_p->name);
    g_check_maybe_string_eq(be_w_p->name, glr_name);
    printf("%p as void pointer \n", be_w_p->params);
    g_check_cmp_size((uintptr_t)be_w_p->params, ==, 1);
    printf("%zu param as uintptr_t \n", (uintptr_t)be_w_p->params);

}

void register_names_tests(void) {
  g_test_add_func("/core/names/tt_backend_short_name", test_tt_backend_short_name);
  g_test_add_func("/core/names/tt_backend_description", test_tt_backend_description);
  g_test_add_func("/core/names/tt_query_backend_by_name", test_tt_query_backend_by_name);
 }
