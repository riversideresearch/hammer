/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"

#include <assert.h>

// TODO: split this up.
typedef struct {
    const HParser *p, *sep;
    size_t count;
    bool min_p;
} HRepeat;

static HParseResult *parse_many(void *env, HParseState *state) {
    HRepeat *env_ = (HRepeat *)env;
    size_t size = env_->count;
    if (size <= 0)
        size = 4;
    if (size > 1024)
        size = 1024; // let's try parsing some elements first...
    HCountedArray *seq = h_carray_new_sized(state->arena, size);
    size_t count = 0;
    HInputStream bak;
    HActionPlan *iteration_plan = NULL;
    while (env_->min_p || env_->count > count) {
        bak = state->input_stream;
        iteration_plan = state->action_plan;
        if (count > 0 && env_->sep != NULL) {
            HParseResult *sep = h_do_parse(env_->sep, state);
            if (!sep)
                goto stop;
        }
        HParseResult *elem = h_do_parse(env_->p, state);
        if (!elem)
            goto stop;
        if (elem->ast)
            h_carray_append(seq, (void *)elem->ast);
        count++;
    }
    assert(count == env_->count);
succ:; // necessary for the label to be here...
    HParsedToken *res = a_new(HParsedToken, 1);
    res->token_type = TT_SEQUENCE;
    res->token_data.seq = seq;
    res->index = 0;
    res->bit_length = 0;
    res->bit_offset = 0;
    return make_result(state->arena, res);
stop:
    if (want_suspend(state))
        return NULL; // bail out early, leaving overrun flag
    if (count >= env_->count) {
        state->input_stream = bak;
        state->action_plan = iteration_plan;
        goto succ;
    }
    return NULL;
}

static bool many_isValidRegular(void *env) {
    HRepeat *repeat = (HRepeat *)env;
    return (repeat->p->vtable->isValidRegular(repeat->p->env) &&
            (repeat->sep == NULL || repeat->sep->vtable->isValidRegular(repeat->sep->env)));
}

static bool many_isValidCF(void *env) {
    HRepeat *repeat = (HRepeat *)env;
    return (repeat->p->vtable->isValidCF(repeat->p->env) &&
            (repeat->sep == NULL || repeat->sep->vtable->isValidCF(repeat->sep->env)));
}

// turn (_ x (_ y (_ z ()))) into (x y z) where '_' are optional
static HParsedToken *reshape_many(const HParseResult *p, void *user) {
    HCountedArray *seq = h_carray_new(p->arena);

    const HParsedToken *tok = p->ast;
    while (tok) {
        assert(tok->token_type == TT_SEQUENCE);
        if (tok->token_data.seq->used > 0) {
            size_t n = tok->token_data.seq->used;
            assert(n <= 3);
            h_carray_append(seq, tok->token_data.seq->elements[n - 2]);
            tok = tok->token_data.seq->elements[n - 1];
        } else {
            tok = NULL;
        }
    }

    HParsedToken *res = a_new_(p->arena, HParsedToken, 1);
    res->token_type = TT_SEQUENCE;
    res->token_data.seq = seq;
    res->index = p->ast->index;
    res->bit_offset = p->ast->bit_offset;
    res->bit_length = p->bit_length;
    return res;
}

static void desugar_many(HAllocator *mm__, HCFStack *stk__, void *env) {
    // TODO: refactor this.
    HRepeat *repeat = (HRepeat *)env;
    if (!repeat->min_p) {
        // count is an exact count.
        assert(repeat->sep == NULL);
        HCFS_BEGIN_CHOICE() {
            HCFS_BEGIN_SEQ() {
                for (size_t i = 0; i < repeat->count; i++)
                    HCFS_DESUGAR(repeat->p);
            }
            HCFS_END_SEQ();
        }
        HCFS_END_CHOICE();
        return;
    }
    assert(repeat->count <= 1);

    /* many(A) =>
           Ma  -> A Mar
               -> \epsilon (but not if many1/sepBy1 is used)
           Mar -> Sep A Mar
               -> \epsilon
    */

    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() {
            HCFS_DESUGAR(repeat->p);
            HCFS_BEGIN_CHOICE() { // Mar
                HCFS_BEGIN_SEQ() {
                    if (repeat->sep != NULL) {
                        HCFS_DESUGAR(repeat->sep);
                    }
                    // stk__->last_completed->reshape = h_act_ignore; // BUG: This modifies a
                    // memoized entry.
                    HCFS_DESUGAR(repeat->p);
                    HCFS_APPEND(HCFS_THIS_CHOICE);
                }
                HCFS_END_SEQ();
                HCFS_BEGIN_SEQ() {}
                HCFS_END_SEQ();
            }
            HCFS_END_CHOICE(); // Mar
        }
        if (repeat->count == 0) {
            HCFS_BEGIN_SEQ() {
                // HCFS_DESUGAR(h_ignore__m(mm__, h_epsilon_p()));
            }
            HCFS_END_SEQ();
        }
        HCFS_THIS_CHOICE->reshape = reshape_many;
    }
    HCFS_END_CHOICE();
}

static bool many_ctrvm(HRVMProg *prog, void *env) {
    HRepeat *repeat = (HRepeat *)env;
    uint16_t clear_to_mark = h_rvm_create_action(prog, h_svm_action_clear_to_mark, NULL);

    if (repeat->min_p) {
        h_rvm_insert_insn(prog, RVM_PUSH, 0);
        assert(repeat->count < 2);
        uint16_t end_fork = 0xFFFF;
        if (repeat->count == 0)
            end_fork = h_rvm_insert_insn(prog, RVM_FORK, 0xFFFF);
        uint16_t goto_mid = h_rvm_insert_insn(prog, RVM_GOTO, 0xFFFF);
        uint16_t nxt = h_rvm_get_ip(prog);
        if (repeat->sep != NULL) {
            h_rvm_insert_insn(prog, RVM_PUSH, 0);
            if (!h_compile_regex(prog, repeat->sep))
                return false;
            h_rvm_insert_insn(prog, RVM_ACTION, clear_to_mark);
        }
        h_rvm_patch_arg(prog, goto_mid, h_rvm_get_ip(prog));
        if (!h_compile_regex(prog, repeat->p))
            return false;
        h_rvm_insert_insn(prog, RVM_FORK, nxt);
        if (repeat->count == 0)
            h_rvm_patch_arg(prog, end_fork, h_rvm_get_ip(prog));

        h_rvm_insert_insn(prog, RVM_ACTION,
                          h_rvm_create_action(prog, h_svm_action_make_sequence, NULL));
        return true;
    }

    h_rvm_insert_insn(prog, RVM_PUSH, 0);
    for (size_t i = 0; i < repeat->count; i++) {
        if (repeat->sep != NULL && i != 0) {
            h_rvm_insert_insn(prog, RVM_PUSH, 0);
            if (!h_compile_regex(prog, repeat->sep))
                return false;
            h_rvm_insert_insn(prog, RVM_ACTION, clear_to_mark);
        }
        if (!h_compile_regex(prog, repeat->p))
            return false;
    }
    h_rvm_insert_insn(prog, RVM_ACTION,
                      h_rvm_create_action(prog, h_svm_action_make_sequence, NULL));
    return true;
}

static const HParserVtable many_vt = {
    .parse = parse_many,
    .isValidRegular = many_isValidRegular,
    .isValidCF = many_isValidCF,
    .compile_to_rvm = many_ctrvm,
    .desugar = desugar_many,
    .higher = true,
};

HParser *h_many(const HParser *p) { return h_many__m(&system_allocator, p); }
HParser *h_many__m(HAllocator *mm__, const HParser *p) {
    HRepeat *env = h_new(HRepeat, 1);
    env->p = p;
    env->sep = NULL;
    env->count = 0;
    env->min_p = true;
    return h_new_parser(mm__, &many_vt, env);
}

HParser *h_many1(const HParser *p) { return h_many1__m(&system_allocator, p); }
HParser *h_many1__m(HAllocator *mm__, const HParser *p) {
    HRepeat *env = h_new(HRepeat, 1);
    env->p = p;
    env->sep = NULL;
    env->count = 1;
    env->min_p = true;
    return h_new_parser(mm__, &many_vt, env);
}

HParser *h_repeat_n(const HParser *p, const size_t n) {
    return h_repeat_n__m(&system_allocator, p, n);
}
HParser *h_repeat_n__m(HAllocator *mm__, const HParser *p, const size_t n) {
    HRepeat *env = h_new(HRepeat, 1);
    env->p = p;
    env->sep = NULL;
    env->count = n;
    env->min_p = false;
    return h_new_parser(mm__, &many_vt, env);
}

HParser *h_sepBy(const HParser *p, const HParser *sep) {
    return h_sepBy__m(&system_allocator, p, sep);
}
HParser *h_sepBy__m(HAllocator *mm__, const HParser *p, const HParser *sep) {
    HRepeat *env = h_new(HRepeat, 1);
    env->p = p;
    env->sep = sep;
    env->count = 0;
    env->min_p = true;
    return h_new_parser(mm__, &many_vt, env);
}

HParser *h_sepBy1(const HParser *p, const HParser *sep) {
    return h_sepBy1__m(&system_allocator, p, sep);
}
HParser *h_sepBy1__m(HAllocator *mm__, const HParser *p, const HParser *sep) {
    HRepeat *env = h_new(HRepeat, 1);
    env->p = p;
    env->sep = sep;
    env->count = 1;
    env->min_p = true;
    return h_new_parser(mm__, &many_vt, env);
}

typedef struct {
    const HParser *length;
    const HParser *value;
} HLenVal;

static HParseResult *parse_length_value(void *env, HParseState *state) {
    HLenVal *lv = (HLenVal *)env;
    HParseResult *len = h_do_parse(lv->length, state);
    if (!len)
        return NULL;
    if (len->ast->token_type != TT_UINT)
        h_platform_errx(1, "Length parser must return an unsigned integer");
    // TODO: allocate this using public functions
    HRepeat repeat = {
        .p = lv->value, .sep = NULL, .count = len->ast->token_data.uint, .min_p = false};
    return parse_many(&repeat, state);
}

static const HParserVtable length_value_vt = {
    .parse = parse_length_value,
    .isValidRegular = h_false,
    .isValidCF = h_false,
};

HParser *h_length_value(const HParser *length, const HParser *value) {
    return h_length_value__m(&system_allocator, length, value);
}
HParser *h_length_value__m(HAllocator *mm__, const HParser *length, const HParser *value) {
    HLenVal *env = h_new(HLenVal, 1);
    env->length = length;
    env->value = value;
    return h_new_parser(mm__, &length_value_vt, env);
}

static HParseResult *parse_cap(void *env, HParseState *state) {
    HRepeat *env_ = (HRepeat *)env;
    size_t size = env_->count;
    if (size <= 0)
        size = 4;
    if (size > 1024)
        size = 1024; // let's try parsing some elements first...
    HCountedArray *seq = h_carray_new_sized(state->arena, size);
    size_t count = 0;
    HInputStream bak;
    // handle edge case of many1_cap with count=0
    if (env_->min_p && env_->count == 0) {
        return NULL;
    }
    while (env_->count > count) {
        bak = state->input_stream;
        HParseResult *elem = h_do_parse(env_->p, state);
        if (!elem)
            goto stop;
        if (elem->ast)
            h_carray_append(seq, (void *)elem->ast);
        count++;
    }
succ:; // necessary for the label to be here...
    HParsedToken *res = a_new(HParsedToken, 1);
    res->token_type = TT_SEQUENCE;
    res->token_data.seq = seq;
    res->index = 0;
    res->bit_length = 0;
    res->bit_offset = 0;
    return make_result(state->arena, res);
stop:
    if (want_suspend(state))
        return NULL;                      // bail out early, leaving overrun flag
    else if (!env_->min_p || count > 0) { // if min_p is true, at least one parse must succeed.
        state->input_stream = bak;
        goto succ;
    } else
        return NULL;
}

/*
 * Build a bounded repetition grammar fragment for up to `remaining`
 * occurrences of parser `p`.
 *
 * This is used by desugar_cap() to enforce the maximum count without
 * generating an unbounded recursive repetition. The helper creates a
 * choice between parsing one instance of `p` followed by up to
 * `remaining - 1` more, or matching the empty sequence.
 */
static void desugar_cap_tail(HAllocator *mm__, HCFStack *stk__, const HParser *p,
                             size_t remaining) {
    if (remaining == 0) {
        HCFS_BEGIN_SEQ() {}
        HCFS_END_SEQ();
        return;
    }

    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() {
            HCFS_DESUGAR(p);
            if (remaining > 1) {
                desugar_cap_tail(mm__, stk__, p, remaining - 1);
            }
        }
        HCFS_END_SEQ();

        HCFS_BEGIN_SEQ() {}
        HCFS_END_SEQ();
    }
    HCFS_END_CHOICE();
}

/*
 * Desugar an up-to-N repetition parser.
 *
 * - If count == 0:
 *     * min_p false => allow empty match
 *     * min_p true  => no match at all, because many1_cap(0) is invalid
 * - If count == 1 and min_p true:
 *     translate to a single occurrence of `p`.
 * - Otherwise:
 *     parse one `p`, then optionally up to count-1 more with desugar_cap_tail().
 *
 * reshape_many is attached so the resulting tree collapses trailing optional
 * structure to the expected sequence form.
 */
static void desugar_cap(HAllocator *mm__, HCFStack *stk__, void *env) {
    // mostly unchanged from desugar_many, but bounded by cap->count.
    HRepeat *cap = (HRepeat *)env;

    if (cap->count == 0) {
        if (cap->min_p) {
            HCFS_BEGIN_CHOICE() {}
            HCFS_END_CHOICE();
        } else {
            HCFS_BEGIN_CHOICE() {
                HCFS_BEGIN_SEQ() {}
                HCFS_END_SEQ();
            }
            HCFS_END_CHOICE();
        }
        return;
    }

    if (cap->min_p && cap->count == 1) {
        HCFS_BEGIN_CHOICE() {
            HCFS_BEGIN_SEQ() { HCFS_DESUGAR(cap->p); }
            HCFS_END_SEQ();
        }
        HCFS_END_CHOICE();
        return;
    }

    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() {
            HCFS_DESUGAR(cap->p);
            if (cap->count > 1) {
                desugar_cap_tail(mm__, stk__, cap->p, cap->count - 1);
            }
        }
        HCFS_END_SEQ();

        if (!cap->min_p) {
            HCFS_BEGIN_SEQ() {}
            HCFS_END_SEQ();
        }

        HCFS_THIS_CHOICE->reshape = reshape_many;
    }
    HCFS_END_CHOICE();
}

static const HParserVtable cap_vt = {
    .parse = parse_cap,
    .isValidRegular = many_isValidRegular,
    .isValidCF = many_isValidCF,
    .desugar = desugar_cap,
    .higher = true,
};

HParser *h_many_cap(const HParser *p, const size_t n) {
    return h_many_cap__m(&system_allocator, p, n);
}
HParser *h_many_cap__m(HAllocator *mm__, const HParser *p, const size_t n) {
    HRepeat *env = h_new(HRepeat, 1);
    env->p = p;
    env->sep = NULL; // sep has no functionaliy yet TODO: add sep functionality if we want it.
    env->count = n;
    env->min_p = false; // min_p has different meaning for h_many_cap, but the structs were mostly
                        // the same so we can reuse HRepeat
    return h_new_parser(mm__, &cap_vt, env);
}

HParser *h_many1_cap(const HParser *p, const size_t n) {
    return h_many1_cap__m(&system_allocator, p, n);
}
HParser *h_many1_cap__m(HAllocator *mm__, const HParser *p, const size_t n) {
    HRepeat *env = h_new(HRepeat, 1);
    env->p = p;
    env->sep = NULL;
    env->count = n;
    env->min_p = true;

    return h_new_parser(mm__, &cap_vt, env);
}
