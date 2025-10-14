#include "parser_internal.h"

struct bytes_env {
    size_t length;
};

static HParseResult *parse_bytes(void *env_, HParseState *state) {
    struct bytes_env *env = env_;
    uint8_t *bs;
    size_t i;

    bs = a_new(uint8_t, env->length);
    for (i = 0; i < env->length && !state->input_stream.overrun; i++)
        bs[i] = h_read_bits(&state->input_stream, 8, false);

    HParsedToken *result = a_new(HParsedToken, 1);
    result->token_type = TT_BYTES;
    result->bytes.token = bs;
    result->bytes.len = env->length;
    result->index = 0;
    result->bit_length = 0;
    result->bit_offset = 0;
    return make_result(state->arena, result);
}

static const HParserVtable bytes_vt = {
    .parse = parse_bytes,
    .isValidRegular = h_false, // XXX need desugar_bytes, reshape_bytes
    .isValidCF = h_false,      // XXX need bytes_ctrvm
};

HParser *h_bytes(size_t len) { return h_bytes__m(&system_allocator, len); }

HParser *h_bytes__m(HAllocator *mm__, size_t len) {
    struct bytes_env *env = h_new(struct bytes_env, 1);
    env->length = len;
    return h_new_parser(mm__, &bytes_vt, env);
}
