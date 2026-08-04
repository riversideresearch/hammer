/* Test suite for Hammer.
 * Copyright (C) 2012  Meredith L. Patterson, Dan "TQ" Hirsch
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "test_suite.h"

#include "hammer.h"

#include <glib.h>

extern void register_bitreader_tests(void);
extern void register_bitwriter_tests(void);
extern void register_parser_tests(void);
extern void register_floating_point_parser_tests(void);
extern void register_basic_parser_tests(void);
extern void register_integer_parser_tests(void);
extern void register_additional_parser_tests(void);
extern void register_parser_internal_tests(void);
extern void register_permutation_tests(void);
extern void register_seek_tests(void);
extern void register_sequence_tests(void);
extern void register_token_tests(void);
extern void register_value_tests(void);
extern void register_whitespace_tests(void);
extern void register_xor_tests(void);
extern void register_missing_tests(void);
extern void register_packrat_tests(void);
extern void register_regex_tests(void);
extern void register_trace_tests(void);
extern void register_contextfree_backend_tests(void);
extern void register_hammer_tests(void);
extern void register_glue_tests(void);
extern void register_registry_tests(void);
extern void register_desugar_tests(void);
extern void register_dispatch_tests(void);
extern void register_internal_tests(void);
extern void register_unimplemented_tests(void);
extern void register_grammar_tests(void);
extern void register_cfgrammar_tests(void);
extern void register_platform_tests(void);
extern void register_misc_tests(void);
extern void register_mm_tests(void);
extern void register_names_tests(void);
extern void register_param_k_tests(void);
extern void register_benchmark_tests(void);
extern void register_regression_tests(void);
extern void register_allocator_tests(void);
extern void register_datastructures_tests(void);
extern void register_pprint_tests(void);
extern void register_sloballoc_tests(void);
extern void register_system_allocator_tests(void);

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    // register various test suites...
    register_bitreader_tests();
    register_bitwriter_tests();
    register_parser_tests();
    register_floating_point_parser_tests();
    register_basic_parser_tests();
    register_integer_parser_tests();
    register_additional_parser_tests();
    register_parser_internal_tests();
    register_permutation_tests();
    register_seek_tests();
    register_sequence_tests();
    register_token_tests();
    register_value_tests();
    register_whitespace_tests();
    register_trace_tests();
    register_xor_tests();
    register_missing_tests();
    register_packrat_tests();
    register_hammer_tests();
    register_glue_tests();
    register_registry_tests();
    register_desugar_tests();
    register_dispatch_tests();
    register_internal_tests();
    register_unimplemented_tests();
    register_grammar_tests();
    register_cfgrammar_tests();
    register_platform_tests();
    register_misc_tests();
    register_mm_tests();
    register_names_tests();
    register_regression_tests();
    register_benchmark_tests(); // Always run for coverage
    register_allocator_tests();
    register_datastructures_tests();
    register_param_k_tests();
    register_pprint_tests();
    register_sloballoc_tests();
    register_system_allocator_tests();

    g_test_run();
}
