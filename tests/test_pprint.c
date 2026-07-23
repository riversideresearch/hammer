#include "hammer.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

typedef struct PPrintOOMAllocator_ {
    HAllocator backing;
    void *live_ptr;
    size_t outstanding;
    size_t realloc_calls;
    void *realloc_ptr;
    size_t realloc_size;
} PPrintOOMAllocator;

static void *pprint_oom_alloc(void *env, size_t size) {
    PPrintOOMAllocator *state = env;
    void *ptr = state->backing.alloc(&state->backing, size);

    if (ptr) {
        state->live_ptr = ptr;
        state->outstanding++;
    }
    return ptr;
}

static void *pprint_oom_realloc(void *env, void *ptr, size_t size) {
    PPrintOOMAllocator *state = env;

    state->realloc_calls++;
    state->realloc_ptr = ptr;
    state->realloc_size = size;

    if (!ptr)
        return pprint_oom_alloc(env, size);

    /* Simulate realloc failing while leaving its input allocation intact. */
    return NULL;
}

static void pprint_oom_free(void *env, void *ptr) {
    PPrintOOMAllocator *state = env;

    if (!ptr)
        return;

    state->backing.free(&state->backing, ptr);
    if (state->outstanding > 0)
        state->outstanding--;
    if (state->live_ptr == ptr)
        state->live_ptr = NULL;
}

static HAllocatorVtable pprint_oom_vtable = {
    .alloc = pprint_oom_alloc,
    .realloc = pprint_oom_realloc,
    .free = pprint_oom_free,
};

static void test_pprint_null(void) {
    FILE *f = tmpfile();
    if (!f)
        return;
    h_pprint(f, NULL, 0, 2);
    fclose(f);
}
static void test_pprint_none(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_NONE;
    FILE *f = tmpfile();
    if (f) {
        h_pprint(f, tok, 0, 2);
        fclose(f);
    }
    h_delete_arena(arena);
}
static void test_pprint_bytes(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_BYTES;
    tok->token_data.bytes.token = (uint8_t *)"test";
    tok->token_data.bytes.len = 4;
    FILE *f = tmpfile();
    if (f) {
        h_pprint(f, tok, 0, 2);
        fclose(f);
    }
    h_delete_arena(arena);
}
static void test_pprint_bytes_special(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_BYTES;
    const uint8_t special[] = {'"', '\\', 0x01, 0x7F};
    tok->token_data.bytes.token = (uint8_t *)special;
    tok->token_data.bytes.len = 4;
    FILE *f = tmpfile();
    if (f) {
        h_pprint(f, tok, 0, 2);
        fclose(f);
    }
    h_delete_arena(arena);
}
static void test_pprint_sint(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_SINT;
    tok->token_data.sint = -12345;
    FILE *f = tmpfile();
    if (f) {
        h_pprint(f, tok, 0, 2);
        fclose(f);
    }
    h_delete_arena(arena);
}
static void test_pprint_uint(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_UINT;
    tok->token_data.uint = 12345;
    FILE *f = tmpfile();
    if (f) {
        h_pprint(f, tok, 0, 2);
        fclose(f);
    }
    h_delete_arena(arena);
}
static void test_pprint_double(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_DOUBLE;
    tok->token_data.dbl = 3.14159;
    FILE *f = tmpfile();
    if (f) {
        h_pprint(f, tok, 0, 2);
        fclose(f);
    }
    h_delete_arena(arena);
}
static void test_pprint_float(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_FLOAT;
    tok->token_data.flt = 3.14f;
    FILE *f = tmpfile();
    if (f) {
        h_pprint(f, tok, 0, 2);
        fclose(f);
    }
    h_delete_arena(arena);
}
static void test_pprint_sequence_empty(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_SEQUENCE;
    tok->token_data.seq = h_carray_new(arena);
    FILE *f = tmpfile();
    if (f) {
        h_pprint(f, tok, 0, 2);
        fclose(f);
    }
    h_delete_arena(arena);
}
static void test_pprint_sequence_small(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_SEQUENCE;
    tok->token_data.seq = h_carray_new(arena);
    HParsedToken *elem = h_arena_malloc(arena, sizeof(HParsedToken));
    elem->token_type = TT_UINT;
    elem->token_data.uint = 42;
    h_carray_append(tok->token_data.seq, elem);
    FILE *f = tmpfile();
    if (f) {
        h_pprint(f, tok, 0, 2);
        fclose(f);
    }
    h_delete_arena(arena);
}
static void test_pprint_sequence_large(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_SEQUENCE;
    tok->token_data.seq = h_carray_new(arena);
    for (int i = 0; i < 5; i++) {
        HParsedToken *elem = h_arena_malloc(arena, sizeof(HParsedToken));
        elem->token_type = TT_UINT;
        elem->token_data.uint = i;
        h_carray_append(tok->token_data.seq, elem);
    }
    FILE *f = tmpfile();
    if (f) {
        h_pprint(f, tok, 0, 2);
        fclose(f);
    }
    h_delete_arena(arena);
}
static void test_pprint_user(void) {
    HTokenType user_type = h_allocate_token_type("test.pprint.user");
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = user_type;
    FILE *f = tmpfile();
    if (f) {
        h_pprint(f, tok, 0, 2);
        fclose(f);
    }
    h_delete_arena(arena);
}
static void test_pprintln(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_UINT;
    tok->token_data.uint = 42;
    FILE *f = tmpfile();
    if (f) {
        h_pprintln(f, tok);
        fclose(f);
    }
    h_delete_arena(arena);
}
static void test_write_result_unamb_none(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_NONE;
    char *result = h_write_result_unamb(tok);
    g_check_cmp_ptr(result, !=, NULL);
    if (result) {
        g_check_string(result, ==, "null");
        system_allocator.free(&system_allocator, result);
    }
    h_delete_arena(arena);
}
static void test_write_result_unamb_bytes_empty(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_BYTES;
    tok->token_data.bytes.token = NULL;
    tok->token_data.bytes.len = 0;
    char *result = h_write_result_unamb(tok);
    g_check_cmp_ptr(result, !=, NULL);
    if (result) {
        system_allocator.free(&system_allocator, result);
    }
    h_delete_arena(arena);
}
static void test_write_result_unamb_bytes_nonempty(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_BYTES;
    tok->token_data.bytes.token = (uint8_t *)"AB";
    tok->token_data.bytes.len = 2;
    char *result = h_write_result_unamb(tok);
    g_check_cmp_ptr(result, !=, NULL);
    if (result) {
        system_allocator.free(&system_allocator, result);
    }
    h_delete_arena(arena);
}
static void test_write_result_unamb_sint_negative(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_SINT;
    tok->token_data.sint = -12345;
    char *result = h_write_result_unamb(tok);
    g_check_cmp_ptr(result, !=, NULL);
    if (result) {
        system_allocator.free(&system_allocator, result);
    }
    h_delete_arena(arena);
}
static void test_write_result_unamb_sint_positive(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_SINT;
    tok->token_data.sint = 12345;
    char *result = h_write_result_unamb(tok);
    g_check_cmp_ptr(result, !=, NULL);
    if (result) {
        system_allocator.free(&system_allocator, result);
    }
    h_delete_arena(arena);
}
static void test_write_result_unamb_uint(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_UINT;
    tok->token_data.uint = 12345;
    char *result = h_write_result_unamb(tok);
    g_check_cmp_ptr(result, !=, NULL);
    if (result) {
        system_allocator.free(&system_allocator, result);
    }
    h_delete_arena(arena);
}
static void test_write_result_unamb_double(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_DOUBLE;
    tok->token_data.dbl = 3.14159;
    char *result = h_write_result_unamb(tok);
    g_check_cmp_ptr(result, !=, NULL);
    if (result) {
        system_allocator.free(&system_allocator, result);
    }
    h_delete_arena(arena);
}
static void test_write_result_unamb_float(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_FLOAT;
    tok->token_data.flt = 3.14f;
    char *result = h_write_result_unamb(tok);
    g_check_cmp_ptr(result, !=, NULL);
    if (result) {
        system_allocator.free(&system_allocator, result);
    }
    h_delete_arena(arena);
}
static void test_write_result_unamb_err(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_ERR;
    char *result = h_write_result_unamb(tok);
    g_check_cmp_ptr(result, !=, NULL);
    if (result) {
        g_check_string(result, ==, "ERR");
        system_allocator.free(&system_allocator, result);
    }
    h_delete_arena(arena);
}
static void test_write_result_unamb_sequence(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_SEQUENCE;
    tok->token_data.seq = h_carray_new(arena);
    HParsedToken *elem = h_arena_malloc(arena, sizeof(HParsedToken));
    elem->token_type = TT_UINT;
    elem->token_data.uint = 42;
    h_carray_append(tok->token_data.seq, elem);
    char *result = h_write_result_unamb(tok);
    g_check_cmp_ptr(result, !=, NULL);
    if (result) {
        system_allocator.free(&system_allocator, result);
    }
    h_delete_arena(arena);
}
static void test_write_result_unamb_user(void) {
    HTokenType user_type = h_allocate_token_type("test.pprint.user.unamb");
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = user_type;
    char *result = h_write_result_unamb(tok);
    g_check_cmp_ptr(result, !=, NULL);
    if (result) {
        system_allocator.free(&system_allocator, result);
    }
    h_delete_arena(arena);
}

static void test_write_result_unamb_realloc_failure_does_not_leak(void) {
    const uint8_t bytes[] = {0, 0, 0, 0};
    HParsedToken bytes_token = {
        .token_type = TT_BYTES,
        .token_data.bytes = {.token = bytes, .len = sizeof(bytes)},
    };
    HParsedToken *elements[] = {&bytes_token};
    HCountedArray sequence = {
        .capacity = 1,
        .used = 1,
        .arena = NULL,
        .elements = elements,
    };
    HParsedToken token = {
        .token_type = TT_SEQUENCE,
        .token_data.seq = &sequence,
    };

    HAllocator saved_allocator = system_allocator;
    PPrintOOMAllocator state = {.backing = saved_allocator};
    h_allocator_wrap(&system_allocator, &pprint_oom_vtable, &state);

    /*
     * This token prints as "(<00.00.00.00>)", exactly 15 bytes.  The only
     * growth attempt is therefore for the trailing NUL byte, so realloc can
     * fail without a later append dereferencing the lost NULL buffer.
     */
    char *result = h_write_result_unamb(&token);
    bool result_is_null = result == NULL;
    bool realloc_had_input = state.realloc_ptr != NULL;

    /* Perform all cleanup available to a normal API caller. */
    system_allocator.free(&system_allocator, result);
    size_t outstanding_after_normal_cleanup = state.outstanding;

    /*
     * Recover the deliberately orphaned allocation so this failing regression
     * test does not itself leak memory under Valgrind or a sanitizer.
     */
    if (state.live_ptr)
        pprint_oom_free(&state, state.live_ptr);

    system_allocator = saved_allocator;

    g_check_cmp_int(result_is_null, ==, true);
    g_check_cmp_size(state.realloc_calls, ==, 1);
    g_check_cmp_int(realloc_had_input, ==, true);
    g_check_cmp_size(state.realloc_size, ==, 32);
    g_check_cmp_size(outstanding_after_normal_cleanup, ==, 0);
}

static void test_buffer_functions(void) {
    HArena *arena = h_new_arena(&system_allocator, 4096);
    HParsedToken *tok = h_arena_malloc(arena, sizeof(HParsedToken));
    tok->token_type = TT_SEQUENCE;
    tok->token_data.seq = h_carray_new(arena);

    for (int i = 0; i < 50; i++) {
        HParsedToken *elem = h_arena_malloc(arena, sizeof(HParsedToken));
        elem->token_type = TT_UINT;
        elem->token_data.uint = i;
        h_carray_append(tok->token_data.seq, elem);
    }
    char *result = h_write_result_unamb(tok);
    g_check_cmp_ptr(result, !=, NULL);
    if (result) {
        system_allocator.free(&system_allocator, result);
    }
    h_delete_arena(arena);
}

void register_pprint_tests(void) {
    g_test_add_func("/core/pprint/null", test_pprint_null);
    g_test_add_func("/core/pprint/none", test_pprint_none);
    g_test_add_func("/core/pprint/bytes", test_pprint_bytes);
    g_test_add_func("/core/pprint/bytes_special", test_pprint_bytes_special);
    g_test_add_func("/core/pprint/sint", test_pprint_sint);
    g_test_add_func("/core/pprint/uint", test_pprint_uint);
    g_test_add_func("/core/pprint/double", test_pprint_double);
    g_test_add_func("/core/pprint/float", test_pprint_float);
    g_test_add_func("/core/pprint/sequence_empty", test_pprint_sequence_empty);
    g_test_add_func("/core/pprint/sequence_small", test_pprint_sequence_small);
    g_test_add_func("/core/pprint/sequence_large", test_pprint_sequence_large);
    g_test_add_func("/core/pprint/user", test_pprint_user);
    g_test_add_func("/core/pprint/pprintln", test_pprintln);
    g_test_add_func("/core/pprint/write_result_unamb_none", test_write_result_unamb_none);
    g_test_add_func("/core/pprint/write_result_unamb_bytes_empty",
                    test_write_result_unamb_bytes_empty);
    g_test_add_func("/core/pprint/write_result_unamb_bytes_nonempty",
                    test_write_result_unamb_bytes_nonempty);
    g_test_add_func("/core/pprint/write_result_unamb_sint_negative",
                    test_write_result_unamb_sint_negative);
    g_test_add_func("/core/pprint/write_result_unamb_sint_positive",
                    test_write_result_unamb_sint_positive);
    g_test_add_func("/core/pprint/write_result_unamb_uint", test_write_result_unamb_uint);
    g_test_add_func("/core/pprint/write_result_unamb_double", test_write_result_unamb_double);
    g_test_add_func("/core/pprint/write_result_unamb_float", test_write_result_unamb_float);
    g_test_add_func("/core/pprint/write_result_unamb_err", test_write_result_unamb_err);
    g_test_add_func("/core/pprint/write_result_unamb_sequence", test_write_result_unamb_sequence);
    g_test_add_func("/core/pprint/write_result_unamb_user", test_write_result_unamb_user);
    g_test_add_func("/core/pprint/write_result_unamb_realloc_failure_does_not_leak",
                    test_write_result_unamb_realloc_failure_does_not_leak);
    g_test_add_func("/core/pprint/buffer_functions", test_buffer_functions);
}
