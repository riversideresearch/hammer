#include "hammer.h"
#include "test_suite.h"

#include <glib.h>

/* Not part of public API headers; provide prototype for test */
const HParser *h_unimplemented(void);

static void test_unimplemented_returns_error_token(void) {
    const HParser *p = h_unimplemented();
    const char *input = "";
    HParseResult *res = h_parse((const HParser *)p, (const uint8_t *)input, 0);
    g_check_cmp_ptr(res, !=, NULL);
    g_check_cmp_ptr(res->ast, !=, NULL);
    g_check_cmp_int(res->ast->token_type, ==, TT_ERR);
    /* Do not free: unimplemented returns a static HParseResult */
}

void register_unimplemented_tests(void) {
    g_test_add_func("/core/parser/unimplemented/error_token",
                    test_unimplemented_returns_error_token);
}
