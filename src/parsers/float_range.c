/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"

typedef struct {
    const HParser *p;
    float lower;
    float upper;
} HFloatRange;

static HParseResult *parse_float_range(void *env, HParseState *state) {
    HFloatRange *r_env = env;
    HParseResult *ret = h_do_parse(r_env->p, state);

    if (!ret || !ret->ast)
        return NULL;

    if (ret->ast->token_type != TT_FLOAT)
        return NULL;

    const float value = ret->ast->token_data.flt;
    if (!(r_env->lower <= value && value <= r_env->upper))
        return NULL;

    return ret;
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
    // p must be a float parser, which means it is using parse_float
    // TODO: re-add this check
    // assert_message(p->vtable == &float_vt, "float_range requires a float parser");

    // and regardless, the bounds need to fit in the parser in question
    // TODO: check this as well.

    HFloatRange *r_env = h_new(HFloatRange, 1);
    r_env->p = p;
    r_env->lower = lower;
    r_env->upper = upper;
    return h_new_parser(mm__, &float_range_vt, r_env);
}
