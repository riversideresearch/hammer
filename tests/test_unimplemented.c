#include "hammer.h"
#include "test_suite.h"

#include <glib.h>

/* Not part of public API headers; provide prototype for test */
const HParser *h_unimplemented(void);
const HParser *h_unimplemented__m(HAllocator *mm__);

static void test_unimplemented_returns_error_token(void) {
    // Test h_unimplemented() - line 21
    const HParser *p = h_unimplemented();
    g_check_cmp_ptr(p, !=, NULL);

    // Compile the parser first (required before parsing)
    HParser *p_mutable = (HParser *)p;
    int compile_result = h_compile(p_mutable, PB_PACKRAT, NULL);
    // Unimplemented parser might not compile, but that's OK - we can still test the function call
    (void)compile_result;

    // Test parse_unimplemented() - lines 3, 8
    // Note: This will crash if backend_vtable is NULL, so we need compilation
    const char *input = "";
    HParseResult *res = h_parse(p, (const uint8_t *)input, 0);
    g_check_cmp_ptr(res, !=, NULL);
    g_check_cmp_ptr(res->ast, !=, NULL);
    g_check_cmp_int(res->ast->token_type, ==, TT_ERR);
    /* Do not free: unimplemented returns a static HParseResult */
}

static void test_unimplemented_m(void) {
    // Test h_unimplemented__m() - line 22
    extern const HParser *h_unimplemented__m(HAllocator * mm__);

    // Test that h_unimplemented__m returns a valid parser
    const HParser *p = h_unimplemented__m(&system_allocator);
    g_check_cmp_ptr(p, !=, NULL);

    // Verify it returns the same parser instance as h_unimplemented()
    const HParser *p2 = h_unimplemented();
    g_check_cmp_ptr(p, ==, p2);

    // Compile the parser first (required before parsing)
    HParser *p_mutable = (HParser *)p;
    int compile_result = h_compile(p_mutable, PB_PACKRAT, NULL);
    (void)compile_result;

    // Test parsing with it to ensure parse_unimplemented is called (lines 3, 8)
    const char *input = "test";
    HParseResult *res = h_parse(p, (const uint8_t *)input, 4);
    g_check_cmp_ptr(res, !=, NULL);
    g_check_cmp_ptr(res->ast, !=, NULL);
    g_check_cmp_int(res->ast->token_type, ==, TT_ERR);
    /* Do not free: unimplemented returns a static HParseResult */
}

void register_unimplemented_tests(void) {
    g_test_add_func("/core/parser/unimplemented/error_token",
                    test_unimplemented_returns_error_token);
    g_test_add_func("/core/parser/unimplemented/m", test_unimplemented_m);
}
