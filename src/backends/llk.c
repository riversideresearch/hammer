#include "../cfgrammar.h"
#include "../internal.h"
#include "../parsers/parser_internal.h"
#include "../trace.h"
#include "params.h"

#include <assert.h>

/* Generating the LL(k) parse table */

/* Maps each nonterminal (HCFChoice) of the grammar to a HStringMap that
 * maps lookahead strings to productions (HCFSequence).
 */
typedef struct HLLkTable_ {
    size_t kmax;
    HHashTable *rows;
    HCFChoice *start; // start symbol
    HCFStartWrapper *owned_start;
    HArena *arena;
    HAllocator *mm__;
} HLLkTable;

/* Interface to look up an entry in the parse table. */
const HCFSequence *h_llk_lookup(const HLLkTable *table, const HCFChoice *x,
                                const HInputStream *stream) {
    const HStringMap *row = h_hashtable_get(table->rows, x);
    if (row == NULL)
        return NULL;
    assert(row != NULL); // the table should have one row for each nonterminal

    if (row->epsilon_branch)
        return NULL;
    assert(!row->epsilon_branch); // would match without looking at the input
                                  // XXX cases where this could be useful?

    return h_stringmap_get_lookahead(row, *stream);
}

/* Allocate a new parse table. */
HLLkTable *h_llktable_new(HAllocator *mm__) {
    // NB the parse table gets an arena separate from the grammar so we can free
    //    the latter after table generation.
    HArena *arena = h_new_arena(mm__, 0); // default blocksize
    if (arena == NULL)
        return NULL;
    assert(arena != NULL);
    HHashTable *rows = h_hashtable_new(arena, h_eq_ptr, h_hash_ptr);
    if (rows == NULL) {
        h_delete_arena(arena);
        return NULL;
    }
    assert(rows != NULL);

    HLLkTable *table = h_new(HLLkTable, 1);
    if (table == NULL) {
        h_delete_arena(arena);
        return NULL;
    }
    assert(table != NULL);
    table->mm__ = mm__;
    table->arena = arena;
    table->rows = rows;
    table->start = NULL;
    table->owned_start = NULL;

    return table;
}

void h_llktable_free(HLLkTable *table) {
    if (table == NULL)
        return;
    HAllocator *mm__ = table->mm__;
    h_delete_arena(table->arena);
    if (table->owned_start)
        h_free(table->owned_start);
    h_free(table);
}

void *const CONFLICT = (void *)(uintptr_t)(-1);

// helper for stringmap_merge
static void *combine_entries(HHashSet *workset, void *dst, const void *src) {
    if (!dst)
        return (void *)src;
    if (!src)
        return dst;

    assert(dst != NULL);
    assert(src != NULL);

    if (dst == CONFLICT) { // previous conflict
        h_hashset_put(workset, src);
    } else if (dst != src) { // new conflict
        h_hashset_put(workset, dst);
        h_hashset_put(workset, src);
        dst = CONFLICT;
    }

    return dst;
}

// add the mappings of src to dst, marking conflicts and adding the conflicting
// values to workset.
static void stringmap_merge(HHashSet *workset, HStringMap *dst, HStringMap *src) {
    if (src->epsilon_branch) {
        if (dst->epsilon_branch)
            dst->epsilon_branch =
                combine_entries(workset, dst->epsilon_branch, src->epsilon_branch);
        else
            dst->epsilon_branch = src->epsilon_branch;
    } else {
        // if there is a non-conflicting value on the left (dst) side, it means
        // that prediction is already unambiguous. we can drop the right (src)
        // side we were going to extend with.
        if (dst->epsilon_branch && dst->epsilon_branch != CONFLICT)
            return;
    }

    if (src->end_branch) {
        if (dst->end_branch)
            dst->end_branch = combine_entries(workset, dst->end_branch, src->end_branch);
        else
            dst->end_branch = src->end_branch;
    }

    // iterate over src->char_branches
    const HHashTable *ht = src->char_branches;
    for (size_t i = 0; i < ht->capacity; i++) {
        for (HHashTableEntry *hte = &ht->contents[i]; hte; hte = hte->next) {
            if (hte->key == NULL)
                continue;

            HCharKey c = (HCharKey)hte->key;
            HStringMap *src_ = hte->value;

            if (src_) {
                HStringMap *dst_ = h_hashtable_get(dst->char_branches, (void *)c);
                if (dst_) {
                    stringmap_merge(workset, dst_, src_);
                } else {
                    if (src_->arena != dst->arena)
                        src_ = h_stringmap_copy(dst->arena, src_);
                    h_hashtable_put(dst->char_branches, (void *)c, src_);
                }
            }
        }
    }
}

/* Generate entries for the productions of A in the given table row. */
static int fill_table_row(size_t kmax, HCFGrammar *g, HStringMap *row, const HCFChoice *A) {
    HHashSet *workset;

    // initialize working set to the productions of A
    workset = h_hashset_new(g->arena, h_eq_ptr, h_hash_ptr);
    for (HCFSequence **s = A->data.seq; *s; s++)
        h_hashset_put(workset, *s);

    // run until workset exhausted or kmax hit
    size_t k;
    for (k = 1; k <= kmax; k++) {
        // allocate a fresh workset for the next round
        HHashSet *nextset = h_hashset_new(g->arena, h_eq_ptr, h_hash_ptr);

        // iterate over the productions in workset...
        const HHashTable *ht = workset;
        for (size_t i = 0; i < ht->capacity; i++) {
            for (HHashTableEntry *hte = &ht->contents[i]; hte; hte = hte->next) {
                if (hte->key == NULL)
                    continue;

                HCFSequence *rhs = (void *)hte->key;
                if (rhs == NULL || rhs == CONFLICT) {
                    h_hashset_free(workset);
                    return -1;
                }
                assert(rhs != NULL);
                assert(rhs != CONFLICT); // just to be sure there's no mixup

                // calculate predict set; let values map to rhs
                HStringMap *pred = h_predict(k, g, A, rhs);
                h_stringmap_replace(pred, NULL, rhs);

                // merge predict set into the row
                // accumulates conflicts in new workset
                stringmap_merge(nextset, row, pred);
            }
        }

        // switch to the updated workset
        h_hashset_free(workset);
        workset = nextset;

        // if the workset is empty, row is without conflict; we're done
        if (h_hashset_empty(workset))
            break;

        // clear conflict markers for next iteration
        h_stringmap_replace(row, CONFLICT, NULL);
    }

    h_hashset_free(workset);
    return (k > kmax) ? -1 : 0;
}

/* Generate the LL(k) parse table from the given grammar.
 * Returns -1 on error, 0 on success.
 */
static int fill_table(size_t kmax, HCFGrammar *g, HLLkTable *table) {
    table->kmax = kmax;
    table->start = g->start;

    // iterate over g->nts
    size_t i;
    HHashTableEntry *hte;
    for (i = 0; i < g->nts->capacity; i++) {
        for (hte = &g->nts->contents[i]; hte; hte = hte->next) {
            if (hte->key == NULL)
                continue;
            const HCFChoice *a = hte->key; // production's left-hand symbol
            if (a->type != HCF_CHOICE)
                return -1;
            assert(a->type == HCF_CHOICE);

            // create table row for this nonterminal
            HStringMap *row = h_stringmap_new(table->arena);
            h_hashtable_put(table->rows, a, row);

            if (fill_table_row(kmax, g, row, a) < 0) {
                // unresolvable conflicts in row
                // NB we don't worry about deallocating anything, h_llk_compile will
                //    delete the whole arena for us.
                return -1;
            }
        }
    }

    return 0;
}

int h_llk_compile(HAllocator *mm__, HParser *parser, const void *params) {
    size_t kmax = params ? (uintptr_t)params : DEFAULT_KMAX;
    if (kmax == 0)
        return -1;
    assert(kmax > 0);

    // Convert parser to a CFG. This can fail as indicated by a NULL return.
    HCFGrammar *grammar = h_cfgrammar(mm__, parser);
    if (grammar == NULL)
        return -1; // -> Backend unsuitable for this parser.

    // TODO: eliminate common prefixes
    // TODO: eliminate left recursion
    // TODO: avoid conflicts by splitting occurances?

    // generate table and store in parser->backend_data.
    HLLkTable *table = h_llktable_new(mm__);
    if (!table) {
        h_cfgrammar_free(grammar);
        return -1;
    }
    if (fill_table(kmax, grammar, table) < 0) {
        // the table was ambiguous
        h_cfgrammar_free(grammar);
        h_llktable_free(table);
        return -2;
    }
    parser->backend_data = table;

    table->owned_start = grammar->owned_start;
    grammar->owned_start = NULL;

    // free grammar and its arena.
    // desugared parsers (HCFChoice and HCFSequence) are unaffected by this.
    h_cfgrammar_free(grammar);

    return 0;
}

void h_llk_free(HParser *parser) {
    HLLkTable *table = parser->backend_data;
    h_llktable_free(table);
    parser->backend_data = NULL;
    parser->backend_vtable = h_get_default_backend_vtable();
    parser->backend = h_get_default_backend();
}

/* LL(k) driver */

typedef struct {
    HArena *arena;  // will hold the results
    HArena *tarena; // tmp, deleted after parse
    HSlist *stack;
    HCountedArray *seq; // accumulates current parse result
    HActionPlan *action_plan;

    uint8_t *buf;     // for lookahead across chunk boundaries
                      // allocated to size 2*kmax
                      // new chunk starts at index kmax
                      // ( 0  ... kmax ...  2*kmax-1 )
                      //   \_old_/\______new_______/
    HInputStream win; // win.length is set to 0 when not in use
} HLLkState;

typedef struct {
    HCFChoice *symbol;
    size_t start;
    uint8_t bit_offset;
} HLLkFrame;

static void llk_expected_from_map(const HStringMap *map, bool expected[256], bool *expected_eof) {
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

static void llk_trace_lookup_failure(const HStringMap *row, HInputStream stream,
                                     const HParser *parser) {
    const HStringMap *map = row;
    if (!map) {
        size_t index = stream.pos + stream.index;
        CF_TRACE_FAILURE(index, index, H_PARSE_ERROR_HIGHER_ORDER, parser, NULL, false);
        return;
    }
    while (map && !map->epsilon_branch) {
        size_t index = stream.pos + stream.index;
        uint8_t actual = h_read_bits(&stream, 8, false);
        if (stream.overrun) {
            bool expected[256], expected_eof;
            llk_expected_from_map(map, expected, &expected_eof);
            CF_TRACE_FAILURE(index, index, H_PARSE_ERROR_UNEXPECTED_EOF, parser, expected,
                             expected_eof);
            return;
        }

        const HStringMap *next = h_stringmap_get_char(map, actual);
        if (!next) {
            bool expected[256], expected_eof;
            llk_expected_from_map(map, expected, &expected_eof);
            CF_TRACE_FAILURE(index, index + 1, H_PARSE_ERROR_PRIMITIVE_MISMATCH, parser, expected,
                             expected_eof);
            return;
        }
        map = next;
    }
}

static void llk_trace_terminal_failure(const HCFChoice *symbol, size_t index, size_t end,
                                       bool unexpected_eof, const HParser *root_parser) {
    bool expected[256] = {false};
    bool expected_eof = false;
    if (symbol->type == HCF_END)
        expected_eof = true;
    else if (symbol->type == HCF_CHAR)
        expected[symbol->data.chr] = true;
    else if (symbol->type == HCF_CHARSET)
        for (size_t i = 0; i < 256; i++)
            expected[i] = charset_isset(symbol->data.charset, (uint8_t)i);

    CF_TRACE_FAILURE(index, end,
                     unexpected_eof ? H_PARSE_ERROR_UNEXPECTED_EOF
                                    : H_PARSE_ERROR_PRIMITIVE_MISMATCH,
                     symbol->parser ? symbol->parser : root_parser, expected, expected_eof);
}

// in order to construct the parse tree, we delimit the symbol stack into
// frames corresponding to production right-hand sides. since only left-most
// derivations are produced this linearization is unique.
// the 'mark' allocated below simply reserves a memory address to use as the
// frame delimiter.
// nonterminals, instead of being popped and forgotten, are put back onto the
// stack below the mark to tell us which validations and semantic actions to
// execute on their corresponding result.
// also on the stack below the mark, we store the previously accumulated
// value for the surrounding production.
static void const *const MARK = &MARK; // stack frame delimiter

static HLLkState *llk_parse_start_(HAllocator *mm__, const HParser *parser) {
    const HLLkTable *table = parser->backend_data;
    if (table == NULL)
        return NULL;
    assert(table != NULL);

    HLLkState *s = h_new(HLLkState, 1);
    if (!s)
        return NULL;
    s->arena = h_new_arena(mm__, 0);
    s->tarena = h_new_arena(mm__, 0);
    if (!s->arena || !s->tarena) {
        if (s->arena)
            h_delete_arena(s->arena);
        if (s->tarena)
            h_delete_arena(s->tarena);
        h_free(s);
        return NULL;
    }
    s->stack = h_slist_new(s->tarena);
    s->seq = h_carray_new(s->arena);
    s->action_plan = NULL;
    s->buf = h_arena_malloc(s->tarena, 2 * table->kmax);

    s->win.input = s->buf;
    s->win.length = 0; // unused

    // initialize with the start symbol on the stack.
    h_slist_push(s->stack, table->start);

    return s;
}

// helper: add new input to the lookahead window
static bool append_win(size_t kmax, HLLkState *s, HInputStream *stream) {
    if (stream->bit_offset != 0 || s->win.input != s->buf || s->win.length != kmax ||
        s->win.index >= kmax)
        return false;

    assert(stream->bit_offset == 0);
    assert(s->win.input == s->buf);
    assert(s->win.length == kmax);
    assert(s->win.index < kmax);

    size_t n = stream->length - stream->index; // bytes to copy
    if (n > kmax)
        n = kmax;

    memcpy(s->buf + kmax, stream->input + stream->index, n);
    s->win.length += n;
    return true;
}

// helper: save old input to the lookahead window
static bool save_win(size_t kmax, HLLkState *s, HInputStream *stream) {
    if (stream->bit_offset != 0)
        return false;

    assert(stream->bit_offset == 0);

    size_t len = stream->length - stream->index;
    if (len >= kmax)
        return false;
    assert(len < kmax);

    if (len == 0) {
        // stream empty? nothing to do.
        return true;
    } else if (s->win.length > 0) {
        // window active? should contain all of stream.
        if (s->win.length != kmax + len || s->win.index > kmax)
            return false;
        assert(s->win.length == kmax + len);
        assert(s->win.index <= kmax);

        // shift contents down:
        //
        //   (0                 kmax            )
        //         ...   \_old_/\_new_/   ...
        //
        //   (0                 kmax            )
        //    ... \_old_/\_new_/       ...
        //
        s->win.pos += len; // position of the window shifts up
        len = s->win.length - s->win.index;
        if (len > kmax)
            return false;
        assert(len <= kmax);
        memmove(s->buf + kmax - len, s->buf + s->win.index, len);
    } else {
        // window not active? save stream to window.
        // buffer starts kmax bytes below chunk boundary
        s->win.pos = stream->pos - kmax;
        memcpy(s->buf + kmax - len, stream->input + stream->index, len);
    }

    // metadata
    s->win = *stream;
    s->win.input = s->buf;
    s->win.index = kmax - len;
    s->win.length = kmax;
    return true;
}

// returns partial result or NULL (no parse)
static HCountedArray *llk_parse_chunk_(HLLkState *s, const HParser *parser, HInputStream *chunk,
                                       bool trace_failures) {
    HParsedToken *tok = NULL; // will hold result token
    HActionPlan *tok_plan = NULL;
    HCFChoice *x = NULL; // current symbol (from top of stack)
    HInputStream *stream;

    if (chunk->index != 0 || chunk->bit_offset != 0)
        return NULL;
    assert(chunk->index == 0);
    assert(chunk->bit_offset == 0);

    const HLLkTable *table = parser->backend_data;
    if (table == NULL)
        return NULL;
    assert(table != NULL);

    HArena *arena = s->arena;
    HArena *tarena = s->tarena;
    HSlist *stack = s->stack;
    size_t kmax = table->kmax;

    if (!s->seq)
        return NULL; // parse already failed

    // out-of-memory handling
    jmp_buf except;
    h_arena_set_except(arena, &except);
    h_arena_set_except(tarena, &except);
    if (setjmp(except))
        goto no_parse;

    HCountedArray *seq = s->seq;
    HActionPlan *plan = s->action_plan;

    if (s->win.length > 0) {
        if (!append_win(kmax, s, chunk))
            goto no_parse;
        stream = &s->win;
    } else {
        stream = chunk;
    }

    // when we empty the stack, the parse is complete.
    while (!h_slist_empty(stack)) {
        tok = NULL;
        tok_plan = NULL;
        size_t symbol_start = stream->pos + stream->index;
        bool completed_nonterminal = false;

        // pop top of stack for inspection
        x = h_slist_pop(stack);
        if (x == NULL)
            goto no_parse;
        assert(x != NULL);

        if (x != MARK && x->type == HCF_CHOICE) {
            // x is a nonterminal; apply the appropriate production and continue
            const HParser *origin = x->parser ? x->parser : parser;
            if (trace_failures)
                CF_TRACE_PARSER_ENTER(origin, symbol_start, "predict");

            // look up applicable production in parse table
            const HStringMap *row = h_hashtable_get(table->rows, x);
            const HCFSequence *p = h_llk_lookup(table, x, stream);
            if (p == NULL) {
                if (trace_failures)
                    llk_trace_lookup_failure(row, *stream, origin);
                if (trace_failures)
                    CF_TRACE_PARSER_EXIT(origin, symbol_start, symbol_start, NULL, false,
                                         "predict");
                goto no_parse;
            }
            if (p == NEED_INPUT) {
                goto need_input;
            }

            // an infinite loop case that shouldn't happen
            if (p->items[0] == x) {
                if (trace_failures)
                    CF_TRACE_PARSER_EXIT(origin, symbol_start, symbol_start, NULL, false,
                                         "left recursion");
                goto no_parse;
            }
            assert(!p->items[0] || p->items[0] != x);

            // push stack frame
            HLLkFrame *frame = h_arena_malloc(tarena, sizeof(*frame));
            frame->symbol = x;
            frame->start = symbol_start;
            frame->bit_offset = stream->bit_offset;
            h_slist_push(stack, seq);          // save current partial value
            h_slist_push(stack, plan);         // save its deferred action plan
            h_slist_push(stack, frame);        // save the nonterminal and its start position
            h_slist_push(stack, (void *)MARK); // frame delimiter

            // open a fresh result sequence
            seq = h_carray_new(arena);
            plan = NULL;

            // push production's rhs onto the stack (in reverse order)
            HCFChoice **s;
            for (s = p->items; *s; s++)
                ;
            for (s--; s >= p->items; s--)
                h_slist_push(stack, *s);

            continue; // no result to record
        }

        // the top of stack is such that there will be a result...
        tok = h_arena_malloc(arena, sizeof(HParsedToken));
        if (x == MARK) {
            // hit stack frame boundary...
            // wrap the accumulated parse result, this sequence is finished
            tok->token_type = TT_SEQUENCE;
            tok->token_data.seq = seq;

            // recover original nonterminal and result sequence
            tok_plan = plan;
            if (h_slist_empty(stack))
                goto no_parse;
            HLLkFrame *frame = h_slist_pop(stack);
            x = frame->symbol;
            completed_nonterminal = true;
            symbol_start = frame->start;
            tok->index = frame->start;
            tok->bit_offset = frame->bit_offset;
            if (h_slist_empty(stack))
                goto no_parse;
            plan = h_slist_pop(stack);
            if (h_slist_empty(stack))
                goto no_parse;
            seq = h_slist_pop(stack);
            // tok becomes next left-most element of higher-level sequence
        } else {
            // x is a terminal or simple charset; match against input

            tok->index = stream->pos + stream->index;
            tok->bit_offset = stream->bit_offset;
            const HParser *origin = x->parser ? x->parser : parser;
            if (trace_failures)
                CF_TRACE_PARSER_ENTER(origin, tok->index, "terminal");

            // consume the input token
            uint8_t input = h_read_bits(stream, 8, false);

            // when old chunk consumed from window, switch to new chunk
            if (s->win.length > 0 && s->win.index >= kmax) {
                s->win.length = 0; // disable the window
                stream = chunk;
            }

            switch (x->type) {
            case HCF_END:
                if (!stream->overrun) {
                    if (trace_failures)
                        llk_trace_terminal_failure(x, tok->index, tok->index + 1, false, parser);
                    if (trace_failures)
                        CF_TRACE_PARSER_EXIT(origin, tok->index, tok->index + 1, NULL, false,
                                             "terminal");
                    goto no_parse;
                }
                if (!stream->last_chunk)
                    goto need_input;
                tok = NULL;
                break;

            case HCF_CHAR:
                if (stream->overrun) {
                    if (stream->last_chunk && trace_failures)
                        llk_trace_terminal_failure(x, tok->index, tok->index, true, parser);
                    if (stream->last_chunk && trace_failures)
                        CF_TRACE_PARSER_EXIT(origin, tok->index, tok->index, NULL, false,
                                             "terminal");
                    goto need_input;
                }
                if (input != x->data.chr) {
                    if (trace_failures)
                        llk_trace_terminal_failure(x, tok->index, tok->index + 1, false, parser);
                    if (trace_failures)
                        CF_TRACE_PARSER_EXIT(origin, tok->index, tok->index + 1, NULL, false,
                                             "terminal");
                    goto no_parse;
                }
                tok->token_type = TT_UINT;
                tok->token_data.uint = x->data.chr;
                break;

            case HCF_CHARSET:
                if (stream->overrun) {
                    if (stream->last_chunk && trace_failures)
                        llk_trace_terminal_failure(x, tok->index, tok->index, true, parser);
                    if (stream->last_chunk && trace_failures)
                        CF_TRACE_PARSER_EXIT(origin, tok->index, tok->index, NULL, false,
                                             "terminal");
                    goto need_input;
                }
                if (!charset_isset(x->data.charset, input)) {
                    if (trace_failures)
                        llk_trace_terminal_failure(x, tok->index, tok->index + 1, false, parser);
                    if (trace_failures)
                        CF_TRACE_PARSER_EXIT(origin, tok->index, tok->index + 1, NULL, false,
                                             "terminal");
                    goto no_parse;
                }
                tok->token_type = TT_UINT;
                tok->token_data.uint = input;
                break;

            default: // should not be reached
                assert_message(0, "unknown HCFChoice type");
                if (trace_failures)
                    CF_TRACE_PARSER_EXIT(origin, tok->index, tok->index, NULL, false, "terminal");
                goto no_parse;
            }
        }

        // 'tok' has been parsed; process it

        // perform token reshape if indicated
        if (x->reshape) {
            HParsedToken *t = x->reshape(make_result(arena, tok), x->user_data);
            if (t && tok) {
                t->index = tok->index;
                t->bit_offset = tok->bit_offset;
                tok = t;
            } else {
                tok = NULL;
            }
        }

        // call validation and semantic action, if present
        if (x->pred && !x->pred(make_result(tarena, tok), x->user_data)) {
            if (trace_failures)
                CF_TRACE_FAILURE(symbol_start, stream->pos + stream->index,
                                 H_PARSE_ERROR_SEMANTIC_PREDICATE, x->parser ? x->parser : parser,
                                 NULL, false);
            if (trace_failures)
                CF_TRACE_PARSER_EXIT(x->parser ? x->parser : parser, symbol_start,
                                     stream->pos + stream->index, tok, false,
                                     completed_nonterminal ? "predicate" : "terminal predicate");
            goto no_parse; // validation failed -> no parse
        }
        if (x->plan_action)
            tok = x->plan_action(make_result(arena, tok), x->user_data, &tok_plan);
        else if (x->action)
            tok = (HParsedToken *)x->action(make_result(arena, tok), x->user_data);

        if (trace_failures)
            CF_TRACE_PARSER_EXIT(x->parser ? x->parser : parser, symbol_start,
                                 stream->pos + stream->index, tok, true,
                                 completed_nonterminal ? "reduce" : "terminal");

        // append to result sequence
        h_carray_append(seq, tok);
        plan = h_action_plan_concat(arena, plan, tok_plan);
    }

    // success
    // since we started with a single nonterminal on the stack, seq should
    // contain exactly the parse result.
    if (seq->used != 1)
        goto no_parse;
    assert(seq->used == 1);

end:
    s->action_plan = plan;
    h_arena_set_except(arena, NULL);
    h_arena_set_except(tarena, NULL);
    return seq;

no_parse:
    seq = NULL;
    plan = NULL;
    goto end;

need_input:
    if (stream->last_chunk)
        goto no_parse;
    if (!save_win(kmax, s, chunk))
        goto no_parse;
    h_slist_push(stack, x); // try this symbol again next time
    goto end;
}

static HParseResult *llk_parse_finish_(HAllocator *mm__, HLLkState *s) {
    HParseResult *res = NULL;

    if (s->seq) {
        if (s->seq->used != 1) {
            h_delete_arena(s->arena);
            res = NULL;
        } else {
            assert(s->seq->used == 1);
            if (!h_action_plan_execute(s->arena, s->action_plan)) {
                h_delete_arena(s->arena);
                res = NULL;
            } else {
                res = make_result(s->arena, s->seq->elements[0]);
            }
        }
    } else {
        h_delete_arena(s->arena);
    }

    h_delete_arena(s->tarena);
    h_free(s);
    return res;
}

HParseResult *h_llk_parse(HAllocator *mm__, const HParser *parser, HInputStream *stream) {
    CF_TRACE_BEGIN(PB_LL, parser, stream->input, stream->length);
    HLLkState *s = llk_parse_start_(mm__, parser);
    if (!s) {
        CF_TRACE_END(false);
        return NULL;
    }

    assert(stream->last_chunk);
    if (!stream->last_chunk) {
        llk_parse_finish_(mm__, s);
        CF_TRACE_END(false);
        return NULL;
    }
    s->seq = llk_parse_chunk_(s, parser, stream, TRACE_ENABLED());

    HParseResult *res = llk_parse_finish_(mm__, s);
    if (res)
        res->bit_length = stream->index * 8 + stream->bit_offset;

    CF_TRACE_END(res != NULL);

    return res;
}

void h_llk_parse_start(HSuspendedParser *s) {
    s->backend_state = llk_parse_start_(s->mm__, s->parser);
}

bool h_llk_parse_chunk(HSuspendedParser *s, HInputStream *input) {
    HLLkState *state = s->backend_state;
    if (!state)
        return true;

    state->seq = llk_parse_chunk_(state, s->parser, input, false);

    h_arena_set_except(state->arena, NULL);
    h_arena_set_except(state->tarena, NULL);

    return (state->seq == NULL || h_slist_empty(state->stack));
}

HParseResult *h_llk_parse_finish(HSuspendedParser *s) {
    if (!s->backend_state)
        return NULL;
    return llk_parse_finish_(s->mm__, s->backend_state);
}

char *h_llk_get_description(HAllocator *mm__, HParserBackend be, void *param) {
    const char *backend_name = "LL";
    size_t k, len;
    char *descr = NULL;

    k = h_get_param_k(param);

    descr = h_format_description_with_param_k(mm__, backend_name, k);

    return descr;
}

char *h_llk_get_short_name(HAllocator *mm__, HParserBackend be, void *param) {
    const char *backend_name = "LL";

    size_t k;
    char *name = NULL;

    k = h_get_param_k(param);

    name = h_format_name_with_param_k(mm__, backend_name, k);

    return name;
}

int h_llk_extract_params(HParserBackendWithParams *be_with_params,
                         backend_with_params_t *be_with_params_t) {

    return h_extract_param_k(be_with_params, be_with_params_t);
}

HParserBackendVTable h__llk_backend_vtable = {
    .compile = h_llk_compile,
    .parse = h_llk_parse,
    .free = h_llk_free,

    .parse_start = h_llk_parse_start,
    .parse_chunk = h_llk_parse_chunk,
    .parse_finish = h_llk_parse_finish,

    .copy_params = h_copy_numeric_param,
    /* No free_param needed, since it's not actually allocated */

    /* Name/param resolution functions */
    .backend_short_name = "llk",
    .backend_description = "LL(k) parser backend",
    .get_description_with_params = h_llk_get_description,
    .get_short_name_with_params = h_llk_get_short_name,

    /*extraction of params from string*/
    .extract_params = h_llk_extract_params};
