#include "parser_internal.h"

#include <assert.h>
#include <stdint.h>

static HParseResult *parse_ch(void *env, HParseState *state) {
    uint8_t c = (uint8_t)(uintptr_t)(env);
    uint8_t r = (uint8_t)h_read_bits(&state->input_stream, 8, false);
    if (c == r) {
        HParsedToken *tok = a_new(HParsedToken, 1);
        tok->token_type = TT_UINT;
        tok->uint = r;
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

static const HParserVtable ch_vt = {
    .parse = parse_ch,
    .isValidRegular = h_true,
    .isValidCF = h_true,
    .desugar = desugar_ch,
    .higher = false,
};

HParser *h_ch(const uint8_t c) { return h_ch__m(&system_allocator, c); }
HParser *h_ch__m(HAllocator *mm__, const uint8_t c) {
    return h_new_parser(mm__, &ch_vt, (void *)(uintptr_t)c);
}
