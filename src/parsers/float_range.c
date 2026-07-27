/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"

typedef struct {
    const HParser *p;
    double lower;
    double upper;
} HFloatRange;

static bool float_range_match(const HParsedToken *token, const HFloatRange *range) {
    double value;

    if (!token)
        return false;

    switch (token->token_type) {
    case TT_FLOAT:
        value = (double)token->token_data.flt;
        break;
    case TT_DOUBLE:
        value = token->token_data.dbl;
        break;
    default:
        return false;
    }

    /*
     * Writing this as a conjunction deliberately rejects NaNs.  It also
     * preserves double-precision bounds when p produces a TT_FLOAT.
     */
    return range->lower <= value && value <= range->upper;
}

static HParseResult *parse_float_range(void *env, HParseState *state) {
    HFloatRange *r_env = env;
    HParseResult *ret = h_do_parse(r_env->p, state);

    if (!ret || !ret->ast)
        return NULL;

    return float_range_match(ret->ast, r_env) ? ret : NULL;
}

static bool float_range_predicate(HParseResult *p, void *user_data) {
    HFloatRange *range = (HFloatRange *)user_data;
    return p && float_range_match(p->ast, range);
}

static void desugar_float_range(HAllocator *mm__, HCFStack *stk__, void *env) {
    HFloatRange *range = (HFloatRange *)env;

    /*
     * Desugaring p retains its float-specific reshape operation.  The outer
     * production then forwards that token and applies the inclusive range
     * predicate to the resulting TT_FLOAT or TT_DOUBLE.
     */
    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() { HCFS_DESUGAR(range->p); }
        HCFS_END_SEQ();
        HCFS_THIS_CHOICE->reshape = h_act_first;
        HCFS_THIS_CHOICE->pred = float_range_predicate;
        HCFS_THIS_CHOICE->user_data = range;
    }
    HCFS_END_CHOICE();
}

static bool h_svm_action_validate_float_range(HArena *arena, HSVMContext *ctx, void *env) {
    HFloatRange *r_env = (HFloatRange *)env;
    HParsedToken *head = ctx->stack[ctx->stack_count - 1];
    switch (head->token_type) {
    case TT_DOUBLE:
        return r_env->lower <= head->token_data.dbl && r_env->upper >= head->token_data.dbl;
    case TT_FLOAT:
        return r_env->lower <= (double)head->token_data.flt &&
               r_env->upper >= (double)head->token_data.flt;
    default:
        return false;
    }
}

static bool fr_ctrvm(HRVMProg *prog, void *env) {
    HFloatRange *r_env = (HFloatRange *)env;
    if (!h_compile_regex(prog, r_env->p))
        return false;
    h_rvm_insert_insn(prog, RVM_ACTION,
                      h_rvm_create_action(prog, h_svm_action_validate_float_range, env));
    return true;
}

static const HParserVtable float_range_vt = {
    .parse = parse_float_range,
    .isValidRegular = h_true,
    .isValidCF = h_true,
    .desugar = desugar_float_range,
    .compile_to_rvm = fr_ctrvm,
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
