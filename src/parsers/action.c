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

static bool append_action(
    HActionCollection *collection,
    HParseState *state,
    const HParseResult *result,
    HParsedToken *placeholder,
    HAction action,
    void *user_data
) {
    if (!collection || !state || !result ||
        !placeholder || !action) {
        return false;
    }

    /*
     * Initialize the collection for this parse.
     */
    if (!collection->head) {
        collection->tail = NULL;
        collection->count = 0;
        collection->arena = state->arena;
    } else if (collection->arena != state->arena) {
        /*
         * The collection still contains entries belonging to another parse.
         */
        return false;
    }

    HActionEntry *entry =
        h_arena_malloc(
            state->arena,
            sizeof(*entry)
        );

    if (!entry)
        return false;

    *entry = (HActionEntry){
        .res = *result,
        .placeholder = placeholder,
        .action = action,
        .user_data = user_data,
        .next = NULL,
    };

    if (collection->tail) {
        collection->tail->next = entry;
    } else {
        collection->head = entry;
    }

    collection->tail = entry;
    collection->count++;

    return true;
}
// Collection Reset
void h_action_collection_reset(
    HActionCollection *collection
) {
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
    HActionEntry *entry;
    HActionCollection *collection;
} HParseActionStash;

static HParseResult *parse_action_stash(void *env, HParseState *state) {
    HParseActionStash *a = (HParseActionStash *)env;
    if (a->p && a->action) {
        HParseResult *tmp = h_do_parse(a->p, state);
        if (tmp) {
            HParsedToken *placeholder =
                h_arena_malloc_noinit(state->arena, sizeof(*placeholder));
            if (tmp->ast) {
                *placeholder = *tmp->ast;
            } else {
                *placeholder = (HParsedToken){
                    .token_type = TT_NONE,
                    .index = 0,
                    .bit_length = 0,
                    .bit_offset = 0,
                };
            }
            if (!append_action(
                    a->collection,
                    state,
                    tmp,
                    placeholder,
                    a->action,
                    a->user_data))
                return NULL;
            return make_result(state->arena, placeholder);
        } else
            return NULL;
    } else // either the parser's missing or the action's missing
        return NULL;
}

/*
static void desugar_action_stash(HAllocator *mm__, HCFStack *stk__, void *env) {
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

static bool action_stash_isValidRegular(void *env) {
    HParseAction *a = (HParseAction *)env;
    return a->p->vtable->isValidRegular(a->p->env);
}

static bool action_stash_isValidCF(void *env) {
    HParseAction *a = (HParseAction *)env;
    return a->p->vtable->isValidCF(a->p->env);
}

static bool h_svm_action_action_stash(HArena *arena, HSVMContext *ctx, void *arg) {
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

static bool action_stash_ctrvm(HRVMProg *prog, void *env) {
    HParseAction *a = (HParseAction *)env;
    h_rvm_insert_insn(prog, RVM_PUSH, 0);
    if (!h_compile_regex(prog, a->p))
        return false;
    h_rvm_insert_insn(prog, RVM_ACTION, h_rvm_create_action(prog, h_svm_action_action_stash, a));
    return true;
}
*/
static const HParserVtable action_stash_vt = {
    .parse = parse_action_stash,
    .isValidRegular = h_false, // action_stash_isValidRegular,
    .isValidCF = h_false, // action_stash_isValidCF,
    //.compile_to_rvm = action_stash_ctrvm,
    //.desugar = desugar_action_stash,
    .higher = true,
};

HParser *h_action_stash(const HParser *p, const HAction a, void *user_data, HActionCollection *ac) {
    return h_action_stash__m(&system_allocator, p, a, user_data, ac);
}

HParser *h_action_stash__m(HAllocator *mm__, const HParser *p, const HAction a, void *user_data, HActionCollection *ac) {
    HParseActionStash *env = h_new(HParseActionStash, 1);
    env->p = p;
    env->action = a;
    env->user_data = user_data;
    env->collection = ac;
    return h_new_parser(mm__, &action_stash_vt, env);
}

static bool apply_actions(HActionCollection *collection) {
    if (!collection)
        return false;

    HActionEntry *entry = collection->head;

    while (entry) {
        if (!entry->action || !entry->placeholder)
            return false;

        HParsedToken *transformed =
            entry->action(
                &entry->res,
                entry->user_data
            );

        if (transformed) {
            if (transformed != entry->placeholder)
                *entry->placeholder = *transformed;
        } else {
            entry->placeholder->token_type = TT_NONE;
        }

        entry = entry->next;
    }

    /*
     * The arena owns the entries. Do not free them individually.
     * Merely stop the collection from referencing them.
     */
    collection->head = NULL;
    collection->tail = NULL;
    collection->count = 0;
    collection->arena = NULL;

    return true;
}

// Action Apply
typedef struct {
    const HParser *p;
    HActionCollection *collection;
} HParseActionApply;

static HParseResult *parse_action_apply(void *env, HParseState *state) {
    HParseActionApply *a = (HParseActionApply *)env;

    if (!a || !a->p || !a->collection)
        return NULL;

    /*
     * Parse the wrapped parser first. h_action_stash parsers encountered
     * inside it populate the collection.
     */
    HParseResult *res = h_do_parse(a->p, state);
    if (!res) {
        h_action_collection_reset(a->collection);
        return NULL;
    }

    /*
     * Only apply the stashed actions after the complete wrapped parser has
     * succeeded.
     */
    if (!apply_actions(a->collection)) {
        h_action_collection_reset(a->collection);
        return NULL;
    }

    return res;
}

static const HParserVtable action_apply_vt = {
    .parse = parse_action_apply,
    .isValidRegular = h_false,
    .isValidCF = h_false,
    .higher = true,
};

HParser *h_action_apply(HParser *p, HActionCollection *collection) {
    return h_action_apply__m(&system_allocator, p, collection);
}

HParser *h_action_apply__m(HAllocator *mm__, HParser *p,  HActionCollection *collection) {
    if (!mm__ || !p || !collection)
        return NULL;

    HParseActionApply *env = h_new(HParseActionApply, 1);
    if (!env)
        return NULL;

    env->p = p;
    env->collection = collection;
    return h_new_parser(mm__, &action_apply_vt, env);
}
