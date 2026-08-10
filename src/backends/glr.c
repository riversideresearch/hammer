#include "../trace.h"
#include "lr.h"
#include "params.h"

#include <assert.h>

static bool glr_step(HLREngine **winner, HSlist *engines, HLREngine *engine,
                     const HLRAction *action);

/* GLR compilation (LALR w/o failing on conflict) */

int h_glr_compile(HAllocator *mm__, HParser *parser, const void *params) {
    if (!parser->vtable->isValidCF(parser->env)) {
        return -1;
    }
    int result = h_lalr_compile(mm__, parser, params);

    if (result == -2 && parser->backend_data) {
        // table is there, just has conflicts? nevermind, that's okay.
        result = 0;
    }

    return result;
}

void h_glr_free(HParser *parser) { h_lalr_free(parser); }

/* Merging engines (when they converge on the same state) */

static HLREngine *lrengine_merge(HLREngine *old, HLREngine *new) {
    HArena *arena = old->arena;

    HLREngine *ret = h_arena_malloc(arena, sizeof(HLREngine));

    assert(old->state == new->state);
    assert(old->input.input == new->input.input);
    if (old->state != new->state || old->input.input != new->input.input)
        return NULL;

    *ret = *old;
    ret->stack = h_slist_new(arena);
    ret->merged[0] = old;
    ret->merged[1] = new;
    if (old->trace_failures)
        CF_TRACE_GLR_MERGE(old->trace_id, new->trace_id, old->state,
                           old->input.pos + old->input.index);

    return ret;
}

static HSlist *demerge_stack(HSlistNode *bottom, HSlist *stack) {
    HArena *arena = stack->arena;

    HSlist *ret = h_slist_new(arena);

    // copy the stack from the top
    HSlistNode **y = &ret->head;
    for (HSlistNode *x = stack->head; x; x = x->next) {
        HSlistNode *node = h_arena_malloc(arena, sizeof(HSlistNode));
        node->elem = x->elem;
        node->next = NULL;
        *y = node;
        y = &node->next;
    }
    *y = bottom; // attach the ancestor stack

    return ret;
}

static inline HLREngine *respawn(HLREngine *eng, HSlist *stack) {
    // NB: this can be a destructive update because an engine is not used for
    // anything after it is merged.
    eng->stack = demerge_stack(eng->stack->head, stack);
    return eng;
}

static HLREngine *demerge(HLREngine **winner, HSlist *engines, HLREngine *engine,
                          const HLRAction *action, size_t depth) {
    // no-op on engines that are not merged
    if (!engine->merged[0])
        return engine;

    if (!engine->merged[1])
        return NULL;

    HSlistNode *p = engine->stack->head;
    for (size_t i = 0; i < depth; i++) {
        // if stack hits bottom, respawn ancestors
        if (p == NULL) {
            HLREngine *a = respawn(engine->merged[0], engine->stack);
            HLREngine *b = respawn(engine->merged[1], engine->stack);

            // continue demerge until final depth reached
            a = demerge(winner, engines, a, action, depth - i);
            b = demerge(winner, engines, b, action, depth - i);
            if (!a || !b)
                return NULL;

            // step and stow one ancestor...
            glr_step(winner, engines, a, action);

            // ...and return the other
            return b;
        }
        p = p->next;
    }

    return engine; // there is enough stack before the merge point
}

/* Forking engines (on conflicts) */

HLREngine *fork_engine(const HLREngine *engine) {
    HLREngine *eng2 = h_arena_malloc(engine->tarena, sizeof(HLREngine));
    eng2->table = engine->table;
    eng2->state = engine->state;
    eng2->input = engine->input;
    eng2->merged[0] = NULL;
    eng2->merged[1] = NULL;

    // shallow-copy the stack
    // this works because h_slist_push and h_slist_drop never modify
    // the underlying structure of HSlistNodes, only the head pointer.
    // in fact, this gives us prefix sharing for free.
    eng2->stack = h_arena_malloc(engine->tarena, sizeof(HSlist));
    *eng2->stack = *engine->stack;

    eng2->arena = engine->arena;
    eng2->tarena = engine->tarena;
    eng2->trace_failures = engine->trace_failures;
    eng2->root_parser = engine->root_parser;
    eng2->trace_id = engine->trace_failures
                         ? CF_TRACE_GLR_FORK(engine->trace_id, engine->state,
                                             engine->input.pos + engine->input.index)
                         : engine->trace_id;
    return eng2;
}

static const HLRAction *handle_conflict(HLREngine **winner, HSlist *engines,
                                        const HLREngine *engine, const HSlist *branches) {
    if (!branches || !branches->head || !branches->head->next)
        return NULL;

    // there should be at least two conflicting actions
    assert(branches->head);
    assert(branches->head->next); // this is just a consistency check

    // fork a new engine for all but the first action
    for (HSlistNode *x = branches->head->next; x; x = x->next) {
        HLRAction *act = x->elem;
        if (!act)
            continue;
        HLREngine *eng = fork_engine(engine);

        // perform one step and add to engines
        glr_step(winner, engines, eng, act);
    }

    // return first action for use with original engine
    return branches->head->elem;
}

/* GLR driver */

static bool glr_step(HLREngine **winner, HSlist *engines, HLREngine *engine,
                     const HLRAction *action) {
    // handle forks and demerges (~> spawn engines)
    if (action) {
        if (action->type == HLR_CONFLICT) {
            // fork engine on conflicts
            action = handle_conflict(winner, engines, engine, action->data.branches);
        } else if (action->type == HLR_REDUCE) {
            // demerge/respawn as needed
            /* Each grammar symbol occupies a (state, semantic-value) pair. */
            size_t depth = 2 * action->data.production.length;
            engine = demerge(winner, engines, engine, action, depth);
            if (!engine)
                return false;
        }
    }

    bool run = h_lrengine_step(engine, action);

    if (run) {
        // store engine in the list, merge if necessary
        HSlistNode *x;
        for (x = engines->head; x; x = x->next) {
            HLREngine *eng = x->elem;
            if (eng->state == engine->state && eng->input.index == engine->input.index) {
                HLREngine *merged = lrengine_merge(eng, engine);
                if (!merged)
                    return false;
                x->elem = merged;
                break;
            }
        }
        if (!x) // no merge happened
            h_slist_push(engines, engine);
    } else if (engine->state == HLR_SUCCESS) {
        // Save the successful engine. The selected engine's deferred action
        // plan is executed only after GLR has finished this result-selection
        // round, never while another derivation is still speculative.
        *winner = engine;
    }

    return run;
}

HParseResult *h_glr_parse(HAllocator *mm__, const HParser *parser, HInputStream *stream) {
    CF_TRACE_BEGIN(PB_GLR, parser, stream->input, stream->length);
    HLRTable *table = parser->backend_data;
    if (!table) {
        CF_TRACE_END(false);
        return NULL;
    }

    HArena *arena = h_new_arena(mm__, 0);  // will hold the results
    HArena *tarena = h_new_arena(mm__, 0); // tmp, deleted after parse

    // out-of-memory handling
    jmp_buf except;
    h_arena_set_except(arena, &except);
    h_arena_set_except(tarena, &except);
    if (setjmp(except)) {
        h_delete_arena(arena);
        h_delete_arena(tarena);
        CF_TRACE_END(false);
        return NULL;
    }

    // allocate engine lists (will hold one engine per state)
    // these are swapped each iteration
    HSlist *engines = h_slist_new(tarena);
    HSlist *engback = h_slist_new(tarena);

    // create initial engine
    HLREngine *initial = h_lrengine_new(arena, tarena, table, stream);
    initial->trace_failures = TRACE_ENABLED();
    initial->root_parser = parser;
    h_slist_push(engines, initial);

    HLREngine *winner = NULL;
    while (winner == NULL && !h_slist_empty(engines)) {
        assert(h_slist_empty(engback));
        if (!h_slist_empty(engback))
            break;

        // step all engines
        while (!h_slist_empty(engines)) {
            HLREngine *engine = h_slist_pop(engines);
            const HLRAction *action = h_lrengine_action(engine);
            if (action == NULL || action == NEED_INPUT)
                h_lrengine_trace_action_failure(engine);
            if (action != NEED_INPUT)
                glr_step(&winner, engback, engine, action);
            // XXX detect ambiguous results - two engines terminating at the same pos
            // -> kill both engines, i.e. ignore if there is a later unamb. success
        }

        // swap the lists
        HSlist *tmp = engines;
        engines = engback;
        engback = tmp;
    }

    HParseResult *result = NULL;
    if (winner) {
        result = h_lrengine_result(winner);
        if (result && !h_lrengine_execute_plan(winner))
            result = NULL;
    }

    if (!result)
        h_delete_arena(arena);
    h_delete_arena(tarena);
    CF_TRACE_END(result != NULL);
    return result;
}

char *h_glr_get_description(HAllocator *mm__, HParserBackend be, void *param) {
    const char *backend_name = "GLR";
    size_t k;
    char *descr = NULL;

    k = h_get_param_k(param);

    descr = h_format_description_with_param_k(mm__, backend_name, k);

    return descr;
}

char *h_glr_get_short_name(HAllocator *mm__, HParserBackend be, void *param) {
    const char *backend_name = "GLR";

    size_t k;
    char *name = NULL;

    k = h_get_param_k(param);

    name = h_format_name_with_param_k(mm__, backend_name, k);

    return name;
}

int h_glr_extract_params(HParserBackendWithParams *be_with_params,
                         backend_with_params_t *be_with_params_t) {

    return h_extract_param_k(be_with_params, be_with_params_t);
}

HParserBackendVTable h__glr_backend_vtable = {
    .compile = h_glr_compile,
    .parse = h_glr_parse,
    .free = h_glr_free,

    .copy_params = h_copy_numeric_param,
    /* No free_param needed, since it's not actually allocated */

    /* Name/param resolution functions */
    .backend_short_name = "glr",
    .backend_description = "GLR(k) parser backend",
    .get_description_with_params = h_glr_get_description,
    .get_short_name_with_params = h_glr_get_short_name,

    .extract_params = h_glr_extract_params};
