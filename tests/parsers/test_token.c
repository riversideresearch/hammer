#include "glue.h"
#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>

// Test token.c: reshape_token (lines 28-50)
static void test_reshape_token(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    HParser *token = h_token((const uint8_t *)"abc", 3);
    h_compile(token, be, NULL);
    HParseResult *res = h_parse(token, (const uint8_t *)"abc", 3);
    if (res) {
        g_check_cmp_ptr(res->ast, !=, NULL);
        if (res->ast && res->ast->token_type == TT_BYTES) {
            g_check_cmp_int(res->ast->bytes.len, ==, 3);
        }
        h_parse_result_free(res);
    }

    HParser *token2 = h_token((const uint8_t *)"xyz", 3);
    HCFChoice *desugared = h_desugar(&system_allocator, NULL, token2);
    if (desugared && desugared->reshape) {
        HArena *arena = h_new_arena(&system_allocator, 0);
        HParsedToken *seq_token = h_make_seq(arena);
        HParsedToken *elem1 = h_make_uint(arena, 120);
        h_carray_append(seq_token->seq, elem1);
        HParsedToken *elem2 = h_make_uint(arena, 121);
        h_carray_append(seq_token->seq, elem2);
        HParsedToken *elem3 = h_make_uint(arena, 122);
        h_carray_append(seq_token->seq, elem3);
        HParseResult mock_result = {.arena = arena, .ast = seq_token, .bit_length = 24};
        HParsedToken *reshaped = desugared->reshape(&mock_result, NULL);
        g_check_cmp_ptr(reshaped, !=, NULL);
        h_delete_arena(arena);
    }
}

void register_token_tests(void) {
    g_test_add_data_func("/core/parser/packrat/reshape_token", GINT_TO_POINTER(PB_PACKRAT),
                         test_reshape_token);
}
