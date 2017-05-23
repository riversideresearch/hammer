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
}

/* Reference backend names */
static const char * packrat_name = "packrat";
static const char * regular_name = "regex";
static const char * llk_name = "llk";

static void test_tt_backend_short_name(void) {
  const char *name = NULL;

  name = h_get_name_for_backend(PB_PACKRAT);
  g_check_maybe_string_eq(name, packrat_name);
  name = h_get_name_for_backend(PB_REGULAR);
  g_check_maybe_string_eq(name, regular_name);
  name = h_get_name_for_backend(PB_LLk);
  g_check_maybe_string_eq(name, llk_name);
}

void register_names_tests(void) {
  g_test_add_func("/core/names/tt_backend_short_name", test_tt_backend_short_name);
  g_test_add_func("/core/names/tt_backend_description", test_tt_backend_description);
}
