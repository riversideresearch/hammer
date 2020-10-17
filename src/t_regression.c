#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include "glue.h"
#include "hammer.h"
#include "test_suite.h"
#include "internal.h"

static void test_bug118(void) {
  // https://github.com/UpstandingHackers/hammer/issues/118
  // Adapted from https://gist.github.com/mrdomino/c6bc91a7cb3b9817edb5

  HParseResult* p;
  const uint8_t *input = (uint8_t*)"\x69\x5A\x6A\x7A\x8A\x9A";
 
#define MY_ENDIAN (BIT_BIG_ENDIAN | BYTE_LITTLE_ENDIAN)
    H_RULE(nibble, h_with_endianness(MY_ENDIAN, h_bits(4, false)));
    H_RULE(sample, h_with_endianness(MY_ENDIAN, h_bits(10, false)));
#undef MY_ENDIAN
 
    H_RULE(samples, h_sequence(h_repeat_n(sample, 3), h_ignore(h_bits(2, false)), NULL));
 
    H_RULE(header_ok, h_sequence(nibble, nibble, NULL));
    H_RULE(header_weird, h_sequence(nibble, nibble, nibble, NULL));
 
    H_RULE(parser_ok, h_sequence(header_ok, samples, NULL));
    H_RULE(parser_weird, h_sequence(header_weird, samples, NULL));
 
 
    p = h_parse(parser_weird, input, 6);
    g_check_cmp_int32(p->bit_length, ==, 44);
    h_parse_result_free(p);
    p = h_parse(parser_ok, input, 6);
    g_check_cmp_int32(p->bit_length, ==, 40);
    h_parse_result_free(p);
}

static void test_seq_index_path(void) {
  HArena *arena = h_new_arena(&system_allocator, 0);

  HParsedToken *seq = h_make_seqn(arena, 1);
  HParsedToken *seq2 = h_make_seqn(arena, 2);
  HParsedToken *tok1 = h_make_uint(arena, 41);
  HParsedToken *tok2 = h_make_uint(arena, 42);

  seq->seq->elements[0] = seq2;
  seq->seq->used = 1;
  seq2->seq->elements[0] = tok1;
  seq2->seq->elements[1] = tok2;
  seq2->seq->used = 2;

  g_check_cmp_int(h_seq_index_path(seq, 0, -1)->token_type, ==, TT_SEQUENCE);
  g_check_cmp_int(h_seq_index_path(seq, 0, 0, -1)->token_type, ==, TT_UINT);
  g_check_cmp_int64(h_seq_index_path(seq, 0, 0, -1)->uint, ==, 41);
  g_check_cmp_int64(h_seq_index_path(seq, 0, 1, -1)->uint, ==, 42);
}

#define MK_INPUT_STREAM(buf,len,endianness_)  \
  {					      \
      .input = (uint8_t*)buf,		      \
      .length = len,			      \
      .index = 0,			      \
      .bit_offset = 0,			      \
      .endianness = endianness_		      \
  }

static void test_read_bits_48(void) {
  {
    HInputStream is = MK_INPUT_STREAM("\x12\x34\x56\x78\x9A\xBC", 6, BIT_LITTLE_ENDIAN | BYTE_LITTLE_ENDIAN);
    g_check_cmp_int64(h_read_bits(&is, 32, false), ==, 0x78563412);
    g_check_cmp_int64(h_read_bits(&is, 16, false), ==, 0xBC9A);
  }
  {
    HInputStream is = MK_INPUT_STREAM("\x12\x34\x56\x78\x9A\xBC", 6, BIT_LITTLE_ENDIAN | BYTE_LITTLE_ENDIAN);
    g_check_cmp_int64(h_read_bits(&is, 31, false), ==, 0x78563412);
    g_check_cmp_int64(h_read_bits(&is, 17, false), ==, 0x17934);
  }
  {
    HInputStream is = MK_INPUT_STREAM("\x12\x34\x56\x78\x9A\xBC", 6, BIT_LITTLE_ENDIAN | BYTE_LITTLE_ENDIAN);
    g_check_cmp_int64(h_read_bits(&is, 33, false), ==, 0x78563412);
    g_check_cmp_int64(h_read_bits(&is, 17, false), ==, 0x5E4D);
  }
  {
    HInputStream is = MK_INPUT_STREAM("\x12\x34\x56\x78\x9A\xBC", 6, BIT_LITTLE_ENDIAN | BYTE_LITTLE_ENDIAN);
    g_check_cmp_int64(h_read_bits(&is, 36, false), ==, 0xA78563412);
    g_check_cmp_int64(h_read_bits(&is, 12, false), ==, 0xBC9);
  }
  {
    HInputStream is = MK_INPUT_STREAM("\x12\x34\x56\x78\x9A\xBC", 6, BIT_LITTLE_ENDIAN | BYTE_LITTLE_ENDIAN);
    g_check_cmp_int64(h_read_bits(&is, 40, false), ==, 0x9A78563412);
    g_check_cmp_int64(h_read_bits(&is, 8, false), ==, 0xBC);
  }
  {
    HInputStream is = MK_INPUT_STREAM("\x12\x34\x56\x78\x9A\xBC", 6, BIT_LITTLE_ENDIAN | BYTE_LITTLE_ENDIAN);
    g_check_cmp_int64(h_read_bits(&is, 48, false), ==, 0xBC9A78563412);
  }
}

static void test_llk_zero_end(void) {
    HParserBackend be = PB_LLk;
    HParser *z = h_ch('\x00');
    HParser *az = h_sequence(h_ch('a'), z, NULL);
    HParser *ze = h_sequence(z, h_end_p(), NULL);
    HParser *aze = h_sequence(h_ch('a'), z, h_end_p(), NULL);

    // some cases surrounding the bug
    g_check_parse_match (z, be, "\x00", 1, "u0");
    g_check_parse_failed(z, be, "", 0);
    g_check_parse_match (ze, be, "\x00", 1, "(u0)");
    g_check_parse_failed(ze, be, "\x00b", 2);
    g_check_parse_failed(ze, be, "", 0);
    g_check_parse_match (az, be, "a\x00", 2, "(u0x61 u0)");
    g_check_parse_match (aze, be, "a\x00", 2, "(u0x61 u0)");
    g_check_parse_failed(aze, be, "a\x00b", 3);

    // the following should not parse but did when the LL(k) backend failed to
    // check for the end of input, mistaking it for a zero character.
    g_check_parse_failed(az, be, "a", 1);
    g_check_parse_failed(aze, be, "a", 1);
}

HParser *k_test_wrong_bit_length(HAllocator *mm__, const HParsedToken *tok, void *env)
{
    return h_ch__m(mm__, 'b');
}

static void test_wrong_bit_length(void) {
    HParseResult *r;
    HParser *p;

    p = h_right(h_ch('a'), h_ch('b'));
    r = h_parse(p, (const uint8_t *)"ab", 2);
    g_check_cmp_int64(r->bit_length, ==, 16);
    h_parse_result_free(r);

    p = h_bind(h_ch('a'), k_test_wrong_bit_length, NULL);
    r = h_parse(p, (const uint8_t *)"ab", 2);
    g_check_cmp_int64(r->bit_length, ==, 16);
    h_parse_result_free(r);
}

static void test_lalr_charset_lhs(void) {
    HParserBackend be = PB_LALR;

    HParser *p = h_many(h_choice(h_sequence(h_ch('A'), h_ch('B'), NULL),
                                 h_in((uint8_t*)"AB",2), NULL));

    // the above would abort because of an unhandled case in trying to resolve
    // a conflict where an item's left-hand-side was an HCF_CHARSET.
    // however, the compile should fail - the conflict cannot be resolved.

    if(h_compile(p, be, NULL) == 0) {
        g_test_message("LALR compile didn't detect ambiguous grammar");

        // it says it compiled it - well, then it should parse it!
        // (this helps us see what it thinks it should be doing.)
        g_check_parse_match(p, be, "AA",2, "(u0x41 u0x41)");
        g_check_parse_match(p, be, "AB",2, "((u0x41 u0x42))");

        g_test_fail();
        return;
    }
}

static void test_cfg_many_seq(void) {
    HParser *p = h_many(h_sequence(h_ch('A'), h_ch('B'), NULL));

    g_check_parse_match(p, PB_LLk,  "ABAB",4, "((u0x41 u0x42) (u0x41 u0x42))");
    g_check_parse_match(p, PB_LALR, "ABAB",4, "((u0x41 u0x42) (u0x41 u0x42))");
    g_check_parse_match(p, PB_GLR,  "ABAB",4, "((u0x41 u0x42) (u0x41 u0x42))");
    // these would instead parse as (u0x41 u0x42 u0x41 u0x42) due to a faulty
    // reshape on h_many.
}

static uint8_t test_charset_bits__buf[256];
static void *test_charset_bits__alloc(HAllocator *allocator, size_t size)
{
    g_check_cmp_uint64(size, ==, 256/8);
    assert(size <= 256);
    return test_charset_bits__buf;
}
static void test_charset_bits(void) {
    // charset would allocate 256 bytes instead of 256 bits (= 32 bytes)

    HAllocator alloc = {
        .alloc = test_charset_bits__alloc,
        .realloc = NULL,
        .free = NULL,
    };
    test_charset_bits__buf[32] = 0xAB;
    new_charset(&alloc);
    for(size_t i=0; i<32; i++)
        g_check_cmp_uint32(test_charset_bits__buf[i], ==, 0);
    g_check_cmp_uint32(test_charset_bits__buf[32], ==, 0xAB);
}


// Allocator for reproducing error 19.

// The bug is a result of uninitialized data being used, initially
// assumed to be zero.  Unfortunately, this assumption is often true,
// so reproducing the bug reliably and in a minimal fashion requires
// making it false.  Fortunately, glibc malloc has an M_PERTURB option
// for making that assumption false.  Unfortunately, we want the test
// to reproduce the bug on systems that don't use glibc.  Fortunately,
// the standard Hammer system allocator has a DEBUG__MEMFILL option to
// fill uninitialized memory with a fill byte.  Unfortunately, you
// have to recompile Hammer with that symbol #defined in order to
// enable it.  Fortunately, hammer allows you to supply your own
// allocator.  So this is a simple non-#define-dependent allocator
// that writes 0xbabababa† over all the memory it allocates.  (But not
// the memory it reallocs, because, as it happens, the uninitialized
// memory in this case didn't come from a realloc.)
//
// Honestly I think we ought to remove the #ifdefs from
// system_allocator and always compile both the DEBUG__MEMFILL version
// and the non-DEBUG__MEMFILL version, merely changing which one is
// system_allocator, which is after all a struct of three pointers
// that can even be modified at run-time.
//
// † Can you hear it, Mr. Toot?

static void* deadbeefing_malloc(HAllocator *allocator, size_t size) {
    char *block = malloc(size);
    if (block) memset(block, 0xba, size);
    return block;
}

// Don't deadbeef on realloc because it isn't necessary to reproduce this bug.
static void* deadbeefing_realloc(HAllocator *allocator, void *uptr, size_t size) {
    return realloc(uptr, size);
}

static void deadbeefing_free(HAllocator *allocator, void *uptr) {
    free(uptr);
}

static HAllocator deadbeefing_allocator = {
    .alloc = deadbeefing_malloc,
    .realloc = deadbeefing_realloc,
    .free = deadbeefing_free,
};

static void test_bug_19() {
    void *args[] = {
        h_ch_range__m(&deadbeefing_allocator, '0', '9'),
        h_ch_range__m(&deadbeefing_allocator, 'A', 'Z'),
        h_ch_range__m(&deadbeefing_allocator, 'a', 'z'),
        NULL,
    };

    HParser *parser = h_choice__ma(&deadbeefing_allocator, args);

    // In bug 19 ("GLR backend reaches unreachable code"), this call
    // would fail because h_choice__ma allocated an HParser with h_new
    // and didn't initialize its ->desugared field; consequently in
    // the call chain h_compile ... h_lalr_compile ... h_desugar,
    // h_desugar would find that ->desugared was already non-NULL (set
    // to 0xbabababa in the above deadbeefing_malloc), and just return
    // it, leading to a crash immediately afterwards in collect_nts.
    // We don't actually care if the compile succeeds or fails, just
    // that it doesn't crash.
    h_compile(parser, PB_GLR, NULL);

    // The same bug happened in h_sequence__ma.
    h_compile(h_sequence__ma(&deadbeefing_allocator, args), PB_GLR, NULL);

    // It also exists in h_permutation__ma, but it doesn't happen to
    // manifest in the same way.  I don't know how to write a test for
    // the h_permutation__ma case.
    g_assert_true(1);
}

static void test_flatten_null() {
  // h_act_flatten() produces a flat sequence from a nested sequence. it also
  // hapens to produce a one-element sequence when given a non-sequence token.
  // but given a null token (as from h_epsilon_p() or h_ignore()), it would
  // previously segfault.
  //
  // let's make sure the behavior is consistent and a singular null token
  // produces the same thing as a sequence around h_epsilon_p() or h_ignore().

  HParser *A = h_many(h_ch('a'));
  HParser *B = h_ch('b');
  HParser *C = h_sequence(h_ch('c'), NULL);

  HParser *V = h_action(h_epsilon_p(), h_act_flatten, NULL);
  HParser *W = h_action(B, h_act_flatten, NULL);
  HParser *X = h_action(h_sequence(h_ignore(A), NULL), h_act_flatten, NULL);
  HParser *Y = h_action(h_sequence(h_epsilon_p(), NULL), h_act_flatten, NULL);
  HParser *Z = h_action(h_sequence(A, B, C, NULL), h_act_flatten, NULL);

  g_check_parse_match(V, PB_PACKRAT, "", 0, "()");
  g_check_parse_match(W, PB_PACKRAT, "b", 1, "(u0x62)");
  g_check_parse_match(X, PB_PACKRAT, "", 0, "()");
  g_check_parse_match(Y, PB_PACKRAT, "", 0, "()");
  g_check_parse_match(Z, PB_PACKRAT, "aabc", 4, "(u0x61 u0x61 u0x62 u0x63)");

#if 0 // XXX ast->bit_length and ast->index are currently not set
  // let's also check that position and length info get attached correctly...

  HParseResult *p = h_parse(h_sequence(A,V,B, NULL), (uint8_t *)"aaab", 4);

  // top-level token
  assert(p != NULL);
  assert(p->ast != NULL);
  g_check_cmp_int64(p->bit_length, ==, 32);
  g_check_cmp_size(p->ast->bit_length, ==, 32);
  g_check_cmp_size(p->ast->index, ==, 0);
  g_check_cmp_int((int)p->ast->bit_offset, ==, 0);

  // the empty sequence
  HParsedToken *tok = H_INDEX_TOKEN(p->ast, 1);
  assert(tok != NULL);
  assert(tok->token_type == TT_SEQUENCE);
  assert(tok->seq->used == 0);
  g_check_cmp_size(tok->bit_length, ==, 0);
  g_check_cmp_size(tok->index, ==, 2);
  g_check_cmp_int((int)tok->bit_offset, ==, 0);
#endif // 0
}

#if 0 // XXX ast->bit_length and ast->index are currently not set
static void test_ast_length_index() {
  HParser *A = h_many(h_ch('a'));
  HParser *B = h_ch('b');
  HParser *C = h_sequence(h_ch('c'), NULL);

  const uint8_t input[] = "aabc";
  size_t len = sizeof input - 1; // sans null
  HParseResult *p = h_parse(h_sequence(A,B,C, NULL), input, len);
  assert(p != NULL);
  assert(p->ast != NULL);

  // top-level token
  g_check_cmp_int64(p->bit_length, ==, (int64_t)(8 * len));
  g_check_cmp_size(p->ast->bit_length, ==, 8 * len);
  g_check_cmp_size(p->ast->index, ==, 0);

  HParsedToken *tok;

  // "aa"
  tok = H_INDEX_TOKEN(p->ast, 0);
  g_check_cmp_size(tok->bit_length, ==, 16);
  g_check_cmp_size(tok->index, ==, 0);

  // "a", "a"
  tok = H_INDEX_TOKEN(p->ast, 0, 0);
  g_check_cmp_size(tok->bit_length, ==, 8);
  g_check_cmp_size(tok->index, ==, 0);
  tok = H_INDEX_TOKEN(p->ast, 0, 1);
  g_check_cmp_size(tok->bit_length, ==, 8);
  g_check_cmp_size(tok->index, ==, 1);

  // "b"
  tok = H_INDEX_TOKEN(p->ast, 1);
  g_check_cmp_size(tok->bit_length, ==, 8);
  g_check_cmp_size(tok->index, ==, 2);

  // "c"
  tok = H_INDEX_TOKEN(p->ast, 2);
  g_check_cmp_size(tok->bit_length, ==, 8);
  g_check_cmp_size(tok->index, ==, 3);
  tok = H_INDEX_TOKEN(p->ast, 2, 0);
  g_check_cmp_size(tok->bit_length, ==, 8);
  g_check_cmp_size(tok->index, ==, 3);
}
#endif // 0

static void test_issue91() {
  // this ambiguous grammar caused intermittent (?) assertion failures when
  // trying to compile with the LALR backend:
  //
  // assertion "action->type == HLR_SHIFT" failed: file "src/backends/lalr.c",
  // line 34, function "follow_transition"
  //
  // cf. https://gitlab.special-circumstanc.es/hammer/hammer/issues/91

  H_RULE(schar,   h_ch_range(' ', '~'));    /* overlaps digit */
  H_RULE(digit,   h_ch_range('0', '9'));
  H_RULE(digits,  h_choice(h_repeat_n(digit, 2), digit, NULL));
  H_RULE(p,       h_many(h_choice(schar, digits, NULL)));

  int r = h_compile(p, PB_LALR, NULL);
  g_check_cmp_int(r, ==, -2);
}

// a different instance of issue 91
static void test_issue87() {
  HParser *a = h_ch('a');
  HParser *a2 = h_ch_range('a', 'a');
  HParser *p = h_many(h_many(h_choice(a, a2, NULL)));

  int r = h_compile(p, PB_LALR, NULL);
  g_check_cmp_int(r, ==, -2);
}

static void test_issue92() {
  HParser *a = h_ch('a');
  HParser *b = h_ch('b');

  HParser *str_a  = h_indirect();
  HParser *str_b  = h_choice(h_sequence(b, str_a, NULL), str_a, NULL);
                    //h_sequence(h_optional(b), str_a, NULL);  // this works
  HParser *str_a_ = h_optional(h_sequence(a, str_b, NULL));
  HParser *str    = str_a;
  h_bind_indirect(str_a, str_a_);
  /*
   * grammar generated from the above:
   *
   *   A -> B           -- "augmented" with a fresh start symbol
   *   B -> C           -- B = str_a
   *      | ""
   *   C -> "a" D       -- C = h_sequence(a, str_b)
   *   D -> E           -- D = str_b
   *      | B
   *   E -> "b" B       -- E = h_sequence(b, str_a)
   *
   * transformed to the following "enhanced grammar":
   *
   *    S  -> 0B3
   *   0B3 -> 0C2
   *        | ""
   *   1B4 -> 1C2
   *        | ""
   *   6B8 -> 6C2
   *        | ""           (*) here
   *   0C2 -> "a" 1D7
   *   1C2 -> "a" 1D7
   *   6C2 -> "a" 1D7
   *   1D7 -> 1E5
   *        | 1B4
   *   1E5 -> "b" 6B8
   */

  /*
   * the following call would cause an assertion failure.
   *
   * assertion "!h_stringmap_empty(fs)" failed: file
   * "src/backends/lalr.c", line 341, function "h_lalr_compile"
   *
   * the bug happens when trying to compute h_follow() for 6B8 in state 6,
   * production "" (*). intermediate results could end up in the memoization
   * table and be treated as final by later calls to h_follow(). the problem
   * could appear or not depending on the order of nonterminals (i.e. pointers)
   * in a hashtable.
   */
  int r = h_compile(str, PB_LALR, NULL);
  g_check_cmp_int(r, ==, 0);
}

static void test_issue83() {
  HParser *p = h_sequence(h_sequence(NULL, NULL), h_nothing_p(), NULL);
  /*
   * A -> B
   * B -> C D
   * C -> ""
   * D -x
   *
   * (S) -> 0B1
   * 0B1 -> 0C2 2D3
   * 0C2 -> ""           (*) h_follow()
   * 2D3 -x
   */

  /*
   * similar to issue 91, this would cause the same assertion failure, but for
   * a different reason. the follow set of 0C2 above is equal to the first set
   * of 2D3, but 2D3 is an empty choice. The first set of an empty choice
   * is legitimately empty. the asserting in h_lalr_compile() missed this case.
   */
  int r = h_compile(p, PB_LALR, NULL);
  g_check_cmp_int(r, ==, 0);
}

/*
 * This is Meg's cut-down bug 60 test case
 */

static void test_bug60() {
    /* There is probably an even smaller example that shows the issue */

	  HParser *zed = NULL;
	  HParser *alpha = NULL;
	  HParser *vchar = NULL;
	  HParser *why = NULL;
	  HParser *plural_zed = NULL;
	  HParser *plural_zed_zed = NULL;
	  HParser *a_to_zed = NULL;
	  HParser *alphas = NULL;
	  HParser *rule = NULL;
	  HParser *rulelist = NULL;
	  HParser *p = NULL;
	  HParseResult *r = NULL;
	  int n;

	  zed =  h_ch('z');

	  vchar = h_ch_range(0x79, 0x7a); /* allows y and z */

	  alpha = h_ch('a');

	  why = h_ch('y');

	  plural_zed = h_sequence(
	      why,
	      h_many(h_choice(alpha, vchar, NULL)),
	      NULL);
	  plural_zed_zed = h_choice(plural_zed, zed, NULL);
	  alphas = h_choice(alpha, h_sequence(plural_zed_zed, alpha, NULL), NULL);

	  a_to_zed = h_sequence(
	      zed,
	      h_many(h_sequence(h_many1(alphas), zed, NULL)),
	      NULL);
	  rule = h_sequence(a_to_zed, plural_zed_zed, NULL);
	  rulelist = h_many1(h_choice(
	      rule,
	      h_sequence(h_many(alphas), plural_zed_zed, NULL),
	      NULL));

	  p = rulelist;

	  g_check_parse_ok(p, PB_GLR, "ayzza", 5);
	  g_check_parse_match(p, PB_GLR, "ayzza", 5, "(((u0x61) (u0x79 (u0x7a u0x7a u0x61))))");

}

/*
 * This is the original bug60 test case; cut down from an ABNF parser
 */

#define BUG60_ABNF_SCAN_UP_TO 64

static void test_bug60_abnf() {
  HParser *newline = NULL;
  HParser *alpha = NULL;
  HParser *sp = NULL;
  HParser *vchar = NULL;
  HParser *equal = NULL;
  HParser *semicolon = NULL;
  HParser *comment = NULL;
  HParser *c_nl = NULL;
  HParser *c_sp = NULL;
  HParser *defined_as = NULL;
  HParser *alphas = NULL;
  HParser *rule = NULL;
  HParser *rulelist = NULL;
  HParser *p = NULL;
  int i, j, r, s_size;
  char *s = NULL;
  const char *test_string_template = "x = y z%s;%s\n\n";
  char buf_1[BUG60_ABNF_SCAN_UP_TO+1];
  char buf_2[2*BUG60_ABNF_SCAN_UP_TO+1];

  newline = h_ch('\n');
  alpha = h_choice(h_ch_range('A', 'Z'), h_ch_range('a', 'z'), NULL);
  sp = h_ch(' ');
  vchar = h_ch_range(0x21, 0x7e);
  equal = h_ch('=');
  semicolon = h_ch(';');
  comment = h_sequence(
    semicolon,
    h_many(h_choice(sp, vchar, NULL)),
    newline,
    NULL);
  c_nl = h_choice(comment, newline, NULL);
  c_sp = h_choice(sp, h_sequence(c_nl, sp, NULL), NULL);
  defined_as = h_sequence(h_many(c_sp), equal, h_many(c_sp), NULL);
  alphas = h_sequence(
    alpha,
    h_many(h_sequence(h_many1(c_sp), alpha, NULL)),
    h_many(c_sp),
    NULL);
  rule = h_sequence(alpha, defined_as, alphas, c_nl, NULL);
  rulelist = h_many1(h_choice(
    rule,
    h_sequence(h_many(c_sp), c_nl, NULL),
    NULL));

  p = rulelist;
  g_check_compilable(p, PB_GLR, 1);

  /* Have a buffer for the string */
  s_size = strlen(test_string_template) + 3*BUG60_ABNF_SCAN_UP_TO + 1;
  s = malloc(s_size);
  g_check_cmp_ptr(s, !=, NULL);

  /*
   * Try to parse all the different strings according to the template up to
   * the scan limit.
   *
   * Correct behavior: it parses for all values of i, j.
   * Bugged behavior: when i % 3 != 0, parse failures begin to occur at
   * j == (i / 3) + (i % 3).
   */
  for (i = 0; i < BUG60_ABNF_SCAN_UP_TO; ++i) {
    memset(buf_1, ' ', i);
    buf_1[i] = 0;
    for (j = 0; j < 2*BUG60_ABNF_SCAN_UP_TO; ++j) {
      memset(buf_2, 'x', j);
      buf_2[j] = 0;
      snprintf(s, s_size, test_string_template, buf_1, buf_2);
      g_check_parse_ok_no_compile(p, s, strlen(s));
    }
  }

  free(s);
}

void register_regression_tests(void) {
  g_test_add_func("/core/regression/bug118", test_bug118);
  g_test_add_func("/core/regression/seq_index_path", test_seq_index_path);
  g_test_add_func("/core/regression/read_bits_48", test_read_bits_48);
  g_test_add_func("/core/regression/llk_zero_end", test_llk_zero_end);
  g_test_add_func("/core/regression/wrong_bit_length", test_wrong_bit_length);
  g_test_add_func("/core/regression/lalr_charset_lhs", test_lalr_charset_lhs);
  g_test_add_func("/core/regression/cfg_many_seq", test_cfg_many_seq);
  g_test_add_func("/core/regression/charset_bits", test_charset_bits);
  g_test_add_func("/core/regression/bug19", test_bug_19);
  g_test_add_func("/core/regression/flatten_null", test_flatten_null);
  //XXX g_test_add_func("/core/regression/ast_length_index", test_ast_length_index);
  g_test_add_func("/core/regression/issue91", test_issue91);
  g_test_add_func("/core/regression/issue87", test_issue87);
  g_test_add_func("/core/regression/issue92", test_issue92);
  g_test_add_func("/core/regression/issue83", test_issue83);
  g_test_add_func("/core/regression/bug60", test_bug60);
  g_test_add_func("/core/regression/bug60_abnf", test_bug60_abnf);
}
