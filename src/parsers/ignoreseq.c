/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"

#include <assert.h>

// XXX I'm thinking these parsers should be removed entirely in favor of an
//     equivalent family of HActions.  --pesco

//
// general case: parse sequence, pick one result
//

typedef struct HIgnoreSeq_ {
    const HParser **parsers;
    size_t len;   // how many parsers in 'ps'
    size_t which; // whose result to return
} HIgnoreSeq;

static void free_env(HAllocator *allocator, void *environment) {
    HIgnoreSeq *s = environment;

    if (s == NULL)
        return;

    allocator->free(allocator, s->parsers);
    allocator->free(allocator, s);
}

static HParseResult *parse_ignoreseq(void *env, HParseState *state) {
    const HIgnoreSeq *seq = (HIgnoreSeq *)env;
    HParseResult *res = NULL;

    for (size_t i = 0; i < seq->len; ++i) {
        HParseResult *tmp = h_do_parse(seq->parsers[i], state);
        if (!tmp)
            return NULL;
        else if (i == seq->which) {
            res = tmp;
            res->bit_length = 0; // recalculate
        }
    }

    return res;
}

static void desugar_ignoreseq(HAllocator *mm__, HCFStack *stk__, void *env) {
    HIgnoreSeq *seq = (HIgnoreSeq *)env;

    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() {
            for (size_t i = 0; i < seq->len; ++i)
                HCFS_DESUGAR(seq->parsers[i]);
        }
        HCFS_END_SEQ();

        if (seq->which == 0)
            HCFS_THIS_CHOICE->reshape = h_act_first;
        else if (seq->which == 1)
            HCFS_THIS_CHOICE->reshape = h_act_second; // for h_middle
        else if (seq->which == seq->len - 1)
            HCFS_THIS_CHOICE->reshape = h_act_last;
        else
            assert(!"Ignoreseq must select item 0, 1, or n-1");
    }
    HCFS_END_CHOICE();
}

static bool is_isValidRegular(void *env) {
    HIgnoreSeq *seq = (HIgnoreSeq *)env;
    for (size_t i = 0; i < seq->len; ++i) {
        if (!seq->parsers[i]->vtable->isValidRegular(seq->parsers[i]->env))
            return false;
    }
    return true;
}

static bool is_isValidCF(void *env) {
    HIgnoreSeq *seq = (HIgnoreSeq *)env;
    for (size_t i = 0; i < seq->len; ++i) {
        if (!seq->parsers[i]->vtable->isValidCF(seq->parsers[i]->env))
            return false;
    }
    return true;
}

static bool h_svm_action_ignoreseq(HArena *arena, HSVMContext *ctx, void *env) {
    HIgnoreSeq *seq = (HIgnoreSeq *)env;
    HParsedToken *save = NULL;

    size_t stack_count = ctx->stack_count;
    for (size_t i = seq->len; i-- > 0;) {
        if (stack_count == 0)
            return false;

        HParsedToken *top = ctx->stack[stack_count - 1];
        if (top->token_type == TT_MARK) {
            stack_count--;
        } else {
            if (stack_count < 2 || ctx->stack[stack_count - 2]->token_type != TT_MARK)
                return false;
            if (i == seq->which)
                save = top;
            stack_count -= 2;
        }
    }

    ctx->stack_count = stack_count;
    if (save)
        ctx->stack[ctx->stack_count++] = save;
    return true;
}

static bool is_ctrvm(HRVMProg *prog, void *env) {
    HIgnoreSeq *seq = (HIgnoreSeq *)env;
    for (size_t i = 0; i < seq->len; ++i) {
        h_rvm_insert_insn(prog, RVM_PUSH, 0);
        if (!h_compile_regex(prog, seq->parsers[i]))
            return false;
    }
    HIgnoreSeq *rvm_seq = h_rvm_alloc(prog, sizeof(*rvm_seq));
    *rvm_seq = *seq;
    rvm_seq->parsers = NULL;
    h_rvm_insert_insn(prog, RVM_ACTION, h_rvm_create_action(prog, h_svm_action_ignoreseq, rvm_seq));
    return true;
}

static const HParserVtable ignoreseq_vt = {
    .name = "h_ignoreseq",
    .parse = parse_ignoreseq,
    .isValidRegular = is_isValidRegular,
    .isValidCF = is_isValidCF,
    .compile_to_rvm = is_ctrvm,
    .desugar = desugar_ignoreseq,
    .higher = true,
};

//
// API frontends
//

static HParser *h_leftright__m(HAllocator *mm__, const HParser *p, const HParser *q, size_t which) {
    HIgnoreSeq *seq = h_new(HIgnoreSeq, 1);
    seq->parsers = h_new(const HParser *, 2);
    seq->parsers[0] = p;
    seq->parsers[1] = q;
    seq->len = 2;
    seq->which = which;

    return h_new_parser_with_free(mm__, &ignoreseq_vt, seq, free_env);
}

HParser *h_left(const HParser *p, const HParser *q) {
    return h_leftright__m(&system_allocator, p, q, 0);
}
HParser *h_left__m(HAllocator *mm__, const HParser *p, const HParser *q) {
    return h_leftright__m(mm__, p, q, 0);
}

HParser *h_right(const HParser *p, const HParser *q) {
    return h_leftright__m(&system_allocator, p, q, 1);
}
HParser *h_right__m(HAllocator *mm__, const HParser *p, const HParser *q) {
    return h_leftright__m(mm__, p, q, 1);
}

HParser *h_middle(const HParser *p, const HParser *x, const HParser *q) {
    return h_middle__m(&system_allocator, p, x, q);
}
HParser *h_middle__m(HAllocator *mm__, const HParser *p, const HParser *x, const HParser *q) {
    HIgnoreSeq *seq = h_new(HIgnoreSeq, 1);
    seq->parsers = h_new(const HParser *, 3);
    seq->parsers[0] = p;
    seq->parsers[1] = x;
    seq->parsers[2] = q;
    seq->len = 3;
    seq->which = 1;

    return h_new_parser_with_free(mm__, &ignoreseq_vt, seq, free_env);
}
