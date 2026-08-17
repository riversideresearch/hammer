/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"

static HParseResult *parse_unimplemented(void *env, HParseState *state) {
    (void)env;
    (void)state;
    static HParsedToken token = {.token_type = TT_ERR};
    static HParseResult result = {.ast = &token};
    return &result;
}

static const HParserVtable unimplemented_vt = {
    .name = "h_unimplemented",
    .parse = parse_unimplemented,
    .isValidRegular = h_false,
    .isValidCF = h_false,
    .desugar = NULL,
    .higher = true,
};

static HParser unimplemented = {.vtable = &unimplemented_vt, .env = NULL};

const HParser *h_unimplemented(void) { return &unimplemented; }
const HParser *h_unimplemented__m(HAllocator *mm__) { return &unimplemented; }
