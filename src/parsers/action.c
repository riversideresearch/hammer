/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"

#include <assert.h>

typedef struct {
    const HParser *p;
    HAction action;
    void *user_data;
} HParseAction;

static HParseResult *parse_action(void *env, HParseState *state) {
    HParseAction *a = (HParseAction *)env;
    if (a->p && a->action) {
        HParseResult *tmp = h_do_parse(a->p, state);
        // HParsedToken *tok = a->action(h_do_parse(a->p, state));
        if (tmp) {
            HParsedToken *tok = (HParsedToken *)a->action(tmp, a->user_data);
            return make_result(state->arena, tok);
        } else
            return NULL;
    } else // either the parser's missing or the action's missing
        return NULL;
}

static void desugar_action(HAllocator *mm__, HCFStack *stk__, void *env) {
    HParseAction *a = (HParseAction *)env;

    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() { HCFS_DESUGAR(a->p); }
        HCFS_END_SEQ();
        HCFS_THIS_CHOICE->user_data = a->user_data;
        HCFS_THIS_CHOICE->action = a->action;
        HCFS_THIS_CHOICE->reshape = h_act_first;
    }
    HCFS_END_CHOICE();
}

static bool action_isValidRegular(void *env) {
    HParseAction *a = (HParseAction *)env;
    return a->p->vtable->isValidRegular(a->p->env);
}

static bool action_isValidCF(void *env) {
    HParseAction *a = (HParseAction *)env;
    return a->p->vtable->isValidCF(a->p->env);
}

static bool h_svm_action_action(HArena *arena, HSVMContext *ctx, void *arg) {
    HParseResult res;
    HParseAction *a = arg;
    assert(ctx->stack_count >= 1);
    HParsedToken *top = ctx->stack[ctx->stack_count - 1];
    if (top->token_type != TT_MARK &&
        (ctx->stack_count < 2 || ctx->stack[ctx->stack_count - 2]->token_type != TT_MARK))
        return false;
    res.ast = top->token_type == TT_MARK ? NULL : top;
    res.arena = arena;
    HParsedToken *action_result = a->action(&res, a->user_data);
    if (top->token_type == TT_MARK) {
        if (action_result)
            ctx->stack[ctx->stack_count - 1] = action_result;
        else
            ctx->stack_count--;
    } else {
        if (action_result) {
            ctx->stack[ctx->stack_count - 2] = action_result;
            ctx->stack_count--;
        } else {
            ctx->stack_count -= 2;
        }
    }
    return true;
}

static bool action_ctrvm(HRVMProg *prog, void *env) {
    HParseAction *a = (HParseAction *)env;
    h_rvm_insert_insn(prog, RVM_PUSH, 0);
    if (!h_compile_regex(prog, a->p))
        return false;
    HParseAction *rvm_action = h_rvm_alloc(prog, sizeof(*rvm_action));
    *rvm_action = *a;
    rvm_action->p = NULL;
    h_rvm_insert_insn(prog,
                      RVM_ACTION,
                      h_rvm_create_action(prog, h_svm_action_action, rvm_action));
    return true;
}

static const HParserVtable action_vt = {
    .parse = parse_action,
    .isValidRegular = action_isValidRegular,
    .isValidCF = action_isValidCF,
    .compile_to_rvm = action_ctrvm,
    .desugar = desugar_action,
    .higher = true,
};

HParser *h_action(const HParser *p, const HAction a, void *user_data) {
    return h_action__m(&system_allocator, p, a, user_data);
}

HParser *h_action__m(HAllocator *mm__, const HParser *p, const HAction a, void *user_data) {
    HParseAction *env = h_new(HParseAction, 1);
    env->p = p;
    env->action = a;
    env->user_data = user_data;
    return h_new_parser(mm__, &action_vt, env);
}
