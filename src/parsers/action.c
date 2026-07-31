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
    HArena *arena,
    const HParseResult *result,
    HParsedToken *placeholder,
    HAction action,
    void *user_data
) {
    if (!collection || !arena || !result ||
        !placeholder || !action) {
        return false;
    }

    /*
     * Initialize the collection for this parse.
     */
    if (!collection->head) {
        collection->tail = NULL;
        collection->count = 0;
        collection->arena = arena;
    } else if (collection->arena != arena) {
        /*
         * The collection still contains entries belonging to another parse.
         */
        return false;
    }

    HActionEntry *entry =
        h_arena_malloc(
            arena,
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
                    state->arena,
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
 * Context-free backends invoke this semantic action after the wrapped
 * nonterminal has reduced. Stash the user's action instead of invoking it.
 */
static HParsedToken *action_stash_cf(const HParseResult *result, void *user_data) {
    HParseActionStash *a = (HParseActionStash *)user_data;
    if (!a || !a->action || !a->collection || !result || !result->arena)
        return NULL;

    HParsedToken *placeholder =
        h_arena_malloc_noinit(result->arena, sizeof(*placeholder));

    if (result->ast) {
        *placeholder = *result->ast;
    } else {
        *placeholder = (HParsedToken){
            .token_type = TT_NONE,
            .index = 0,
            .bit_offset = 0,
            .bit_length = result->bit_length,
        };
    }

    if (!append_action(
            a->collection,
            result->arena,
            result,
            placeholder,
            a->action,
            a->user_data)) {
        return NULL;
    }

    return placeholder;
}

static void desugar_action_stash(HAllocator *mm__, HCFStack *stk__, void *env) {
    HParseActionStash *a = (HParseActionStash *)env;

    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() { HCFS_DESUGAR(a->p); }
        HCFS_END_SEQ();
        HCFS_THIS_CHOICE->user_data = a;
        HCFS_THIS_CHOICE->action = action_stash_cf;
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
    HParsedToken *child =
        child_count ? ctx->stack[mark_index + 1] : NULL;
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

    if (!append_action(
            a->collection,
            arena,
            &res,
            placeholder,
            a->action,
            a->user_data)) {
        return false;
    }

    return true;
}

static bool action_stash_ctrvm(HRVMProg *prog, void *env) {
    HParseActionStash *a = (HParseActionStash *)env;
    h_rvm_insert_insn(prog, RVM_PUSH, 0);
    if (!h_compile_regex(prog, a->p))
        return false;
    h_rvm_insert_insn(prog, RVM_ACTION, h_rvm_create_action(prog, h_svm_action_action_stash, a));
    return true;
}

static const HParserVtable action_stash_vt = {
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

    if(!collection->head)
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

    if (!a || !a->p)
        return NULL;

    /*
     * Parse the wrapped parser first. h_action_stash parsers encountered
     * inside it populate the collection.
     */
    HParseResult *res = h_do_parse(a->p, state);
    if (!res) {
        if(a->collection){
            h_action_collection_reset(a->collection);
            return NULL;
        }
    }

    /*
     * Only apply the stashed actions after the complete wrapped parser has
     * succeeded.
     */
    if(a->collection){
        if (!apply_actions(a->collection)) {
            h_action_collection_reset(a->collection);
        }
    }

    return res;
}

/*
 * The outer h_action_apply reduction occurs only after its wrapped grammar
 * has reduced successfully, so all stash reductions are available here.
 */
static HParsedToken *action_apply_cf(const HParseResult *result, void *user_data) {
    HParseActionApply *a = (HParseActionApply *)user_data;
    if (!a || !result)
        return NULL;

    if(a->collection){
        if (!apply_actions(a->collection)) {
            h_action_collection_reset(a->collection);
        }
    }

    return (HParsedToken *)result->ast;
}

static void desugar_action_apply(HAllocator *mm__, HCFStack *stk__, void *env) {
    HParseActionApply *a = (HParseActionApply *)env;

    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() { HCFS_DESUGAR(a->p); }
        HCFS_END_SEQ();
        HCFS_THIS_CHOICE->user_data = a;
        HCFS_THIS_CHOICE->action = action_apply_cf;
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

static bool h_svm_action_action_apply(HArena *arena, HSVMContext *ctx, void *arg) {
    (void)arena;
    (void)ctx;

    HParseActionApply *a = (HParseActionApply *)arg;
    if (!a)
        return false;

    if(a->collection){
        if (!apply_actions(a->collection)) {
            h_action_collection_reset(a->collection);
        }
    }

    return true;
}

static bool action_apply_ctrvm(HRVMProg *prog, void *env) {
    HParseActionApply *a = (HParseActionApply *)env;
    if (!h_compile_regex(prog, a->p))
        return false;
    h_rvm_insert_insn(prog, RVM_ACTION, h_rvm_create_action(prog, h_svm_action_action_apply, a));
    return true;
}

static const HParserVtable action_apply_vt = {
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

HParser *h_action_apply__m(HAllocator *mm__, HParser *p,  HActionCollection *collection) {
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
