/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"
#include "../trace.h"

typedef struct {
    const HParser *p;
    int64_t lower;
    int64_t upper;
} HRange;

static bool int_range_match(const HParsedToken *token, const HRange *range) {
    bool valid;
    if (!token)
        return false;
    switch (token->token_type) {
    case TT_SINT:
        valid = range->lower <= token->token_data.sint && token->token_data.sint <= range->upper;
        break;
    case TT_UINT:
        valid = (uint64_t)range->lower <= token->token_data.uint &&
                token->token_data.uint <= (uint64_t)range->upper;
        break;
    default:
        return false;
    }
    if (!valid)
        h_trace_note_int_range(token, range->lower, range->upper);
    return valid;
}

static HParseResult *parse_int_range(void *env, HParseState *state) {
    HRange *r_env = (HRange *)env;
    HParseResult *ret = h_do_parse(r_env->p, state);
    if (!ret || !ret->ast)
        return NULL;
    return int_range_match(ret->ast, r_env) ? ret : NULL;
}

static bool int_range_predicate(HParseResult *result, void *user_data) {
    HRange *range = user_data;

    if (!result || !result->ast)
        return false;

    return int_range_match(result->ast, range);
}

struct bits_env {
    size_t length;
    uint8_t signedp;
};

static void desugar_int_range(HAllocator *mm__, HCFStack *stk__, void *env) {
    HRange *range = env;

    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() { HCFS_DESUGAR(range->p); }
        HCFS_END_SEQ();

        HCFS_THIS_CHOICE->reshape = h_act_first;
        HCFS_THIS_CHOICE->pred = int_range_predicate;
        HCFS_THIS_CHOICE->user_data = range;
    }
    HCFS_END_CHOICE();
}

static bool h_svm_action_mark_int_range(HArena *arena, HSVMContext *ctx, void *env) {
    (void)arena;

    if (ctx->stack_count == 0 || ctx->stack[ctx->stack_count - 1]->token_type != TT_MARK)
        return false;

    ctx->stack[ctx->stack_count - 1]->token_data.user = env;
    return true;
}

static bool h_svm_action_validate_int_range(HArena *arena, HSVMContext *ctx, void *env) {
    HRange *r_env = (HRange *)env;
    if (ctx->stack_count == 0)
        return false;
    HParsedToken *head = ctx->stack[ctx->stack_count - 1];
    bool valid;

    switch (head->token_type) {
    case TT_SINT:
        valid = r_env->lower <= head->token_data.sint && r_env->upper >= head->token_data.sint;
        break;
    case TT_UINT:
        valid = (uint64_t)r_env->lower <= head->token_data.uint &&
                (uint64_t)r_env->upper >= head->token_data.uint;
        break;
    default:
        return false;
    }

    if (valid) {
        /*
         * Higher parsers such as attr_bool may leave capture marks below the
         * resulting token.  Collapse them only as far as int_range's tagged
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
    ctx->failure.lower = r_env->lower;
    ctx->failure.upper = r_env->upper;
    ctx->failure.parser = "h_int_range";
    if (head->token_type == TT_SINT)
        ctx->failure.actual.sint = head->token_data.sint;
    else
        ctx->failure.actual.uint = head->token_data.uint;
    return false;
}

static bool ir_ctrvm(HRVMProg *prog, void *env) {
    HRange *r_env = (HRange *)env;
    HRange *rvm_range = h_rvm_alloc(prog, sizeof(*rvm_range));
    *rvm_range = *r_env;
    rvm_range->p = NULL;

    h_rvm_insert_insn(prog, RVM_PUSH, 0);
    h_rvm_insert_insn(prog, RVM_ACTION,
                      h_rvm_create_action(prog, h_svm_action_mark_int_range, rvm_range));
    if (!h_compile_regex(prog, r_env->p))
        return false;
    h_rvm_insert_insn(prog, RVM_ACTION,
                      h_rvm_create_action(prog, h_svm_action_validate_int_range, rvm_range));
    return true;
}

static bool int_isValidRegular(void *env) {
    HRange *r_env = (HRange *)env;
    return r_env->p->vtable->isValidRegular(r_env->p->env);
}

static bool int_isValidCF(void *env) {
    HRange *r_env = (HRange *)env;
    return r_env->p->vtable->isValidCF(r_env->p->env);
}

static const HParserVtable int_range_vt = {
    .parse = parse_int_range,
    .isValidRegular = int_isValidRegular,
    .isValidCF = int_isValidCF,
    .compile_to_rvm = ir_ctrvm,
    .desugar = desugar_int_range,
    .higher = false,
};

HParser *h_int_range(const HParser *p, const int64_t lower, const int64_t upper) {
    return h_int_range__m(&system_allocator, p, lower, upper);
}
HParser *h_int_range__m(HAllocator *mm__, const HParser *p, const int64_t lower,
                        const int64_t upper) {
    HRange *r_env = h_new(HRange, 1);
    r_env->p = p;
    r_env->lower = lower;
    r_env->upper = upper;
    return h_new_parser(mm__, &int_range_vt, r_env);
}
