/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"

static HParseResult *parse_nothing(void *x, HParseState *y) {
    (void)(x);
    (void)(y);
    // not a mistake, this parser always fails
    return NULL;
}

static bool nothing_predicate(HParseResult *result, void *user_data) {
    (void)result;
    (void)user_data;
    return false;
}

static void desugar_nothing(HAllocator *mm__, HCFStack *stk__, void *env) {
    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() {}
        HCFS_END_SEQ();
        HCFS_THIS_CHOICE->pred = nothing_predicate;
    }
    HCFS_END_CHOICE();
}

static bool nothing_ctrvm(HRVMProg *prog, void *env) {
    h_rvm_insert_insn(prog, RVM_MATCH, 0x0000);
    h_rvm_insert_insn(prog, RVM_MATCH, 0xFFFF);
    return true;
}

static const HParserVtable nothing_vt = {
    .parse = parse_nothing,
    .isValidRegular = h_true,
    .isValidCF = h_true,
    .compile_to_rvm = nothing_ctrvm,
    .desugar = desugar_nothing,
    .higher = false,
};

bool h_is_nothing_parser(const HParser *parser) {
    return parser && parser->vtable == &nothing_vt;
}

HParser *h_nothing_p() { return h_nothing_p__m(&system_allocator); }
HParser *h_nothing_p__m(HAllocator *mm__) {
    return h_new_parser_with_free(mm__, &nothing_vt, NULL, h_no_free_env);
}
