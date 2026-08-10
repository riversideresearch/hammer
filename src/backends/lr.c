#include "lr.h"

#include "../parsers/parser_internal.h"
#include "../trace.h"

#include <assert.h>
#include <ctype.h>

/* Comparison and hashing functions */

// compare symbols - terminals by value, others by pointer
bool h_eq_symbol(const void *p, const void *q) {
    const HCFChoice *x = p, *y = q;
    return (x == y || (x->type == HCF_END && y->type == HCF_END) ||
            (x->type == HCF_CHAR && y->type == HCF_CHAR && x->data.chr == y->data.chr));
}

// hash symbols - terminals by value, others by pointer
HHashValue h_hash_symbol(const void *p) {
    const HCFChoice *x = p;
    if (x->type == HCF_END)
        return 0;
    else if (x->type == HCF_CHAR)
        return x->data.chr * 33;
    else
        return h_hash_ptr(p);
}

// compare LR items by value
static bool eq_lr_item(const void *p, const void *q) {
    const HLRItem *a = p, *b = q;

    if (!h_eq_symbol(a->lhs, b->lhs))
        return false;
    if (a->mark != b->mark)
        return false;
    if (a->len != b->len)
        return false;

    for (size_t i = 0; i < a->len; i++)
        if (!h_eq_symbol(a->rhs[i], b->rhs[i]))
            return false;

    return true;
}

// hash LALR items
static inline HHashValue hash_lr_item(const void *p) {
    const HLRItem *x = p;
    HHashValue hash = 0;

    hash += h_hash_symbol(x->lhs);
    for (HCFChoice **p = x->rhs; *p; p++)
        hash += h_hash_symbol(*p);
    hash += x->mark;

    return hash;
}

// compare item sets (DFA states)
bool h_eq_lr_itemset(const void *p, const void *q) { return h_hashset_equal(p, q); }

// hash LR item sets (DFA states) - hash the elements and sum
HHashValue h_hash_lr_itemset(const void *p) {
    HHashValue hash = 0;

    H_FOREACH_KEY((const HHashSet *)p, HLRItem * item)
    hash += hash_lr_item(item);
    H_END_FOREACH

    return hash;
}

bool h_eq_transition(const void *p, const void *q) {
    const HLRTransition *a = p, *b = q;
    return (a->from == b->from && a->to == b->to && h_eq_symbol(a->symbol, b->symbol));
}

HHashValue h_hash_transition(const void *p) {
    const HLRTransition *t = p;
    return (h_hash_symbol(t->symbol) + t->from + t->to); // XXX ?
}

/* Constructors */

HLRItem *h_lritem_new(HArena *a, HCFChoice *lhs, HCFChoice **rhs, size_t mark) {
    HLRItem *ret = h_arena_malloc(a, sizeof(HLRItem));

    size_t len = 0;
    for (HCFChoice **p = rhs; *p; p++)
        len++;
    if (mark > len)
        return NULL;
    assert(mark <= len);

    ret->lhs = lhs;
    ret->rhs = rhs;
    ret->len = len;
    ret->mark = mark;

    return ret;
}

HLRState *h_lrstate_new(HArena *arena) { return h_hashset_new(arena, eq_lr_item, hash_lr_item); }

HLRTable *h_lrtable_new(HAllocator *mm__, size_t nrows) {
    HArena *arena = h_new_arena(mm__, 0); // default blocksize
    if (arena == NULL)
        return NULL;
    assert(arena != NULL);

    HLRTable *ret = h_new(HLRTable, 1);
    if (ret == NULL) {
        h_delete_arena(arena);
        return NULL;
    }
    ret->nrows = nrows;
    ret->ntmap = h_arena_malloc(arena, nrows * sizeof(HHashTable *));
    ret->tmap = h_arena_malloc(arena, nrows * sizeof(HStringMap *));
    ret->forall = h_arena_malloc(arena, nrows * sizeof(HLRAction *));
    ret->expected_parsers = h_arena_malloc(arena, nrows * sizeof(*ret->expected_parsers));
    ret->inadeq = h_slist_new(arena);
    ret->arena = arena;
    ret->mm__ = mm__;

    for (size_t i = 0; i < nrows; i++) {
        ret->ntmap[i] = h_hashtable_new(arena, h_eq_symbol, h_hash_symbol);
        ret->tmap[i] = h_stringmap_new(arena);
        ret->forall[i] = NULL;
        ret->expected_parsers[i] = NULL;
    }

    return ret;
}

void h_lrtable_free(HLRTable *table) {
    HAllocator *mm__ = table->mm__;
    h_delete_arena(table->arena);
    h_free(table);
}

HLRAction *h_shift_action(HArena *arena, size_t nextstate) {
    HLRAction *action = h_arena_malloc(arena, sizeof(HLRAction));
    action->type = HLR_SHIFT;
    action->data.nextstate = nextstate;
    return action;
}

HLRAction *h_reduce_action(HArena *arena, const HLRItem *item) {
    HLRAction *action = h_arena_malloc(arena, sizeof(HLRAction));
    action->type = HLR_REDUCE;
    action->data.production.lhs = item->lhs;
    action->data.production.length = item->len;
#ifndef NDEBUG
    action->data.production.rhs = item->rhs;
#endif
    return action;
}

// adds 'new' to the branches of 'action'
// returns 'action' if it is already of type HLR_CONFLICT
// allocates a new HLRAction otherwise
HLRAction *h_lr_conflict(HArena *arena, HLRAction *action, HLRAction *new) {
    if (action->type != HLR_CONFLICT) {
        HLRAction *old = action;
        action = h_arena_malloc(arena, sizeof(HLRAction));
        action->type = HLR_CONFLICT;
        action->data.branches = h_slist_new(arena);
        h_slist_push(action->data.branches, old);
        h_slist_push(action->data.branches, new);
    } else {
        // check if 'new' is already among branches
        HSlistNode *x;
        for (x = action->data.branches->head; x; x = x->next) {
            if (x->elem == new)
                break;
        }
        // add 'new' if it is not already in list
        if (x == NULL)
            h_slist_push(action->data.branches, new);
    }

    return action;
}

bool h_lrtable_row_empty(const HLRTable *table, size_t i) {
    return (h_hashtable_empty(table->ntmap[i]) && h_stringmap_empty(table->tmap[i]));
}

/* LR driver */

static HLREngine *h_lrengine_new_(HArena *arena, HArena *tarena, const HLRTable *table) {
    HLREngine *engine = h_arena_malloc(tarena, sizeof(HLREngine));

    engine->table = table;
    engine->state = 0;
    engine->stack = h_slist_new(tarena);
    engine->merged[0] = NULL;
    engine->merged[1] = NULL;
    engine->arena = arena;
    engine->tarena = tarena;
    engine->trace_failures = false;
    engine->root_parser = NULL;
    engine->trace_id = 0;

    return engine;
}

HLREngine *h_lrengine_new(HArena *arena, HArena *tarena, const HLRTable *table,
                          const HInputStream *stream) {
    HLREngine *engine = h_lrengine_new_(arena, tarena, table);
    engine->input = *stream;
    return engine;
}

static const HLRAction *terminal_lookup(const HLREngine *engine, const HInputStream *stream) {
    const HLRTable *table = engine->table;
    size_t state = engine->state;

    assert(state < table->nrows);
    if (state >= table->nrows)
        return NULL;

    if (table->forall[state]) {
        assert(h_lrtable_row_empty(table, state)); // that would be a conflict
        if (!h_lrtable_row_empty(table, state))
            return NULL;
        return table->forall[state];
    } else {
        return h_stringmap_get_lookahead(table->tmap[state], *stream);
    }
}

static void lr_expected_from_map(const HStringMap *map, bool expected[256], bool *expected_eof) {
    memset(expected, 0, 256 * sizeof(*expected));
    *expected_eof = false;
    if (!map)
        return;

    *expected_eof = map->end_branch != NULL;
    const HHashTable *branches = map->char_branches;
    for (size_t i = 0; i < branches->capacity; i++) {
        for (HHashTableEntry *entry = &branches->contents[i]; entry; entry = entry->next) {
            if (entry->key)
                expected[key_char((HCharKey)entry->key)] = true;
        }
    }
}

void h_lrengine_trace_action_failure(const HLREngine *engine) {
    size_t state = engine->state;
    if (!engine->trace_failures || state >= engine->table->nrows)
        return;

    const HStringMap *map = engine->table->tmap[state];
    HInputStream input = engine->input;
    const HParser *origin = engine->table->expected_parsers[state];
    if (!origin)
        origin = engine->root_parser;
    CF_TRACE_LR_ERROR(engine->trace_id, state, input.pos + input.index, origin);

    if (!map) {
        size_t index = input.pos + input.index;
        CF_TRACE_FAILURE(index, index, H_PARSE_ERROR_HIGHER_ORDER, origin, NULL, false);
        return;
    }

    while (map && !map->epsilon_branch) {
        size_t index = input.pos + input.index;
        uint8_t actual = h_read_bits(&input, 8, false);
        if (input.overrun) {
            bool expected[256], expected_eof;
            lr_expected_from_map(map, expected, &expected_eof);
            CF_TRACE_FAILURE(index, index, H_PARSE_ERROR_UNEXPECTED_EOF, origin, expected,
                             expected_eof);
            return;
        }

        const HStringMap *next = h_stringmap_get_char(map, actual);
        if (!next) {
            bool expected[256], expected_eof;
            lr_expected_from_map(map, expected, &expected_eof);
            CF_TRACE_FAILURE(index, index + 1, H_PARSE_ERROR_PRIMITIVE_MISMATCH, origin, expected,
                             expected_eof);
            return;
        }
        map = next;
    }
}

static const HLRAction *nonterminal_lookup(const HLREngine *engine, const HCFChoice *symbol) {
    const HLRTable *table = engine->table;
    size_t state = engine->state;

    assert(state < table->nrows);
    if (state >= table->nrows)
        return NULL;

    assert(!table->forall[state]); // contains only reduce entries
                                   // we are only looking for shifts
    if (table->forall[state])
        return NULL;

    return h_hashtable_get(table->ntmap[state], symbol);
}

const HLRAction *h_lrengine_action(const HLREngine *engine) {
    return terminal_lookup(engine, &engine->input);
}

static HParsedToken *consume_input(HLREngine *engine) {
    HParsedToken *v;

    uint8_t c = h_read_bits(&engine->input, 8, false);

    if (engine->input.overrun) { // end of input
        v = NULL;
    } else {
        v = h_arena_malloc(engine->arena, sizeof(HParsedToken));
        v->token_type = TT_UINT;
        v->token_data.uint = c;
        v->index = engine->input.pos + engine->input.index - 1;
        v->bit_offset = engine->input.bit_offset;
    }

    return v;
}

// run LR parser for one round; returns false when finished
bool h_lrengine_step(HLREngine *engine, const HLRAction *action) {
    // short-hand names
    HSlist *stack = engine->stack;
    HArena *arena = engine->arena;
    HArena *tarena = engine->tarena;
    size_t action_state = engine->state;

    if (action == NULL)
        return false; // no handle recognizable in input, terminate

    assert(action->type == HLR_SHIFT || action->type == HLR_REDUCE);
    if (action->type != HLR_SHIFT && action->type != HLR_REDUCE)
        return false;

    if (action->type == HLR_REDUCE) {
        size_t len = action->data.production.length;
        HCFChoice *symbol = action->data.production.lhs;
        if (!symbol)
            return false;

        // semantic value of the reduction result
        HParsedToken *value = h_arena_malloc(arena, sizeof(HParsedToken));
        value->token_type = TT_SEQUENCE;
        value->token_data.seq = h_carray_new_sized(arena, len);
        HActionPlan *plan = NULL;

        // pull values off the stack, rewinding state accordingly
        HParsedToken *v = NULL;
        for (size_t i = 0; i < len; i++) {
            if (h_slist_empty(stack))
                return false;
            HLRSemanticValue *child = h_slist_drop(stack);
            if (!child)
                return false;
            v = child->ast;
            if (h_slist_empty(stack))
                return false;
            engine->state = (uintptr_t)h_slist_drop(stack);

            // collect values in result sequence
            value->token_data.seq->elements[len - 1 - i] = v;
            value->token_data.seq->used++;

            // Values are popped right-to-left. Prepend this child's plan so
            // deferred actions retain their left-to-right parse order.
            plan = h_action_plan_concat(arena, child->plan, plan);
        }
        if (v) {
            // result position equals position of left-most symbol
            value->index = v->index;
            value->bit_offset = v->bit_offset;
        } else {
            // result position is current input position  XXX ?
            value->index = engine->input.pos + engine->input.index;
            value->bit_offset = engine->input.bit_offset;
        }
        size_t reduction_start = value->index;

        // perform token reshape if indicated
        if (symbol->reshape) {
            v = symbol->reshape(make_result(arena, value), symbol->user_data);
            if (v) {
                v->index = value->index;
                v->bit_offset = value->bit_offset;
                value = v;
            } else
                value = NULL;
        }

        // call validation and semantic action, if present
        if (symbol->pred && !symbol->pred(make_result(tarena, value), symbol->user_data)) {
            if (engine->trace_failures) {
                CF_TRACE_FAILURE(reduction_start, engine->input.pos + engine->input.index,
                                 H_PARSE_ERROR_SEMANTIC_PREDICATE,
                                 symbol->parser ? symbol->parser : engine->root_parser, NULL,
                                 false);
                CF_TRACE_LR_REDUCE(engine->trace_id, action_state, engine->state, len,
                                   reduction_start, engine->input.pos + engine->input.index,
                                   symbol->parser ? symbol->parser : engine->root_parser, value,
                                   false);
            }
            return false; // validation failed -> no parse; terminate
        }
        if (symbol->plan_action)
            value = symbol->plan_action(make_result(arena, value), symbol->user_data, &plan);
        else if (symbol->action)
            value = symbol->action(make_result(arena, value), symbol->user_data);

        // this is LR, building a right-most derivation bottom-up, so no reduce can
        // follow a reduce. we can also assume no conflict follows for GLR if we
        // use LALR tables, because only terminal symbols (lookahead) get reduces.
        const HLRAction *shift = nonterminal_lookup(engine, symbol);
        if (shift == NULL)
            return false; // parse error
        assert(shift->type == HLR_SHIFT);
        if (shift->type != HLR_SHIFT)
            return false;

        // piggy-back the shift right here, never touching the input
        HLRSemanticValue *semantic = h_arena_malloc(tarena, sizeof(*semantic));
        semantic->ast = value;
        semantic->plan = plan;
        h_slist_push(stack, (void *)(uintptr_t)engine->state);
        h_slist_push(stack, semantic);
        engine->state = shift->data.nextstate;
        if (engine->trace_failures)
            CF_TRACE_LR_REDUCE(engine->trace_id, action_state, engine->state, len,
                               reduction_start, engine->input.pos + engine->input.index,
                               symbol->parser ? symbol->parser : engine->root_parser, value, true);

        // check for success
        if (engine->state == HLR_SUCCESS) {
            assert(symbol == engine->table->start);
            if (symbol != engine->table->start)
                return false;
            return false;
        }
    } else {
        assert(action->type == HLR_SHIFT);
        size_t input_start = engine->input.pos + engine->input.index;
        HParsedToken *value = consume_input(engine);
        HLRSemanticValue *semantic = h_arena_malloc(tarena, sizeof(*semantic));
        semantic->ast = value;
        semantic->plan = NULL;
        h_slist_push(stack, (void *)(uintptr_t)engine->state);
        h_slist_push(stack, semantic);
        engine->state = action->data.nextstate;
        if (engine->trace_failures) {
            const HParser *origin = action_state < engine->table->nrows
                                        ? engine->table->expected_parsers[action_state]
                                        : NULL;
            CF_TRACE_LR_SHIFT(engine->trace_id, action_state, engine->state, input_start,
                              origin ? origin : engine->root_parser, value);
        }
    }

    return true;
}

HParseResult *h_lrengine_result(HLREngine *engine) {
    // parsing was successful iff the engine reaches the end state
    if (engine->state == HLR_SUCCESS) {
        // on top of the stack is the start symbol's semantic value
        assert(!h_slist_empty(engine->stack));
        if (h_slist_empty(engine->stack))
            return NULL;
        HLRSemanticValue *value = engine->stack->head->elem;
        if (!value)
            return NULL;
        HParseResult *res = make_result(engine->arena, value->ast);
        res->bit_length = (engine->input.pos + engine->input.index) * 8;
        return res;
    } else {
        return NULL;
    }
}

bool h_lrengine_execute_plan(HLREngine *engine) {
    if (!engine || engine->state != HLR_SUCCESS || h_slist_empty(engine->stack))
        return false;

    HLRSemanticValue *value = engine->stack->head->elem;
    if (!value)
        return false;
    return h_action_plan_execute(engine->arena, value->plan);
}

HParseResult *h_lr_parse(HAllocator *mm__, const HParser *parser, HInputStream *stream) {
    CF_TRACE_BEGIN(PB_LALR, parser, stream->input, stream->length);
    HLRTable *table = parser->backend_data;
    if (!table) {
        CF_TRACE_END(false);
        return NULL;
    }

    HArena *arena = h_new_arena(mm__, 0);  // will hold the results
    HArena *tarena = h_new_arena(mm__, 0); // tmp, deleted after parse
    HLREngine *engine = h_lrengine_new(arena, tarena, table, stream);
    engine->trace_failures = TRACE_ENABLED();
    engine->root_parser = parser;

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

    // iterate engine to completion
    while (true) {
        const HLRAction *action = h_lrengine_action(engine);
        if (action == NULL || action == NEED_INPUT)
            h_lrengine_trace_action_failure(engine);
        if (action == NEED_INPUT || !h_lrengine_step(engine, action))
            break;
    }

    HParseResult *result = h_lrengine_result(engine);
    if (result && !h_lrengine_execute_plan(engine))
        result = NULL;
    if (!result)
        h_delete_arena(arena);
    h_delete_arena(tarena);
    CF_TRACE_END(result != NULL);
    return result;
}

void h_lr_parse_start(HSuspendedParser *s) {
    HLRTable *table = s->parser->backend_data;
    assert(table != NULL);
    if (!table) {
        s->backend_state = NULL;
        return;
    }

    HArena *arena = h_new_arena(s->mm__, 0);  // will hold the results
    HArena *tarena = h_new_arena(s->mm__, 0); // tmp, deleted after parse
    HLREngine *engine = h_lrengine_new_(arena, tarena, table);

    s->backend_state = engine;
}

// cf. comment before run_trace in regex.c
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wclobbered"
#endif
bool h_lr_parse_chunk(HSuspendedParser *s, HInputStream *stream) {
    HLREngine *engine = s->backend_state;
    if (!engine)
        return true;

    engine->input = *stream;

    bool run = true;

    // out-of-memory handling
    jmp_buf except;
    h_arena_set_except(engine->arena, &except);
    h_arena_set_except(engine->tarena, &except);
    if (setjmp(except)) {
        run = false;                          // done immediately
        assert(engine->state != HLR_SUCCESS); // h_parse_finish will return NULL
    }

    while (run) {
        // check input against table to determine which action to take
        const HLRAction *action = h_lrengine_action(engine);
        if (action == NEED_INPUT) {
            // XXX assume lookahead 1
            assert(engine->input.length - engine->input.index == 0);
            if (engine->input.length - engine->input.index != 0) {
                run = false;
                break;
            }
            break;
        }

        // execute action
        run = h_lrengine_step(engine, action);
        if (engine->input.overrun && !engine->input.last_chunk)
            break;
    }

    h_arena_set_except(engine->arena, NULL);
    h_arena_set_except(engine->tarena, NULL);

    *stream = engine->input;
    return !run; // done if engine no longer running
}
// Reenable -Wclobber
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

HParseResult *h_lr_parse_finish(HSuspendedParser *s) {
    HLREngine *engine = s->backend_state;
    if (!engine)
        return NULL;

    HParseResult *result = h_lrengine_result(engine);
    if (result && !h_lrengine_execute_plan(engine))
        result = NULL;
    if (!result)
        h_delete_arena(engine->arena);
    h_delete_arena(engine->tarena);
    return result;
}

/* Pretty-printers */

void h_pprint_lritem(FILE *f, const HCFGrammar *g, const HLRItem *item) {
    h_pprint_symbol(f, g, item->lhs);
    fputs(" ->", f);

    HCFChoice **x = item->rhs;
    HCFChoice **mark = item->rhs + item->mark;
    if (*x == NULL) {
        fputc('.', f);
    } else {
        while (*x) {
            if (x == mark)
                fputc('.', f);
            else
                fputc(' ', f);

            if ((*x)->type == HCF_CHAR) {
                // condense character strings
                fputc('"', f);
                h_pprint_char(f, (*x)->data.chr);
                for (x++; *x; x++) {
                    if (x == mark)
                        break;
                    if ((*x)->type != HCF_CHAR)
                        break;
                    h_pprint_char(f, (*x)->data.chr);
                }
                fputc('"', f);
            } else {
                h_pprint_symbol(f, g, *x);
                x++;
            }
        }
        if (x == mark)
            fputs(".", f);
    }
}

void h_pprint_lrstate(FILE *f, const HCFGrammar *g, const HLRState *state, unsigned int indent) {
    bool first = true;
    H_FOREACH_KEY(state, HLRItem * item)
    if (!first)
        for (unsigned int i = 0; i < indent; i++)
            fputc(' ', f);
    first = false;
    h_pprint_lritem(f, g, item);
    fputc('\n', f);
    H_END_FOREACH
}

static void pprint_transition(FILE *f, const HCFGrammar *g, const HLRTransition *t) {
    fputs("-", f);
    h_pprint_symbol(f, g, t->symbol);
    fprintf(f, "->%zu", t->to);
}

void h_pprint_lrdfa(FILE *f, const HCFGrammar *g, const HLRDFA *dfa, unsigned int indent) {
    for (size_t i = 0; i < dfa->nstates; i++) {
        unsigned int indent2 = indent + fprintf(f, "%4zu: ", i);
        h_pprint_lrstate(f, g, dfa->states[i], indent2);
        for (HSlistNode *x = dfa->transitions->head; x; x = x->next) {
            const HLRTransition *t = x->elem;
            if (t->from == i) {
                for (unsigned int i = 0; i < indent2 - 2; i++)
                    fputc(' ', f);
                pprint_transition(f, g, t);
                fputc('\n', f);
            }
        }
    }
}

void pprint_lraction(FILE *f, const HCFGrammar *g, const HLRAction *action) {
    switch (action->type) {
    case HLR_SHIFT:
        if (action->data.nextstate == HLR_SUCCESS)
            fputs("s~", f);
        else
            fprintf(f, "s%zu", action->data.nextstate);
        break;
    case HLR_REDUCE:
        fputs("r(", f);
        h_pprint_symbol(f, g, action->data.production.lhs);
        fputs(" -> ", f);
#ifdef NDEBUG
        // if we can't print the production, at least print its length
        fprintf(f, "[%zu]", action->data.production.length);
#else
        HCFSequence seq = {action->data.production.rhs};
        h_pprint_sequence(f, g, &seq);
#endif
        fputc(')', f);
        break;
    case HLR_CONFLICT:
        fputc('!', f);
        for (HSlistNode *x = action->data.branches->head; x; x = x->next) {
            HLRAction *branch = x->elem;
            assert(branch->type != HLR_CONFLICT); // no nesting
            pprint_lraction(f, g, branch);
            if (x->next)
                fputc('/', f); // separator
        }
        break;
    default:
        assert_message(0, "not reached");
    }
}

static void valprint_lraction(FILE *file, void *env, void *val) {
    const HLRAction *action = val;
    const HCFGrammar *grammar = env;
    pprint_lraction(file, grammar, action);
}

static void pprint_lrtable_terminals(FILE *file, const HCFGrammar *g, const HStringMap *map) {
    h_pprint_stringmap(file, ' ', valprint_lraction, (void *)g, map);
}

void h_pprint_lrtable(FILE *f, const HCFGrammar *g, const HLRTable *table, unsigned int indent) {
    for (size_t i = 0; i < table->nrows; i++) {
        for (unsigned int j = 0; j < indent; j++)
            fputc(' ', f);
        fprintf(f, "%4zu:", i);
        if (table->forall[i]) {
            fputc(' ', f);
            pprint_lraction(f, g, table->forall[i]);
            if (!h_lrtable_row_empty(table, i))
                fputs(" !!", f);
        }
        H_FOREACH(table->ntmap[i], HCFChoice * symbol, HLRAction * action)
        fputc(' ', f); // separator
        h_pprint_symbol(f, g, symbol);
        fputc(':', f);
        pprint_lraction(f, g, action);
        H_END_FOREACH
        fputc(' ', f); // separator
        pprint_lrtable_terminals(f, g, table->tmap[i]);
        fputc('\n', f);
    }

#if 0
  fputs("inadeq=", f);
  for(HSlistNode *x=table->inadeq->head; x; x=x->next) {
    fprintf(f, "%zu ", (uintptr_t)x->elem);
  }
  fputc('\n', f);
#endif
}

HCFGrammar *h_pprint_lr_info(FILE *f, HParser *p) {
    HAllocator *mm__ = &system_allocator;

    fprintf(f, "\n==== G R A M M A R ====\n");
    HCFGrammar *g = h_cfgrammar_(mm__, h_desugar_augmented(p));
    if (g == NULL) {
        fprintf(f, "h_cfgrammar failed\n");
        return NULL;
    }
    h_pprint_grammar(f, g, 0);

    fprintf(f, "\n==== D F A ====\n");
    HLRDFA *dfa = h_lr0_dfa(g);
    if (dfa) {
        h_pprint_lrdfa(f, g, dfa, 0);
    } else {
        fprintf(f, "h_lalr_dfa failed\n");
    }

    fprintf(f, "\n==== L R ( 0 )  T A B L E ====\n");
    HLRTable *table0 = h_lr0_table(g, dfa);
    if (table0) {
        h_pprint_lrtable(f, g, table0, 0);
    } else {
        fprintf(f, "h_lr0_table failed\n");
    }
    h_lrtable_free(table0);

    return g;
}
