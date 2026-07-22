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
    if (ctx->stack[ctx->stack_count - 1]->token_type != TT_MARK) {
        res.ast = ctx->stack[ctx->stack_count - 1];
    } else {
        res.ast = NULL;
    }
    res.arena = arena;
    HParsedToken *action_result = a->action(&res, a->user_data);
    if (action_result)
        ctx->stack[ctx->stack_count - 1] = action_result;
    else
        ctx->stack_count--;
    return true;
}

static bool action_ctrvm(HRVMProg *prog, void *env) {
    HParseAction *a = (HParseAction *)env;
    h_rvm_insert_insn(prog, RVM_PUSH, 0);
    if (!h_compile_regex(prog, a->p))
        return false;
    h_rvm_insert_insn(prog, RVM_ACTION, h_rvm_create_action(prog, h_svm_action_action, a));
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
// HActionCollection Append
// Action Wait

typedef struct {
    const HParser *p;
    HAction action;
    void *user_data;
    HActionCollection *collection;
} HParseActionWait;

static HParseResult *parse_action_wait(void *env, HParseState *state) {
    HParseActionWait *a = (HParseActionWait *)env;
    if (a->p && a->action) {
        HParseResult *res = h_do_parse(a->p, state);
        if (res) {
            a->collection->res = res;
            a->collection->action=a->action;
            a->collection->user_data = a->user_data;
            return res;
        } else
            return NULL;
    } else // either the parser's missing or the action's missing
        return NULL;
}

/*
static void desugar_action_wait(HAllocator *mm__, HCFStack *stk__, void *env) {
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

static bool action_wait_isValidRegular(void *env) {
    HParseAction *a = (HParseAction *)env;
    return a->p->vtable->isValidRegular(a->p->env);
}

static bool action_wait_isValidCF(void *env) {
    HParseAction *a = (HParseAction *)env;
    return a->p->vtable->isValidCF(a->p->env);
}

static bool h_svm_action_action_wait(HArena *arena, HSVMContext *ctx, void *arg) {
    HParseResult res;
    HParseAction *a = arg;
    assert(ctx->stack_count >= 1);
    if (ctx->stack[ctx->stack_count - 1]->token_type != TT_MARK) {
        res.ast = ctx->stack[ctx->stack_count - 1];
    } else {
        res.ast = NULL;
    }
    res.arena = arena;
    HParsedToken *action_result = a->action(&res, a->user_data);
    if (action_result)
        ctx->stack[ctx->stack_count - 1] = action_result;
    else
        ctx->stack_count--;
    return true;
}

static bool action_wait_ctrvm(HRVMProg *prog, void *env) {
    HParseAction *a = (HParseAction *)env;
    h_rvm_insert_insn(prog, RVM_PUSH, 0);
    if (!h_compile_regex(prog, a->p))
        return false;
    h_rvm_insert_insn(prog, RVM_ACTION, h_rvm_create_action(prog, h_svm_action_action_wait, a));
    return true;
}
*/
static const HParserVtable action_wait_vt = {
    .parse = parse_action_wait,
    .isValidRegular = h_false, // action_wait_isValidRegular,
    .isValidCF = h_false, // action_wait_isValidCF,
    //.compile_to_rvm = action_wait_ctrvm,
    //.desugar = desugar_action_wait,
    .higher = true,
};

HParser *h_action_wait(const HParser *p, const HAction a, void *user_data, HActionCollection *ac) {
    return h_action_wait__m(&system_allocator, p, a, user_data, ac);
}

HParser *h_action_wait__m(HAllocator *mm__, const HParser *p, const HAction a, void *user_data, HActionCollection *ac) {
    HParseActionWait *env = h_new(HParseActionWait, 1);
    env->p = p;
    env->action = a;
    env->user_data = user_data;
    env->collection = ac;
    return h_new_parser(mm__, &action_wait_vt, env);
}
// On Success

void h_action_on_success(HActionCollection *collection) {
    if (!collection)
        return;
    HParsedToken *transformed =
        collection->action(
            collection->res,
            collection->user_data
        );
    collection->res->ast = transformed;
    collection->pending = false;
}
/*
void h_action_on_success__m(HAllocator *mm__, HActionCollection ac, size_t size) {
    
    
}*/