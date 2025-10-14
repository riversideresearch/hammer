#include "parser_internal.h"

static HParseResult *parse_nothing(void *x, HParseState *y) {
    (void)(x);
    (void)(y);
    // not a mistake, this parser always fails
    return NULL;
}

static void desugar_nothing(HAllocator *mm__, HCFStack *stk__, void *env) {
    HCFS_BEGIN_CHOICE() {}
    HCFS_END_CHOICE();
}

static const HParserVtable nothing_vt = {
    .parse = parse_nothing,
    .isValidRegular = h_true,
    .isValidCF = h_true,
    .desugar = desugar_nothing,
    .higher = false,
};

HParser *h_nothing_p() { return h_nothing_p__m(&system_allocator); }
HParser *h_nothing_p__m(HAllocator *mm__) { return h_new_parser(mm__, &nothing_vt, NULL); }
