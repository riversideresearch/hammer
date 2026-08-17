/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"

#include <assert.h>
#include <stdint.h>

static HParseResult *parse_ch(void *env, HParseState *state) {
    uint8_t c = (uint8_t)(uintptr_t)(env);
    uint8_t r = (uint8_t)h_read_bits(&state->input_stream, 8, false);
    if (c == r) {
        HParsedToken *tok = a_new(HParsedToken, 1);
        tok->token_type = TT_UINT;
        tok->token_data.uint = r;
        tok->index = 0;
        tok->bit_length = 0;
        tok->bit_offset = 0;
        return make_result(state->arena, tok);
    } else {
        return NULL;
    }
}

static void desugar_ch(HAllocator *mm__, HCFStack *stk__, void *env) {
    HCFS_ADD_CHAR((uint8_t)(uintptr_t)(env));
}

static bool h_svm_action_ch(HArena *arena, HSVMContext *ctx, void *env) {
    HParsedToken *top = ctx->stack[ctx->stack_count - 1];
    assert(top->token_type == TT_BYTES);
    uint64_t res = 0;
    for (size_t i = 0; i < top->token_data.bytes.len; i++)
        res = (res << 8) | top->token_data.bytes.token[i];
    top->token_data.uint = res;
    top->token_type = TT_UINT;
    return true;
}

static bool ch_ctrvm(HRVMProg *prog, void *env) {
    uint8_t c = (uint8_t)(uintptr_t)(env);
    h_rvm_insert_insn(prog, RVM_PUSH, 0);
    h_rvm_insert_insn(prog, RVM_MATCH, c | c << 8);
    h_rvm_insert_insn(prog, RVM_STEP, 0);
    h_rvm_insert_insn(prog, RVM_CAPTURE, 0);
    h_rvm_insert_insn(prog, RVM_ACTION, h_rvm_create_action(prog, h_svm_action_ch, NULL));
    return true;
}

static size_t trace_expectations_ch(void *env, size_t consumed, bool overrun, bool expected[256],
                                    bool *expected_eof) {
    (void)consumed;
    (void)overrun;
    (void)expected_eof;
    expected[(uint8_t)(uintptr_t)env] = true;
    return 0;
}

static const HParserVtable ch_vt = {
    .name = "h_ch",
    .parse = parse_ch,
    .isValidRegular = h_true,
    .isValidCF = h_true,
    .compile_to_rvm = ch_ctrvm,
    .desugar = desugar_ch,
    .trace_expectations = trace_expectations_ch,
    .higher = false,
};

HParser *h_ch(const uint8_t c) { return h_ch__m(&system_allocator, c); }
HParser *h_ch__m(HAllocator *mm__, const uint8_t c) {
    return h_new_parser_with_free(mm__, &ch_vt, (void *)(uintptr_t)c, h_no_free_env);
}
