#include "parser_internal.h"

static HParseResult* parse_not(void* env, HParseState* state) {
  HInputStream bak = state->input_stream;
  if (h_do_parse((HParser*)env, state))
    return NULL;
  if (want_suspend(state))
    return NULL;		// bail out early, leaving overrun flag
  // regular parse failure -> success
  state->input_stream = bak;
  return make_result(state->arena, NULL);
}

static const HParserVtable not_vt = {
  .parse = parse_not,
  .isValidRegular = h_false,  /* see and.c for why */
  .isValidCF = h_false,
  .higher = true,
};

HParser* h_not(const HParser* p) {
  return h_not__m(&system_allocator, p);
}
HParser* h_not__m(HAllocator* mm__, const HParser* p) {
  void* env = (void*)p;
  return h_new_parser(mm__, &not_vt, env);
}
