/* Copyright (c) 2026 Riverside Research */
#include "parser_internal.h"

#include <assert.h>
#include <ctype.h>

static HParseResult *parse_whitespace(void *env, HParseState *state) {
    char c;
    HInputStream bak;
    do {
        bak = state->input_stream;
        c = h_read_bits(&state->input_stream, 8, false);
        if (want_suspend(state))
            return NULL; // bail out early, leaving overrun flag
        if (state->input_stream.overrun)
            break;
    } while (isspace((int)c));
    state->input_stream = bak;
    return h_do_parse((HParser *)env, state);
}

static const char SPACE_CHRS[6] = {' ', '\f', '\n', '\r', '\t', '\v'};

static void desugar_whitespace(HAllocator *mm__, HCFStack *stk__, void *env) {

    HCharset ws_cs = new_charset(mm__);
    for (size_t i = 0; i < sizeof(SPACE_CHRS); i++)
        charset_set(ws_cs, SPACE_CHRS[i], 1);

    HCFS_BEGIN_CHOICE() {
        HCFS_BEGIN_SEQ() {
            HCFS_BEGIN_CHOICE() {
                HCFS_BEGIN_SEQ() {
                    HCFS_ADD_CHARSET(ws_cs);
                    HCFS_APPEND(HCFS_THIS_CHOICE); // yay circular pointer!
                }
                HCFS_END_SEQ();
                HCFS_BEGIN_SEQ() {}
                HCFS_END_SEQ();
            }
            HCFS_END_CHOICE();
            HCFS_DESUGAR((HParser *)env);
        }
        HCFS_END_SEQ();
        HCFS_THIS_CHOICE->reshape = h_act_last;
    }
    HCFS_END_CHOICE();
}

static bool ws_isValidRegular(void *env) {
    HParser *p = (HParser *)env;
    return p->vtable->isValidRegular(p->env);
}

static bool ws_isValidCF(void *env) {
    HParser *p = (HParser *)env;
    return p->vtable->isValidCF(p->env);
}

static bool ws_ctrvm(HRVMProg *prog, void *env) {
    HParser *p = (HParser *)env;
    uint16_t start = h_rvm_get_ip(prog);

    uint16_t ranges[] = {
        0x0D09,
        0x2020,
    };

    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
        uint16_t next = h_rvm_insert_insn(prog, RVM_FORK, 0);
        h_rvm_insert_insn(prog, RVM_MATCH, ranges[i]);
        h_rvm_insert_insn(prog, RVM_STEP, 0);
        h_rvm_insert_insn(prog, RVM_GOTO, start);
        h_rvm_patch_arg(prog, next, h_rvm_get_ip(prog));
    }
    return h_compile_regex(prog, p);
}

static const HParserVtable whitespace_vt = {
    .name = "h_whitespace",
    .parse = parse_whitespace,
    .isValidRegular = ws_isValidRegular,
    .isValidCF = ws_isValidCF,
    .compile_to_rvm = ws_ctrvm,
    .desugar = desugar_whitespace,
    .higher = false,
};

HParser *h_whitespace(const HParser *p) { return h_whitespace__m(&system_allocator, p); }
HParser *h_whitespace__m(HAllocator *mm__, const HParser *p) {
    void *env = (void *)p;
    return h_new_parser_with_free(mm__, &whitespace_vt, env, h_no_free_env);
}
