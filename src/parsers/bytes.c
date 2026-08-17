/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"
#include <stdint.h>

struct bytes_env {
    size_t length; // Length in bytes
};

static HParseResult *parse_bytes(void *env_, HParseState *state) {
    struct bytes_env *env = env_;
    uint8_t *bs;
    size_t i;

    bs = a_new(uint8_t, env->length);
    for (i = 0; i < env->length && !state->input_stream.overrun; i++)
        bs[i] = (uint8_t)h_read_bits(&state->input_stream, 8, false);

    HParsedToken *result = a_new(HParsedToken, 1);
    result->token_type = TT_BYTES;
    result->token_data.bytes.token = bs;
    result->token_data.bytes.len = env->length;
    result->index = 0;
    result->bit_length = 0;
    result->bit_offset = 0;
    return make_result(state->arena, result);
}

static HParsedToken *reshape_bytes(const HParseResult *p, void *user_data) {
    assert(p->ast);
    assert(p->ast->token_type == TT_SEQUENCE);

    HCountedArray *seq = p->ast->token_data.seq;
    uint8_t *arr = h_arena_malloc(p->arena, seq->used);

    for (size_t i = 0; i < seq->used; i++) {
        HParsedToken *t = seq->elements[i];
        assert(t->token_type == TT_UINT && t->token_data.uint <= 255);
        arr[i] = (uint8_t)t->token_data.uint;
    }

    HParsedToken *tok = h_arena_malloc(p->arena, sizeof(HParsedToken));
    tok->token_type = TT_BYTES;
    tok->token_data.bytes.len = seq->used;
    tok->token_data.bytes.token = arr;
    tok->index = 0;
    tok->bit_offset = 0;
    tok->bit_length = 0;
    return tok;
}

static void desugar_bytes(HAllocator *mm__, HCFStack *stk__, void *env) {
    struct bytes_env *be = (struct bytes_env *)env;
    if (!be) {
        HCFS_BEGIN_CHOICE() {}
        HCFS_END_CHOICE();
        return;
    }
    size_t len = be->length;
    if (len == 0) {
        // empty sequence
        HCFS_BEGIN_CHOICE() {}
        HCFS_END_CHOICE();
        return;
    }

    HCharset cs = new_charset(mm__);
    for (int i = 0; i <= 255; i++)
        charset_set(cs, (uint8_t)i, 1);

    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ();
        {
            for (size_t i = 0; i < len; ++i) {
                HCFS_ADD_CHARSET(cs);
            }
        }
        HCFS_END_SEQ();
        HCFS_THIS_CHOICE->reshape = reshape_bytes;
        HCFS_THIS_CHOICE->user_data = NULL;
    }
    HCFS_END_CHOICE();
}

static bool bytes_ctrvm(HRVMProg *prog, void *env) {
    struct bytes_env *env_ = (struct bytes_env *)env;
    h_rvm_insert_insn(prog, RVM_PUSH, 0);
    for (size_t i = 0; i < env_->length; ++i) {
        h_rvm_insert_insn(prog, RVM_MATCH, 0xFF00);
        h_rvm_insert_insn(prog, RVM_STEP, 0);
    }
    h_rvm_insert_insn(prog, RVM_CAPTURE, 0);
    return true;
}

static const HParserVtable bytes_vt = {.name = "h_bytes",
                                       .parse = parse_bytes,
                                       .desugar = desugar_bytes,
                                       .isValidRegular = h_true,
                                       .isValidCF = h_true,
                                       .compile_to_rvm = bytes_ctrvm,
                                       .higher = false};

HParser *h_bytes(size_t len) { return h_bytes__m(&system_allocator, len); }

HParser *h_bytes__m(HAllocator *mm__, size_t len) {
    struct bytes_env *env = h_new(struct bytes_env, 1);
    env->length = len;
    return h_new_parser(mm__, &bytes_vt, env);
}
