/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"

typedef struct {
    const HParser *p;
    float lower;
    float upper;
} HRange;

static HParseResult *parse_float_range(void *env, HParseState *state) {
    HRange *r_env = (HRange *)env;
    HParseResult *ret = h_do_parse(r_env->p, state);
    if (!ret || !ret->ast)
        return NULL;
    if (ret->ast->token_type == TT_FLOAT) {
        if (r_env->lower <= ret->ast->token_data.flt && r_env->upper >= ret->ast->token_data.flt)
            return ret;
        else
            return NULL;
    }
    else
        return NULL;
}

static const HParserVtable float_range_vt = {
    .parse = parse_float_range,
    .isValidRegular = h_false,
    .isValidCF = h_false,
    .higher = false,
};

HParser *h_float_range(const HParser *p, const float lower, const float upper) {
    return h_float_range__m(&system_allocator, p, lower, upper);
}
HParser *h_float_range__m(HAllocator *mm__, const HParser *p, const float lower,
                        const float upper) {
    // p must be an float parser, which means it's using parse_float
    // TODO: re-add this check
    // assert_message(p->vtable == &bits_vt, "float_range requires a float parser");

    // and regardless, the bounds need to fit in the parser in question
    // TODO: check this as well.

    HRange *r_env = h_new(HRange, 1);
    r_env->p = p;
    r_env->lower = lower;
    r_env->upper = upper;
    return h_new_parser(mm__, &float_range_vt, r_env);
}
