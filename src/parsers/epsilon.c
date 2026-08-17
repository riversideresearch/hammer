/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"

static HParseResult *parse_epsilon(void *env, HParseState *state) {
    (void)env;
    HParseResult *res = a_new(HParseResult, 1);
    res->ast = NULL;
    res->arena = state->arena;
    res->bit_length = 0;
    return res;
}

static bool epsilon_ctrvm(HRVMProg *prog, void *env) { return true; }

static const HParserVtable epsilon_vt = {
    .name = "h_epsilon",
    .parse = parse_epsilon,
    .isValidRegular = h_true,
    .isValidCF = h_true,
    .compile_to_rvm = epsilon_ctrvm,
    .desugar = desugar_epsilon,
    .higher = false,
};

HParser *h_epsilon_p() { return h_epsilon_p__m(&system_allocator); }
HParser *h_epsilon_p__m(HAllocator *mm__) {
    return h_new_parser_with_free(mm__, &epsilon_vt, NULL, h_no_free_env);
}
