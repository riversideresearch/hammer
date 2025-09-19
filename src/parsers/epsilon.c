#include "parser_internal.h"

static HParseResult* parse_epsilon(void* env, HParseState* state) {
  (void)env;
  HParseResult* res = a_new(HParseResult, 1);
  res->ast = NULL;
  res->arena = state->arena;
  res->bit_length = 0;
  return res;
}


static const HParserVtable epsilon_vt = {
  .parse = parse_epsilon,
  .isValidRegular = h_true,
  .isValidCF = h_true,
  .desugar = desugar_epsilon,
  .higher = false,
};

HParser* h_epsilon_p() {
  return h_epsilon_p__m(&system_allocator);
}
HParser* h_epsilon_p__m(HAllocator* mm__) {
  HParser *epsilon_p = h_new(HParser, 1);
  epsilon_p->desugared = NULL;
  epsilon_p->backend_data = NULL;
  epsilon_p->backend = h_get_default_backend();
  epsilon_p->backend_vtable = h_get_default_backend_vtable();
  epsilon_p->vtable = &epsilon_vt;
  return epsilon_p;
}
