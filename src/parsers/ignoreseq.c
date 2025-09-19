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

static const HParserVtable ignoreseq_vt = {
    .parse = parse_ignoreseq,
    .isValidRegular = is_isValidRegular,
    .isValidCF = is_isValidCF,
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

    return h_new_parser(mm__, &ignoreseq_vt, seq);
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

    return h_new_parser(mm__, &ignoreseq_vt, seq);
}
