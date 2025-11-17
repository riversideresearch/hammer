#include "hammer.h"
#include "test_suite.h"

#include <glib.h>
#include <stdio.h>

HParserTestcase testcases[] = {{(unsigned char *)"1,2,3", 5, "(u0x31 u0x32 u0x33)"},
                               {(unsigned char *)"1,3,2", 5, "(u0x31 u0x33 u0x32)"},
                               {(unsigned char *)"1,3", 3, "(u0x31 u0x33)"},
                               {(unsigned char *)"3", 1, "(u0x33)"},
                               {NULL, 0, NULL}};

static void test_benchmark_1() {
    HParser *parser = h_sepBy1(h_choice(h_ch('1'), h_ch('2'), h_ch('3'), NULL), h_ch(','));

    HBenchmarkResults *res = h_benchmark(parser, testcases);
    g_check_cmp_ptr(res, !=, NULL);
    h_benchmark_report(stderr, res);
    // Free the results (if there's a free function, otherwise just check it doesn't crash)
}

static void test_benchmark_m() {
    HParser *parser = h_ch('x');
    HParserTestcase simple_cases[] = {{(unsigned char *)"x", 1, "u0x78"}, {NULL, 0, NULL}};
    
    HBenchmarkResults *res = h_benchmark__m(&system_allocator, parser, simple_cases);
    g_check_cmp_ptr(res, !=, NULL);
    
    // Test h_benchmark_report with results
    FILE *tmp = tmpfile();
    h_benchmark_report(tmp, res);
    fclose(tmp);
}

void register_benchmark_tests(void) {
    g_test_add_func("/core/benchmark/1", test_benchmark_1);
    g_test_add_func("/core/benchmark/m", test_benchmark_m);
}
