/* Copyright (c) 2026 Riverside Research */
#include "../trace.h"
#include "parser_internal.h"

static HParseResult *parse_not(void *env, HParseState *state) {
    HInputStream bak = state->input_stream;
    HActionPlan *bak_plan = state->action_plan;
    size_t start = state->input_stream.pos + state->input_stream.index;
    if (h_do_parse((HParser *)env, state)) {
        size_t end = state->input_stream.pos + state->input_stream.index;
        state->input_stream = bak;
        state->action_plan = bak_plan;
        h_trace_note_failure(state->input_stream.trace, H_PARSE_ERROR_HIGHER_ORDER,
                             "negative lookahead failed because its parser matched", start, end);
        return NULL;
    }
    if (want_suspend(state))
        return NULL; // bail out early, leaving overrun flag
    // regular parse failure -> success
    state->input_stream = bak;
    state->action_plan = bak_plan;
    return make_result(state->arena, NULL);
}

static const HParserVtable not_vt = {
    .name = "h_not",
    .parse = parse_not,
    .isValidRegular = h_false, /* see and.c for why */
    .isValidCF = h_false,
    .higher = true,
};

HParser *h_not(const HParser *p) { return h_not__m(&system_allocator, p); }
HParser *h_not__m(HAllocator *mm__, const HParser *p) {
    void *env = (void *)p;
    return h_new_parser_with_free(mm__, &not_vt, env, h_no_free_env);
}
