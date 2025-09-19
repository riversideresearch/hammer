#include <glib.h>
#include <string.h>
#include <sys/resource.h>
#include "test_suite.h"
#include "hammer.h"

static void test_tt_user(void) {
  g_check_cmp_int32(TT_USER, >, TT_NONE);
  g_check_cmp_int32(TT_USER, >, TT_BYTES);
  g_check_cmp_int32(TT_USER, >, TT_SINT);
  g_check_cmp_int32(TT_USER, >, TT_UINT);
  g_check_cmp_int32(TT_USER, >, TT_SEQUENCE);
  g_check_cmp_int32(TT_USER, >, TT_ERR);
}

static void test_tt_registry(void) {
  int id = h_allocate_token_type("com.upstandinghackers.test.token_type");
  g_check_cmp_int32(id, >=, TT_USER);
  int id2 = h_allocate_token_type("com.upstandinghackers.test.token_type_2");
  g_check_cmp_int32(id2, !=, id);
  g_check_cmp_int32(id2, >=, TT_USER);
  g_check_cmp_int32(id, ==, h_get_token_type_number("com.upstandinghackers.test.token_type"));
  g_check_cmp_int32(id2, ==, h_get_token_type_number("com.upstandinghackers.test.token_type_2"));
  g_check_string("com.upstandinghackers.test.token_type", ==, h_get_token_type_name(id));
  g_check_string("com.upstandinghackers.test.token_type_2", ==, h_get_token_type_name(id2));
  if (h_get_token_type_name(0) != NULL) {
    g_test_message("Unknown token type should not return a name");
    g_test_fail();
  }
  g_check_cmp_int32(h_get_token_type_number("com.upstandinghackers.test.unkown_token_type"), ==, 0);
}

// test out-of-memory handling with a selectively failing allocator
static void *fail_alloc(HAllocator *mm__, size_t size) {
  if(size - 0xdead <= 0x30) // allow for overhead of arena link structure
    return NULL;
  return system_allocator.alloc(&system_allocator, size);
}
static void *fail_realloc(HAllocator *mm__, void *ptr, size_t size) {
  return system_allocator.realloc(&system_allocator, ptr, size);
}
static void fail_free(HAllocator *mm__, void *ptr) {
  return system_allocator.free(&system_allocator, ptr);
}
static HAllocator fail_allocator = {fail_alloc, fail_realloc, fail_free};
static HParsedToken *act_oom(const HParseResult *r, void *user) {
  void *buf = h_arena_malloc(r->arena, 0xdead);
  assert(buf != NULL);
  return NULL;  // succeed with null token
}
static void test_oom(void) {
  HParser *p = h_action(h_ch('x'), act_oom, NULL);
    // this should always fail, but never crash

  // sanity-check: parses should succeed with the normal allocator...
  g_check_parse_ok(p, PB_PACKRAT, "x",1);
  g_check_parse_chunks_ok(p, PB_PACKRAT, "",0, "x",1);

  // ...and fail gracefully with the broken one
  HAllocator *mm__ = &fail_allocator;
  g_check_parse_failed__m(mm__, p, PB_PACKRAT, "x",1);
  g_check_parse_chunks_failed__m(mm__, p, PB_PACKRAT, "",0, "x",1);
}

void register_misc_tests(void) {
  g_test_add_func("/core/misc/tt_user", test_tt_user);
  g_test_add_func("/core/misc/tt_registry", test_tt_registry);
  g_test_add_func("/core/misc/oom", test_oom);
}
