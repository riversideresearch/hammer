#include "glue.h"
#include "hammer.h"
#include "internal.h"
#include "parsers/parser_internal.h"
#include "test_suite.h"

#include <float.h>
#include <glib.h>
#include <math.h>
#include <stdint.h>

static void test_float16(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    const HParser *p = h_float16();
    const uint8_t input[2] = {0x3E, 0x00}; // 1.5
    HParseResult *result = h_parse(p, input, 2);
    float pi = result->ast->token_data.flt;
    g_check_parse_match(p, be, input, 2, "f0x1.8p+0");
}

static void test_float32(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    const HParser *p = h_float32();
    const uint8_t input[4] = {0x40, 0x49, 0x0F, 0xDB}; // pi
    HParseResult *result = h_parse(p, input, 4);
    float pi = result->ast->token_data.flt;
    g_check_parse_match(p, be, input, 4, "f0x1.921fb6p+1");
}

static void test_double64(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    const HParser *p = h_float64();
    const uint8_t input[8] = {0x40, 0x09, 0x21, 0xFB, 0x54, 0x44, 0x2D, 0x18}; // pi
    HParseResult *result = h_parse(p, input, 8);
    double pi = result->ast->token_data.dbl;
    g_check_parse_match(p, be, input, 8, "d0x1.921fb54442d18p+1");
}

static void test_float16_edgecases(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    const HParser *p = h_float16();
    HParseResult *result;
    float v;

    /* +Infinity (float16 0x7C00) */
    const uint8_t inf16[2] = {0x7C, 0x00};
    result = h_parse(p, inf16, 2);
    v = result->ast->token_data.flt;
    g_assert_true(isinf(v));
    g_assert_true(v > 0);

    /* -Infinity (float16 0xFC00) */
    const uint8_t ninf16[2] = {0xFC, 0x00};
    result = h_parse(p, ninf16, 2);
    v = result->ast->token_data.flt;
    g_assert_true(isinf(v));
    g_assert_true(v < 0);

    /* Quiet NaN (float16 0x7E00) */
    const uint8_t nan16[2] = {0x7E, 0x01};
    result = h_parse(p, nan16, 2);
    v = result->ast->token_data.flt;
    g_assert_true(isnan(v));

    /* zero (float16 0x0000) */
    const uint8_t zero16[2] = {0x00, 0x00};
    result = h_parse(p, zero16, 2);
    v = result->ast->token_data.flt;
    g_assert_cmpfloat(v, ==, 0.0f);

    /* Negative zero (float16 0x8000) */
    const uint8_t negzero16[2] = {0x80, 0x00};
    result = h_parse(p, negzero16, 2);
    v = result->ast->token_data.flt;
    g_check_cmpdouble(v, ==, 0.0f);
    g_assert_true(signbit(v));

    /* Smallest positive subnormal (float16 0x0001) */
    const uint8_t subnorm16[2] = {0x00, 0x01};
    result = h_parse(p, subnorm16, 2);
    v = result->ast->token_data.flt;
    g_assert_cmpfloat(v, ==, 0.000000059604644775390625f);
    g_assert_true(v > 0.0f);

    /* Largest finite (float16 0x7BFF) */
    const uint8_t maxfin16[2] = {0x7B, 0xFF};
    result = h_parse(p, maxfin16, 2);
    v = result->ast->token_data.flt;
    g_assert_true(isfinite(v));
}

static void test_float32_edgecases(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    const HParser *p = h_float32();
    HParseResult *result;
    float v;

    /* +Infinity (0x7F800000) */
    const uint8_t inf32[4] = {0x7F, 0x80, 0x00, 0x00};
    result = h_parse(p, inf32, 4);
    v = result->ast->token_data.flt;
    g_assert_true(isinf(v));
    g_assert_true(v > 0);

    /* -Infinity (0xFF800000) */
    const uint8_t ninf32[4] = {0xFF, 0x80, 0x00, 0x00};
    result = h_parse(p, ninf32, 4);
    v = result->ast->token_data.flt;
    g_assert_true(isinf(v));
    g_assert_true(v < 0);

    /* Quiet NaN (0x7FC00000) */
    const uint8_t nan32[4] = {0x7F, 0xC0, 0x00, 0x00};
    result = h_parse(p, nan32, 4);
    v = result->ast->token_data.flt;
    g_assert_true(isnan(v));

    /* zero (0x00000000) */
    const uint8_t zero32[4] = {0x00, 0x00, 0x00, 0x00};
    result = h_parse(p, zero32, 4);
    v = result->ast->token_data.flt;
    g_check_cmpfloat(v, ==, 0.0f);

    /* Negative zero (0x80000000) */
    const uint8_t negzero32[4] = {0x80, 0x00, 0x00, 0x00};
    result = h_parse(p, negzero32, 4);
    v = result->ast->token_data.flt;
    g_check_cmpdouble(v, ==, 0.0f);
    g_assert_true(signbit(v));

    /* Smallest positive subnormal (0x00000001) */
    const uint8_t subnorm32[4] = {0x00, 0x00, 0x00, 0x01};
    result = h_parse(p, subnorm32, 4);
    v = result->ast->token_data.flt;
    g_assert_true(v > 0.0f);
    g_assert_cmpfloat(v, ==, 0.0000000000000000000000000000000000000000000014012984643f);

    /* Largest finite (0x7F7FFFFF) */
    const uint8_t maxfin32[4] = {0x7F, 0x7F, 0xFF, 0xFF};
    result = h_parse(p, maxfin32, 4);
    v = result->ast->token_data.flt;
    g_assert_true(isfinite(v));
    g_check_cmpfloat(v, <=, FLT_MAX);
}

static void test_double64_edgecases(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    const HParser *p = h_float64();
    HParseResult *result;
    double v;

    /* +Infinity (0x7FF0000000000000) */
    const uint8_t inf64[8] = {0x7F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    result = h_parse(p, inf64, 8);
    v = result->ast->token_data.dbl;
    g_assert_true(isinf(v));
    g_assert_true(v > 0);

    /* -Infinity (0xFFF0000000000000) */
    const uint8_t ninf64[8] = {0xFF, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    result = h_parse(p, ninf64, 8);
    v = result->ast->token_data.dbl;
    g_assert_true(isinf(v));
    g_assert_true(v < 0);

    /* Quiet NaN (0x7FF8000000000000) */
    const uint8_t nan64[8] = {0x7F, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    result = h_parse(p, nan64, 8);
    v = result->ast->token_data.dbl;
    g_assert_true(isnan(v));

    /* zero (0x0000000000000000) */
    const uint8_t zero64[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    result = h_parse(p, zero64, 8);
    v = result->ast->token_data.dbl;
    g_check_cmpdouble(v, ==, 0.0);

    /* Negative zero (0x8000000000000000) */
    const uint8_t negzero64[8] = {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    result = h_parse(p, negzero64, 8);
    v = result->ast->token_data.dbl;
    g_check_cmpdouble(v, ==, 0.0);
    g_assert_true(signbit(v));

    /* Smallest positive subnormal (0x0000000000000001) */
    const uint8_t subnorm64[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    result = h_parse(p, subnorm64, 8);
    v = result->ast->token_data.dbl;
    g_assert_true(v > 0.0);

    /* Largest finite (0x7FEFFFFFFFFFFFFF) */
    const uint8_t maxfin64[8] = {0x7F, 0xEF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    result = h_parse(p, maxfin64, 8);
    v = result->ast->token_data.dbl;
    g_assert_true(isfinite(v));
    g_check_cmpdouble(v, <=, DBL_MAX);
}

static void test_float_truncated(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);

    const HParser *p16 = h_float16();
    const uint8_t short16[1] = {0x3c};
    g_check_parse_failed(p16, be, short16, sizeof short16);

    const HParser *p32 = h_float32();
    const uint8_t short32[3] = {0x3f, 0x80, 0x00};
    g_check_parse_failed(p32, be, short32, sizeof short32);

    const HParser *p64 = h_float64();
    const uint8_t short64[7] = {0x3f, 0xf0, 0, 0, 0, 0, 0};
    g_check_parse_failed(p64, be, short64, sizeof short64);
}

// Merged test_floats.c
// Helper function for double parser test
static HParsedToken *act_double(const HParseResult *p, void *u) {
    return H_MAKE_DOUBLE((double)H_FIELD_UINT(0) + (double)H_FIELD_UINT(1) / 10);
}

static void test_make_double(gconstpointer backend) {
    HParser *b = h_uint8();
    HParser *dbl = h_action(h_sequence(b, b, NULL), act_double, NULL);
    uint8_t input[] = {4, 2};
    g_check_parse_match(dbl, (HParserBackend)GPOINTER_TO_INT(backend), input, 2,
                        "d0x1.0cccccccccccdp+2");
}

// Helper function for float parser test
static HParsedToken *act_float(const HParseResult *p, void *u) {
    return H_MAKE_FLOAT((float)H_FIELD_UINT(0) + (float)H_FIELD_UINT(1) / 10);
}

static void test_make_float(gconstpointer backend) {
    HParser *b = h_uint8();
    HParser *flt = h_action(h_sequence(b, b, NULL), act_float, NULL);
    uint8_t input[] = {4, 2};
    g_check_parse_match(flt, (HParserBackend)GPOINTER_TO_INT(backend), input, 2, "f0x1.0cccccp+2");
}

void register_floating_point_parser_tests(void) {
    g_test_add_data_func("/core/parser/float/float16", GINT_TO_POINTER(PB_PACKRAT), test_float16);
    g_test_add_data_func("/core/parser/float/float32", GINT_TO_POINTER(PB_PACKRAT), test_float32);
    g_test_add_data_func("/core/parser/float/double64", GINT_TO_POINTER(PB_PACKRAT), test_double64);
    g_test_add_data_func("/core/parser/float/float16-edgecases", GINT_TO_POINTER(PB_PACKRAT),
                         test_float16_edgecases);
    g_test_add_data_func("/core/parser/float/float32-edgecases", GINT_TO_POINTER(PB_PACKRAT),
                         test_float32_edgecases);
    g_test_add_data_func("/core/parser/float/double64-edgecases", GINT_TO_POINTER(PB_PACKRAT),
                         test_double64_edgecases);
    g_test_add_data_func("/core/parser/float/truncated", GINT_TO_POINTER(PB_PACKRAT),
                         test_float_truncated);
    g_test_add_data_func("/core/parser/packrat/make_double", GINT_TO_POINTER(PB_PACKRAT),
                         test_make_double);
    g_test_add_data_func("/core/parser/packrat/make_float", GINT_TO_POINTER(PB_PACKRAT),
                         test_make_float);
}
