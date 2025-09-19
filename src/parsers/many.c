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
    while (env_->min_p || env_->count > count) {
        bak = state->input_stream;
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
    res->seq = seq;
    res->index = 0;
    res->bit_length = 0;
    res->bit_offset = 0;
    return make_result(state->arena, res);
stop:
    if (want_suspend(state))
        return NULL; // bail out early, leaving overrun flag
    if (count >= env_->count) {
        state->input_stream = bak;
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
        if (tok->seq->used > 0) {
            size_t n = tok->seq->used;
            assert(n <= 3);
            h_carray_append(seq, tok->seq->elements[n - 2]);
            tok = tok->seq->elements[n - 1];
        } else {
            tok = NULL;
        }
    }

    HParsedToken *res = a_new_(p->arena, HParsedToken, 1);
    res->token_type = TT_SEQUENCE;
    res->seq = seq;
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

static const HParserVtable many_vt = {
    .parse = parse_many,
    .isValidRegular = many_isValidRegular,
    .isValidCF = many_isValidCF,
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
    HRepeat repeat = {.p = lv->value, .sep = NULL, .count = len->ast->uint, .min_p = false};
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
