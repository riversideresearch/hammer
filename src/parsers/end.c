#include "parser_internal.h"

static HParseResult* parse_end(void *env, HParseState *state) {
  if (state->input_stream.index == state->input_stream.length) {
    HParseResult *ret = a_new(HParseResult, 1);
    ret->ast = NULL;
    ret->bit_length = 0;
    ret->arena = state->arena;
    return ret;
  } else {
    return NULL;
  }
}

static void desugar_end(HAllocator *mm__, HCFStack *stk__, void *env) {
  HCFS_ADD_END();
}

static bool end_ctrvm(HRVMProg *prog, void *env) {
  h_rvm_insert_insn(prog, RVM_EOF, 0);
  return true;
}

static const HParserVtable end_vt = {
  .parse = parse_end,
  .isValidRegular = h_true,
  .isValidCF = h_true,
  .desugar = desugar_end,
  .compile_to_rvm = end_ctrvm,
  .higher = false,
};

HParser* h_end_p() {
  return h_end_p__m(&system_allocator);
}

HParser* h_end_p__m(HAllocator* mm__) { 
  return h_new_parser(mm__, &end_vt, NULL);
}
