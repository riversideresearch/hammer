/* Copyright (c) 2026 Riverside Research */
#include "../internal.h"
#include "parser_internal.h"

#include <assert.h>
#include <string.h>

static HParseResult *parse_charset(void *env, HParseState *state) {
    uint8_t in = h_read_bits(&state->input_stream, 8, false);
    HCharset cs = (HCharset)env;

    if (charset_isset(cs, in)) {
        HParsedToken *tok = a_new(HParsedToken, 1);
        tok->token_type = TT_UINT;
        tok->token_data.uint = in;
        tok->index = 0;
        tok->bit_length = 0;
        tok->bit_offset = 0;
        return make_result(state->arena, tok);
    } else
        return NULL;
}

static void desugar_charset(HAllocator *mm__, HCFStack *stk__, void *env) {
    HCharset cs = new_charset(mm__);
    memcpy(cs, env, sizeof(unsigned int) * (256 / (sizeof(unsigned int) * 8)));
    HCFS_ADD_CHARSET(cs);
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

static bool cs_ctrvm(HRVMProg *prog, void *env) {
    HCharset cs = (HCharset)env;
    h_rvm_insert_insn(prog, RVM_PUSH, 0);

    uint16_t start = h_rvm_get_ip(prog);
    uint8_t range_start = 0;
    bool collecting = false;

    for (unsigned int i = 0; i < 256; i++) {
        if (charset_isset(cs, i)) {
            if (!collecting) {
                collecting = true;
                range_start = i;
            }
        } else if (collecting) {
            collecting = false;
            uint16_t insn = h_rvm_insert_insn(prog, RVM_FORK, 0);
            h_rvm_insert_insn(prog, RVM_MATCH, range_start | ((i - 1) << 8));
            h_rvm_insert_insn(prog, RVM_GOTO, 0);
            h_rvm_patch_arg(prog, insn, h_rvm_get_ip(prog));
        }
    }
    if (collecting) {
        uint16_t insn = h_rvm_insert_insn(prog, RVM_FORK, 0);
        h_rvm_insert_insn(prog, RVM_MATCH, range_start | (255 << 8));
        h_rvm_insert_insn(prog, RVM_GOTO, 0);
        h_rvm_patch_arg(prog, insn, h_rvm_get_ip(prog));
    }

    h_rvm_insert_insn(prog, RVM_MATCH, 0x00FF);
    uint16_t jump = h_rvm_insert_insn(prog, RVM_STEP, 0);
    for (size_t i = start; i < jump; ++i) {
        if (RVM_GOTO == prog->insns[i].op)
            h_rvm_patch_arg(prog, i, jump);
    }

    h_rvm_insert_insn(prog, RVM_CAPTURE, 0);
    h_rvm_insert_insn(prog, RVM_ACTION, h_rvm_create_action(prog, h_svm_action_ch, NULL));
    return true;
}

static size_t trace_expectations_charset(void *env, size_t consumed, bool overrun,
                                         bool expected[256], bool *expected_eof) {
    (void)consumed;
    (void)overrun;
    (void)expected_eof;
    HCharset cs = env;
    for (size_t i = 0; i < 256; i++)
        expected[i] |= charset_isset(cs, (uint8_t)i);
    return 0;
}

static const HParserVtable charset_vt = {
    .name = "h_charset",
    .parse = parse_charset,
    .isValidRegular = h_true,
    .isValidCF = h_true,
    .compile_to_rvm = cs_ctrvm,
    .desugar = desugar_charset,
    .trace_expectations = trace_expectations_charset,
    .higher = false,
};

HParser *h_ch_range(const uint8_t lower, const uint8_t upper) {
    return h_ch_range__m(&system_allocator, lower, upper);
}
HParser *h_ch_range__m(HAllocator *mm__, const uint8_t lower, const uint8_t upper) {
    HCharset cs = new_charset(mm__);
    for (int i = 0; i < 256; i++)
        charset_set(cs, i, (lower <= i) && (i <= upper));
    return h_new_parser(mm__, &charset_vt, cs);
}

static HParser *h_in_or_not__m(HAllocator *mm__, const uint8_t *options, size_t count, int val) {
    HCharset cs = new_charset(mm__);
    for (size_t i = 0; i < 256; i++)
        charset_set(cs, i, 1 - val);
    for (size_t i = 0; i < count; i++)
        charset_set(cs, options[i], val);

    return h_new_parser(mm__, &charset_vt, cs);
}

HParser *h_in(const uint8_t *options, size_t count) {
    return h_in_or_not__m(&system_allocator, options, count, 1);
}

HParser *h_in__m(HAllocator *mm__, const uint8_t *options, size_t count) {
    return h_in_or_not__m(mm__, options, count, 1);
}

HParser *h_not_in(const uint8_t *options, size_t count) {
    return h_in_or_not__m(&system_allocator, options, count, 0);
}

HParser *h_not_in__m(HAllocator *mm__, const uint8_t *options, size_t count) {
    return h_in_or_not__m(mm__, options, count, 0);
}
