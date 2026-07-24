/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"

typedef struct {
    const HParser *p;
    double lower;
    double upper;
} HFloatRange;

static HParseResult *parse_float_range(void *env, HParseState *state) {
    HFloatRange *r_env = env;
    HParseResult *ret = h_do_parse(r_env->p, state);

    if (!ret || !ret->ast)
        return NULL;

    switch (ret->ast->token_type) {
    case TT_FLOAT:
        if (r_env->lower <= (double)ret->ast->token_data.flt &&
            (double)ret->ast->token_data.flt <= r_env->upper)
            return ret;
        return NULL;
    case TT_DOUBLE:
        if (r_env->lower <= ret->ast->token_data.dbl &&
            ret->ast->token_data.dbl <= r_env->upper)
            return ret;
        return NULL;
    case TT_SINT:
        if (r_env->lower <= (double)ret->ast->token_data.sint && r_env->upper >= (double)ret->ast->token_data.sint)
            return ret;
        else
            return NULL;
    case TT_UINT:
        if (r_env->lower <= (double)ret->ast->token_data.uint &&
            r_env->upper >= (double)ret->ast->token_data.uint)
            return ret;
        else
            return NULL;
    default:
        return NULL;
    }
}

static const HParserVtable float_range_vt = {
    .parse = parse_float_range,
    .isValidRegular = h_false,
    .isValidCF = h_false,
    .higher = false,
};

HParser *h_float_range(const HParser *p, const double lower, const double upper) {
    return h_float_range__m(&system_allocator, p, lower, upper);
}

HParser *h_float_range__m(HAllocator *mm__, const HParser *p, const double lower,
                          const double upper) {
    HFloatRange *r_env = h_new(HFloatRange, 1);
    r_env->p = p;
    r_env->lower = lower;
    r_env->upper = upper;
    return h_new_parser(mm__, &float_range_vt, r_env);
}
