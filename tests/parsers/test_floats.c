#include "glue.h"
#include "hammer.h"
#include "test_suite.h"

#include <glib.h>

static HParsedToken *act_double(const HParseResult *p, void *u) {
    return H_MAKE_DOUBLE((double)H_FIELD_UINT(0) + (double)H_FIELD_UINT(1) / 10);
}

static void test_double(gconstpointer backend) {
    HParser *b = h_uint8();
    HParser *dbl = h_action(h_sequence(b, b, NULL), act_double, NULL);
    uint8_t input[] = {4, 2};
    g_check_parse_match(dbl, (HParserBackend)GPOINTER_TO_INT(backend), input, 2,
                        "d0x1.0cccccccccccdp+2");
}

static HParsedToken *act_float(const HParseResult *p, void *u) {
    return H_MAKE_FLOAT((float)H_FIELD_UINT(0) + (float)H_FIELD_UINT(1) / 10);
}

static void test_float(gconstpointer backend) {
    HParser *b = h_uint8();
    HParser *flt = h_action(h_sequence(b, b, NULL), act_float, NULL);
    uint8_t input[] = {4, 2};
    g_check_parse_match(flt, (HParserBackend)GPOINTER_TO_INT(backend), input, 2, "f0x1.0cccccp+2");
}

void register_float_parser_tests(void) {
    g_test_add_data_func("/core/parser/packrat/double", GINT_TO_POINTER(PB_PACKRAT), test_double);
    g_test_add_data_func("/core/parser/packrat/float", GINT_TO_POINTER(PB_PACKRAT), test_float);
}


