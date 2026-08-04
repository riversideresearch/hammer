/* Copyright (c) 2026 Riverside Research */
#include "cfgrammar.h"
#include "glue.h"
#include "hammer.h"
#include "internal.h"
#include "parsers/parser_internal.h"
#include "test_suite.h"
#include <glib.h>

#define ITERATIONS 100
#define NUM_TYPES 14
#define BODY_SIZE 1

static void test_dispatch_basic_functionality(void) {

    uint8_t buf[256];
    int buf_size = 1 + BODY_SIZE;
    buf[0] = 1; // Opcode byte

    buf[1] = (uint8_t){0x41};

    OpcodeMap entries[] = {{1, h_ch(0x41)}, {2, h_ch(0x42)}, {2013, h_uint32()}};
    HParser *discriminator = h_uint8();

    HParser *message = h_dispatch(discriminator, entries, NULL);

    HParseResult *res = h_parse(message, buf, buf_size);

    g_check_cmp_ptr(res, !=, NULL);
    g_check_cmp_ptr(res->ast, !=, NULL);
}

static void test_dispatch_incorrect_opcode(void) {

    uint8_t buf[256];
    int buf_size = 1 + BODY_SIZE;
    buf[0] = 2; // Incorrect opcode byte

    buf[1] = (uint8_t){0x41};

    OpcodeMap entries[] = {{1, h_ch(0x41)}, {2, h_ch(0x42)}, {2013, h_uint32()}};

    HParser *discriminator = h_uint8();
    HParser *default_parser = h_ch(0x43);

    HParser *message = h_dispatch(discriminator, entries, default_parser);

    HParseResult *res = h_parse(message, buf, buf_size);

    g_check_cmp_ptr(res, ==, NULL);
}

static void test_dispatch_32_bits(void) {

    uint8_t buf[256];
    int buf_size = 8;

    // opcode = 2013 (0x000007DD)
    buf[0] = 0x00;
    buf[1] = 0x00;
    buf[2] = 0x07;
    buf[3] = 0xDD;

    // body = 2048 (0x00000800)
    buf[4] = 0x00;
    buf[5] = 0x00;
    buf[6] = 0x08;
    buf[7] = 0x00;

    OpcodeMap entries[] = {{1, h_ch(0x41)}, {2, h_ch(0x42)}, {2013, h_uint32()}};

    HParser *message = h_dispatch(h_uint32(), entries, NULL);

    HParseResult *res = h_parse(message, buf, buf_size);

    g_check_cmp_ptr(res, !=, NULL);
    g_check_cmp_ptr(res->ast, !=, NULL);

    size_t expected_bits = 64;

    g_check_cmp_int(res->bit_length, ==, expected_bits);
}

static void test_dispatch_bit_length_sequence(void) {
    /* Build a buffer containing three messages back‑to‑back */
    uint8_t buf[256];

    /* Each message is: opcode byte + one body byte */
    buf[0] = 1;
    buf[1] = 0x41; /* message 1 */
    buf[2] = 2;
    buf[3] = 0x42; /* message 2 */
    buf[4] = 3;
    buf[5] = 0x43; /* message 3 */

    size_t buf_size = 6;

    /* Three dispatch tables, one for each message */
    OpcodeMap entries1[] = {{1, h_ch(0x41)}};
    OpcodeMap entries2[] = {{2, h_ch(0x42)}};
    OpcodeMap entries3[] = {{3, h_ch(0x43)}};

    /* Each dispatch parses one opcode + one body byte */
    HParser *d1 = h_dispatch(h_uint8(), entries1, NULL);
    HParser *d2 = h_dispatch(h_uint8(), entries2, NULL);
    HParser *d3 = h_dispatch(h_uint8(), entries3, NULL);

    /* Sequence of three dispatches */
    HParser *seq = h_sequence(d1, d2, d3, NULL);

    /* Parse the whole buffer */
    HParseResult *res = h_parse(seq, buf, buf_size);

    g_check_cmp_ptr(res, !=, NULL);
    g_check_cmp_ptr(res->ast, !=, NULL);

    /*
     * Expected bit_length:
     * Each dispatch consumes 2 bytes = 16 bits.
     * Three dispatches = 48 bits.
     */
    size_t expected_bits = 3 * 16;

    g_check_cmp_int(res->bit_length, ==, expected_bits);
}

static void test_dispatch_does_not_mutate_cached_body(void) {
    uint8_t buf[256];
    int buf_size = 1 + BODY_SIZE;

    buf[0] = 1;
    buf[1] = (uint8_t){0x41};

    HParser *body_parser = h_ch(0x41);
    OpcodeMap entries[] = {{1, body_parser}};

    // Parse the body alone to populate any body parser cache
    HParseResult *body_first = h_parse(body_parser, buf + 1, BODY_SIZE);
    g_check_cmp_ptr(body_first, !=, NULL);
    g_check_cmp_int(body_first->bit_length, ==, BODY_SIZE * 8);

    // Build and run the dispatch parser (which should not mutate cached body result)
    HParser *discriminator = h_uint8();
    HParser *message = h_dispatch(discriminator, entries, NULL);
    g_check_cmp_ptr(message, !=, NULL);

    HParseResult *dispatch_res = h_parse(message, buf, buf_size);
    g_check_cmp_ptr(dispatch_res, !=, NULL);

    // Parse the body again
    HParseResult *body_second = h_parse(body_parser, buf + 1, BODY_SIZE);
    g_check_cmp_ptr(body_second, !=, NULL);

    // Assert the cached body result was not mutated by dispatch
    g_check_cmp_int(body_first->bit_length, ==, body_second->bit_length);

    g_check_cmp_int(body_first->bit_length, ==, body_second->bit_length);
    g_check_cmp_int(body_first->ast->token_type, ==, body_second->ast->token_type);
    g_check_cmp_int(body_first->ast->token_data.uint, ==, body_second->ast->token_data.uint);
}

static void test_dispatch_default_case(void) {

    uint8_t buf[256];
    int buf_size = 1 + BODY_SIZE;
    buf[0] = 2; // Opcode byte

    buf[1] = (uint8_t){0x42};

    OpcodeMap entries[] = {{1, h_ch(0x41)}, {2013, h_uint32()}};
    HParser *discriminator = h_uint8();
    HParser *default_parser = h_ch(0x42);

    HParser *message = h_dispatch(discriminator, entries, default_parser);

    HParseResult *res = h_parse(message, buf, buf_size);

    g_check_cmp_ptr(res, !=, NULL);
    g_check_cmp_ptr(res->ast, !=, NULL);
}

static void test_dispatch_incorrect_discriminator(void) {

    uint8_t buf[256];
    int buf_size = 1 + BODY_SIZE;
    buf[0] = 2; // Opcode byte

    buf[1] = (uint8_t){0x42};

    OpcodeMap entries[] = {{1, h_ch(0x41)}, {2013, h_uint32()}};
    HParser *discriminator = h_ch(0x41);
    HParser *default_parser = h_sequence(h_ch(0x32), h_ch(0x42), NULL);

    HParser *message = h_dispatch(discriminator, entries, default_parser);

    HParseResult *res = h_parse(message, buf, buf_size);

    g_check_cmp_ptr(res, ==, NULL);
}

static void test_dispatch_bytes(void) {

    uint8_t buf[256];
    buf[0] = 0x00; // start opcode bytes
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x01; // end opcode bytes
    buf[4] = 0x41; // body byte

    size_t buf_size = 5;

    /* discriminator produces TT_BYTES */
    HParser *discriminator = h_bytes(4);

    OpcodeMap entries[] = {{1, h_ch(0x41)}, {2, h_ch(0x42)}, {2013, h_uint32()}};

    HParser *message = h_dispatch(discriminator, entries, NULL);

    HParseResult *res = h_parse(message, buf, buf_size);

    g_check_cmp_ptr(res, !=, NULL);
    g_check_cmp_ptr(res->ast, !=, NULL);

    // Expected bit_length: 4 byte discriminator + 1 byte body = 40 bits
    g_check_cmp_int(res->bit_length, ==, 40);
}

static void test_dispatch_5_bytes(void) {

    // opcode still functions over >4 bytes if the largest bytes aren't used
    uint8_t buf[256];
    buf[0] = 0x00; // start opcode bytes
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x01; // end opcode bytes
    buf[5] = 0x41; // body byte

    size_t buf_size = 6;

    /* discriminator produces TT_BYTES */
    HParser *discriminator = h_bytes(5);

    OpcodeMap entries[] = {{1, h_ch(0x41)}, {2, h_ch(0x42)}, {2013, h_uint32()}};

    HParser *message = h_dispatch(discriminator, entries, NULL);

    HParseResult *res = h_parse(message, buf, buf_size);

    g_check_cmp_ptr(res, !=, NULL);
    g_check_cmp_ptr(res->ast, !=, NULL);

    // Expected bit_length: 5 byte discriminator + 1 byte body = 48 bits
    g_check_cmp_int(res->bit_length, ==, 48);

    // check that parse fails with a 40 bit number.
    buf[0] = 0xFF; // NEW opcode byte overflow
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x01; // end opcode bytes
    buf[5] = 0x41;

    res = h_parse(message, buf, buf_size);

    g_check_cmp_ptr(res, ==, NULL);
}

// Helpers for testing for memory leaks
typedef struct {
    HAllocator base;
    size_t allocs;
    size_t frees;
} LeakAlloc;

static void *leak_malloc(HAllocator *mm, size_t n) {
    LeakAlloc *la = (LeakAlloc *)mm;
    la->allocs++;
    return malloc(n);
}

static void leak_free(HAllocator *mm, void *p) {
    LeakAlloc *la = (LeakAlloc *)mm;
    la->frees++;
    free(p);
}

static void leak_init(LeakAlloc *la) {
    la->allocs = 0;
    la->frees  = 0;
    la->base.alloc = leak_malloc;
    la->base.free   = leak_free;
}

static void test_dispatch_no_parse_failure_leak(void) {
    LeakAlloc la;
    leak_init(&la);

    HArena *arena = h_new_arena(&la.base, 0);

    uint8_t buf[] = {1, 0x99};  //opcode 1, mismatched character
    HParser *discriminator = h_uint8();
    HParser *body = h_ch(0x41);

    OpcodeMap entries[] = {
        {1, body}
    };

    HParser *message = h_dispatch(discriminator, entries, NULL);

    HParseResult *res = h_parse(message, buf, sizeof(buf));

    g_check_cmp_ptr(res, ==, NULL);

    h_delete_arena(arena);

    g_check_cmp_int(la.frees, ==, la.allocs);
}

static void test_dispatch_parser_reuse(void) {
    HParser *shared = h_ch(0x41);

    OpcodeMap entries[] = {
        {1, shared},
        {2, shared},
        {2013, shared}
    };

    HParser *discriminator = h_uint8();
    HParser *message = h_dispatch(discriminator, entries, NULL);

    HCFStack *stk;
    stk = h_cfstack_new(&system_allocator);

    h_desugar(&system_allocator, stk, message);

     /* Find the outer choice node */
    HCFChoice *choice = NULL;
    for (int i = 0; i < 2; i++) {
        if (stk->stack[i]->type == HCF_CHOICE) {
            choice = (HCFChoice *)stk->stack[i];
            break;
        }
    }

    g_check_cmp_ptr(choice, !=, NULL);

    /* Now inspect each branch */
    HCFChoice *first_body_node = NULL;

    for (size_t i = 0; i < 2; i++) {
        HCFSequence *seq = choice->data.seq[i];

        /* seq->elems[1] is the body parser */
        HCFChoice *body = seq->items[1];

        if (first_body_node == NULL){
            first_body_node = body;}
        else{
            g_check_cmp_ptr(body, ==, first_body_node);}
    }

    h_cfstack_free(&system_allocator, stk);
}

static HParser *make_dispatch_from_stack_map(void) {
    OpcodeMap entries[] = {{1, h_ch(0x41)}};
    return h_dispatch(h_uint8(), entries, NULL);
}

static void consume_stack_space(void) {
    volatile uint8_t scratch[16384];
    for (size_t i = 0; i < sizeof(scratch); ++i) {
        scratch[i] = (uint8_t)i;
    }
}

static void test_dispatch_copies_stack_local_opcode_map(void) {
    HParser *message = make_dispatch_from_stack_map();
    g_check_cmp_ptr(message, !=, NULL);

    consume_stack_space();

    uint8_t buf[] = {1, 0x41};
    HParseResult *res = h_parse(message, buf, sizeof(buf));
    g_check_cmp_ptr(res, !=, NULL);
    g_check_cmp_ptr(res->ast, !=, NULL);

    HCFStack *stk = h_cfstack_new(&system_allocator);
    h_desugar(&system_allocator, stk, message);
    h_cfstack_free(&system_allocator, stk);
}

static void test_dispatch_prettyprint(gconstpointer backend) {
    HParser *disc = h_ch('a');
    HParser *body = h_ch('b');
    OpcodeMap map[] = {{97, body}};
    HParser *dispatch = h_dispatch__s(disc, map, 1, NULL);

    /* The pretty-printer should show both the body and the opcode */
    g_check_parse_match(dispatch, (HParserBackend)GPOINTER_TO_INT(backend), "ab", 2, "(u0x61 u0x62)");
}

void register_dispatch_tests(void) {
    g_test_add_func("/core/dispatch/basic_functionality", test_dispatch_basic_functionality);
    g_test_add_func("/core/dispatch/incorrect_opcode", test_dispatch_incorrect_opcode);
    g_test_add_func("/core/dispatch/32_bits", test_dispatch_32_bits);
    g_test_add_func("/core/dispatch/bit_length", test_dispatch_bit_length_sequence);
    g_test_add_func("/core/dispatch/cached_body_mutation",
                    test_dispatch_does_not_mutate_cached_body);
    g_test_add_func("/core/dispatch/default_case", test_dispatch_default_case);
    g_test_add_func("/core/dispatch/incorrect_discriminator",
                    test_dispatch_incorrect_discriminator);
    g_test_add_func("/core/dispatch/bytes", test_dispatch_bytes);
    g_test_add_func("/core/dispatch/5_bytes", test_dispatch_5_bytes);
    g_test_add_func("/core/dispatch/no_parse_failure_leak", test_dispatch_no_parse_failure_leak);
    g_test_add_func("/core/dispatch/parser_reuse", test_dispatch_parser_reuse);
    g_test_add_func("/core/dispatch/copies_stack_local_opcode_map", test_dispatch_copies_stack_local_opcode_map);
    g_test_add_data_func("/core/dispatch/prettyprint",GINT_TO_POINTER(PB_PACKRAT), test_dispatch_prettyprint);
}