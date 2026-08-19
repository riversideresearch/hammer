#include "lr.h"
#include "params.h"

/* LALR-via-SLR grammar transformation */

static inline size_t seqsize(void *p_) {
    size_t n = 0;
    for (void **p = p_; *p; p++)
        n++;
    return n + 1;
}

static HLRAction *lrtable_lookup(const HLRTable *table, size_t state, const HCFChoice *symbol) {
    switch (symbol->type) {
    case HCF_END:
        return table->tmap[state]->end_branch;
    case HCF_CHAR:
        return h_stringmap_get(table->tmap[state], &symbol->data.chr, 1, false);
    default:
        // nonterminal case
        return h_hashtable_get(table->ntmap[state], symbol);
    }
}

static bool follow_transition(const HLRTable *table, size_t x, HCFChoice *A, size_t *nextstate) {
    HLRAction *action = lrtable_lookup(table, x, A);
    if (action == NULL)
        return false;
    assert(action != NULL);

    // we are interested in a transition out of state x, i.e. a shift action.
    // while there could also be reduce actions associated with A in state x,
    // those are not what we are here for. so if action is a conflict, search it
    // for the shift. there will only be one and it will be the bottom element.
    if (action->type == HLR_CONFLICT) {
        if (!action->data.branches)
            return false;
        HSlistNode *x;
        for (x = action->data.branches->head; x; x = x->next) {
            action = x->elem;
            if (action->type == HLR_CONFLICT)
                return false;
            assert(action->type != HLR_CONFLICT); // no nesting of conflicts
            if (action->type == HLR_SHIFT)
                break;
        }
        if (x == NULL || x->next != NULL)
            return false;
        assert(x != NULL && x->next == NULL); // shift found at the bottom
    }
    if (action->type != HLR_SHIFT)
        return false;
    assert(action->type == HLR_SHIFT);

    *nextstate = action->data.nextstate;
    return true;
}

// no-op on terminal symbols
static bool transform_productions(const HLRTable *table, HLREnhGrammar *eg, size_t x,
                                  HCFChoice *xAy) {
    if (xAy->type != HCF_CHOICE) {
        return true;
    }
    // NB: nothing to do on quasi-terminal CHARSET which carries no list of rhs's

    HArena *arena = eg->arena;

    HCFSequence **seq = h_arena_malloc(arena, seqsize(xAy->data.seq) * sizeof(HCFSequence *));
    HCFSequence **p, **q;
    for (p = xAy->data.seq, q = seq; *p; p++, q++) {
        // trace rhs starting in state x and following the transitions
        // xAy -> ... iBj ...

        size_t i = x;
        HCFChoice **B = (*p)->items;
        HCFChoice **items = h_arena_malloc(arena, seqsize(B) * sizeof(HCFChoice *));
        HCFChoice **iBj = items;
        for (; *B; B++, iBj++) {
            size_t j;
            if (!follow_transition(table, i, *B, &j))
                return false;
            HLRTransition i_B_j = {i, *B, j};
            *iBj = h_hashtable_get(eg->tmap, &i_B_j);
            if (*iBj == NULL)
                return false;
            assert(*iBj != NULL);
            i = j;
        }
        *iBj = NULL;

        *q = h_arena_malloc(arena, sizeof(HCFSequence));
        (*q)->items = items;
    }
    *q = NULL;
    xAy->data.seq = seq;
    return true;
}

static HCFChoice *new_enhanced_symbol(HLREnhGrammar *eg, const HCFChoice *sym) {
    HArena *arena = eg->arena;
    HCFChoice *esym = h_arena_malloc(arena, sizeof(HCFChoice));
    *esym = *sym;

    HHashSet *cs = h_hashtable_get(eg->corr, sym);
    if (!cs) {
        cs = h_hashset_new(arena, h_eq_ptr, h_hash_ptr);
        h_hashtable_put(eg->corr, sym, cs);
    }
    h_hashset_put(cs, esym);

    return esym;
}

static HLREnhGrammar *enhance_grammar(const HCFGrammar *g, const HLRDFA *dfa,
                                      const HLRTable *table) {
    HAllocator *mm__ = g->mm__;
    HArena *arena = g->arena;

    HLREnhGrammar *eg = h_arena_malloc(arena, sizeof(HLREnhGrammar));
    eg->tmap = h_hashtable_new(arena, h_eq_transition, h_hash_transition);
    eg->smap = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    eg->corr = h_hashtable_new(arena, h_eq_symbol, h_hash_symbol);
    // XXX must use h_eq/hash_ptr for symbols! so enhanced CHARs are different
    eg->arena = arena;

    // establish mapping between transitions and symbols
    for (HSlistNode *x = dfa->transitions->head; x; x = x->next) {
        HLRTransition *t = x->elem;

        assert(!h_hashtable_present(eg->tmap, t));

        HCFChoice *sym = new_enhanced_symbol(eg, t->symbol);
        h_hashtable_put(eg->tmap, t, sym);
        h_hashtable_put(eg->smap, sym, t);
    }

    // transform the productions
    H_FOREACH(eg->tmap, HLRTransition * t, HCFChoice * sym)
    if (!transform_productions(table, eg, t->from, sym))
        return NULL;
    H_END_FOREACH

    // add the start symbol
    HCFChoice *start = new_enhanced_symbol(eg, g->start);
    if (!transform_productions(table, eg, 0, start))
        return NULL;

    eg->grammar = h_cfgrammar_(mm__, start);
    return eg;
}

/* LALR table generation */

static inline bool has_conflicts(HLRTable *table) { return !h_slist_empty(table->inadeq); }

// for each lookahead symbol (fs), put action into tmap
// returns 0 on success, -1 on conflict
// ignores forall entries
static int terminals_put(HStringMap *tmap, const HStringMap *fs, HLRAction *action) {
    int ret = 0;

    if (fs->epsilon_branch) {
        HLRAction *prev = tmap->epsilon_branch;
        if (prev && prev != action) {
            // conflict
            tmap->epsilon_branch = h_lr_conflict(tmap->arena, prev, action);
            ret = -1;
        } else {
            tmap->epsilon_branch = action;
        }
    }

    if (fs->end_branch) {
        HLRAction *prev = tmap->end_branch;
        if (prev && prev != action) {
            // conflict
            tmap->end_branch = h_lr_conflict(tmap->arena, prev, action);
            ret = -1;
        } else {
            tmap->end_branch = action;
        }
    }

    H_FOREACH(fs->char_branches, void *key, HStringMap *fs_)
    HStringMap *tmap_ = h_hashtable_get(tmap->char_branches, key);

    if (!tmap_) {
        tmap_ = h_stringmap_new(tmap->arena);
        h_hashtable_put(tmap->char_branches, key, tmap_);
    }

    if (terminals_put(tmap_, fs_, action) < 0) {
        ret = -1;
    }
    H_END_FOREACH

    return ret;
}

// check whether a sequence of enhanced-grammar symbols (p) matches the given
// (original-grammar) production rhs and terminates in the given end state.
static bool match_production(HLREnhGrammar *eg, HCFChoice **p, HCFChoice **rhs, size_t endstate) {
    size_t state = endstate; // initialized to end in case of empty rhs
    for (; *p && *rhs; p++, rhs++) {
        HLRTransition *t = h_hashtable_get(eg->smap, *p);
        if (t == NULL)
            return false;
        assert(t != NULL);
        if (!h_eq_symbol(t->symbol, *rhs)) {
            return false;
        }
        state = t->to;
    }
    return (*p == *rhs // both NULL
            && state == endstate);
}

// variant of match_production where the production lhs is a charset
// [..x..] -> x
static bool match_charset_production(const HLRTable *table, HLREnhGrammar *eg, const HCFChoice *lhs,
                                     HCFChoice *rhs, size_t endstate) {
    if (lhs->type != HCF_CHARSET || rhs->type != HCF_CHAR)
        return false;
    assert(lhs->type == HCF_CHARSET);
    assert(rhs->type == HCF_CHAR);

    if (!charset_isset(lhs->data.charset, rhs->data.chr))
        return false;

    // determine the enhanced-grammar right-hand side and check end state
    HLRTransition *t = h_hashtable_get(eg->smap, lhs);
    if (t == NULL)
        return false;
    assert(t != NULL);
    size_t nextstate;
    return (follow_transition(table, t->from, rhs, &nextstate) && nextstate == endstate);
}

// check wether any production for sym (enhanced-grammar) matches the given
// (original-grammar) rhs and terminates in the given end state.
static bool match_any_production(const HLRTable *table, HLREnhGrammar *eg, const HCFChoice *sym,
                                 HCFChoice **rhs, size_t endstate) {
    if (sym->type != HCF_CHOICE && sym->type != HCF_CHARSET)
        return false;
    assert(sym->type == HCF_CHOICE || sym->type == HCF_CHARSET);

    if (sym->type == HCF_CHOICE) {
        for (HCFSequence **p = sym->data.seq; *p; p++) {
            if (match_production(eg, (*p)->items, rhs, endstate))
                return true;
        }
    } else { // HCF_CHARSET
        if (rhs[0] == NULL || rhs[1] != NULL)
            return false;
        assert(rhs[0] != NULL);
        assert(rhs[1] == NULL);
        return match_charset_production(table, eg, sym, rhs[0], endstate);
    }

    return false;
}

// desugar parser with a fresh start symbol
// this guarantees that the start symbol will not occur in any productions

HCFChoice *h_desugar_augmented(HParser *parser) {
    if (parser->augmented)
        return parser->augmented;
    HAllocator *mm__ = h_desugar_context_allocator(parser);
    if (!mm__)
        return NULL;
    HCFChoice *augmented = h_alloc(mm__, sizeof(*augmented));

    HCFStack *stk__ = h_cfstack_new(mm__);
    stk__->prealloc = augmented;
    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() { HCFS_DESUGAR(parser); }
        HCFS_END_SEQ();
        HCFS_THIS_CHOICE->reshape = h_act_first;
    }
    HCFS_END_CHOICE();
    h_cfstack_free(mm__, stk__);
    augmented->parser = parser;
    if (parser->diagnostic_label || parser->diagnostic_message || parser->diagnostic_source) {
        HDiagnosticContext *context = h_new(HDiagnosticContext, 1);
        if (!context)
            return NULL;
        context->parser = parser;
        context->choice = NULL;
        context->choice_alternative = 0;
        context->choice_id = 0;
        context->next = NULL;
        augmented->diagnostic_context = context;
    }
    parser->augmented = augmented;
    return augmented;
}

int h_lalr_compile(HAllocator *mm__, HParser *parser, const void *params) {
    size_t k = params ? (uintptr_t)params : DEFAULT_KMAX;
    // generate (augmented) CFG from parser
    // construct LR(0) DFA
    // build LR(0) table
    // if necessary, resolve conflicts "by conversion to SLR"

    if (!parser->vtable->isValidCF(parser->env)) {
        return -1;
    }
    HCFGrammar *g = h_cfgrammar_(mm__, h_desugar_augmented(parser));
    if (g == NULL) // backend not suitable (language not context-free)
        return 2;

    HLRDFA *dfa = h_lr0_dfa(g);
    if (dfa == NULL) { // this should normally not happen
        h_cfgrammar_free(g);
        return 3;
    }

    HLRTable *table = h_lr0_table(g, dfa);
    if (table == NULL) { // this should normally not happen
        h_cfgrammar_free(g);
        return 4;
    }

    if (has_conflicts(table)) {
        HArena *arena = table->arena;

        HLREnhGrammar *eg = enhance_grammar(g, dfa, table);
        if (eg == NULL) { // this should normally not happen
            h_cfgrammar_free(g);
            h_lrtable_free(table);
            return 5;
        }

        // go through the inadequate states; replace inadeq with a new list
        HSlist *inadeq = table->inadeq;
        table->inadeq = h_slist_new(arena);

        for (HSlistNode *x = inadeq->head; x; x = x->next) {
            size_t state = (uintptr_t)x->elem;
            bool inadeq = false;

            // clear old forall entry, it's being replaced by more fine-grained ones
            table->forall[state] = NULL;

            // go through each reducible item of state
            H_FOREACH_KEY(dfa->states[state], HLRItem * item)
            if (item->mark < item->len)
                continue;

            // action to place in the table cells indicated by lookahead
            HLRAction *action = h_reduce_action(arena, item);

            // find all LR(0)-enhanced productions matching item
            HHashSet *lhss = h_hashtable_get(eg->corr, item->lhs);
            if (lhss == NULL)
                continue;
            assert(lhss != NULL);
            H_FOREACH_KEY(lhss, HCFChoice * lhs)
            if (match_any_production(table, eg, lhs, item->rhs, state)) {
                // the left-hand symbol's follow set is this production's
                // contribution to the lookahead
                const HStringMap *fs = h_follow(k, eg->grammar, lhs);
                if (fs == NULL || fs->epsilon_branch != NULL || h_stringmap_empty(fs))
                    continue;
                assert(fs != NULL);
                assert(fs->epsilon_branch == NULL);
                // NB: there is a case where fs can be empty: when reducing by lhs
                // would lead to certain parse failure, by means of h_nothing_p()
                // for instance. in that case, the below code correctly adds no
                // reduce action.
                assert(!h_stringmap_empty(fs)); // XXX

                // for each lookahead symbol, put action into table cell
                if (terminals_put(table->tmap[state], fs, action) < 0)
                    inadeq = true;
            }
            H_END_FOREACH     // enhanced production
                H_END_FOREACH // reducible item

                if (inadeq) {
                h_slist_push(table->inadeq, (void *)(uintptr_t)state);
            }
        }

        h_cfgrammar_free(eg->grammar);
    }

    h_cfgrammar_free(g);
    parser->backend_data = table;
    return has_conflicts(table) ? -2 : 0;
}

void h_lalr_free(HParser *parser) {
    HLRTable *table = parser->backend_data;
    h_lrtable_free(table);
    parser->backend_data = NULL;
    parser->backend_vtable = h_get_default_backend_vtable();
    parser->backend = h_get_default_backend();
}

char *h_lalr_get_description(HAllocator *mm__, HParserBackend be, void *param) {
    const char *backend_name = "LALR";
    size_t k;
    char *descr = NULL;

    k = h_get_param_k(param);

    descr = h_format_description_with_param_k(mm__, backend_name, k);

    return descr;
}

char *h_lalr_get_short_name(HAllocator *mm__, HParserBackend be, void *param) {
    const char *backend_name = "LALR";

    size_t k;
    char *name = NULL;

    k = h_get_param_k(param);

    name = h_format_name_with_param_k(mm__, backend_name, k);

    return name;
}

int h_lalr_extract_params(HParserBackendWithParams *be_with_params,
                          backend_with_params_t *be_with_params_t) {

    return h_extract_param_k(be_with_params, be_with_params_t);
}

HParserBackendVTable h__lalr_backend_vtable = {
    .compile = h_lalr_compile,
    .parse = h_lr_parse,
    .free = h_lalr_free,
    .parse_start = h_lr_parse_start,
    .parse_chunk = h_lr_parse_chunk,
    .parse_finish = h_lr_parse_finish,
    .copy_params = h_copy_numeric_param,
    /* No free_param needed, since it's not actually allocated */

    /* Name/param resolution functions */
    .backend_short_name = "lalr",
    .backend_description = "LALR(k) parser backend",
    .get_description_with_params = h_lalr_get_description,
    .get_short_name_with_params = h_lalr_get_short_name,

    .extract_params = h_lalr_extract_params};

// dummy!
