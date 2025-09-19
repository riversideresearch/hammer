#include <assert.h>
#include <string.h>
#include "../internal.h"
#include "parser_internal.h"

static HParseResult* parse_charset(void *env, HParseState *state) {
  uint8_t in = h_read_bits(&state->input_stream, 8, false);
  HCharset cs = (HCharset)env;

  if (charset_isset(cs, in)) {
    HParsedToken *tok = a_new(HParsedToken, 1);
    tok->token_type = TT_UINT; tok->uint = in;
    tok->index = 0;
    tok->bit_length = 0;
    tok->bit_offset = 0;
    return make_result(state->arena, tok);    
  } else
    return NULL;
}

static void desugar_charset(HAllocator *mm__, HCFStack *stk__, void *env) {
  HCFS_ADD_CHARSET( (HCharset)env );
}


// FUTURE: this is horribly inefficient

static const HParserVtable charset_vt = {
  .parse = parse_charset,
  .isValidRegular = h_true,
  .isValidCF = h_true,
  .desugar = desugar_charset,
  .higher = false,
};

HParser* h_ch_range(const uint8_t lower, const uint8_t upper) {
  return h_ch_range__m(&system_allocator, lower, upper);
}
HParser* h_ch_range__m(HAllocator* mm__, const uint8_t lower, const uint8_t upper) {
  HCharset cs = new_charset(mm__);
  for (int i = 0; i < 256; i++)
    charset_set(cs, i, (lower <= i) && (i <= upper));
  return h_new_parser(mm__, &charset_vt, cs);
}


static HParser* h_in_or_not__m(HAllocator* mm__, const uint8_t *options, size_t count, int val) {
  HCharset cs = new_charset(mm__);
  for (size_t i = 0; i < 256; i++)
    charset_set(cs, i, 1-val);
  for (size_t i = 0; i < count; i++)
    charset_set(cs, options[i], val);

  return h_new_parser(mm__, &charset_vt, cs);
}

HParser* h_in(const uint8_t *options, size_t count) {
  return h_in_or_not__m(&system_allocator, options, count, 1);
}

HParser* h_in__m(HAllocator* mm__, const uint8_t *options, size_t count) {
  return h_in_or_not__m(mm__, options, count, 1);
}

HParser* h_not_in(const uint8_t *options, size_t count) {
  return h_in_or_not__m(&system_allocator, options, count, 0);
}

HParser* h_not_in__m(HAllocator* mm__, const uint8_t *options, size_t count) {
  return h_in_or_not__m(mm__, options, count, 0);
}

