#include "hammer.h"
#include "glue.h"
#include "internal.h"
#include "test_suite.h"

#include <glib.h>
#include <limits.h>

// Test seek.c: default case in switch (lines 35-36)
static void test_seek_default_case(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *seek = h_seek(0, SEEK_SET);

    typedef struct {
        ssize_t offset;
        int whence;
    } HSeek;

    HSeek *seek_env = (HSeek *)seek->env;
    if (seek_env) {
        seek_env->whence = 999;
        h_compile(seek, be, NULL);
        HParseResult *res = h_parse(seek, (const uint8_t *)"abc", 3);
        g_check_cmp_ptr(res, ==, NULL);
    }
}

// Test seek.c: underflow check (line 41)
static void test_seek_underflow(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *seek_underflow = h_seek(-1, SEEK_SET);
    h_compile(seek_underflow, be, NULL);
    HParseResult *res = h_parse(seek_underflow, (const uint8_t *)"abc", 3);
    g_check_cmp_ptr(res, ==, NULL);
}

// Test seek.c: overflow check (line 43)
static void test_seek_overflow(gconstpointer backend) {
    HParserBackend be = (HParserBackend)GPOINTER_TO_INT(backend);
    HParser *seek = h_seek(1, SEEK_END);

    typedef struct {
        ssize_t offset;
        int whence;
    } HSeek;

    HSeek *seek_env = (HSeek *)seek->env;
    if (seek_env) {
        seek_env->offset = (ssize_t)(SIZE_MAX / 2 - 1);
        seek_env->whence = SEEK_END;
        h_compile(seek, be, NULL);
        HParseResult *res = h_parse(seek, (const uint8_t *)"abc", 3);
        (void)res;
    }
}

void register_seek_tests(void) {
    g_test_add_data_func("/core/parser/packrat/seek_default_case", GINT_TO_POINTER(PB_PACKRAT),
                         test_seek_default_case);
    g_test_add_data_func("/core/parser/packrat/seek_underflow", GINT_TO_POINTER(PB_PACKRAT),
                         test_seek_underflow);
    g_test_add_data_func("/core/parser/packrat/seek_overflow", GINT_TO_POINTER(PB_PACKRAT),
                         test_seek_overflow);
}
