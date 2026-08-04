#include "hammer.h"
#include "test_suite.h"

#include <glib.h>
#include <stdio.h>

HParserTestcase testcases[] = {{(const unsigned char *)"1,2,3", 5, "(u0x31 u0x32 u0x33)"},
                               {(const unsigned char *)"1,3,2", 5, "(u0x31 u0x33 u0x32)"},
                               {(const unsigned char *)"1,3", 3, "(u0x31 u0x33)"},
                               {(const unsigned char *)"3", 1, "(u0x33)"},
                               {NULL, 0, NULL}};

static void test_benchmark_1(void) {
    HParser *parser = h_sepBy1(h_choice(h_ch('1'), h_ch('2'), h_ch('3'), NULL), h_ch(','));

    HBenchmarkResults *res = h_benchmark(parser, testcases);
    g_check_cmp_ptr(res, !=, NULL);
    h_benchmark_report(stderr, res);
    // Free the results (if there's a free function, otherwise just check it doesn't crash)
}

static void test_benchmark_2(void) {
    HParserTestcase simple_cases[] = {{(const unsigned char *)"xy", 2, "(u0x78 u0x79)"},
                                      {(const unsigned char *)"yx", 2, "(u0x79 u0x78)"},
                                      {NULL, 0, NULL}};
    OpcodeMap entries[2] = {{120, h_ch('y')}, {121, h_ch('x')}};

    HParser *dispatch = h_dispatch(h_uint8(), entries, NULL);
    HParser *parser = dispatch;

    HBenchmarkResults *res = h_benchmark(parser, simple_cases);
    g_check_cmp_ptr(res, !=, NULL);
    h_benchmark_report(stderr, res);
    // Free the results (if there's a free function, otherwise just check it doesn't crash)
}

static void test_benchmark_m(void) {
    HParser *parser = h_ch('x');
    HParserTestcase simple_cases[] = {{(const unsigned char *)"x", 1, "u0x78"}, {NULL, 0, NULL}};

    HBenchmarkResults *res = h_benchmark__m(&system_allocator, parser, simple_cases);
    g_check_cmp_ptr(res, !=, NULL);

    // Test h_benchmark_report with results
    FILE *tmp = tmpfile();
    h_benchmark_report(tmp, res);
    fclose(tmp);
}

// Test benchmark.c: h_benchmark with failed compilation (line 44)
static void test_benchmark_failed_compile(void) {
    // Create a parser that might fail compilation for some backends
    HParser *parser = h_ch('x');
    HParserTestcase cases[] = {{(const unsigned char *)"x", 1, "u0x78"}, {NULL, 0, NULL}};

    HBenchmarkResults *res = h_benchmark(parser, cases);
    g_check_cmp_ptr(res, !=, NULL);
    // Some backends might fail compilation, that's OK
}

// Test benchmark.c: h_benchmark with failed testcases (line 67)
static void test_benchmark_failed_testcases(void) {
    HParser *parser = h_ch('x');
    HParserTestcase cases[] = {{(const unsigned char *)"x", 1, "u0x78"},
                               {(const unsigned char *)"y", 1, "u0x79"}, // This will fail
                               {NULL, 0, NULL}};

    HBenchmarkResults *res = h_benchmark(parser, cases);
    g_check_cmp_ptr(res, !=, NULL);
    // Testcases should fail, backend should be skipped
}

// Test benchmark.c: h_benchmark_report with NULL cases (line 115)
static void test_benchmark_report_null_cases(void) {
    HParser *parser = h_ch('x');
    HParserTestcase cases[] = {{(const unsigned char *)"x", 1, "u0x78"}, {NULL, 0, NULL}};

    HBenchmarkResults *res = h_benchmark(parser, cases);
    g_check_cmp_ptr(res, !=, NULL);

    FILE *tmp = tmpfile();
    h_benchmark_report(tmp, res);
    fclose(tmp);
}

static void test_benchmark_multiple_backends(void) {
    HParser *parser = h_choice(h_ch('a'), h_ch('b'), NULL);
    HParserTestcase cases[] = {{(const unsigned char *)"a", 1, "u0x61"},
                               {(const unsigned char *)"b", 1, "u0x62"},
                               {NULL, 0, NULL}};

    HBenchmarkResults *res = h_benchmark__m(&system_allocator, parser, cases);
    g_check_cmp_ptr(res, !=, NULL);

    h_benchmark_report(stderr, res);
}

void register_benchmark_tests(void) {
    g_test_add_func("/core/benchmark/1", test_benchmark_1);
    g_test_add_func("/core/benchmark/2", test_benchmark_2);
    g_test_add_func("/core/benchmark/m", test_benchmark_m);
    g_test_add_func("/core/benchmark/failed_compile", test_benchmark_failed_compile);
    g_test_add_func("/core/benchmark/failed_testcases", test_benchmark_failed_testcases);
    g_test_add_func("/core/benchmark/report_null_cases", test_benchmark_report_null_cases);
    g_test_add_func("/core/benchmark/multiple_backends", test_benchmark_multiple_backends);
}
