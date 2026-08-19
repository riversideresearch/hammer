/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"

typedef enum {
    HAP_STASH,
    HAP_CONCAT,
    HAP_APPLY,
} HActionPlanType;

struct HActionPlan_ {
    HActionPlanType type;
    union {
        struct {
            HActionEntry entry;
            HActionCollection *collection;
        } stash;
        struct {
            HActionPlan *left;
            HActionPlan *right;
        } concat;
        struct {
            HActionCollection *collection;
            HActionPlan *child;
        } apply;
    } data;
};

typedef struct HActionPending_ {
    const HActionEntry *entry;
    struct HActionPending_ *next;
} HActionPending;

typedef struct HActionApplyFrame_ {
    HActionCollection *collection;
    HActionPending *head;
    HActionPending *tail;
    struct HActionApplyFrame_ *parent;
} HActionApplyFrame;

HActionPlan *h_action_plan_concat(HArena *arena, HActionPlan *left, HActionPlan *right) {
    if (!left)
        return right;
    if (!right)
        return left;

    HActionPlan *plan = h_arena_malloc_noinit(arena, sizeof(*plan));
    *plan = (HActionPlan){
        .type = HAP_CONCAT,
        .data.concat = {.left = left, .right = right},
    };
    return plan;
}

HActionPlan *h_action_plan_stash(HArena *arena, const HParseResult *result,
                                 HParsedToken *placeholder, HAction action, void *user_data,
                                 HActionCollection *collection) {
    if (!arena || !result || !placeholder || !action || !collection)
        return NULL;

    HActionPlan *plan = h_arena_malloc_noinit(arena, sizeof(*plan));
    *plan = (HActionPlan){
        .type = HAP_STASH,
        .data.stash =
            {
                .entry =
                    {
                        .res = *result,
                        .placeholder = placeholder,
                        .action = action,
                        .user_data = user_data,
                        .next = NULL,
                    },
                .collection = collection,
            },
    };
    return plan;
}

HActionPlan *h_action_plan_apply(HArena *arena, HActionCollection *collection, HActionPlan *child) {
    if (!child || !collection)
        return child;

    HActionPlan *plan = h_arena_malloc_noinit(arena, sizeof(*plan));
    *plan = (HActionPlan){
        .type = HAP_APPLY,
        .data.apply = {.collection = collection, .child = child},
    };
    return plan;
}

static HActionApplyFrame *find_apply_frame(HActionApplyFrame *frame,
                                           HActionCollection *collection) {
    for (; frame; frame = frame->parent) {
        if (frame->collection == collection)
            return frame;
    }
    return NULL;
}

static bool execute_action_plan(HArena *arena, HActionPlan *plan, HActionApplyFrame *frame) {
    if (!plan)
        return true;

    switch (plan->type) {
    case HAP_STASH: {
        HActionApplyFrame *owner = find_apply_frame(frame, plan->data.stash.collection);
        if (!owner)
            return true;

        HActionPending *pending = h_arena_malloc_noinit(arena, sizeof(*pending));
        pending->entry = &plan->data.stash.entry;
        pending->next = NULL;
        if (owner->tail)
            owner->tail->next = pending;
        else
            owner->head = pending;
        owner->tail = pending;
        return true;
    }
    case HAP_CONCAT:
        return execute_action_plan(arena, plan->data.concat.left, frame) &&
               execute_action_plan(arena, plan->data.concat.right, frame);
    case HAP_APPLY:
        break;
    }

    HActionApplyFrame nested = {
        .collection = plan->data.apply.collection,
        .head = NULL,
        .tail = NULL,
        .parent = frame,
    };
    if (!execute_action_plan(arena, plan->data.apply.child, &nested))
        return false;

    for (HActionPending *pending = nested.head; pending; pending = pending->next) {
        const HActionEntry *entry = pending->entry;
        if (!entry || !entry->action || !entry->placeholder)
            return false;

        HParsedToken *transformed = entry->action(&entry->res, entry->user_data);
        if (transformed)
            *entry->placeholder = *transformed;
        else
            entry->placeholder->token_type = TT_NONE;
    }
    return true;
}

bool h_action_plan_execute(HArena *arena, HActionPlan *plan) {
    return execute_action_plan(arena, plan, NULL);
}

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

static bool h_svm_action_mark_action(HArena *arena, HSVMContext *ctx, void *arg) {
    (void)arena;

    if (ctx->stack_count == 0 || ctx->stack[ctx->stack_count - 1]->token_type != TT_MARK)
        return false;

    ctx->stack[ctx->stack_count - 1]->token_data.user = arg;
    return true;
}

static bool h_svm_action_action(HArena *arena, HSVMContext *ctx, void *arg) {
    HParseResult res;
    HParseAction *a = arg;
    size_t boundary = ctx->stack_count;

    while (boundary > 0) {
        --boundary;
        if (ctx->stack[boundary]->token_type == TT_MARK &&
            ctx->stack[boundary]->token_data.user == a)
            break;
    }

    if (boundary == ctx->stack_count || ctx->stack[boundary]->token_type != TT_MARK ||
        ctx->stack[boundary]->token_data.user != a)
        return false;

    res.ast = boundary + 1 < ctx->stack_count ? ctx->stack[ctx->stack_count - 1] : NULL;
    res.arena = arena;
    HParsedToken *action_result = a->action(&res, a->user_data);
    if (action_result) {
        ctx->stack[boundary] = action_result;
        ctx->stack_count = boundary + 1;
    } else {
        ctx->stack_count = boundary;
    }
    return true;
}

static bool action_ctrvm(HRVMProg *prog, void *env) {
    HParseAction *a = (HParseAction *)env;
    HParseAction *rvm_action = h_rvm_alloc(prog, sizeof(*rvm_action));
    *rvm_action = *a;
    rvm_action->p = NULL;

    h_rvm_insert_insn(prog, RVM_PUSH, 0);
    h_rvm_insert_insn(prog, RVM_ACTION,
                      h_rvm_create_action(prog, h_svm_action_mark_action, rvm_action));
    if (!h_compile_regex(prog, a->p))
        return false;
    h_rvm_insert_insn(prog, RVM_ACTION, h_rvm_create_action(prog, h_svm_action_action, rvm_action));
    return true;
}

static const HParserVtable action_vt = {
    .name = "h_action",
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
// Collection Reset
void h_action_collection_reset(HActionCollection *collection) {
    if (!collection)
        return;

    collection->head = NULL;
    collection->tail = NULL;
    collection->count = 0;
    collection->arena = NULL;
}

// Action stash

typedef struct {
    const HParser *p;
    HAction action;
    void *user_data;
    HActionCollection *collection;
} HParseActionStash;

static HParsedToken *make_action_placeholder(HArena *arena, const HParseResult *result) {
    HParsedToken *placeholder = h_arena_malloc_noinit(arena, sizeof(*placeholder));
    if (result->ast) {
        *placeholder = *result->ast;
    } else {
        *placeholder = (HParsedToken){
            .token_type = TT_NONE,
            .index = 0,
            .bit_length = result->bit_length,
            .bit_offset = 0,
        };
    }
    return placeholder;
}

static HParseResult *parse_action_stash(void *env, HParseState *state) {
    HParseActionStash *a = (HParseActionStash *)env;
    if (a->p && a->action) {
        HParseResult *tmp = h_do_parse(a->p, state);
        if (tmp) {
            HParsedToken *placeholder = make_action_placeholder(state->arena, tmp);
            HActionPlan *stash = h_action_plan_stash(state->arena, tmp, placeholder, a->action,
                                                     a->user_data, a->collection);
            if (!stash)
                return NULL;
            state->action_plan = h_action_plan_concat(state->arena, state->action_plan, stash);
            return make_result(state->arena, placeholder);
        } else
            return NULL;
    } else // either the parser's missing or the action's missing
        return NULL;
}

/*
 * Context-free backends invoke this semantic action after the wrapped
 * nonterminal has reduced. Stash the user's action instead of invoking it.
 */
static HParsedToken *action_stash_cf(const HParseResult *result, void *user_data,
                                     HActionPlan **plan) {
    HParseActionStash *a = (HParseActionStash *)user_data;
    if (!a || !a->action || !a->collection || !result || !result->arena || !plan)
        return NULL;

    HParsedToken *placeholder = make_action_placeholder(result->arena, result);
    HActionPlan *stash = h_action_plan_stash(result->arena, result, placeholder, a->action,
                                             a->user_data, a->collection);
    if (!stash)
        return NULL;
    *plan = h_action_plan_concat(result->arena, *plan, stash);

    return placeholder;
}

static void desugar_action_stash(HAllocator *mm__, HCFStack *stk__, void *env) {
    HParseActionStash *a = (HParseActionStash *)env;

    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() { HCFS_DESUGAR(a->p); }
        HCFS_END_SEQ();
        HCFS_THIS_CHOICE->user_data = a;
        HCFS_THIS_CHOICE->plan_action = action_stash_cf;
        HCFS_THIS_CHOICE->reshape = h_act_first;
    }
    HCFS_END_CHOICE();
}

static bool action_stash_isValidRegular(void *env) {
    HParseActionStash *a = env;
    return a->p->vtable->isValidRegular(a->p->env);
}

static bool action_stash_isValidCF(void *env) {
    HParseActionStash *a = (HParseActionStash *)env;
    return a->p->vtable->isValidCF(a->p->env);
}

size_t svm_count_to_mark(HSVMContext *ctx) {
    size_t ctm;
    for (ctm = 0; ctm < ctx->stack_count; ctm++) {
        if (ctx->stack[ctx->stack_count - 1 - ctm]->token_type == TT_MARK) {
            return ctm;
        }
    }
    return ctx->stack_count;
}

static bool h_svm_action_action_stash(HArena *arena, HSVMContext *ctx, void *arg) {
    HParseActionStash *a = (HParseActionStash *)arg;
    if (!a || !a->action || !a->collection || ctx->stack_count < 1)
        return false;

    /*
     * action_stash_ctrvm() inserted a private mark before compiling the
     * wrapped parser. A parser result consists of either zero or one token
     * above that mark.
     */
    size_t child_count = svm_count_to_mark(ctx);
    if (child_count > 1 || child_count >= ctx->stack_count)
        return false;

    size_t mark_index = ctx->stack_count - child_count - 1;
    HParsedToken *placeholder = ctx->stack[mark_index];
    HParsedToken *child = child_count ? ctx->stack[mark_index + 1] : NULL;
    size_t start = placeholder->index;
    size_t bit_length = (ctx->input_pos - start) * 8;

    HParseResult res = {
        .ast = child,
        .arena = arena,
        .bit_length = bit_length,
    };

    if (child) {
        *placeholder = *child;
    } else {
        *placeholder = (HParsedToken){
            .token_type = TT_NONE,
            .index = start,
            .bit_offset = 0,
            .bit_length = bit_length,
        };
    }

    /* Collapse the private mark and optional child to one placeholder. */
    ctx->stack_count = mark_index + 1;

    HActionPlan *stash =
        h_action_plan_stash(arena, &res, placeholder, a->action, a->user_data, a->collection);
    if (!stash)
        return false;
    ctx->action_plan = h_action_plan_concat(arena, ctx->action_plan, stash);

    return true;
}

static bool action_stash_ctrvm(HRVMProg *prog, void *env) {
    HParseActionStash *a = (HParseActionStash *)env;
    HParseActionStash *rvm_action = h_rvm_alloc(prog, sizeof(*rvm_action));
    *rvm_action = *a;
    rvm_action->p = NULL;

    h_rvm_insert_insn(prog, RVM_PUSH, 0);
    if (!h_compile_regex(prog, a->p))
        return false;
    h_rvm_insert_insn(prog, RVM_ACTION,
                      h_rvm_create_action(prog, h_svm_action_action_stash, rvm_action));
    return true;
}

static const HParserVtable action_stash_vt = {
    .name = "h_action_stash",
    .parse = parse_action_stash,
    .isValidRegular = action_stash_isValidRegular,
    .isValidCF = action_stash_isValidCF,
    .compile_to_rvm = action_stash_ctrvm,
    .desugar = desugar_action_stash,
    .higher = true,
};

HParser *h_action_stash(const HParser *p, const HAction a, void *user_data, HActionCollection *ac) {
    return h_action_stash__m(&system_allocator, p, a, user_data, ac);
}

HParser *h_action_stash__m(HAllocator *mm__, const HParser *p, const HAction a, void *user_data,
                           HActionCollection *ac) {
    HParseActionStash *env = h_new(HParseActionStash, 1);
    env->p = p;
    env->action = a;
    env->user_data = user_data;
    env->collection = ac;
    return h_new_parser(mm__, &action_stash_vt, env);
}

// Action Apply
typedef struct {
    const HParser *p;
    HActionCollection *collection;
} HParseActionApply;

static HParseResult *parse_action_apply(void *env, HParseState *state) {
    HParseActionApply *a = (HParseActionApply *)env;

    if (!a || !a->p)
        return NULL;

    HParseResult *res = h_do_parse(a->p, state);
    if (!res)
        return NULL;

    state->action_plan = h_action_plan_apply(state->arena, a->collection, state->action_plan);
    return res;
}

static HParsedToken *action_apply_cf(const HParseResult *result, void *user_data,
                                     HActionPlan **plan) {
    HParseActionApply *a = (HParseActionApply *)user_data;
    if (!a || !result || !plan)
        return NULL;

    *plan = h_action_plan_apply(result->arena, a->collection, *plan);
    return (HParsedToken *)result->ast;
}

static void desugar_action_apply(HAllocator *mm__, HCFStack *stk__, void *env) {
    HParseActionApply *a = (HParseActionApply *)env;

    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() { HCFS_DESUGAR(a->p); }
        HCFS_END_SEQ();
        HCFS_THIS_CHOICE->user_data = a;
        HCFS_THIS_CHOICE->plan_action = action_apply_cf;
        HCFS_THIS_CHOICE->reshape = h_act_first;
    }
    HCFS_END_CHOICE();
}

static bool action_apply_isValidCF(void *env) {
    HParseActionApply *a = env;
    return a->p->vtable->isValidCF(a->p->env);
}

static bool action_apply_isValidRegular(void *env) {
    HParseActionApply *a = env;
    return a->p->vtable->isValidRegular(a->p->env);
}

typedef struct HSVMActionPlanFrame_ {
    HParseActionApply *owner;
    HActionPlan *parent_plan;
    struct HSVMActionPlanFrame_ *parent;
} HSVMActionPlanFrame;

static bool h_svm_action_begin_apply(HArena *arena, HSVMContext *ctx, void *arg) {
    HParseActionApply *a = (HParseActionApply *)arg;
    if (!a)
        return false;

    HSVMActionPlanFrame *frame = h_arena_malloc_noinit(arena, sizeof(*frame));
    frame->owner = a;
    frame->parent_plan = ctx->action_plan;
    frame->parent = ctx->action_plan_frames;
    ctx->action_plan_frames = frame;
    ctx->action_plan = NULL;
    return true;
}

static bool h_svm_action_end_apply(HArena *arena, HSVMContext *ctx, void *arg) {
    HParseActionApply *a = (HParseActionApply *)arg;
    HSVMActionPlanFrame *frame = ctx->action_plan_frames;

    if (!a || !frame || frame->owner != a)
        return false;

    HActionPlan *scoped = h_action_plan_apply(arena, a->collection, ctx->action_plan);
    ctx->action_plan = h_action_plan_concat(arena, frame->parent_plan, scoped);
    ctx->action_plan_frames = frame->parent;
    return true;
}

static bool action_apply_ctrvm(HRVMProg *prog, void *env) {
    HParseActionApply *a = (HParseActionApply *)env;
    HParseActionApply *rvm_action = h_rvm_alloc(prog, sizeof(*rvm_action));
    *rvm_action = *a;
    rvm_action->p = NULL;

    h_rvm_insert_insn(prog, RVM_ACTION,
                      h_rvm_create_action(prog, h_svm_action_begin_apply, rvm_action));
    if (!h_compile_regex(prog, a->p))
        return false;
    h_rvm_insert_insn(prog, RVM_ACTION,
                      h_rvm_create_action(prog, h_svm_action_end_apply, rvm_action));
    return true;
}

static const HParserVtable action_apply_vt = {
    .name = "h_action_apply",
    .parse = parse_action_apply,
    .isValidRegular = action_apply_isValidRegular,
    .isValidCF = action_apply_isValidCF,
    .desugar = desugar_action_apply,
    .compile_to_rvm = action_apply_ctrvm,
    .higher = true,
};

HParser *h_action_apply(HParser *p, HActionCollection *collection) {
    return h_action_apply__m(&system_allocator, p, collection);
}

HParser *h_action_apply__m(HAllocator *mm__, HParser *p, HActionCollection *collection) {
    if (!mm__ || !p)
        return NULL;

    HParseActionApply *env = h_new(HParseActionApply, 1);
    if (!env)
        return NULL;

    env->p = p;
    if (collection)
        env->collection = collection;
    else
        env->collection = NULL;

    return h_new_parser(mm__, &action_apply_vt, env);
}
