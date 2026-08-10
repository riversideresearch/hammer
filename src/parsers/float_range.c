/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"
#include "../trace.h"

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
    bool valid = range->lower <= value && value <= range->upper;
    if (!valid)
        h_trace_note_float_range(token, range->lower, range->upper);
    return valid;
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

static bool h_svm_action_mark_float_range(HArena *arena, HSVMContext *ctx, void *env) {
    (void)arena;

    if (ctx->stack_count == 0 || ctx->stack[ctx->stack_count - 1]->token_type != TT_MARK)
        return false;

    ctx->stack[ctx->stack_count - 1]->token_data.user = env;
    return true;
}

static bool h_svm_action_validate_float_range(HArena *arena, HSVMContext *ctx, void *env) {
    HFloatRange *r_env = (HFloatRange *)env;
    HParsedToken *head = ctx->stack[ctx->stack_count - 1];
    bool valid;

    switch (head->token_type) {
    case TT_DOUBLE:
        valid = r_env->lower <= head->token_data.dbl && r_env->upper >= head->token_data.dbl;
        break;
    case TT_FLOAT:
        valid = r_env->lower <= (double)head->token_data.flt &&
                r_env->upper >= (double)head->token_data.flt;
        break;
    default:
        return false;
    }

    if (valid) {
        /*
         * Higher parsers such as attr_bool may leave capture marks below the
         * resulting token.  Collapse them only as far as float_range's tagged
         * mark so an enclosing parser's mark remains on the stack.
         */
        size_t first = ctx->stack_count - 1;
        while (first > 0) {
            --first;
            if (ctx->stack[first]->token_type == TT_MARK &&
                ctx->stack[first]->token_data.user == r_env) {
                ctx->stack[first] = head;
                ctx->stack_count = first + 1;
                return true;
            }
        }
        return false;
    }
    ctx->failure.kind = SVM_FAILURE_RANGE;
    ctx->failure.start = head->index;
    ctx->failure.end = ctx->input_pos;
    ctx->failure.actual_type = head->token_type;
    ctx->failure.float_lower = r_env->lower;
    ctx->failure.float_upper = r_env->upper;
    ctx->failure.float_actual =
        head->token_type == TT_FLOAT ? (double)head->token_data.flt : head->token_data.dbl;
    ctx->failure.parser = "h_float_range";
    return false;
}

static bool fr_ctrvm(HRVMProg *prog, void *env) {
    HFloatRange *r_env = (HFloatRange *)env;
    HFloatRange *rvm_range = h_rvm_alloc(prog, sizeof(*rvm_range));
    *rvm_range = *r_env;
    rvm_range->p = NULL;

    h_rvm_insert_insn(prog, RVM_PUSH, 0);
    h_rvm_insert_insn(prog, RVM_ACTION,
                      h_rvm_create_action(prog, h_svm_action_mark_float_range, rvm_range));
    if (!h_compile_regex(prog, r_env->p))
        return false;
    h_rvm_insert_insn(prog, RVM_ACTION,
                      h_rvm_create_action(prog, h_svm_action_validate_float_range, rvm_range));
    return true;
}

static bool float_range_isValidRegular(void *env) {
    HFloatRange *range = (HFloatRange *)env;
    return range->p->vtable->isValidRegular(range->p->env);
}

static bool float_range_isValidCF(void *env) {
    HFloatRange *range = (HFloatRange *)env;
    return range->p->vtable->isValidCF(range->p->env);
}

static const HParserVtable float_range_vt = {
    .parse = parse_float_range,
    .isValidRegular = float_range_isValidRegular,
    .isValidCF = float_range_isValidCF,
    .desugar = desugar_float_range,
    .compile_to_rvm = fr_ctrvm,
    .higher = false,
};

HParser *h_float_range(const HParser *p, const double lower, const double upper) {
    return h_float_range__m(&system_allocator, p, lower, upper);
}

HParser *h_float_range__m(HAllocator *mm__, const HParser *p, const double lower,
                          const double upper) {
    /*
     * Do not require the outer parser to be the primitive float parser:
     * higher parsers can transparently wrap a float-producing parser.
     * parse_float_range and the compiled validator still reject non-float
     * results.
     */
    if (!p)
        return NULL;
    HFloatRange *r_env = h_new(HFloatRange, 1);
    r_env->p = p;
    r_env->lower = lower;
    r_env->upper = upper;
    return h_new_parser(mm__, &float_range_vt, r_env);
}
